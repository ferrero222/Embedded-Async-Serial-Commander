/******************************************************************************
 *                              _    ____   ____                              *
 *                   ======    / \  / ___| / ___| ======       (c)03.10.2025  *
 *                   ======   / _ \ \___ \| |     ======           v1.0.0     *
 *                   ======  / ___ \ ___) | |___  ======                      *
 *                   ====== /_/   \_\____/ \____| ======                      *  
 *                                                                            *
 ******************************************************************************/
#ifndef __ASC_MDL_GPRS_H
#define __ASC_MDL_GPRS_H

/*******************************************************************************
 * Include files
 ******************************************************************************/
/*******************************************************************************
 * Config
 ******************************************************************************/
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
typedef struct asc_mdl_gprs_server_t {
  char mode[4]; 
  char ip[256]; 
  char port[6]; 
} asc_mdl_gprs_server_t;

typedef struct asc_mdl_gprs_data_t {
  char* data; 
  char* answ;
} asc_mdl_gprs_data_t;

/*******************************************************************************
 * Local variable definitions ('static')
 ******************************************************************************/
/*******************************************************************************
 * Function implementation - global ('extern') and local ('static')
 ******************************************************************************/
/*******************************************************************************
 ** @brief  Function init GPRS
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is NULL
 ** @param  meta   Meta data of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool asc_mdl_gprs_init(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 ** @brief  Function to create and config socket. (SINGLE SOCKET)
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is NULL
 ** @param  meta   Meta data of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool asc_mdl_gprs_socket_config(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 ** @brief  Function to connect socket.
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @asc_mdl_gprs_server_t
 **                Should exist only when this function is executing
 ** @param  meta   Meta data of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool asc_mdl_gprs_socket_connect(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 ** @brief  Function to connect socket.
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is @asc_mdl_gprs_server_t
 **                Should exist only when this function is executing
 ** @param  meta   Meta data of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool asc_mdl_gprs_socket_send_recieve(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 ** @brief  Function to disconnect from socket
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is NULL
 ** @param  meta   Meta data of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool asc_mdl_gprs_socket_disconnect(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 ** @brief  Function to deinit gprs
 ** @param  ctx    core context
 ** @param  cb     cb when proc will be done. Can be NULL
 ** @param  param  input param if function is required them. Here is NULL
 ** @param  meta   Meta data of function execution. Will be passe to the cb by the
 **                end of execution. Can be NULL
 ** @return true - proc started, false - smthg is wrong
 ******************************************************************************/
bool asc_mdl_gprs_deinit(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta);

#endif //__ASC_MDL_TCP_H