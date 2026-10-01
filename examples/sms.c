/*******************************************************************************
 *                              TACT Example                         26.11.2025 *
 *                                 v1.0                                        *
 *       This example is showing how to catch incoming sms URC, get msg from   *
 *       it and send back as echo.                                             *
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
#include "tact_mdl_sms.h"

/*******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/
static void tact_sms_urc_cb(const ringslice_t urc_slice);
static void tact_sms_read_cb(const bool result, void* const ctx, const void* const data);

/*******************************************************************************
 * Local variable definitions ('static')
 ******************************************************************************/
tact_context_t simcom_ctx = {0};

/*******************************************************************************
 * Function implementation - global ('extern') and local ('static')
 ******************************************************************************/
/*******************************************************************************
 ** @brief  Static methods for sms
 ** @param  None
 ** @return None
 ******************************************************************************/
static tact_urc_queue_t test_urc_sms = {"+CMTI:", tact_sms_urc_cb};

/* Get sms */
static void tact_sms_urc_cb(const ringslice_t urc_slice)
{
  tact_mdl_sms_msg_t sms = {0};
  int index = 0;
  if(ringslice_scanf(&urc_slice, "+CMTI:%*[^,],%d\x0d", &index) == 1 && index > 0 && index <= UINT16_MAX)
  {
    sms.index = (uint16_t)index;
    tact_mdl_sms_read(&simcom_ctx, tact_sms_read_cb, &sms, NULL);
  }
}

/* Send echo sms */
static void tact_sms_read_cb(const bool result, void* const ctx, const void* const data)
{
  if(!result) return;
  tact_mdl_sms_msg_t* sms = (tact_mdl_sms_msg_t*)data;
  tact_mdl_sms_send_text(&simcom_ctx, NULL, sms, NULL);
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
  tact_mdl_modem_init(&simcom_ctx, NULL, NULL, NULL);
  tact_urc_enqueue(&simcom_ctx, &test_urc_sms);
  while(1)
  {
    tact_timers_proc(); //proc programm timers (10ms included inside of it)
  }
}






