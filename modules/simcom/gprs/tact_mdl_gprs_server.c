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
#include "tact_mdl_gprs_server.h"
#include <string.h>
#include "tact_port.h"

/*******************************************************************************
 * Global variable definitions (declared in header file with 'extern')
 ******************************************************************************/
/*******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/
/*******************************************************************************
 * Local types definitions
 ******************************************************************************/
 typedef enum {
  IPD_HEADER_INCOMPLETE,
  IPD_HEADER_INVALID,
  IPD_HEADER_VALID,
} ipd_header_result_t;

/*******************************************************************************
 * Local variable definitions ('static')
 ******************************************************************************/
/*******************************************************************************
 * Function implementation - global ('extern') and local ('static')
 ******************************************************************************/
/*******************************************************************************
 ** @brief  Find IPD header in buffer
 ** @param  data Buffer to search
 ** @param  len  Buffer length
 ** @return Pointer to start of IPD header or NULL if not found
 ******************************************************************************/
static uint8_t* find_ipd_header(uint8_t* data, uint16_t len)
{
  for(uint32_t i = 0; i + 3u < len; ++i)
  {
    if(data[i] == '+' && data[i+1] == 'I' && data[i+2] == 'P' && data[i+3] == 'D') return data + i;
  }
  return NULL;
}

/*******************************************************************************
 ** @brief  Parse IPD header
 ** @param  header_start Start of IPD header
 ** @param  data_len     Length of available data from header start
 ** @param  out_payload_len  Output parsed payload length
 ** @param  out_header_len   Output parsed header length
 ** @return true if header parsed successfully
 ******************************************************************************/
static ipd_header_result_t parse_ipd_header(const uint8_t* header_start, uint16_t data_len, uint16_t buffer_size, uint16_t* out_payload_len, uint16_t* out_header_len)
{
  static const uint8_t suffix[] = "TCP:";
  uint32_t pos = 4;
  uint32_t payload_len = 0;
  if(data_len < 4) return IPD_HEADER_INCOMPLETE;
  if(memcmp(header_start, "+IPD", 4) != 0) return IPD_HEADER_INVALID;
  if(data_len == pos) return IPD_HEADER_INCOMPLETE;
  if(header_start[pos++] != ',') return IPD_HEADER_INVALID;
  if(pos == data_len) return IPD_HEADER_INCOMPLETE;
  if(header_start[pos] < '0' || header_start[pos] > '9') return IPD_HEADER_INVALID;
  while(pos < data_len && header_start[pos] >= '0' && header_start[pos] <= '9')
  {
    uint8_t digit = (uint8_t)(header_start[pos] - '0');
    if(payload_len > (UINT16_MAX - digit) / 10u) return IPD_HEADER_INVALID;
    payload_len = payload_len * 10u + digit;
    ++pos;
  }
  if(pos == data_len) return IPD_HEADER_INCOMPLETE;
  if(header_start[pos++] != ',') return IPD_HEADER_INVALID;
  for(uint8_t i = 0; i < sizeof(suffix) - 1u; ++i)
  {
    if(pos == data_len) return IPD_HEADER_INCOMPLETE;
    if(header_start[pos++] != suffix[i]) return IPD_HEADER_INVALID;
  }
  if(pos > buffer_size || payload_len > (uint32_t)(buffer_size - pos)) return IPD_HEADER_INVALID;
  *out_payload_len = (uint16_t)payload_len;
  *out_header_len = (uint16_t)pos;
  return IPD_HEADER_VALID;
}

/*******************************************************************************
 ** @brief  Process complete packet
 ** @param  ctx          Stream context
 ** @param  cb           Callback for complete packets
 ******************************************************************************/
static void consume_complete_packet(tact_gprs_stream_ctx_t* stream_ctx)
{
  uint32_t total_packet_len = (uint32_t)stream_ctx->header_len + stream_ctx->expected_len;
  stream_ctx->data_len -= total_packet_len;
  if(stream_ctx->data_len > 0) memmove(stream_ctx->buffer, stream_ctx->buffer + total_packet_len, stream_ctx->data_len);
  stream_ctx->expected_len = 0;
  stream_ctx->header_len = 0;
  stream_ctx->packet_in_progress = false;
}

/*******************************************************************************
 ** @brief  Initialize gprs stream context
 ** @param  ctx          Pointer to context
 ** @param  packet_size  Max packet size
 ** @return true if success, false otherwise
 ******************************************************************************/
bool tact_gprs_stream_ctx_init(tact_context_t* const tact_ctx, tact_gprs_stream_ctx_t* stream_ctx, uint16_t packet_size)
{
  if(!tact_ctx || !stream_ctx || !packet_size || !tact_get_init(tact_ctx).init) return false;
  if(stream_ctx->buffer || stream_ctx->callback_in_progress) return false;
  TACT_CRITICAL_ENTER
  stream_ctx->buffer = (uint8_t*)tact_malloc(tact_ctx, packet_size);
  if (!stream_ctx->buffer) 
  {
    TACT_DEBUG(tact_ctx, "[TACT][ERROR] Failed to allocate stream buffer", NULL);
    TACT_CRITICAL_EXIT
    return false;
  }
  stream_ctx->buffer_size = packet_size;
  stream_ctx->data_len = 0;
  stream_ctx->expected_len = 0;
  stream_ctx->header_len = 0;
  stream_ctx->packet_in_progress = false;
  stream_ctx->callback_in_progress = false;
  TACT_CRITICAL_EXIT
  return true;
}

