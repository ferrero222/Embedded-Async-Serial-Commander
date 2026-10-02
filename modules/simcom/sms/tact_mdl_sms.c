/******************************************************************************
 *              _____      _       ____   _____  ======                      *
 *      ====== |_   _|    / \     / ___| |_   _| ======    (c)03.10.2025     *
 *      ======   | |     / _ \   | |       | |   ======        v1.0.0        *
 *      ======   | |    / ___ \  | |___    | |   ======                      *
 *      ======   |_|   /_/   \_\  \____|   |_|   ======                      *
 *                                                                           *
 ******************************************************************************/
/*******************************************************************************
 * Include files
 ******************************************************************************/
#include "tact_core.h"
#include "tact_mdl_sms.h"
#include "ringslice.h" 
#include <stdio.h>

/*******************************************************************************
 * Global variable definitions (declared in header file with 'extern')
 ******************************************************************************/
/*******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/
static void tact_mdl_sms_cmgr_cb(ringslice_t rs_data, bool result, void* const data);

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
** @brief Checks if the SMS text field contains a valid, non-empty string
** @param value Pointer to the SMS text string to be checked
** @param capacity Maximum capacity of the text buffer
** @return true - string is valid, not empty, and null-terminated within capacity,
**         false - otherwise
******************************************************************************/
static bool tact_mdl_sms_has_text(const char* const value, const size_t capacity)
{
  return value && value[0] && memchr(value, '\0', capacity);
}

/*******************************************************************************
 ** @brief  Function to set sms format
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @tact_mdl_sms_msg_t
 **                Should exist only when this function is executing
 ** @param  meta   Meta data of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_sms_format_set(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !tact_get_init(ctx).init || !param) return false;
  char cmgf[32] = {0};
  const tact_mdl_sms_msg_t* sms = (const tact_mdl_sms_msg_t*)param;
  int written = snprintf(cmgf, sizeof(cmgf), "%sAT+CMGF=%u%s", TACT_CMD_SAVE, (unsigned)sms->format, TACT_CMD_CRLF);
  if(written < 0 || (size_t)written >= sizeof(cmgf)) return false;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    TACT_ITEM(cmgf, NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 ** @brief  Function to set sc
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @tact_mdl_sms_msg_t
 **                Should exist only when this function is executing
 ** @param  meta   Meta data of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_sms_sc_set(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !tact_get_init(ctx).init || !param) return false;
  char csca[96] = {0};
  const tact_mdl_sms_msg_t* sms = (const tact_mdl_sms_msg_t*)param;
  if(!tact_mdl_sms_has_text(sms->num, sizeof(sms->num))) return false;
  int written = snprintf(csca, sizeof(csca), "%sAT+CSCA=\"%s\"%s", TACT_CMD_SAVE, sms->num, TACT_CMD_CRLF);
  if(written < 0 || (size_t)written >= sizeof(csca)) return false;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    TACT_ITEM(csca, NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 ** @brief  Function to send text sms
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @tact_mdl_sms_msg_t
 **                Should exist only when this function is executing
 ** @param  meta   Meta data of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_sms_send_text(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !tact_get_init(ctx).init || !param) return false;
  const tact_mdl_sms_msg_t* sms = (const tact_mdl_sms_msg_t*)param;
  if(!tact_mdl_sms_has_text(sms->num, sizeof(sms->num)) || !tact_mdl_sms_has_text(sms->msg, sizeof(sms->msg))) return false;
  char cmgs[96] = {0};
  char text[sizeof(TACT_CMD_SAVE) + sizeof(sms->msg) + sizeof(TACT_CMD_CTRL_Z) - 2u] = {0};
  int written = snprintf(cmgs, sizeof(cmgs), "%sAT+CMGS=\"%s\"%s", TACT_CMD_SAVE, sms->num, TACT_CMD_CRLF);
  if(written < 0 || (size_t)written >= sizeof(cmgs)) return false;
  written = snprintf(text, sizeof(text), "%s%s%s", TACT_CMD_SAVE, sms->msg, TACT_CMD_CTRL_Z);
  if(written < 0 || (size_t)written >= sizeof(text)) return false;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    TACT_ITEM("AT+CMGF=1"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 1,  150, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CSCS=\"GSM\""TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 2, 500, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(cmgs,                  "> |>",    TACT_PARCE_RAW, 1,  150, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(text,           TACT_CMD_FORCE,    TACT_PARCE_RAW, 1,  150, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(NULL,                "+CMGS:", TACT_PARCE_SIMCOM, 1, 4000, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(NULL,                    "OK", TACT_PARCE_SIMCOM, 1,  100, 1, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(NULL,           TACT_CMD_FORCE,    TACT_PARCE_RAW, 1,    0, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 ** @brief  Function to read SMS
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @tact_mdl_sms_msg_t
 **                Should exist only when this function is executing
 ** @param  meta   Meta data of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_sms_read(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !tact_get_init(ctx).init || !param) return false;
  char cmgr[64] = {0};
  const tact_mdl_sms_msg_t* sms = (const tact_mdl_sms_msg_t*)param;
  int written = snprintf(cmgr, sizeof(cmgr), "%sAT+CMGR=%u%s", TACT_CMD_SAVE, (unsigned)sms->index, TACT_CMD_CRLF);
  if(written < 0 || (size_t)written >= sizeof(cmgr)) return false;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  { 
    TACT_ITEM("AT+CMGF=1"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 1, 150, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(cmgr,                    NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 0, tact_mdl_sms_cmgr_cb, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, sizeof(tact_mdl_sms_msg_t), meta)) return false;
  return true;
}

/**
 *  @brief cmgr cb
 */
