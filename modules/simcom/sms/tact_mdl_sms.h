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
#ifndef __TACT_MDL_SMS_H
#define __TACT_MDL_SMS_H

/*******************************************************************************
 * Local pre-processor symbols/macros ('#define')
 ******************************************************************************/
/*******************************************************************************
 * Global variable definitions (declared in header file with 'extern')
 ******************************************************************************/
/*******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/
/*******************************************************************************
 * Local types definitions
 ******************************************************************************/
/*******************************************************************************
 * Local variable definitions ('static')
 ******************************************************************************/
typedef struct tact_mdl_sms_msg_t {
  uint8_t format;
  char num[64];  
  char msg[161];  /* NUL-terminated text, up to 160 bytes; encoding is caller-owned. */
  uint16_t index;
  uint8_t mode;
} tact_mdl_sms_msg_t;

/*******************************************************************************
 * Function implementation - global ('extern') and local ('static')
 ******************************************************************************/
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
bool tact_mdl_sms_format_set(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta);

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
bool tact_mdl_sms_sc_set(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 ** @brief  Function to send text sms
 **         Sends up to 160 caller-supplied bytes; does not validate the text.
 **         The modem remains in text mode with the GSM charset.
 **         The payload is submitted once; +CMGS confirms success, and the
 **         following OK is consumed when present.
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @tact_mdl_sms_msg_t
 **                Should exist only when this function is executing
 ** @param  meta   Meta data of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_sms_send_text(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta);

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
bool tact_mdl_sms_read(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 ** @brief  Function to delete SMS
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @tact_mdl_sms_msg_t
 **                Should exist only when this function is executing
 ** @param  meta   Meta data of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_sms_delete(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta);

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
bool tact_mdl_sms_indicate(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta);

#endif // __TACT_MDL_SMS_H
