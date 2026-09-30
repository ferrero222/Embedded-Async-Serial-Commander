/*******************************************************************************
 *                              ASC Example                         21.11.2025 *
 *                                 v1.0                                        *
 *       This example is showing how to connect to MQTT broker, publish        *
 *       a constant message and disconnect, using one single chain and         *
 *       ready-made MQTT modules from ASC.                                     *
 ******************************************************************************/
/*******************************************************************************
 * Include files
 ******************************************************************************/
/* Replace these with your actual hardware-specific headers */
//#include "hc32_ddl.h"
//#include "boot.h"
//#include "proc.h"
//#include "timers.h"
//#include "sim_proc.h"
//#include "hc32f460_utility.h"

#include "asc_core.h"
#include "asc_port.h"
#include "asc_mdl_general.h"
#include "asc_mdl_gprs.h"
#include "asc_mdl_mqtt.h"
#include "asc_chain.h"

/*******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/
/*******************************************************************************
 * Local variable definitions ('static')
 ******************************************************************************/
asc_context_t simcom_ctx = {0};

/*******************************************************************************
 * MQTT configuration
 ******************************************************************************/
/* Fill in your broker settings here */
static asc_mdl_mqtt_cfg_t asc_mqtt_cfg =
{
  .client_index  = 0,
  .client_id     = "asc_device_001",
  .server_addr   = "tcp://broker.emqx.io:1883",
  .keepalive     = 60,
  .clean_session = 1,
  .username      = "",
  .password      = "",
};

/* Message to publish */
static asc_mdl_mqtt_msg_t asc_mqtt_msg =
{
  .client_index = 0,
  .qos          = 1,
  .topic        = "/asc/test",
  .payload      = "Hello from ASC!",
  .retained     = 0,
  .pub_timeout  = 30,
};

/*******************************************************************************
 * Function implementation - global ('extern') and local ('static')
 ******************************************************************************/
/*******************************************************************************
 ** \brief  Initialize MQTT test chain
 ** \param  None
 ** \retval ptr to created chain
 ******************************************************************************/
asc_chain_t* test_chain_init(void)
{
  chain_step_t mqtt_steps[] =
  {
    //Main flow
    ASC_CHAIN("INIT_MODEM", "NEXT", "MODEM_RESTART", asc_mdl_modem_init,  NULL, NULL,  NULL, 1),
    ASC_CHAIN("GPRS_INIT", "NEXT", "GPRS_DEINIT", asc_mdl_gprs_init, NULL, NULL, NULL, 1),
    ASC_CHAIN("SOCKET_CONFIG", "NEXT", "GPRS_DEINIT", asc_mdl_gprs_socket_config, NULL, NULL, NULL, 1),

    ASC_CHAIN("MQTT_START", "NEXT", "GPRS_DEINIT", asc_mdl_mqtt_start, NULL, NULL, NULL, 1),
    ASC_CHAIN("MQTT_ACQUIRE", "NEXT", "MQTT_STOP", asc_mdl_mqtt_acquire, NULL, &asc_mqtt_cfg, NULL, 1),
    ASC_CHAIN("MQTT_CONNECT", "NEXT", "MQTT_RELEASE", asc_mdl_mqtt_connect, NULL, &asc_mqtt_cfg, NULL, 2),
    
	ASC_CHAIN_LOOP_START(0),
		ASC_CHAIN("MQTT_PUBLISH", "NEXT", "MQTT_DISCONNECT", asc_mdl_mqtt_publish, NULL, &asc_mqtt_msg, NULL, 2),
		ASC_CHAIN_DELAY(30000),
	ASC_CHAIN_LOOP_END,

    //Error handlers
    ASC_CHAIN("MQTT_DISCONNECT", "MQTT_CONNECT", "MQTT_RELEASE", asc_mdl_mqtt_disconnect, NULL, &asc_mqtt_cfg, NULL, 1),
    ASC_CHAIN("MQTT_RELEASE", "MQTT_ACQUIRE", "MQTT_STOP", asc_mdl_mqtt_release, NULL, &asc_mqtt_cfg, NULL, 1),
    ASC_CHAIN("MQTT_STOP", "MQTT_START", "GPRS_DEINIT", asc_mdl_mqtt_stop, NULL, NULL, NULL, 1),
	
    ASC_CHAIN("GPRS_DEINIT", "GPRS_INIT", "MODEM_RESTART", asc_mdl_gprs_deinit, NULL, NULL, NULL, 1),
	ASC_CHAIN("MODEM_RESTART", "INIT_MODEM", "MODEM_RESTART", asc_mdl_modem_reset, NULL, NULL, NULL, 1),
  };

  asc_chain_t* chain = asc_chain_create("MQTT", mqtt_steps, sizeof(mqtt_steps)/sizeof(chain_step_t), &simcom_ctx);
  if(!chain || !asc_chain_start(chain))
  {
    if(chain) (void)asc_chain_destroy_ex(chain);
    return NULL;
  }
  return chain;
}

/*******************************************************************************
 ** \brief  Main function
 ** \param  None
 ** \retval None
 ******************************************************************************/
int main(void)
{
  /* Replace with your actual hardware init */
  // asc_boot();

  /* Initialize ASC library:
   *   - my_printf:     your printf function for debug output
   *   - gsm_proc_send_data: your UART write function
   *   - uart_gsm_ctx.rx_buf: your ring buffer structure
   */
  // asc_init(&simcom_ctx, my_printf, gsm_proc_send_data, (asc_ring_buffer_t*)&uart_gsm_ctx.rx_buf);

  if(!asc_get_init(&simcom_ctx).init)
  {
    /* Initialize simcom_ctx with board-specific callbacks and RX storage first. */
    return -1;
  }
  asc_chain_t* chain = test_chain_init();
  if(!chain) return -1;

  while(1)
  {
    /* The board scheduler must call asc_core_proc(&simcom_ctx) every 10 ms
     * and feed received UART bytes through asc_rx_push(). */

    if(asc_chain_is_running(chain))
    {
      (void)asc_chain_run(chain);
    }
    else break;
  }
  (void)asc_chain_destroy_ex(chain);
  asc_deinit(&simcom_ctx);
  return 0;
}
