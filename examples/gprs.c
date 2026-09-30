/*******************************************************************************
 *                              ASC Example                         21.11.2025 *
 *                                 v1.0                                        *
 *       This example is showing how to send data to wialon server             *
 *       through SIM868 using one single chain and ready-made modules          *
 *       from ASC.                                                             *
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
#include "asc_core.h"
#include "asc_mdl_general.h"
#include "asc_mdl_gprs.h"
#include "asc_chain.h"
#include "wialon.h"

/*******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/
/*******************************************************************************
 * Local variable definitions ('static')
 ******************************************************************************/
asc_context_t simcom_ctx = {0};

/*******************************************************************************
 * Function implementation - global ('extern') and local ('static')
 ******************************************************************************/
/*******************************************************************************
 ** \brief  Hard reset function
 ** \param  None
 ** \retval None
 ******************************************************************************/ 
static bool asc_hard_reset(void)
{
  restart(MemManage);
  return true;
}

/*******************************************************************************
 ** \brief  Methods and statics for rtd
 ** \param  None
 ** \retval None
 ******************************************************************************/ 
static asc_mdl_rtd_t asc_rtd = {0};

/* Get rtd */
static void asc_rtd_cb(const bool result, void* const ctx, const void* const data)
{
  if(data && result) asc_rtd = *(asc_mdl_rtd_t*)data;
}
  
