/*******************************************************************************
 *                              ASC Example                         26.11.2025 *
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
#include "asc_core.h"
#include "asc_mdl_general.h"
#include "asc_mdl_sms.h"

/*******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/
static void asc_sms_urc_cb(const ringslice_t urc_slice);
static void asc_sms_read_cb(const bool result, void* const ctx, const void* const data);

/*******************************************************************************
 * Local variable definitions ('static')
 ******************************************************************************/
asc_context_t simcom_ctx = {0};

/*******************************************************************************
 * Function implementation - global ('extern') and local ('static')
 ******************************************************************************/
/*******************************************************************************
 ** @brief  Static methods for sms
 ** @param  None
 ** @return None
 ******************************************************************************/
static asc_urc_queue_t test_urc_sms = {"+CMTI:", asc_sms_urc_cb};

/* Get sms */
static void asc_sms_urc_cb(const ringslice_t urc_slice)
{
  asc_mdl_sms_msg_t sms = {0};
  int index = 0;
  if(ringslice_scanf(&urc_slice, "+CMTI:%*[^,],%d\x0d", &index) == 1 && index > 0 && index <= UINT16_MAX)
  {
    sms.index = (uint16_t)index;
    asc_mdl_sms_read(&simcom_ctx, asc_sms_read_cb, &sms, NULL);
  }
}

/* Send echo sms */
static void asc_sms_read_cb(const bool result, void* const ctx, const void* const data)
{
  if(!result) return;
  asc_mdl_sms_msg_t* sms = (asc_mdl_sms_msg_t*)data;
  asc_mdl_sms_send_text(&simcom_ctx, NULL, sms, NULL);
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
  asc_mdl_modem_init(&simcom_ctx, NULL, NULL, NULL);
  asc_urc_enqueue(&simcom_ctx, &test_urc_sms);
  while(1)
  {
    asc_timers_proc(); //proc programm timers (10ms included inside of it)
  }
}






