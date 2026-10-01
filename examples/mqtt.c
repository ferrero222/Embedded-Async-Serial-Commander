/*******************************************************************************
 *                              TACT Example                         21.11.2025 *
 *                                 v1.0                                        *
 *       This example is showing how to connect to MQTT broker, publish        *
 *       a constant message and disconnect, using one single chain and         *
 *       ready-made MQTT modules from TACT.                                     *
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

#include "tact_core.h"
#include "tact_port.h"
#include "tact_mdl_general.h"
#include "tact_mdl_gprs.h"
#include "tact_mdl_mqtt.h"
#include "tact_chain.h"

/*******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/
/*******************************************************************************
 * Local variable definitions ('static')
 ******************************************************************************/
tact_context_t simcom_ctx = {0};

/*******************************************************************************
 * MQTT configuration
 ******************************************************************************/
/* Fill in your broker settings here */
static tact_mdl_mqtt_cfg_t tact_mqtt_cfg =
{
  .client_index  = 0,
  .client_id     = "tact_device_001",
  .server_addr   = "tcp://broker.emqx.io:1883",
  .keepalive     = 60,
  .clean_session = 1,
  .username      = "",
  .password      = "",
};

/* Message to publish */
static tact_mdl_mqtt_msg_t tact_mqtt_msg =
{
  .client_index = 0,
  .qos          = 1,
  .topic        = "/tact/test",
  .payload      = "Hello from TACT!",
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
tact_chain_t* test_chain_init(void)
{
  chain_step_t mqtt_steps[] =
  {
    //Main flow
    TACT_CHAIN("INIT_MODEM", "NEXT", "MODEM_RESTART", tact_mdl_modem_init,  NULL, NULL,  NULL, 1),
    TACT_CHAIN("GPRS_INIT", "NEXT", "GPRS_DEINIT", tact_mdl_gprs_init, NULL, NULL, NULL, 1),
    TACT_CHAIN("SOCKET_CONFIG", "NEXT", "GPRS_DEINIT", tact_mdl_gprs_socket_config, NULL, NULL, NULL, 1),

    TACT_CHAIN("MQTT_START", "NEXT", "GPRS_DEINIT", tact_mdl_mqtt_start, NULL, NULL, NULL, 1),
    TACT_CHAIN("MQTT_ACQUIRE", "NEXT", "MQTT_STOP", tact_mdl_mqtt_acquire, NULL, &tact_mqtt_cfg, NULL, 1),
    TACT_CHAIN("MQTT_CONNECT", "NEXT", "MQTT_RELEASE", tact_mdl_mqtt_connect, NULL, &tact_mqtt_cfg, NULL, 2),
    
	TACT_CHAIN_LOOP_START(0),
		TACT_CHAIN("MQTT_PUBLISH", "NEXT", "MQTT_DISCONNECT", tact_mdl_mqtt_publish, NULL, &tact_mqtt_msg, NULL, 2),
		TACT_CHAIN_DELAY(30000),
	TACT_CHAIN_LOOP_END,

    //Error handlers
    TACT_CHAIN("MQTT_DISCONNECT", "MQTT_CONNECT", "MQTT_RELEASE", tact_mdl_mqtt_disconnect, NULL, &tact_mqtt_cfg, NULL, 1),
    TACT_CHAIN("MQTT_RELEASE", "MQTT_ACQUIRE", "MQTT_STOP", tact_mdl_mqtt_release, NULL, &tact_mqtt_cfg, NULL, 1),
    TACT_CHAIN("MQTT_STOP", "MQTT_START", "GPRS_DEINIT", tact_mdl_mqtt_stop, NULL, NULL, NULL, 1),
	
    TACT_CHAIN("GPRS_DEINIT", "GPRS_INIT", "MODEM_RESTART", tact_mdl_gprs_deinit, NULL, NULL, NULL, 1),
	TACT_CHAIN("MODEM_RESTART", "INIT_MODEM", "MODEM_RESTART", tact_mdl_modem_reset, NULL, NULL, NULL, 1),
  };

  tact_chain_t* chain = tact_chain_create("MQTT", mqtt_steps, sizeof(mqtt_steps)/sizeof(chain_step_t), &simcom_ctx);
  if(!chain || !tact_chain_start(chain))
  {
    if(chain) (void)tact_chain_destroy_ex(chain);
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
  // tact_boot();

  /* Initialize TACT library:
   *   - my_printf:     your printf function for debug output
   *   - gsm_proc_send_data: your UART write function
   *   - uart_gsm_ctx.rx_buf: your ring buffer structure
   */
  // tact_init(&simcom_ctx, my_printf, gsm_proc_send_data, (tact_ring_buffer_t*)&uart_gsm_ctx.rx_buf);

  if(!tact_get_init(&simcom_ctx).init)
  {
    /* Initialize simcom_ctx with board-specific callbacks and RX storage first. */
    return -1;
  }
  tact_chain_t* chain = test_chain_init();
  if(!chain) return -1;

  while(1)
  {
    /* The board scheduler must call tact_core_proc(&simcom_ctx) every 10 ms
     * and feed received UART bytes through tact_rx_push(). */

    if(tact_chain_is_running(chain))
    {
      (void)tact_chain_run(chain);
    }
    else break;
  }
  (void)tact_chain_destroy_ex(chain);
  tact_deinit(&simcom_ctx);
  return 0;
}