/* Check rtd */
static bool asc_rtd_check(void)
{
  if(strlen(asc_rtd.modem_imei) == 0 || strlen(asc_rtd.modem_id) == 0    ||
     strlen(asc_rtd.modem_rev) == 0  || strlen(asc_rtd.modem_clock) == 0 ||
     strlen(asc_rtd.sim_iccid) == 0)
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
static asc_mdl_gprs_data_t asc_server_data = {0};
static asc_mdl_gprs_server_t asc_server_connect = {.mode = "TCP", .ip = "YOUR_IP", .port = "YOUR_PORT"};

/* Clean wialon msg cb */
static bool asc_server_data_clean(void)
{
  if(asc_server_data.data) asc_free(&simcom_ctx, asc_server_data.data);
  if(asc_server_data.answ) asc_free(&simcom_ctx, asc_server_data.answ);
  asc_server_data.data = 0;
  asc_server_data.answ = 0;
  return true;
}

/* Create wialon login function to exec */
static bool asc_server_data_wialon_login(void)
{
  asc_server_data_clean();
  asc_server_data.data = asc_malloc(&simcom_ctx, 100);
  asc_server_data.answ = asc_malloc(&simcom_ctx, 50);
  if(!asc_server_data.data || !asc_server_data.answ) return false;
  sprintf(asc_server_data.data, "2.0;%.15s;NA;", asc_rtd.modem_imei);
  app_create_wialon_msg("#L#", asc_server_data.data, 100);
  sprintf(asc_server_data.answ, "#AL#1\r\n");
  return true;
}

/* Create wialon data function to exec */
static bool asc_server_data_wialon_packet(void)
{
  asc_server_data_clean();
  asc_server_data.data = asc_malloc(&simcom_ctx, 250);
  asc_server_data.answ = asc_malloc(&simcom_ctx, 50);
  if(!asc_server_data.data || !asc_server_data.answ) return false;
  sprintf(asc_server_data.data, 
          "NA;NA;NA;NA;NA;NA;NA;NA;NA;NA;NA;NA;NA;;NA;imei:3:%s,id:3:%s,rev:3:%s,clock:3:%s,iccid:3:%s,oper:3:%s,rssi:1:%d;",
          asc_rtd.modem_imei, 
          asc_rtd.modem_id, 
          asc_rtd.modem_rev, 
          asc_rtd.modem_clock, 
          asc_rtd.sim_iccid, 
          asc_rtd.sim_operator,
          asc_rtd.sim_rssi);
  app_create_wialon_msg("#D#", asc_server_data.data, 250);
  sprintf(asc_server_data.answ, "#AD#1\r\n");
  return true;
}

/*******************************************************************************
 ** \brief  Main function of project
 ** \param  None
 ** \retval None
 ******************************************************************************/ 
asc_chain_t* test_chain_init(void)
{
  chain_step_t tcp_steps[] = 
  {   
    //Main
    ASC_CHAIN("INIT_MODEM", "NEXT", "MODEM RESTART", asc_mdl_modem_init, NULL, NULL, NULL, 1),
    ASC_CHAIN("GPRS INIT", "NEXT", "GPRS DEINIT", asc_mdl_gprs_init, NULL, NULL, NULL, 1),
    ASC_CHAIN("SOCKET CONFIG", "NEXT", "GPRS INIT", asc_mdl_gprs_socket_config, NULL, NULL, NULL, 1),
    ASC_CHAIN("CONNECT TO SERVER", "NEXT", "SOCKET CONFIG", asc_mdl_gprs_socket_connect, NULL, &asc_server_connect, NULL, 1),
    ASC_CHAIN("GET RTD", "NEXT", "DISCONNECT FROM SERVER", asc_mdl_rtd, asc_rtd_cb, NULL, NULL, 1),
    ASC_CHAIN_EXEC("CHECK RTD", "NEXT", "GET RTD", asc_rtd_check),
    
    ASC_CHAIN_EXEC("CREATE WIALON LOGIN", "NEXT", "DISCONNECT FROM SERVER", asc_server_data_wialon_login),
    ASC_CHAIN("SEND WIALON LOGIN", "NEXT", "DISCONNECT FROM SERVER", asc_mdl_gprs_socket_send_recieve, NULL, &asc_server_data, NULL, 3),
    
    ASC_CHAIN_LOOP_START(10),
      ASC_CHAIN("GET RTD LOOP", "NEXT", "DISCONNECT FROM SERVER", asc_mdl_rtd, asc_rtd_cb, NULL, NULL, 1),
      ASC_CHAIN_EXEC("CREATE WIALON DATA", "NEXT", "DISCONNECT FROM SERVER", asc_server_data_wialon_packet),
      ASC_CHAIN("SEND WIALON DATA", "NEXT", "DISCONNECT FROM SERVER", asc_mdl_gprs_socket_send_recieve, NULL, &asc_server_data, NULL, 3),
      ASC_CHAIN_DELAY(1000),
    ASC_CHAIN_LOOP_END,
    
    ASC_CHAIN_EXEC("WIALON DATA CLEAN", "STOP", "HARD RESET", asc_server_data_clean),
    
    //Error
    ASC_CHAIN("GPRS DEINIT", "GPRS INIT", "MODEM RESTART", asc_mdl_gprs_deinit, NULL, NULL, NULL, 1),
    ASC_CHAIN("DISCONNECT FROM SERVER", "CONNECT TO SERVER", "GPRS DEINIT", asc_mdl_gprs_socket_disconnect, NULL, NULL, NULL, 1),
    ASC_CHAIN("MODEM RESTART", "GPRS INIT", "MODEM RESTART", asc_mdl_modem_reset, NULL, NULL, NULL, 1),
    
    //Critical
    ASC_CHAIN_EXEC("HARD RESET", "STOP", "STOP", asc_hard_reset),

  };
  asc_chain_t* chain = asc_chain_create("TCP", tcp_steps, sizeof(tcp_steps)/sizeof(chain_step_t), &simcom_ctx);
  asc_chain_start(chain);
  return chain;
}
  

/*******************************************************************************
 ** \brief  Main function of project
 ** \param  None
 ** \retval None
 ******************************************************************************/ 
void main(void)
{
  asc_boot(); //init hardware, pins, uart, clock and etc.
  asc_init(&simcom_ctx, my_printf, gsm_proc_send_data, (asc_ring_buffer_t*)&uart_gsm_ctx.rx_buf); //atl lib init
  asc_chain_t* chain = test_chain_init(); //create behavior scenario using atl chain
  while(1)
  {
    asc_timers_proc(); //proc programm timers (10ms included inside of it)
    if(asc_chain_is_running(chain))
    {
      asc_chain_run(chain);
      if(!asc_chain_is_running(chain))
      {
        asc_chain_destroy(chain); //chain was done and stopped
        chain = NULL;
      }
    }
  }
}
