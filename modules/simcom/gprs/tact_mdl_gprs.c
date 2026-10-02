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
#include "tact_mdl_gprs.h"
#include "tact_port.h"
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
 * @brief Check that a GPRS string terminates within its declared capacity.
 * @details Bounds the string scan before formatting the queued AT command.
 *          Contents are passed to the modem unchanged.
 * @param[in] value String storage to examine.
 * @param[in] capacity Available storage in bytes.
 * @param[out] length Number of characters before the terminator.
 * @retval true A terminator was found within capacity.
 * @retval false A pointer is invalid or the string is not terminated.
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
 * @brief Initialize GPRS registration.
 * @details Selects the operator automatically and waits for registration and packet
 *          attachment. Connection ordering and recovery belong to the Chain.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Unused; pass NULL.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
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
 * @brief Configure one non-transparent socket.
 * @details Enables automatic RX with +IPD,<length>,TCP/UDP: framing, without a
 *          remote-address prompt. Enables the send prompt and normal SEND OK.
 *          Configure before connecting; this is the legacy SIM800/SIM868 module.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Unused; pass NULL.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
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
    TACT_ITEM("AT+CIPSRIP?"TACT_CMD_CRLF,        "+CIPSRIP: 0", TACT_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIPSRIP=0"TACT_CMD_CRLF,                NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL, NULL, TACT_NO_ARG),
    /* Keep the RX framing deterministic and the fixed-length send prompt enabled. */
    TACT_ITEM("AT+CIPSPRT=1"TACT_CMD_CRLF,              NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIPRXGET=0"TACT_CMD_CRLF,             NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIPSHOWTP?"TACT_CMD_CRLF,    "+CIPSHOWTP: 1", TACT_PARCE_SIMCOM,  1, 100, 1, 0, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIPSHOWTP=1"TACT_CMD_CRLF,              NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 * @brief Connect the configured socket to a server.
 * @details Copies the formatted CIPSTART command into the queued entity. Server
 *          strings need to remain valid only until this function returns.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Pointer to tact_mdl_gprs_server_t.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
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
 * @brief Request the prompt for a fixed-length socket transmission.
 * @details Disables echo, queues CIPSEND=size, and completes at >. Echo remains
 *          disabled; reconfigure it separately if needed. No payload, Ctrl+Z, or server
 *          response is handled here. Call stream_tx afterwards with this same
 *          descriptor; its count must initially be zero. The size must fit the
 *          modem's CIPSEND? limit and SEND_MAX. Exclude concurrent AT commands.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Pointer to tact_mdl_gprs_stream_t with direction TX.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_gprs_socket_send_recieve(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !tact_get_init(ctx).init || !param) return false;
  const tact_mdl_gprs_stream_t *stream = (const tact_mdl_gprs_stream_t *)param;
  if(stream->direction != TACT_MDL_GPRS_STREAM_TX || !stream->buffer ||
     !stream->size || stream->size > TACT_MDL_GPRS_SEND_MAX || stream->count) return false;
  char command[48] = {0};
  int written = snprintf(command, sizeof(command), "%sAT+CIPSEND=%lu%s", TACT_CMD_SAVE, (unsigned long)stream->size, TACT_CMD_CRLF);
  if(written < 0 || (size_t)written >= sizeof(command)) return false;
  /* Explicit length ends the modem's input phase; no Ctrl+Z is appended. */
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...]
  {
    TACT_ITEM("AT+CIPSTATUS"TACT_CMD_CRLF, "STATE: CONNECT OK", TACT_PARCE_SIMCOM, 5, 100, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("ATE0"TACT_CMD_CRLF, TACT_CMD_OK, TACT_PARCE_RAW, 0, 100, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(command, ">", TACT_PARCE_RAW, 0, 500, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 * @brief Wait for the modem's transmission result.
 * @details Queues no command or payload; waits for SEND OK after stream_tx has
 *          transferred exactly size bytes. This is not an application reply.
 *          Non-success results fail by timeout; recovery belongs to the Chain.
 *          Do not run the text parser over unsolicited binary RX traffic.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Unused; pass NULL.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_gprs_socket_send_end(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  (void)param;
  if(!ctx || !tact_get_init(ctx).init) return false;
  /* Waiting for the result must never resend the binary payload. */
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...]
  {
    TACT_ITEM(NULL, "SEND OK"TACT_CMD_CRLF, TACT_PARCE_RAW, 0, 64500, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 * @brief Close the current socket.
 * @details Enqueues CIPCLOSE and waits for CLOSE OK. Release raw stream ownership
 *          before allowing the command worker to process this sequence.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Unused; pass NULL.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
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
 * @brief Deactivate the GPRS connection.
 * @details Enqueues CIPSHUT and waits for SHUT OK. Discard any unfinished stream
 *          context when the connection is closed or the modem is reset.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Unused; pass NULL.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
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

/*******************************************************************************
 * @brief Advance a raw socket transmit stream by one bounded write.
 * @details Use as TACT_CHAIN_EXEC in a loop after the > prompt. Transfers at most
 *          STREAM_BLOCK bytes unchanged through the context write callback.
 *          No allocation, escaping, or terminator is added. The callback must
 *          report exactly the accepted byte count; zero must mean nothing sent.
 *          Deadlines, UART ownership, and recovery belong to the caller's Chain.
 * @param[in] ctx Initialized TACT context with an exclusively owned UART.
 * @param[in,out] param Pointer to tact_mdl_gprs_stream_t with direction TX.
 * @retval true count equals size; all payload bytes have been transmitted.
 * @retval false The stream is pending or its context/transport is invalid.
 ******************************************************************************/
bool tact_mdl_gprs_stream_tx(tact_context_t* const ctx, void* const param)
{
  tact_mdl_gprs_stream_t *stream = (tact_mdl_gprs_stream_t *)param;
  if(!ctx || !stream || stream->direction != TACT_MDL_GPRS_STREAM_TX ||
  stream->count > stream->size || (stream->size && !stream->buffer)) return false;
  tact_init_t init = tact_get_init(ctx);
  if(!init.init || !init.tact_write) return false;
  if(stream->count == stream->size) return true;
  uint32_t remaining = stream->size - stream->count;
  uint16_t length = (uint16_t)(remaining > TACT_MDL_GPRS_STREAM_BLOCK ? TACT_MDL_GPRS_STREAM_BLOCK : remaining);
  uint16_t written = init.tact_write(stream->buffer + stream->count, length);
  if(written > length) return false;
  stream->count += written;
  return stream->count == stream->size;
}

/*******************************************************************************
 * @brief Collect binary socket payload from length-delimited +IPD blocks.
 * @details Reads directly from the existing UART ring, at most STREAM_BLOCK
 *          wire bytes per call. Partial headers stay in the ring. After a header,
 *          remaining tracks the payload until it is consumed; embedded NUL,
 *          CR/LF, or +IPD bytes are copied literally. Multiple blocks may fill
 *          the requested size. Returns at count == size, preserving unread
 *          payload and subsequent headers. To continue with another buffer,
 *          reset count and update buffer/size, but preserve remaining.
 *          Initialize remaining to zero only for a new connection/stream.
 *          Exclude tact_core_proc and other RX consumers; configure the TACT
 *          critical hooks to protect the ring from its ISR producer. Text outside
 *          +IPD is skipped, not dispatched as URCs. Timeouts belong to the Chain.
 * @param[in] ctx Initialized TACT context with an exclusively owned RX ring.
 * @param[in,out] param Pointer to tact_mdl_gprs_stream_t with direction RX.
 * @retval true count equals size; all requested payload bytes were received.
 * @retval false More bytes are needed or the parameters/ring are invalid.
 ******************************************************************************/
bool tact_mdl_gprs_stream_rx(tact_context_t* const ctx, void* const param)
{
  tact_mdl_gprs_stream_t *stream = (tact_mdl_gprs_stream_t *)param;
  if(!ctx || !stream || stream->direction != TACT_MDL_GPRS_STREAM_RX ||
     stream->count > stream->size || (stream->size && !stream->buffer)) return false;
  tact_init_t init = tact_get_init(ctx);
  if(!init.init || !init.rx_buff) return false;
  if(stream->count == stream->size) return true;
  TACT_CRITICAL_ENTER
  tact_ring_buffer_t *ring = init.rx_buff;
  if(!ring->buffer || !ring->size || ring->tail >= ring->size ||
     ring->head >= ring->size || ring->count >= ring->size)
  {
    TACT_CRITICAL_EXIT
    return false;
  }
  uint16_t budget = TACT_MDL_GPRS_STREAM_BLOCK;
  while(budget && ring->count && stream->count < stream->size)
  {
    if(!stream->remaining)
    {
      /* Peek only a bounded header. A fragmented header stays in the UART ring. */
      uint8_t header[sizeof("+IPD,4294967295,TCP:") - 1U];
      uint16_t available = ring->count < sizeof(header) ? ring->count : (uint16_t)sizeof(header);
      uint16_t index = ring->tail;
      for(uint16_t i = 0U; i < available; i++)
      {
        header[i] = ring->buffer[index++];
        if(index == ring->size) index = 0U;
      }
      const char *prefix = "+IPD,";
      uint16_t pos = 0U;
      bool invalid = false;
      bool incomplete = false;
      for(; pos < 5U; pos++)
      {
        if(pos == available) { incomplete = true; break; }
        if(header[pos] != (uint8_t)prefix[pos]) { invalid = true; break; }
      }
      uint32_t length = 0U;
      uint16_t digits = 0U;
      if(!invalid && !incomplete)
      {
        while(pos < available && header[pos] >= '0' && header[pos] <= '9')
        {
          uint8_t digit = (uint8_t)(header[pos++] - '0');
          if(++digits > 10U || length > (UINT32_MAX - digit) / 10U) { invalid = true; break; }
          length = length * 10U + digit;
        }
        if(pos == available) incomplete = true;
        else if(!digits) invalid = true;
      }
      if(!invalid && !incomplete)
      {
        /* CIPSHOWTP=1 adds the transport tag, not part of the binary payload. */
        const char *suffix = ",TCP:";
        if(pos + 1U < available && header[pos + 1U] == 'U') suffix = ",UDP:";
        for(uint16_t i = 0U; i < 5U; i++)
        {
          if(pos == available) { incomplete = true; break; }
          if(header[pos++] != (uint8_t)suffix[i]) { invalid = true; break; }
        }
      }
      if(incomplete && !invalid && available < sizeof(header)) break;
      if(invalid || incomplete)
      {
        /* Skip text/noise one byte at a time; never scan inside a known payload. */
        ring->tail++;
        if(ring->tail == ring->size) ring->tail = 0U;
        ring->count--;
        budget--;
        continue;
      }
      if(pos > budget) break;
      ring->tail = (uint16_t)(((uint32_t)ring->tail + pos) % ring->size);
      ring->count = (uint16_t)(ring->count - pos);
      budget = (uint16_t)(budget - pos);
      stream->remaining = length;
      if(!length) continue;
    }
    uint32_t length = stream->size - stream->count;
    if(length > stream->remaining) length = stream->remaining;
    if(length > ring->count) length = ring->count;
    if(length > budget) length = budget;
    /* Payload may contain any byte, including CR/LF, NUL, and another +IPD. */
    for(uint16_t i = 0U; i < (uint16_t)length; i++)
    {
      stream->buffer[stream->count + i] = ring->buffer[ring->tail++];
      if(ring->tail == ring->size) ring->tail = 0U;
    }
    ring->count = (uint16_t)(ring->count - length);
    budget = (uint16_t)(budget - length);
    stream->remaining -= length;
    stream->count += length;
  }
  TACT_CRITICAL_EXIT
  return stream->count == stream->size;
}