static void tact_mdl_sms_cmgr_cb(ringslice_t rs_data, bool result, void* const data)
{
  if(!result || !data) return;
  if(ringslice_is_empty(&rs_data)) return;
  tact_mdl_sms_msg_t* sms = (tact_mdl_sms_msg_t*)data;
  int parsed = ringslice_scanf(&rs_data, "+CMGR: \"%*[^\"]\",\"%63[^\"]\",%*[^\x0d]\x0d\x0a%160[^\x0d]", sms->num, sms->msg);
  if(parsed != 2) {
    sms->num[0] = '\0';
    sms->msg[0] = '\0';
  }
}

/*******************************************************************************
 ** @brief  Function to delete SMS
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @tact_mdl_sms_msg_t
 **                Should exist only when this function is executing
 ** @param  meta   Meta data of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_sms_delete(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !tact_get_init(ctx).init || !param) return false;
  char cmgd[64] = {0};
  const tact_mdl_sms_msg_t* sms = (const tact_mdl_sms_msg_t*)param;
  int written = snprintf(cmgd, sizeof(cmgd), "%sAT+CMGD=%u%s", TACT_CMD_SAVE, (unsigned)sms->index, TACT_CMD_CRLF);
  if(written < 0 || (size_t)written >= sizeof(cmgd)) return false;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  { 
    TACT_ITEM(cmgd, NULL, TACT_PARCE_SIMCOM, 1, 150, 0, 0, NULL, NULL,TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 ** @brief  Function to indicate SMS
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @tact_mdl_sms_msg_t
 **                Should exist only when this function is executing
 ** @param  meta   Meta data of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_sms_indicate(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !tact_get_init(ctx).init || !param) return false;
  char cnmi[64] = {0};
  const tact_mdl_sms_msg_t* sms = (const tact_mdl_sms_msg_t*)param;
  int written = snprintf(cnmi, sizeof(cnmi), "%sAT+CNMI=%u%s", TACT_CMD_SAVE, (unsigned)sms->mode, TACT_CMD_CRLF);
  if(written < 0 || (size_t)written >= sizeof(cnmi)) return false;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  { 
    TACT_ITEM(cnmi, NULL, TACT_PARCE_SIMCOM, 1, 150, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}
