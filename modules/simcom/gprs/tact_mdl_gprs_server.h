/******************************************************************************
 *                              _    ____   ____                              *
 *                   ======    / \  / ___| / ___| ======       (c)03.10.2025  *
 *                   ======   / _ \ \___ \| |     ======           v1.0.0     *
 *                   ======  / ___ \ ___) | |___  ======                      *
 *                   ====== /_/   \_\____/ \____| ======                      *  
 *                                                                            *
 ******************************************************************************/
#ifndef __TACT_MDL_GPRS_SERVER_H
#define __TACT_MDL_GPRS_SERVER_H

/*******************************************************************************
 * Include files
 ******************************************************************************/
#include <stdint.h> 
#include <stdbool.h> 
#include <string.h>

/*******************************************************************************
 * Local pre-processor symbols/macros ('#define')
 ******************************************************************************/
/*******************************************************************************
 * Global variable definitions (declared in header file with 'extern')
 ******************************************************************************/
typedef void (*tact_stream_data_cb)(uint8_t* data, uint16_t len);

typedef struct {
  uint8_t* buffer;           // Accumulation buffer
  uint16_t buffer_size;      // Total buffer size
  uint16_t data_len;         // Current data length in buffer
  uint16_t expected_len;     // Expected payload length while packet_in_progress
  uint16_t header_len;       // Parsed header length
  bool packet_in_progress;   // Packet parsing in progress flag
  bool callback_in_progress; // Prevents reentrant mutation during packet callback
} tact_gprs_stream_ctx_t;

#define TACT_GPRS_STREAM_CTX_INITIALIZER {0}

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
 ** @brief  Initialize GPRS stream context
 ** @param  tact_ctx      Initialized TACT context
 ** @param  stream_ctx   Zero-initialized with TACT_GPRS_STREAM_CTX_INITIALIZER
 ** @param  packet_size  Total accumulation-buffer capacity
 ** @return true if success, false otherwise
 ******************************************************************************/
bool tact_gprs_stream_ctx_init(tact_context_t* const tact_ctx,  tact_gprs_stream_ctx_t* stream_ctx, uint16_t packet_size);

/*******************************************************************************
 ** @brief  Cleanup GPRS stream context
 ** @param  ctx          Pointer to context
 ******************************************************************************/
void tact_gprs_stream_ctx_cleanup(tact_context_t* const tact_ctx, tact_gprs_stream_ctx_t* stream_ctx);

/*******************************************************************************
 ** @brief  Handle GPRS stream data
 ** @param  ctx          Stream context (per connection)
 ** @param  data         Pointer to incoming data
 ** @param  len          Data length
 ** @param  cb           Callback when full packet found
 ** @return true if data processed successfully
 ******************************************************************************/
bool tact_mld_gprs_server_stream_data_handler(tact_context_t* const tact_ctx,  tact_gprs_stream_ctx_t* stream_ctx, uint8_t* data, uint16_t len, tact_stream_data_cb cb);

 #endif //__TACT_MDL_GPRS_SERVER_H
