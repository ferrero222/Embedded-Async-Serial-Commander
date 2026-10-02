/*******************************************************************************
 *                              TACT Example                         21.11.2025 *
 *                                 v1.0                                        *
 *       This example is showing how to send data to wialon server             *
 *       through SIM868 using one single chain and ready-made modules          *
 *       from TACT.                                                             *
 ******************************************************************************/
/*******************************************************************************
 * Include files
 ******************************************************************************/
#include "hc32_ddl.h"
#include "boot.h"
#include "proc.h"
#include "timers.h"
#include "sim_proc.h"
#include "hc32f460_utility.h"
#include "tact_core.h"
#include "tact_mdl_general.h"
#include "tact_mdl_gprs.h"
#include "tact_chain.h"
#include "wialon.h"

/*******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/
/*******************************************************************************
 * Local variable definitions ('static')
 ******************************************************************************/
tact_context_t simcom_ctx = {0};

/*******************************************************************************
 * Function implementation - global ('extern') and local ('static')
 ******************************************************************************/
/*******************************************************************************
 * @brief Restart the example device after an unrecoverable modem failure.
 * @details Synchronous EXEC action; the platform reset normally does not return.
 * @param[in] ctx Unused TACT context supplied by the Chain.
 * @param[in] param Unused EXEC parameters; pass NULL.
 * @retval true The reset request was issued.
 ******************************************************************************/
static bool tact_hard_reset(tact_context_t* const ctx, void* const param)
{
  (void)ctx;
  (void)param;
  restart(MemManage);
  return true;
}

/*******************************************************************************
 ** \brief  Methods and statics for rtd
 ** \param  None
 ** \retval None
 ******************************************************************************/ 
static tact_mdl_rtd_t tact_rtd = {0};

/* Get rtd */
static void tact_rtd_cb(const bool result, void* const ctx, const void* const data)
{
  if(data && result) tact_rtd = *(tact_mdl_rtd_t*)data;
}
  
/*******************************************************************************
 * @brief Check that the modem real-time data has been populated.
 * @details Missing identity, clock, or SIM fields route the Chain back to the
 *          RTD query rather than allowing login with incomplete identity data.
 * @param[in] ctx Unused TACT context supplied by the Chain.
 * @param[in] param Unused EXEC parameters; pass NULL.
 * @retval true All required RTD strings are present.
 * @retval false At least one required field is empty.
 ******************************************************************************/
static bool tact_rtd_check(tact_context_t* const ctx, void* const param)
{
  (void)ctx;
  (void)param;
  if(strlen(tact_rtd.modem_imei) == 0 || strlen(tact_rtd.modem_id) == 0    ||
     strlen(tact_rtd.modem_rev) == 0  || strlen(tact_rtd.modem_clock) == 0 ||
     strlen(tact_rtd.sim_iccid) == 0)
  {
    return false;
  }                                           
  return true;
}

/*******************************************************************************
 ** \brief  Methods and statics for tcp
 ** \param  None
 ** \retval None
 ******************************************************************************/ 
static tact_mdl_gprs_stream_t tact_server_tx = {.direction = TACT_MDL_GPRS_STREAM_TX};
static uint8_t tact_server_answer[sizeof("#AL#1\r\n") - 1U] = {0};
static tact_mdl_gprs_stream_t tact_server_rx = {.buffer = tact_server_answer, .direction = TACT_MDL_GPRS_STREAM_RX};
static const char *tact_server_expected = NULL;
static tact_mdl_gprs_server_t tact_server_connect = {.mode = "TCP", .ip = "YOUR_IP", .port = "YOUR_PORT"};

/*******************************************************************************
 * @brief Release the example's previously prepared Wialon message buffers.
 * @details Frees the caller-owned TX buffer and clears transfer counters. RX
 *          uses fixed caller storage, not another library-allocated buffer.
 * @param[in] ctx TACT context owning the message allocations.
 * @param[in] param Unused EXEC parameters; pass NULL.
 * @retval true The local message buffers have been released.
 ******************************************************************************/
static bool tact_server_data_clean(tact_context_t* const ctx, void* const param)
{
  (void)param;
  if(tact_server_tx.buffer) tact_free(ctx, tact_server_tx.buffer);
  tact_server_tx.buffer = NULL;
  tact_server_tx.size = 0U;
  tact_server_tx.count = 0U;
  tact_server_rx.size = 0U;
  tact_server_rx.count = 0U;
  /* Preserve remaining between reads; reset it only when reconnecting. */
  tact_server_expected = NULL;
  return true;
}

/*******************************************************************************
 * @brief Prepare the Wialon login request and its expected response.
 * @details Replaces earlier buffers before the next asynchronous Chain step
 *          transmits the login and checks the response.
 * @param[in] ctx TACT context providing the message allocator.
 * @param[in] param Unused EXEC parameters; pass NULL.
 * @retval true The TX buffer and fixed-length reply descriptor are ready.
 * @retval false The TX buffer allocation failed.
 ******************************************************************************/
