/******************************************************************************
 *                              _    ____   ____                              *
 *                   ======    / \  / ___| / ___| ======       (c)03.10.2025  *
 *                   ======   / _ \ \___ \| |     ======           v1.0.0     *
 *                   ======  / ___ \ ___) | |___  ======                      *
 *                   ====== /_/   \_\____/ \____| ======                      *
 *                                                                            *
 ******************************************************************************/
#ifndef __ASC_MDL_MQTT_H
#define __ASC_MDL_MQTT_H

/*******************************************************************************
 * Include files
 ******************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "asc_core.h"

/*******************************************************************************
 * Local pre-processor symbols/macros ('#define')
 ******************************************************************************/
#ifndef ASC_MDL_MQTT_CLIENT_ID_MAX
  #define ASC_MDL_MQTT_CLIENT_ID_MAX    128u
#endif
#ifndef ASC_MDL_MQTT_SERVER_ADDR_MAX
  #define ASC_MDL_MQTT_SERVER_ADDR_MAX  256u
#endif
#ifndef ASC_MDL_MQTT_USER_MAX
  #define ASC_MDL_MQTT_USER_MAX         256u
#endif
#ifndef ASC_MDL_MQTT_PASS_MAX
  #define ASC_MDL_MQTT_PASS_MAX         256u
#endif
#ifndef ASC_MDL_MQTT_TOPIC_MAX
  #define ASC_MDL_MQTT_TOPIC_MAX        1024u
#endif
#ifndef ASC_MDL_MQTT_PAYLOAD_MAX
  #define ASC_MDL_MQTT_PAYLOAD_MAX      10240u
#endif

/*******************************************************************************
 * Local types definitions
 ******************************************************************************/
/* The caller supplies modem-valid values and AT-safe text. These functions
 * only check required strings, their bounds, and command buffer capacity. */
typedef struct asc_mdl_mqtt_cfg_t {
  uint8_t client_index;                                 // Supported client IDs: 0 or 1.
  char client_id[ASC_MDL_MQTT_CLIENT_ID_MAX + 1];        // NUL-terminated client ID, 1..MAX bytes.
  char server_addr[ASC_MDL_MQTT_SERVER_ADDR_MAX + 1];    // NUL-terminated tcp://host[:port], 9..MAX bytes; TLS is not configured here.
  uint16_t keepalive;                                    // Keep-alive interval in seconds, 1..64800.
  uint8_t clean_session;                                 // 1 requests a clean session; 0 requests a persistent session.
  char username[ASC_MDL_MQTT_USER_MAX + 1];              // Optional NUL-terminated username; empty disables credentials.
  char password[ASC_MDL_MQTT_PASS_MAX + 1];              // NUL-terminated password; requires a non-empty username.
} asc_mdl_mqtt_cfg_t;

typedef struct asc_mdl_mqtt_msg_t {
  uint8_t client_index;          // Supported client IDs: 0 or 1.
  uint8_t qos;                   // MQTT QoS level: 0, 1, or 2.
  const char* topic;             // NUL-terminated topic/filter; copied before return.
  const char* payload;           // NUL-terminated text payload; embedded NUL/binary payloads are unsupported.
  uint8_t retained;              // Publish retain flag: 0 or 1.
  uint16_t pub_timeout;          // Publish response timeout in seconds: 1..180.
} asc_mdl_mqtt_msg_t;

/*******************************************************************************
 * Function declarations
 ******************************************************************************/
/*******************************************************************************
 ** @brief  Start MQTT service
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is NULL
 ** @param  meta   Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool asc_mdl_mqtt_start(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 ** @brief  Stop MQTT service
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is NULL
 ** @param  meta   Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool asc_mdl_mqtt_stop(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 ** @brief  Acquire an MQTT client
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @asc_mdl_mqtt_cfg_t
 **                Should exist only when this function is executing
 ** @param  meta   Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool asc_mdl_mqtt_acquire(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 ** @brief  Release an MQTT client
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @asc_mdl_mqtt_cfg_t
 **                Should exist only when this function is executing
 ** @param  meta   Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool asc_mdl_mqtt_release(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 ** @brief  Connect to MQTT server
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @asc_mdl_mqtt_cfg_t
 **                Should exist only when this function is executing
 ** @param  meta   Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool asc_mdl_mqtt_connect(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 ** @brief  Disconnect from MQTT server
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @asc_mdl_mqtt_cfg_t
 **                Should exist only when this function is executing
 ** @param  meta   Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool asc_mdl_mqtt_disconnect(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 ** @brief  Publish MQTT message
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @asc_mdl_mqtt_msg_t
 **                Should exist only when this function is executing
 ** @param  meta   Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool asc_mdl_mqtt_publish(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 ** @brief  Subscribe to an MQTT topic
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @asc_mdl_mqtt_msg_t
 **                Should exist only when this function is executing
 ** @param  meta   Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool asc_mdl_mqtt_subscribe(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 ** @brief  Unsubscribe from an MQTT topic
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @asc_mdl_mqtt_msg_t
 **                Should exist only when this function is executing
 ** @param  meta   Context of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool asc_mdl_mqtt_unsubscribe(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta);

#endif //__ASC_MDL_MQTT_H