/*******************************************************************************
 ** @brief  Cleanup GPRS stream context
 ** @param  ctx          Pointer to context
 ******************************************************************************/
void tact_gprs_stream_ctx_cleanup(tact_context_t* const tact_ctx, tact_gprs_stream_ctx_t* stream_ctx)
{
  if(!tact_ctx || !stream_ctx) return;
  TACT_CRITICAL_ENTER
  if(stream_ctx->callback_in_progress)
  {
    TACT_CRITICAL_EXIT
    return;
  }
  if(stream_ctx->buffer) tact_free(tact_ctx, stream_ctx->buffer);
  memset(stream_ctx, 0, sizeof(*stream_ctx));
  TACT_CRITICAL_EXIT
}

/*******************************************************************************
 ** @brief  Handle GPRS stream data
 ** @param  ctx          Stream context (per connection)
 ** @param  data         Pointer to incoming data
 ** @param  len          Data length
 ** @param  cb           Callback when full packet found
 ** @return true if data processed successfully
 ******************************************************************************/
bool tact_mld_gprs_server_stream_data_handler(tact_context_t* const tact_ctx, tact_gprs_stream_ctx_t* stream_ctx, uint8_t* data, uint16_t len, tact_stream_data_cb cb)
{
  TACT_CRITICAL_ENTER
  if(!tact_ctx || !stream_ctx || !tact_get_init(tact_ctx).init || !stream_ctx->buffer ||
     (len && !data) || stream_ctx->callback_in_progress)
  {
    TACT_CRITICAL_EXIT
    return false;
  }
  if(stream_ctx->data_len > stream_ctx->buffer_size)
  {
    stream_ctx->data_len = 0;
    stream_ctx->expected_len = 0;
    stream_ctx->header_len = 0;
    stream_ctx->packet_in_progress = false;
    TACT_CRITICAL_EXIT
    return false;
  }
  // Check if new data fits in buffer
  if ((uint32_t)stream_ctx->data_len + len > stream_ctx->buffer_size)
  {
    TACT_DEBUG(tact_ctx, "[TACT][INFO] Stream buffer overflow, resetting", NULL);
    stream_ctx->data_len = 0;
    stream_ctx->expected_len = 0;
    stream_ctx->header_len = 0;
    stream_ctx->packet_in_progress = false;
    TACT_CRITICAL_EXIT
    return false;
  }
  // Append new data to buffer
  if(len) memcpy(stream_ctx->buffer + stream_ctx->data_len, data, len);
  stream_ctx->data_len += len;
  // Process all complete packets in buffer
  while (stream_ctx->data_len > 0) 
  {
    if (!stream_ctx->packet_in_progress) 
    {
      // Look for IPD header
      uint8_t* ipd_start = find_ipd_header(stream_ctx->buffer, stream_ctx->data_len);
      if (!ipd_start) 
      {
        // No header found, keep last few bytes that could be start of header
        const uint16_t keep_bytes = 4;
        if (stream_ctx->data_len > keep_bytes) 
        {
          memmove(stream_ctx->buffer, stream_ctx->buffer + stream_ctx->data_len - keep_bytes, keep_bytes);
          stream_ctx->data_len = keep_bytes;
        }
        break;
      }
      // Remove data before header
      uint16_t header_offset = (uint16_t)(ipd_start - stream_ctx->buffer);
      if (header_offset > 0) 
      {
        memmove(stream_ctx->buffer, ipd_start, stream_ctx->data_len - header_offset);
        stream_ctx->data_len -= header_offset;
      }
      // Parse header
      ipd_header_result_t header_result = parse_ipd_header(stream_ctx->buffer, stream_ctx->data_len, stream_ctx->buffer_size, &stream_ctx->expected_len, &stream_ctx->header_len);
      if(header_result == IPD_HEADER_INCOMPLETE) break;
      if(header_result == IPD_HEADER_INVALID)
      {
        // Skip one byte so a nested or following +IPD marker can be found.
        memmove(stream_ctx->buffer, stream_ctx->buffer + 1, stream_ctx->data_len - 1u);
        --stream_ctx->data_len;
        TACT_DEBUG(tact_ctx, "[TACT][INFO] Invalid stream header, skipping", NULL);
        continue;
      }
      stream_ctx->packet_in_progress = true;
    }
    // Check if we have complete packet
    if (stream_ctx->packet_in_progress && (uint32_t)stream_ctx->data_len >= (uint32_t)stream_ctx->header_len + stream_ctx->expected_len)
    {
      uint8_t* payload = stream_ctx->buffer + stream_ctx->header_len;
      uint16_t payload_len = stream_ctx->expected_len;
      TACT_DEBUG(tact_ctx, "[TACT][INFO] Found full TCP stream packet, len: %u", payload_len);
      stream_ctx->callback_in_progress = true;
      TACT_CRITICAL_EXIT
      if(cb) cb(payload, payload_len);
      TACT_CRITICAL_ENTER
      stream_ctx->callback_in_progress = false;
      consume_complete_packet(stream_ctx);
    } 
    else 
    {
      // Incomplete packet, wait for more data
      TACT_DEBUG(tact_ctx, "[TACT][INFO] Waiting for full stream data", NULL);
      break;
    }
  }
  TACT_CRITICAL_EXIT
  return true;
}