static bool tact_server_data_wialon_login(tact_context_t* const ctx, void* const param)
{
  (void)param;
  tact_server_data_clean(ctx, NULL);
  tact_server_tx.buffer = tact_malloc(ctx, 100);
  if(!tact_server_tx.buffer) return false;
  sprintf((char *)tact_server_tx.buffer, "2.0;%.15s;NA;", tact_rtd.modem_imei);
  app_create_wialon_msg("#L#", (char *)tact_server_tx.buffer, 100);
  tact_server_tx.size = (uint32_t)strlen((char *)tact_server_tx.buffer);
  tact_server_expected = "#AL#1\r\n";
  tact_server_rx.size = sizeof("#AL#1\r\n") - 1U;
  return true;
}

/*******************************************************************************
 * @brief Prepare one Wialon data message from the latest modem RTD fields.
 * @details Runs once per loop iteration before the ordinary transmit step,
 *          reusing the message descriptor after releasing its previous buffers.
 * @param[in] ctx TACT context providing the message allocator.
 * @param[in] param Unused EXEC parameters; pass NULL.
 * @retval true The TX buffer and fixed-length reply descriptor are ready.
 * @retval false The TX buffer allocation failed.
 ******************************************************************************/
static bool tact_server_data_wialon_packet(tact_context_t* const ctx, void* const param)
{
  (void)param;
  tact_server_data_clean(ctx, NULL);
  tact_server_tx.buffer = tact_malloc(ctx, 250);
  if(!tact_server_tx.buffer) return false;
  sprintf((char *)tact_server_tx.buffer, 
          "NA;NA;NA;NA;NA;NA;NA;NA;NA;NA;NA;NA;NA;;NA;imei:3:%s,id:3:%s,rev:3:%s,clock:3:%s,iccid:3:%s,oper:3:%s,rssi:1:%d;",
          tact_rtd.modem_imei, 
          tact_rtd.modem_id, 
          tact_rtd.modem_rev, 
          tact_rtd.modem_clock, 
          tact_rtd.sim_iccid, 
          tact_rtd.sim_operator,
          tact_rtd.sim_rssi);
  app_create_wialon_msg("#D#", (char *)tact_server_tx.buffer, 250);
  tact_server_tx.size = (uint32_t)strlen((char *)tact_server_tx.buffer);
  tact_server_expected = "#AD#1\r\n";
  tact_server_rx.size = sizeof("#AD#1\r\n") - 1U;
  return true;
}

/*******************************************************************************
 * @brief Check the collected fixed-length Wialon application reply.
 * @details RX only delivers bytes; this example checks their protocol meaning
 *          separately after the receive loop finishes.
 * @param[in] ctx Unused TACT context supplied by the Chain.
 * @param[in] param Unused EXEC parameters; pass NULL.
 * @retval true The collected reply matches the expected acknowledgement.
 * @retval false The reply is missing, incomplete, or negative.
 ******************************************************************************/
static bool tact_server_answer_check(tact_context_t* const ctx, void* const param)
{
  (void)ctx;
  (void)param;
  return tact_server_expected && tact_server_rx.count == tact_server_rx.size &&
         memcmp(tact_server_rx.buffer, tact_server_expected, tact_server_rx.size) == 0;
}

/*******************************************************************************
 ** \brief  Main function of project
 ** \param  None
 ** \retval None
 ******************************************************************************/ 
