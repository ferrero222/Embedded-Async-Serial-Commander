/******************************************************************************
 *                              _    ____   ____                              *
 *                   ======    / \  / ___| / ___| ======       (c)03.10.2025  *
 *                   ======   / _ \ \___ \| |     ======           v1.0.0     *
 *                   ======  / ___ \ ___) | |___  ======                      *
 *                   ====== /_/   \_\____/ \____| ======                      *  
 *                                                                            *
 ******************************************************************************/
/*******************************************************************************
 * Include files
 ******************************************************************************/
#include "tact_core.h"
#include "tact_mdl_general.h"
#include "dbc_assert.h"

/*******************************************************************************
 * Local pre-processor symbols/macros ('#define')
 ******************************************************************************/
/*******************************************************************************
 * Global variable definitions (declared in header file with 'extern')
 ******************************************************************************/
/*******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/
static void tact_mdl_general_ceng_cb(ringslice_t rs_data, bool result, void* const data);

/*******************************************************************************
 * Local types definitions
 ******************************************************************************/
/*******************************************************************************
 * Local variable definitions ('static')
 ******************************************************************************/
/*******************************************************************************
 * Function implementation - global ('extern') and local ('static')
 ******************************************************************************/
/*******************************************************************************
 ** @brief  Function to restart the modem
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Here is NULL
 ** @param  param  input param if function is required them. Here is
 ** @param  meta   Meta data of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_modem_reset(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  (void)param;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    TACT_ITEM("AT+CFUN=1,1"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 ** @brief  Function to init modem
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is NULL
 ** @param  meta   Meta data of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_modem_init(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  (void)param;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    TACT_ITEM("AT"TACT_CMD_CRLF,   NULL, TACT_PARCE_SIMCOM, 5, 100, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT"TACT_CMD_CRLF,   NULL, TACT_PARCE_SIMCOM, 5, 100, 0, 1, NULL, NULL, TACT_NO_ARG),  
    TACT_ITEM("ATE1"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 5, 100, 0, 0, NULL, NULL, TACT_NO_ARG),  
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 ** @brief  Function to get real time data info about modem. @tact_mdl_rtd_t
 **         will be passed to the data paramater in callback. Get it there.
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Here is NULL
 ** @param  param  input param if function is required them. Here is NULL
 ** @param  meta   Meta data of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_rtd(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  (void)param;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {   
    TACT_ITEM("AT+GSN"TACT_CMD_CRLF,       NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL,               "%15[^\x0d]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
    TACT_ITEM("AT+GMM"TACT_CMD_CRLF,       NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL,               "%15[^\x0d]", TACT_ARG(tact_mdl_rtd_t, modem_id)),  
    TACT_ITEM("AT+GMR"TACT_CMD_CRLF,       NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL,      "Revision:%29[^\x0d]", TACT_ARG(tact_mdl_rtd_t, modem_rev)),                
    TACT_ITEM("AT+CCLK?"TACT_CMD_CRLF,  "+CCLK", TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL,      "+CCLK: \"%21[^\"]\"", TACT_ARG(tact_mdl_rtd_t, modem_clock)),          
    TACT_ITEM("AT+CCID"TACT_CMD_CRLF,      NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL,               "%21[^\x0d]", TACT_ARG(tact_mdl_rtd_t, sim_iccid)),             
    TACT_ITEM("AT+COPS?"TACT_CMD_CRLF,  "+COPS", TACT_PARCE_SIMCOM, 20, 100, 0, 1, NULL, "+COPS: 0, 0,\"%49[^\"]\"", TACT_ARG(tact_mdl_rtd_t, sim_operator)),            
    TACT_ITEM("AT+CSQ"TACT_CMD_CRLF,     "+CSQ", TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL,                 "+CSQ: %d", TACT_ARG(tact_mdl_rtd_t, sim_rssi)),             
    TACT_ITEM("AT+CENG=3"TACT_CMD_CRLF,    NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL,                       NULL, TACT_NO_ARG),                
    TACT_ITEM("AT+CENG?"TACT_CMD_CRLF,     NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 0, tact_mdl_general_ceng_cb,    NULL, TACT_NO_ARG),                         
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, sizeof(tact_mdl_rtd_t), meta)) return false;
  return true;
}

/**
 *  @brief ceng cb
 */
static void tact_mdl_general_ceng_cb(ringslice_t rs_data, bool result, void* const data)
{
  if(!result) return;
  if(ringslice_is_empty(&rs_data)) return;
  bool header_skip = false;
  tact_mdl_rtd_t* rtd = (tact_mdl_rtd_t*)data;
  while(ringslice_scanf(
    &rs_data, "+CENG: %d,\"%d,%d,%x,%x,", 
    &rtd->modem_lbs[rtd->lbs_cnt].cell, &rtd->modem_lbs[rtd->lbs_cnt].mcc,
    &rtd->modem_lbs[rtd->lbs_cnt].mnc, &rtd->modem_lbs[rtd->lbs_cnt].lac,
    &rtd->modem_lbs[rtd->lbs_cnt].cell_id))
  {
    if(header_skip && rtd->modem_lbs[rtd->lbs_cnt].cell_id != 0 && rtd->modem_lbs[rtd->lbs_cnt].lac != 0) ++rtd->lbs_cnt;
    if(rtd->lbs_cnt >= 7) break;
    ringslice_t rs_temp = {0}; 
    if(header_skip) rs_temp = ringslice_strstr(&rs_data, "\r\n");
    else            rs_temp = ringslice_strstr(&rs_data, "\r\n\r\n");
    if(ringslice_is_empty(&rs_temp)) break;
    rs_data = ringslice_subslice_after(&rs_data, &rs_temp, 0);
    if(ringslice_is_empty(&rs_data)) break;
    header_skip = true;
  }
}