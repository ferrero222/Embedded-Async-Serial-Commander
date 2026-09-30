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
#include "asc_core.h"
#include "asc_mdl_sms.h"
#include "ringslice.h" 
#include <stdio.h>

/*******************************************************************************
 * Global variable definitions (declared in header file with 'extern')
 ******************************************************************************/
/*******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/
static void asc_mdl_sms_cmgr_cb(ringslice_t rs_data, bool result, void* const data);

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
static bool asc_mdl_sms_has_text(const char* const value, const size_t capacity)
{
  return value && value[0] && memchr(value, '\0', capacity);
}

/*******************************************************************************
 ** @brief  Function to set sms format
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @asc_mdl_sms_msg_t
 **                Should exist only when this function is executing
 ** @param  meta   Meta data of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool asc_mdl_sms_format_set(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !asc_get_init(ctx).init || !param) return false;
  char cmgf[32] = {0};
  const asc_mdl_sms_msg_t* sms = (const asc_mdl_sms_msg_t*)param;
  int written = snprintf(cmgf, sizeof(cmgf), "%sAT+CMGF=%u%s", ASC_CMD_SAVE, (unsigned)sms->format, ASC_CMD_CRLF);
  if(written < 0 || (size_t)written >= sizeof(cmgf)) return false;
  asc_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    ASC_ITEM(cmgf, NULL, ASC_PARCE_SIMCOM, 2, 150, 0, 0, NULL, NULL, ASC_NO_ARG),
  };
  if(!asc_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 ** @brief  Function to set sc
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @asc_mdl_sms_msg_t
 **                Should exist only when this function is executing
 ** @param  meta   Meta data of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool asc_mdl_sms_sc_set(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !asc_get_init(ctx).init || !param) return false;
  char csca[96] = {0};
  const asc_mdl_sms_msg_t* sms = (const asc_mdl_sms_msg_t*)param;
  if(!asc_mdl_sms_has_text(sms->num, sizeof(sms->num))) return false;
  int written = snprintf(csca, sizeof(csca), "%sAT+CSCA=\"%s\"%s", ASC_CMD_SAVE, sms->num, ASC_CMD_CRLF);
  if(written < 0 || (size_t)written >= sizeof(csca)) return false;
  asc_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    ASC_ITEM(csca, NULL, ASC_PARCE_SIMCOM, 2, 150, 0, 0, NULL, NULL, ASC_NO_ARG),
  };
  if(!asc_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 ** @brief  Function to send text sms
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @asc_mdl_sms_msg_t
 **                Should exist only when this function is executing
 ** @param  meta   Meta data of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool asc_mdl_sms_send_text(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !asc_get_init(ctx).init || !param) return false;
  const asc_mdl_sms_msg_t* sms = (const asc_mdl_sms_msg_t*)param;
  if(!asc_mdl_sms_has_text(sms->num, sizeof(sms->num)) || !asc_mdl_sms_has_text(sms->msg, sizeof(sms->msg))) return false;
  char cmgs[96] = {0};
  char text[sizeof(ASC_CMD_SAVE) + sizeof(sms->msg) + sizeof(ASC_CMD_CTRL_Z) - 2u] = {0};
  int written = snprintf(cmgs, sizeof(cmgs), "%sAT+CMGS=\"%s\"%s", ASC_CMD_SAVE, sms->num, ASC_CMD_CRLF);
  if(written < 0 || (size_t)written >= sizeof(cmgs)) return false;
  written = snprintf(text, sizeof(text), "%s%s%s", ASC_CMD_SAVE, sms->msg, ASC_CMD_CTRL_Z);
  if(written < 0 || (size_t)written >= sizeof(text)) return false;
  asc_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    ASC_ITEM("AT+CMGF=1"ASC_CMD_CRLF, NULL, ASC_PARCE_SIMCOM, 1,  150, 0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CSCS=\"GSM\""ASC_CMD_CRLF, NULL, ASC_PARCE_SIMCOM, 2, 500, 0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM(cmgs,                  "> |>",    ASC_PARCE_RAW, 1,  150, 0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM(text,           ASC_CMD_FORCE,    ASC_PARCE_RAW, 1,  150, 0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM(NULL,                "+CMGS:", ASC_PARCE_SIMCOM, 1, 4000, 0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM(NULL,                    "OK", ASC_PARCE_SIMCOM, 1,  100, 1, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM(NULL,           ASC_CMD_FORCE,    ASC_PARCE_RAW, 1,    0, 0, 0, NULL, NULL, ASC_NO_ARG),
  };
  if(!asc_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 ** @brief  Function to read SMS
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @asc_mdl_sms_msg_t
 **                Should exist only when this function is executing
 ** @param  meta   Meta data of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool asc_mdl_sms_read(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !asc_get_init(ctx).init || !param) return false;
  char cmgr[64] = {0};
  const asc_mdl_sms_msg_t* sms = (const asc_mdl_sms_msg_t*)param;
  int written = snprintf(cmgr, sizeof(cmgr), "%sAT+CMGR=%u%s", ASC_CMD_SAVE, (unsigned)sms->index, ASC_CMD_CRLF);
  if(written < 0 || (size_t)written >= sizeof(cmgr)) return false;
  asc_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  { 
    ASC_ITEM("AT+CMGF=1"ASC_CMD_CRLF, NULL, ASC_PARCE_SIMCOM, 1, 150, 0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM(cmgr,                    NULL, ASC_PARCE_SIMCOM, 2, 150, 0, 0, asc_mdl_sms_cmgr_cb, NULL, ASC_NO_ARG),
  };
  if(!asc_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, sizeof(asc_mdl_sms_msg_t), meta)) return false;
  return true;
}

/**
 *  @brief cmgr cb
 */
