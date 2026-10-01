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
#include "tact_mdl_gprs.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

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
/*******************************************************************************
 * Function implementation - global ('extern') and local ('static')
 ******************************************************************************/
/*******************************************************************************
** @brief Calculates the length of a GPRS field and checks if it fits within capacity
** @param value Pointer to the null-terminated string to be checked
** @param capacity Maximum size of the buffer holding the string
** @param length Pointer to store the calculated length of the string
** @return true - string is null-terminated within capacity, false - otherwise
******************************************************************************/
static bool tact_mdl_gprs_field_length(const char* const value, const size_t capacity, size_t* const length)
{
  if(!value || !length) return false;
  const char* terminator = (const char*)memchr(value, '\0', capacity);
  if(!terminator) return false;
  *length = (size_t)(terminator - value);
  return true;
}


/*******************************************************************************
 ** @brief  Function init GPRS
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is NULL
 ** @param  ctx    Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_gprs_init(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  (void)param;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    TACT_ITEM("AT+COPS?"TACT_CMD_CRLF,               "+COPS: 0", TACT_PARCE_SIMCOM, 5,  100, 1, 2, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+COPS=0"TACT_CMD_CRLF,                    NULL, TACT_PARCE_SIMCOM, 5,  100, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CREG?"TACT_CMD_CRLF,  "+CREG: 0,1|+CREG: 0,5", TACT_PARCE_SIMCOM, 30, 100, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CGATT?"TACT_CMD_CRLF,             "+CGATT: 1", TACT_PARCE_SIMCOM, 30, 100, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 ** @brief  Function to create and config socket. (SINGLE SOCKET)
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is NULL
 ** @param  ctx    Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_gprs_socket_config(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  (void)param;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    TACT_ITEM("AT+CIPMODE?"TACT_CMD_CRLF,        "+CIPMODE: 0", TACT_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIPMODE=0"TACT_CMD_CRLF,                NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIPMUX?"TACT_CMD_CRLF,          "+CIPMUX: 0", TACT_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIPMUX=0"TACT_CMD_CRLF,                 NULL, TACT_PARCE_SIMCOM, 30, 100, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIPSTATUS"TACT_CMD_CRLF,   "STATE: IP START", TACT_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CSTT=\"\",\"\",\"\""TACT_CMD_CRLF,      NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIPSTATUS"TACT_CMD_CRLF, "STATE: IP GPRSACT", TACT_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIICR"TACT_CMD_CRLF,                    NULL, TACT_PARCE_SIMCOM, 30, 100, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIPSTATUS"TACT_CMD_CRLF, "STATE: IP GPRSACT", TACT_PARCE_SIMCOM,  3, 100, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIFSR"TACT_CMD_CRLF,           TACT_CMD_FORCE, TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIPHEAD?"TACT_CMD_CRLF,        "+CIPHEAD: 1", TACT_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIPHEAD=1"TACT_CMD_CRLF,                NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIPSRIP?"TACT_CMD_CRLF,        "+CIPSRIP: 1", TACT_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIPSRIP=1"TACT_CMD_CRLF,                NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIPSHOWTP?"TACT_CMD_CRLF,    "+CIPSHOWTP: 1", TACT_PARCE_SIMCOM,  1, 100, 1, 0, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIPSHOWTP=1"TACT_CMD_CRLF,              NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 ** @brief  Function to connect socket.
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @tact_mdl_gprs_server_t
 **                Should exist only when this function is executing
 ** @param  ctx    Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_gprs_socket_connect(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !tact_get_init(ctx).init || !param) return false;
  const tact_mdl_gprs_server_t* tcp = (const tact_mdl_gprs_server_t*)param;
  size_t length = 0;
  if(!tact_mdl_gprs_field_length(tcp->mode, sizeof(tcp->mode), &length) || !length ||
     !tact_mdl_gprs_field_length(tcp->ip, sizeof(tcp->ip), &length)     || !length ||
     !tact_mdl_gprs_field_length(tcp->port, sizeof(tcp->port), &length) || !length
  ) {
    return false;
  }
  char cipstart[sizeof(TACT_CMD_SAVE) + sizeof("AT+CIPSTART=\"\",\"\",\"\"\r\n") - 1u + sizeof(tcp->mode) + sizeof(tcp->ip) + sizeof(tcp->port) - 3u] = {0};
  int written = snprintf(cipstart, sizeof(cipstart), "%sAT+CIPSTART=\"%s\",\"%s\",\"%s\"%s",  TACT_CMD_SAVE, tcp->mode, tcp->ip, tcp->port, TACT_CMD_CRLF);
  if(written < 0 || (size_t)written >= sizeof(cipstart)) return false;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  { 
    TACT_ITEM("AT+CIPSTATUS"TACT_CMD_CRLF, "STATE: IP STATUS|STATE: TCP CLOSED", TACT_PARCE_SIMCOM, 10, 100,  0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(cipstart,                           "CONNECT OK|ALREADY CONNECT", TACT_PARCE_SIMCOM,  6, 500,  0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIPSTATUS"TACT_CMD_CRLF,                  "STATE: CONNECT OK", TACT_PARCE_SIMCOM, 10, 100,  0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIPSEND?"TACT_CMD_CRLF,                           "+CIPSEND:", TACT_PARCE_SIMCOM, 10, 100,  0, 1, NULL, NULL, TACT_NO_ARG),         
    TACT_ITEM("AT+CIPQSEND?"TACT_CMD_CRLF,                       "+CIPQSEND: 0", TACT_PARCE_SIMCOM,  1, 100,  1, 0, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIPQSEND=0"TACT_CMD_CRLF,                                NULL, TACT_PARCE_SIMCOM, 10, 100,  0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 ** @brief  Function to connect socket.
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is ptr to char* data
 **                Should exist only when this function is executing
 ** @param  ctx    Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_gprs_socket_send_recieve(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !tact_get_init(ctx).init || !param) return false;
  const tact_mdl_gprs_data_t* tcp = (const tact_mdl_gprs_data_t*)param;
  if(!tcp->data) return false;
  const size_t marker_len = strlen(TACT_CMD_SAVE);
  const size_t ctrl_z_len = strlen(TACT_CMD_CTRL_Z);
  size_t data_len = 0;
  if(!tact_mdl_gprs_field_length(tcp->data, UINT16_MAX - marker_len - ctrl_z_len + 1u, &data_len)) return false;
  size_t size = marker_len + data_len + ctrl_z_len + 1u;
  char cipsend[48] = {0};
  char* datacmd = (char*)tact_malloc(ctx, size);
  if(!datacmd) return false;
  size_t answer_len = 0;
  char* answer_prefix = NULL;
  if(tcp->answ && !tact_mdl_gprs_field_length(tcp->answ, UINT16_MAX - marker_len + 1u, &answer_len))
  {
    tact_free(ctx, datacmd);
    return false;
  }
  if(answer_len)
  {
    answer_prefix = (char*)tact_malloc(ctx, marker_len + answer_len + 1u);
    if(!answer_prefix) 
    {
      tact_free(ctx, datacmd);
      return false;
    }
    memcpy(answer_prefix, TACT_CMD_SAVE, marker_len);
    memcpy(answer_prefix + marker_len, tcp->answ, answer_len + 1u);
  }
  int written = snprintf(cipsend, sizeof(cipsend), "%sAT+CIPSEND=%zu%s", TACT_CMD_SAVE, data_len + ctrl_z_len, TACT_CMD_CRLF);
  if(written < 0 || (size_t)written >= sizeof(cipsend)) 
  {
    tact_free(ctx, answer_prefix);
    tact_free(ctx, datacmd);
    return false;
  }
  written = snprintf(datacmd, size, "%s%s%s", TACT_CMD_SAVE, tcp->data, TACT_CMD_CTRL_Z);
  if(written < 0 || (size_t)written >= size) {
    tact_free(ctx, answer_prefix);
    tact_free(ctx, datacmd);
    return false;
  }
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    TACT_ITEM("AT+CIPSTATUS"TACT_CMD_CRLF, "STATE: CONNECT OK",  TACT_PARCE_SIMCOM, 5, 100,  0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(cipsend,                        "AT+CIPSEND=&>",     TACT_PARCE_RAW, 3, 500,  0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(datacmd,                          answer_prefix,     TACT_PARCE_RAW, 3, 500,  0, 1, NULL, NULL, TACT_NO_ARG),
  };
  bool res = tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta);
  if(answer_prefix) tact_free(ctx, answer_prefix);
  tact_free(ctx, datacmd);
  return res;
}

/*******************************************************************************
 ** @brief  Function to disconnect from socket
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is NULL
 ** @param  ctx    Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_gprs_socket_disconnect(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  (void)param;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    TACT_ITEM("AT+CIPCLOSE=1"TACT_CMD_CRLF, "CLOSE OK", TACT_PARCE_SIMCOM, 10, 100, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 ** @brief  Function to deinit gprs
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is NULL
 ** @param  ctx    Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_gprs_deinit(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  (void)param;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    TACT_ITEM("AT+CIPSHUT"TACT_CMD_CRLF, "SHUT OK", TACT_PARCE_SIMCOM, 2, 100, 0, 1, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}
