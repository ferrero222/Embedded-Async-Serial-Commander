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
 ** \brief  Hard reset function
 ** \param  None
 ** \retval None
 ******************************************************************************/ 
static bool tact_hard_reset(void)
{
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
  
/* Check rtd */
static bool tact_rtd_check(void)
{
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
static tact_mdl_gprs_data_t tact_server_data = {0};
static tact_mdl_gprs_server_t tact_server_connect = {.mode = "TCP", .ip = "YOUR_IP", .port = "YOUR_PORT"};

/* Clean wialon msg cb */
static bool tact_server_data_clean(void)
{
  if(tact_server_data.data) tact_free(&simcom_ctx, tact_server_data.data);
  if(tact_server_data.answ) tact_free(&simcom_ctx, tact_server_data.answ);
  tact_server_data.data = 0;
  tact_server_data.answ = 0;
  return true;
}

/* Create wialon login function to exec */
static bool tact_server_data_wialon_login(void)
{
  tact_server_data_clean();
  tact_server_data.data = tact_malloc(&simcom_ctx, 100);
  tact_server_data.answ = tact_malloc(&simcom_ctx, 50);
  if(!tact_server_data.data || !tact_server_data.answ) return false;
  sprintf(tact_server_data.data, "2.0;%.15s;NA;", tact_rtd.modem_imei);
  app_create_wialon_msg("#L#", tact_server_data.data, 100);
  sprintf(tact_server_data.answ, "#AL#1\r\n");
  return true;
}

/* Create wialon data function to exec */
static bool tact_server_data_wialon_packet(void)
{
  tact_server_data_clean();
  tact_server_data.data = tact_malloc(&simcom_ctx, 250);
  tact_server_data.answ = tact_malloc(&simcom_ctx, 50);
  if(!tact_server_data.data || !tact_server_data.answ) return false;
  sprintf(tact_server_data.data, 
          "NA;NA;NA;NA;NA;NA;NA;NA;NA;NA;NA;NA;NA;;NA;imei:3:%s,id:3:%s,rev:3:%s,clock:3:%s,iccid:3:%s,oper:3:%s,rssi:1:%d;",
          tact_rtd.modem_imei, 
          tact_rtd.modem_id, 
          tact_rtd.modem_rev, 
          tact_rtd.modem_clock, 
          tact_rtd.sim_iccid, 
          tact_rtd.sim_operator,
          tact_rtd.sim_rssi);
  app_create_wialon_msg("#D#", tact_server_data.data, 250);
  sprintf(tact_server_data.answ, "#AD#1\r\n");
  return true;
}

/*******************************************************************************
 ** \brief  Main function of project
 ** \param  None
 ** \retval None
 ******************************************************************************/ 
tact_chain_t* test_chain_init(void)
{
  chain_step_t tcp_steps[] = 
  {   
    //Main
    TACT_CHAIN("INIT_MODEM", "NEXT", "MODEM RESTART", tact_mdl_modem_init, NULL, NULL, NULL, 1),
    TACT_CHAIN("GPRS INIT", "NEXT", "GPRS DEINIT", tact_mdl_gprs_init, NULL, NULL, NULL, 1),
    TACT_CHAIN("SOCKET CONFIG", "NEXT", "GPRS INIT", tact_mdl_gprs_socket_config, NULL, NULL, NULL, 1),
    TACT_CHAIN("CONNECT TO SERVER", "NEXT", "SOCKET CONFIG", tact_mdl_gprs_socket_connect, NULL, &tact_server_connect, NULL, 1),
    TACT_CHAIN("GET RTD", "NEXT", "DISCONNECT FROM SERVER", tact_mdl_rtd, tact_rtd_cb, NULL, NULL, 1),
    TACT_CHAIN_EXEC("CHECK RTD", "NEXT", "GET RTD", tact_rtd_check),
    
    TACT_CHAIN_EXEC("CREATE WIALON LOGIN", "NEXT", "DISCONNECT FROM SERVER", tact_server_data_wialon_login),
    TACT_CHAIN("SEND WIALON LOGIN", "NEXT", "DISCONNECT FROM SERVER", tact_mdl_gprs_socket_send_recieve, NULL, &tact_server_data, NULL, 3),
    
    TACT_CHAIN_LOOP_START(10),
      TACT_CHAIN("GET RTD LOOP", "NEXT", "DISCONNECT FROM SERVER", tact_mdl_rtd, tact_rtd_cb, NULL, NULL, 1),
      TACT_CHAIN_EXEC("CREATE WIALON DATA", "NEXT", "DISCONNECT FROM SERVER", tact_server_data_wialon_packet),
      TACT_CHAIN("SEND WIALON DATA", "NEXT", "DISCONNECT FROM SERVER", tact_mdl_gprs_socket_send_recieve, NULL, &tact_server_data, NULL, 3),
      TACT_CHAIN_DELAY(1000),
    TACT_CHAIN_LOOP_END,
    
    TACT_CHAIN_EXEC("WIALON DATA CLEAN", "STOP", "HARD RESET", tact_server_data_clean),
    
    //Error
    TACT_CHAIN("GPRS DEINIT", "GPRS INIT", "MODEM RESTART", tact_mdl_gprs_deinit, NULL, NULL, NULL, 1),
    TACT_CHAIN("DISCONNECT FROM SERVER", "CONNECT TO SERVER", "GPRS DEINIT", tact_mdl_gprs_socket_disconnect, NULL, NULL, NULL, 1),
    TACT_CHAIN("MODEM RESTART", "GPRS INIT", "MODEM RESTART", tact_mdl_modem_reset, NULL, NULL, NULL, 1),
    
    //Critical
    TACT_CHAIN_EXEC("HARD RESET", "STOP", "STOP", tact_hard_reset),

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
