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
#include "asc_mdl_gprs.h"
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
static bool asc_mdl_gprs_field_length(const char* const value, const size_t capacity, size_t* const length)
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
bool asc_mdl_gprs_init(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta)
{
  (void)param;
  asc_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    ASC_ITEM("AT+COPS?"ASC_CMD_CRLF,               "+COPS: 0", ASC_PARCE_SIMCOM, 5,  100, 1, 2, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+COPS=0"ASC_CMD_CRLF,                    NULL, ASC_PARCE_SIMCOM, 5,  100, 0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CREG?"ASC_CMD_CRLF,  "+CREG: 0,1|+CREG: 0,5", ASC_PARCE_SIMCOM, 30, 100, 0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CGATT?"ASC_CMD_CRLF,             "+CGATT: 1", ASC_PARCE_SIMCOM, 30, 100, 0, 0, NULL, NULL, ASC_NO_ARG),
  };
  if(!asc_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
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
bool asc_mdl_gprs_socket_config(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta)
{
  (void)param;
  asc_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    ASC_ITEM("AT+CIPMODE?"ASC_CMD_CRLF,        "+CIPMODE: 0", ASC_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CIPMODE=0"ASC_CMD_CRLF,                NULL, ASC_PARCE_SIMCOM, 10, 100, 0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CIPMUX?"ASC_CMD_CRLF,          "+CIPMUX: 0", ASC_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CIPMUX=0"ASC_CMD_CRLF,                 NULL, ASC_PARCE_SIMCOM, 30, 100, 0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CIPSTATUS"ASC_CMD_CRLF,   "STATE: IP START", ASC_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CSTT=\"\",\"\",\"\""ASC_CMD_CRLF,      NULL, ASC_PARCE_SIMCOM, 10, 100, 0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CIPSTATUS"ASC_CMD_CRLF, "STATE: IP GPRSACT", ASC_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CIICR"ASC_CMD_CRLF,                    NULL, ASC_PARCE_SIMCOM, 30, 100, 0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CIPSTATUS"ASC_CMD_CRLF, "STATE: IP GPRSACT", ASC_PARCE_SIMCOM,  3, 100, 0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CIFSR"ASC_CMD_CRLF,           ASC_CMD_FORCE, ASC_PARCE_SIMCOM, 10, 100, 0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CIPHEAD?"ASC_CMD_CRLF,        "+CIPHEAD: 1", ASC_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CIPHEAD=1"ASC_CMD_CRLF,                NULL, ASC_PARCE_SIMCOM, 10, 100, 0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CIPSRIP?"ASC_CMD_CRLF,        "+CIPSRIP: 1", ASC_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CIPSRIP=1"ASC_CMD_CRLF,                NULL, ASC_PARCE_SIMCOM, 10, 100, 0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CIPSHOWTP?"ASC_CMD_CRLF,    "+CIPSHOWTP: 1", ASC_PARCE_SIMCOM,  1, 100, 1, 0, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CIPSHOWTP=1"ASC_CMD_CRLF,              NULL, ASC_PARCE_SIMCOM, 10, 100, 0, 0, NULL, NULL, ASC_NO_ARG),
  };
  if(!asc_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 ** @brief  Function to connect socket.
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @asc_mdl_gprs_server_t
 **                Should exist only when this function is executing
 ** @param  ctx    Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool asc_mdl_gprs_socket_connect(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !asc_get_init(ctx).init || !param) return false;
  const asc_mdl_gprs_server_t* tcp = (const asc_mdl_gprs_server_t*)param;
  size_t length = 0;
  if(!asc_mdl_gprs_field_length(tcp->mode, sizeof(tcp->mode), &length) || !length ||
     !asc_mdl_gprs_field_length(tcp->ip, sizeof(tcp->ip), &length)     || !length ||
     !asc_mdl_gprs_field_length(tcp->port, sizeof(tcp->port), &length) || !length
  ) {
    return false;
  }
  char cipstart[sizeof(ASC_CMD_SAVE) + sizeof("AT+CIPSTART=\"\",\"\",\"\"\r\n") - 1u + sizeof(tcp->mode) + sizeof(tcp->ip) + sizeof(tcp->port) - 3u] = {0};
  int written = snprintf(cipstart, sizeof(cipstart), "%sAT+CIPSTART=\"%s\",\"%s\",\"%s\"%s",  ASC_CMD_SAVE, tcp->mode, tcp->ip, tcp->port, ASC_CMD_CRLF);
  if(written < 0 || (size_t)written >= sizeof(cipstart)) return false;
  asc_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  { 
    ASC_ITEM("AT+CIPSTATUS"ASC_CMD_CRLF, "STATE: IP STATUS|STATE: TCP CLOSED", ASC_PARCE_SIMCOM, 10, 100,  0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM(cipstart,                           "CONNECT OK|ALREADY CONNECT", ASC_PARCE_SIMCOM,  6, 500,  0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CIPSTATUS"ASC_CMD_CRLF,                  "STATE: CONNECT OK", ASC_PARCE_SIMCOM, 10, 100,  0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CIPSEND?"ASC_CMD_CRLF,                           "+CIPSEND:", ASC_PARCE_SIMCOM, 10, 100,  0, 1, NULL, NULL, ASC_NO_ARG),         
    ASC_ITEM("AT+CIPQSEND?"ASC_CMD_CRLF,                       "+CIPQSEND: 0", ASC_PARCE_SIMCOM,  1, 100,  1, 0, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CIPQSEND=0"ASC_CMD_CRLF,                                NULL, ASC_PARCE_SIMCOM, 10, 100,  0, 0, NULL, NULL, ASC_NO_ARG),
  };
  if(!asc_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
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
bool asc_mdl_gprs_socket_send_recieve(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !asc_get_init(ctx).init || !param) return false;
  const asc_mdl_gprs_data_t* tcp = (const asc_mdl_gprs_data_t*)param;
  if(!tcp->data) return false;
  const size_t marker_len = strlen(ASC_CMD_SAVE);
  const size_t ctrl_z_len = strlen(ASC_CMD_CTRL_Z);
  size_t data_len = 0;
  if(!asc_mdl_gprs_field_length(tcp->data, UINT16_MAX - marker_len - ctrl_z_len + 1u, &data_len)) return false;
  size_t size = marker_len + data_len + ctrl_z_len + 1u;
  char cipsend[48] = {0};
  char* datacmd = (char*)asc_malloc(ctx, size);
  if(!datacmd) return false;
  size_t answer_len = 0;
  char* answer_prefix = NULL;
  if(tcp->answ && !asc_mdl_gprs_field_length(tcp->answ, UINT16_MAX - marker_len + 1u, &answer_len))
  {
    asc_free(ctx, datacmd);
    return false;
  }
  if(answer_len)
  {
    answer_prefix = (char*)asc_malloc(ctx, marker_len + answer_len + 1u);
    if(!answer_prefix) 
    {
      asc_free(ctx, datacmd);
      return false;
    }
    memcpy(answer_prefix, ASC_CMD_SAVE, marker_len);
    memcpy(answer_prefix + marker_len, tcp->answ, answer_len + 1u);
  }
  int written = snprintf(cipsend, sizeof(cipsend), "%sAT+CIPSEND=%zu%s", ASC_CMD_SAVE, data_len + ctrl_z_len, ASC_CMD_CRLF);
  if(written < 0 || (size_t)written >= sizeof(cipsend)) 
  {
    asc_free(ctx, answer_prefix);
    asc_free(ctx, datacmd);
    return false;
  }
  written = snprintf(datacmd, size, "%s%s%s", ASC_CMD_SAVE, tcp->data, ASC_CMD_CTRL_Z);
  if(written < 0 || (size_t)written >= size) {
    asc_free(ctx, answer_prefix);
    asc_free(ctx, datacmd);
    return false;
  }
  asc_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    ASC_ITEM("AT+CIPSTATUS"ASC_CMD_CRLF, "STATE: CONNECT OK",  ASC_PARCE_SIMCOM, 5, 100,  0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM(cipsend,                        "AT+CIPSEND=&>",     ASC_PARCE_RAW, 3, 500,  0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM(datacmd,                          answer_prefix,     ASC_PARCE_RAW, 3, 500,  0, 1, NULL, NULL, ASC_NO_ARG),
  };
  bool res = asc_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta);
  if(answer_prefix) asc_free(ctx, answer_prefix);
  asc_free(ctx, datacmd);
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
bool asc_mdl_gprs_socket_disconnect(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta)
{
  (void)param;
  asc_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    ASC_ITEM("AT+CIPCLOSE=1"ASC_CMD_CRLF, "CLOSE OK", ASC_PARCE_SIMCOM, 10, 100, 0, 0, NULL, NULL, ASC_NO_ARG),
  };
  if(!asc_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
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
bool asc_mdl_gprs_deinit(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta)
{
  (void)param;
  asc_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    ASC_ITEM("AT+CIPSHUT"ASC_CMD_CRLF, "SHUT OK", ASC_PARCE_SIMCOM, 2, 100, 0, 1, NULL, NULL, ASC_NO_ARG),
  };
  if(!asc_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}