tact_chain_t* test_chain_init(void)
{
  /* SIM868/Wialon example, not an A76xx TCP implementation. The platform worker
   * must stop AT/URC parsing during TX/RX EXEC loops, keep UART interrupts enabled,
   * yield between iterations, and enforce a monotonic transfer deadline. This
   * request/reply example assumes no unrelated unsolicited payload while waiting
   * for SEND OK; an asynchronous socket needs separate text/binary RX routing. */
  tact_server_rx.remaining = 0U;
  chain_step_t tcp_steps[] = 
  {   
    //Main
    TACT_CHAIN("INIT_MODEM", "NEXT", "MODEM RESTART", tact_mdl_modem_init, NULL, NULL, NULL, 1),
    TACT_CHAIN("GPRS INIT", "NEXT", "GPRS DEINIT", tact_mdl_gprs_init, NULL, NULL, NULL, 1),
    TACT_CHAIN("SOCKET CONFIG", "NEXT", "GPRS INIT", tact_mdl_gprs_socket_config, NULL, NULL, NULL, 1),
    TACT_CHAIN("CONNECT TO SERVER", "NEXT", "SOCKET CONFIG", tact_mdl_gprs_socket_connect, NULL, &tact_server_connect, NULL, 1),
    TACT_CHAIN("GET RTD", "NEXT", "DISCONNECT FROM SERVER", tact_mdl_rtd, tact_rtd_cb, NULL, NULL, 1),
    TACT_CHAIN_EXEC("CHECK RTD", "NEXT", "GET RTD", tact_rtd_check, NULL),
    
    TACT_CHAIN_EXEC("CREATE WIALON LOGIN", "NEXT", "DISCONNECT FROM SERVER", tact_server_data_wialon_login, NULL),
    TACT_CHAIN("SEND WIALON LOGIN", "NEXT", "DISCONNECT FROM SERVER", tact_mdl_gprs_socket_send_recieve, NULL, &tact_server_tx, NULL, 1),
    TACT_CHAIN_LOOP_START(0),
      TACT_CHAIN_EXEC("LOGIN TX", "LOGIN SEND RESULT", "NEXT", tact_mdl_gprs_stream_tx, &tact_server_tx),
    TACT_CHAIN_LOOP_END,
    TACT_CHAIN("LOGIN SEND RESULT", "NEXT", "DISCONNECT FROM SERVER", tact_mdl_gprs_socket_send_end, NULL, NULL, NULL, 1),
    TACT_CHAIN_LOOP_START(0),
      TACT_CHAIN_EXEC("LOGIN RX", "LOGIN CHECK REPLY", "NEXT", tact_mdl_gprs_stream_rx, &tact_server_rx),
    TACT_CHAIN_LOOP_END,
    TACT_CHAIN_EXEC("LOGIN CHECK REPLY", "NEXT", "DISCONNECT FROM SERVER", tact_server_answer_check, NULL),
    
    TACT_CHAIN_LOOP_START(10),
      TACT_CHAIN("GET RTD LOOP", "NEXT", "DISCONNECT FROM SERVER", tact_mdl_rtd, tact_rtd_cb, NULL, NULL, 1),
      TACT_CHAIN_EXEC("CREATE WIALON DATA", "NEXT", "DISCONNECT FROM SERVER", tact_server_data_wialon_packet, NULL),
      TACT_CHAIN("SEND WIALON DATA", "NEXT", "DISCONNECT FROM SERVER", tact_mdl_gprs_socket_send_recieve, NULL, &tact_server_tx, NULL, 1),
      TACT_CHAIN_LOOP_START(0),
        TACT_CHAIN_EXEC("DATA TX", "DATA SEND RESULT", "NEXT", tact_mdl_gprs_stream_tx, &tact_server_tx),
      TACT_CHAIN_LOOP_END,
      TACT_CHAIN("DATA SEND RESULT", "NEXT", "DISCONNECT FROM SERVER", tact_mdl_gprs_socket_send_end, NULL, NULL, NULL, 1),
      TACT_CHAIN_LOOP_START(0),
        TACT_CHAIN_EXEC("DATA RX", "DATA CHECK REPLY", "NEXT", tact_mdl_gprs_stream_rx, &tact_server_rx),
      TACT_CHAIN_LOOP_END,
      TACT_CHAIN_EXEC("DATA CHECK REPLY", "NEXT", "DISCONNECT FROM SERVER", tact_server_answer_check, NULL),
      TACT_CHAIN_DELAY(1000),
    TACT_CHAIN_LOOP_END,
    
    TACT_CHAIN_EXEC("WIALON DATA CLEAN", "STOP", "HARD RESET", tact_server_data_clean, NULL),
    
    //Error
    TACT_CHAIN("GPRS DEINIT", "GPRS INIT", "MODEM RESTART", tact_mdl_gprs_deinit, NULL, NULL, NULL, 1),
    TACT_CHAIN("DISCONNECT FROM SERVER", "CONNECT TO SERVER", "GPRS DEINIT", tact_mdl_gprs_socket_disconnect, NULL, NULL, NULL, 1),
    TACT_CHAIN("MODEM RESTART", "GPRS INIT", "MODEM RESTART", tact_mdl_modem_reset, NULL, NULL, NULL, 1),
    
    //Critical
    TACT_CHAIN_EXEC("HARD RESET", "STOP", "STOP", tact_hard_reset, NULL),

  };
  tact_chain_t* chain = tact_chain_create("TCP", tcp_steps, sizeof(tcp_steps)/sizeof(chain_step_t), &simcom_ctx);
  tact_chain_start(chain);
  return chain;
}
  

/*******************************************************************************
 ** \brief  Main function of project
 ** \param  None
 ** \retval None
 ******************************************************************************/ 
void main(void)
{
  tact_boot(); //init hardware, pins, uart, clock and etc.
  tact_init(&simcom_ctx, my_printf, gsm_proc_send_data, (tact_ring_buffer_t*)&uart_gsm_ctx.rx_buf); //tact lib init
  tact_chain_t* chain = test_chain_init(); //create behavior scenario using tact chain
  while(1)
  {
    tact_timers_proc(); //proc programm timers (10ms included inside of it)
    if(tact_chain_is_running(chain))
    {
      tact_chain_run(chain);
      if(!tact_chain_is_running(chain))
      {
        tact_chain_destroy(chain); //chain was done and stopped
        chain = NULL;
      }
    }
  }
}