static void asc_mdl_sms_cmgr_cb(ringslice_t rs_data, bool result, void* const data)
{
  if(!result || !data) return;
  if(ringslice_is_empty(&rs_data)) return;
  asc_mdl_sms_msg_t* sms = (asc_mdl_sms_msg_t*)data;
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
 ** @param  param  input param if function is required them. Here is @asc_mdl_sms_msg_t
 **                Should exist only when this function is executing
 ** @param  meta   Meta data of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool asc_mdl_sms_delete(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !asc_get_init(ctx).init || !param) return false;
  char cmgd[64] = {0};
  const asc_mdl_sms_msg_t* sms = (const asc_mdl_sms_msg_t*)param;
  int written = snprintf(cmgd, sizeof(cmgd), "%sAT+CMGD=%u%s", ASC_CMD_SAVE, (unsigned)sms->index, ASC_CMD_CRLF);
  if(written < 0 || (size_t)written >= sizeof(cmgd)) return false;
  asc_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  { 
    ASC_ITEM(cmgd, NULL, ASC_PARCE_SIMCOM, 1, 150, 0, 0, NULL, NULL,ASC_NO_ARG),
  };
  if(!asc_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 ** @brief  Function to indicate SMS
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @asc_mdl_sms_msg_t
 **                Should exist only when this function is executing
 ** @param  meta   Meta data of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool asc_mdl_sms_indicate(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !asc_get_init(ctx).init || !param) return false;
  char cnmi[64] = {0};
  const asc_mdl_sms_msg_t* sms = (const asc_mdl_sms_msg_t*)param;
  int written = snprintf(cnmi, sizeof(cnmi), "%sAT+CNMI=%u%s", ASC_CMD_SAVE, (unsigned)sms->mode, ASC_CMD_CRLF);
  if(written < 0 || (size_t)written >= sizeof(cnmi)) return false;
  asc_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  { 
    ASC_ITEM(cnmi, NULL, ASC_PARCE_SIMCOM, 1, 150, 0, 0, NULL, NULL, ASC_NO_ARG),
  };
  if(!asc_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}
