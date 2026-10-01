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
#include "tact_mdl_mqtt.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

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
/*******************************************************************************
 * Function implementation - global ('extern') and local ('static')
 ******************************************************************************/
/*******************************************************************************
** @brief Checks if the core context and its subsystems are initialized
** @param ctx Pointer to the application context structure
** @return true - context is valid and fully initialized, false - otherwise
******************************************************************************/
static bool mqtt_context_ready(tact_context_t* const ctx)
{
  return ctx && tact_get_init(ctx).init;
}

/*******************************************************************************
** @brief Validates and calculates string length within a strict maximum limit
** @param text Pointer to the string to be validated
** @param max_len Maximum allowed string length (excluding null-terminator)
** @param length Pointer to store the calculated length of the string
** @return true - string is null-terminated within limits, false - otherwise
******************************************************************************/
static bool mqtt_bounded_length(const char* const text, const size_t max_len, size_t* const length)
{
  if(!text || !length) return false;
  const char* end = (const char*)memchr(text, '\0', max_len + 1u);
  if(!end) return false;
  *length = (size_t)(end - text);
  return true;
}

/*******************************************************************************
** @brief Formats a variadic string into a buffer with strict overflow checking
** @param output Pointer to the destination character buffer
** @param output_size Total capacity of the destination buffer
** @param format Format string (printf-style) followed by arguments
** @return true - string formatted successfully without truncation, false - otherwise
******************************************************************************/
static bool mqtt_format(char* const output, const size_t output_size, const char* const format, ...)
{
  va_list args;
  va_start(args, format);
  int written = vsnprintf(output, output_size, format, args);
  va_end(args);
  return written >= 0 && (size_t)written < output_size;
}


/*******************************************************************************
 ** @brief  Start MQTT service
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is NULL
 ** @param  meta   Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_mqtt_start(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  (void)param;
  if(!mqtt_context_ready(ctx)) return false;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    TACT_ITEM("AT+CMQTTSTART"TACT_CMD_CRLF, "+CMQTTSTART:0", TACT_PARCE_SIMCOM, 3, 1200, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 ** @brief  Stop MQTT service
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is NULL
 ** @param  meta   Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_mqtt_stop(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  (void)param;
  if(!mqtt_context_ready(ctx)) return false;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    TACT_ITEM("AT+CMQTTSTOP"TACT_CMD_CRLF, "+CMQTTSTOP:0", TACT_PARCE_SIMCOM, 3, 1200, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 ** @brief  Acquire an MQTT client
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @tact_mdl_mqtt_cfg_t
 **                Should exist only when this function is executing
 ** @param  meta   Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_mqtt_acquire(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!mqtt_context_ready(ctx) || !param) return false;
  char cmqttaccq[256] = {0};
  const tact_mdl_mqtt_cfg_t* cfg = (const tact_mdl_mqtt_cfg_t*)param;
  size_t client_id_len = 0;
  if(!mqtt_bounded_length(cfg->client_id, TACT_MDL_MQTT_CLIENT_ID_MAX, &client_id_len) || !client_id_len ||
     !mqtt_format(cmqttaccq, sizeof(cmqttaccq), "%sAT+CMQTTACCQ=%u,\"%s\"%s", TACT_CMD_SAVE, (unsigned)cfg->client_index, cfg->client_id, TACT_CMD_CRLF)
  ) {
    return false;
  }
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    TACT_ITEM(cmqttaccq, NULL, TACT_PARCE_SIMCOM, 3, 300, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 ** @brief  Release an MQTT client
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @tact_mdl_mqtt_cfg_t
 **                Should exist only when this function is executing
 ** @param  meta   Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_mqtt_release(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!mqtt_context_ready(ctx) || !param) return false;
  char cmqttrel[32] = {0};
  const tact_mdl_mqtt_cfg_t* cfg = (const tact_mdl_mqtt_cfg_t*)param;
  if(!mqtt_format(cmqttrel, sizeof(cmqttrel), "%sAT+CMQTTREL=%u%s", TACT_CMD_SAVE, (unsigned)cfg->client_index, TACT_CMD_CRLF)) return false;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    TACT_ITEM(cmqttrel, NULL, TACT_PARCE_SIMCOM, 3, 300, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 ** @brief  Connect to MQTT server
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @tact_mdl_mqtt_cfg_t
 **                Should exist only when this function is executing
 ** @param  meta   Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_mqtt_connect(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!mqtt_context_ready(ctx) || !param) return false;
  const tact_mdl_mqtt_cfg_t* cfg = (const tact_mdl_mqtt_cfg_t*)param;
  char cmqttconnect[sizeof(cfg->server_addr) + sizeof(cfg->username) + sizeof(cfg->password) + 64u] = {0};
  size_t server_len = 0;
  size_t username_len = 0;
  if(!mqtt_bounded_length(cfg->server_addr, TACT_MDL_MQTT_SERVER_ADDR_MAX, &server_len) || !server_len ||
     !mqtt_bounded_length(cfg->username, TACT_MDL_MQTT_USER_MAX, &username_len)
  ){ 
    return false;
  }
  if(username_len > 0u)
  {
    size_t password_len = 0;
    if(!mqtt_bounded_length(cfg->password, TACT_MDL_MQTT_PASS_MAX, &password_len)) return false;
    if(!mqtt_format(cmqttconnect, sizeof(cmqttconnect), "%sAT+CMQTTCONNECT=%u,\"%s\",%u,%u,\"%s\",\"%s\"%s",
                    TACT_CMD_SAVE, (unsigned)cfg->client_index, cfg->server_addr, (unsigned)cfg->keepalive, (unsigned)cfg->clean_session, cfg->username, cfg->password, TACT_CMD_CRLF)
    ){
      return false;
    }
  }
  else
  {
    if(!mqtt_format(cmqttconnect, sizeof(cmqttconnect), "%sAT+CMQTTCONNECT=%u,\"%s\",%u,%u%s",
                    TACT_CMD_SAVE, (unsigned)cfg->client_index, cfg->server_addr, (unsigned)cfg->keepalive, (unsigned)cfg->clean_session, TACT_CMD_CRLF)
    ){ 
      return false;
    }
  }
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    TACT_ITEM(cmqttconnect, "+CMQTTCONNECT:", TACT_PARCE_SIMCOM, 5, 3000, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 ** @brief  Disconnect from MQTT server
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @tact_mdl_mqtt_cfg_t
 **                Should exist only when this function is executing
 ** @param  meta   Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_mqtt_disconnect(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!mqtt_context_ready(ctx) || !param) return false;
  char cmqttdisc[64] = {0};
  const tact_mdl_mqtt_cfg_t* cfg = (const tact_mdl_mqtt_cfg_t*)param;
  if(!mqtt_format(cmqttdisc, sizeof(cmqttdisc), "%sAT+CMQTTDISC=%u,20%s", TACT_CMD_SAVE, (unsigned)cfg->client_index, TACT_CMD_CRLF)) return false;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    TACT_ITEM(cmqttdisc, "+CMQTTDISC:", TACT_PARCE_SIMCOM, 3, 3000, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 ** @brief  Publish MQTT message
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @tact_mdl_mqtt_msg_t
 **                Should exist only when this function is executing
 ** @param  meta   Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_mqtt_publish(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!mqtt_context_ready(ctx) || !param) return false;
  bool res = false;
  const tact_mdl_mqtt_msg_t* msg = (const tact_mdl_mqtt_msg_t*)param;
  size_t topic_len = 0;
  size_t payload_len = 0;
  if(!mqtt_bounded_length(msg->topic, TACT_MDL_MQTT_TOPIC_MAX, &topic_len) || !topic_len || !mqtt_bounded_length(msg->payload, TACT_MDL_MQTT_PAYLOAD_MAX, &payload_len)) return false;
  uint32_t wait_ticks = ((uint32_t)msg->pub_timeout + 10u) * 100u;
  if(wait_ticks > UINT16_MAX) wait_ticks = UINT16_MAX;
  const size_t marker_len = strlen(TACT_CMD_SAVE);
  size_t topic_data_size = marker_len + topic_len + 1u;
  size_t payload_data_size = marker_len + payload_len + 1u;
  char topic_cmd[64] = {0};
  char payload_cmd[64] = {0};
  char pub_cmd[128] = {0};
  char* topic_data = (char*)tact_malloc(ctx, topic_data_size);
  char* payload_data = (char*)tact_malloc(ctx, payload_data_size);
  if(!topic_data) return false;
  if(!payload_data) {tact_free(ctx, topic_data); return false; }

  if(!mqtt_format(topic_cmd, sizeof(topic_cmd), "%sAT+CMQTTTOPIC=%u,%zu%s", TACT_CMD_SAVE, (unsigned)msg->client_index, topic_len, TACT_CMD_CRLF) ||
     !mqtt_format(payload_cmd, sizeof(payload_cmd), "%sAT+CMQTTPAYLOAD=%u,%zu%s", TACT_CMD_SAVE, (unsigned)msg->client_index, payload_len, TACT_CMD_CRLF) ||
     !mqtt_format(pub_cmd, sizeof(pub_cmd), "%sAT+CMQTTPUB=%u,%u,%u,%u%s", TACT_CMD_SAVE, (unsigned)msg->client_index, (unsigned)msg->qos, (unsigned)msg->pub_timeout, (unsigned)msg->retained, TACT_CMD_CRLF)
  ){
    tact_free(ctx, topic_data);
    tact_free(ctx, payload_data);
    return false;
  }
  memcpy(topic_data, TACT_CMD_SAVE, marker_len);
  memcpy(topic_data + marker_len, msg->topic, topic_len + 1u);
  memcpy(payload_data, TACT_CMD_SAVE, marker_len);
  memcpy(payload_data + marker_len, msg->payload, payload_len + 1u);

  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    TACT_ITEM(topic_cmd,             ">",    TACT_PARCE_RAW, 3, 500,  0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(topic_data,           NULL,    TACT_PARCE_RAW, 3, 500,  0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(payload_cmd,           ">",    TACT_PARCE_RAW, 3, 500,  0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(payload_data,         NULL,    TACT_PARCE_RAW, 3, 500,  0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(pub_cmd,      "+CMQTTPUB:", TACT_PARCE_SIMCOM, 3, (uint16_t)wait_ticks, 0, 0, NULL, NULL, TACT_NO_ARG),
  };

  if(tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) res = true;
  tact_free(ctx, topic_data);
  tact_free(ctx, payload_data);
  return res;
}

/*******************************************************************************
 ** @brief  Subscribe to an MQTT topic
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @tact_mdl_mqtt_msg_t
 **                Should exist only when this function is executing
 ** @param  meta   Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_mqtt_subscribe(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!mqtt_context_ready(ctx) || !param) return false;
  bool res = false;
  const tact_mdl_mqtt_msg_t* msg = (const tact_mdl_mqtt_msg_t*)param;
  size_t topic_len = 0;
  if(!mqtt_bounded_length(msg->topic, TACT_MDL_MQTT_TOPIC_MAX, &topic_len) || !topic_len) return false;
  const size_t marker_len = strlen(TACT_CMD_SAVE);
  size_t topic_data_size = marker_len + topic_len + 1u;
  char subtopic_cmd[64] = {0};
  char sub_cmd[64] = {0};
  char* topic_data = (char*)tact_malloc(ctx, topic_data_size);
  if(!topic_data) return false;
  if(!mqtt_format(subtopic_cmd, sizeof(subtopic_cmd), "%sAT+CMQTTSUBTOPIC=%u,%zu,%u%s", TACT_CMD_SAVE, (unsigned)msg->client_index, topic_len, (unsigned)msg->qos, TACT_CMD_CRLF) ||
     !mqtt_format(sub_cmd, sizeof(sub_cmd), "%sAT+CMQTTSUB=%u%s", TACT_CMD_SAVE, (unsigned)msg->client_index, TACT_CMD_CRLF)
  ){
    tact_free(ctx, topic_data);
    return false;
  }
  memcpy(topic_data, TACT_CMD_SAVE, marker_len);
  memcpy(topic_data + marker_len, msg->topic, topic_len + 1u);
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    TACT_ITEM(subtopic_cmd, ">",             TACT_PARCE_RAW,    3, 500,  0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(topic_data,   NULL,            TACT_PARCE_RAW,    3, 500,  0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(sub_cmd,      "+CMQTTSUB:",    TACT_PARCE_SIMCOM, 3, 5000, 0, 0, NULL, NULL, TACT_NO_ARG),
  };

  if(tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) res = true;
  tact_free(ctx, topic_data);
  return res;
}

/*******************************************************************************
 ** @brief  Unsubscribe from an MQTT topic
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @tact_mdl_mqtt_msg_t
 **                Should exist only when this function is executing
 ** @param  meta   Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool tact_mdl_mqtt_unsubscribe(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!mqtt_context_ready(ctx) || !param) return false;
  bool res = false;
  const tact_mdl_mqtt_msg_t* msg = (const tact_mdl_mqtt_msg_t*)param;
  size_t topic_len = 0;
  if(!mqtt_bounded_length(msg->topic, TACT_MDL_MQTT_TOPIC_MAX, &topic_len) || !topic_len) return false;
  const size_t marker_len = strlen(TACT_CMD_SAVE);
  size_t topic_data_size = marker_len + topic_len + 1u;
  char unsubtopic_cmd[64] = {0};
  char unsub_cmd[64] = {0};
  char* topic_data = (char*)tact_malloc(ctx, topic_data_size);
  if(!topic_data) return false;
  if(!mqtt_format(unsubtopic_cmd, sizeof(unsubtopic_cmd), "%sAT+CMQTTUNSUBTOPIC=%u,%zu%s", TACT_CMD_SAVE, (unsigned)msg->client_index, topic_len, TACT_CMD_CRLF) ||
     !mqtt_format(unsub_cmd, sizeof(unsub_cmd), "%sAT+CMQTTUNSUB=%u%s", TACT_CMD_SAVE, (unsigned)msg->client_index, TACT_CMD_CRLF))
  {
    tact_free(ctx, topic_data);
    return false;
  }
  memcpy(topic_data, TACT_CMD_SAVE, marker_len);
  memcpy(topic_data + marker_len, msg->topic, topic_len + 1u);
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    TACT_ITEM(unsubtopic_cmd, ">",               TACT_PARCE_RAW,    3, 500,  0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(topic_data,     NULL,              TACT_PARCE_RAW,    3, 500,  0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(unsub_cmd,      "+CMQTTUNSUB:",    TACT_PARCE_SIMCOM, 3, 5000, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) res = true;
  tact_free(ctx, topic_data);
  return res;
}
