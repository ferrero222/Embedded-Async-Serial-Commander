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
#include "tact_mdl_ftp.h"
#include "tact_port.h"
#include <stdio.h>
#include <string.h>

/*******************************************************************************
 * Local pre-processor symbols/macros ('#define')
 ******************************************************************************/
#define FTP_COMMAND_TIMEOUT_TICKS  900U
#define FTP_TRANSFER_TIMEOUT_TICKS 60000U

/*******************************************************************************
 * Function implementation - global ('extern') and local ('static')
 ******************************************************************************/
/*******************************************************************************
 * @brief Start FTP on the selected PDP context.
 * @details Configure APN/network beforehand. Enqueues CFTPSSTART and waits
 *          for its successful result. Session ordering belongs to the Chain.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Pointer to tact_mdl_ftp_config_t.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_ftp_start(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !tact_get_init(ctx).init) return false;
  const tact_mdl_ftp_config_t *cfg = (const tact_mdl_ftp_config_t *)param;
  if(!cfg || !cfg->pdp_context_id) return false;
  char command[40] = {0};
  char answer[80] = {0};
  int written = snprintf(command, sizeof(command), "%sAT+CFTPSSTART=%u%s", TACT_CMD_SAVE, (unsigned)cfg->pdp_context_id, TACT_CMD_CRLF);
  if(written < 0 || (size_t)written >= sizeof(command)) return false;
  written = snprintf(answer, sizeof(answer), "%s+CFTPSSTART: 0,%u\r\n|+CFTPSSTART:0,%u\r\n", TACT_CMD_SAVE, (unsigned)cfg->pdp_context_id, (unsigned)cfg->pdp_context_id);
  if(written < 0 || (size_t)written >= sizeof(answer)) return false;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...]
  {
    TACT_ITEM(command, TACT_CMD_OK, TACT_PARCE_RAW, 0, FTP_COMMAND_TIMEOUT_TICKS, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(NULL, answer, TACT_PARCE_RAW, 0, FTP_COMMAND_TIMEOUT_TICKS, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 * @brief Select passive FTP data connections.
 * @details Enqueues CFTPSMODE=1. Call after service start and before login;
 *          the modem establishes the data connection outbound.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Unused; pass NULL.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_ftp_mode(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !tact_get_init(ctx).init) return false;
  (void)param;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...]
  {
    TACT_ITEM("AT+CFTPSMODE=1"TACT_CMD_CRLF, TACT_CMD_OK, TACT_PARCE_RAW, 0, FTP_COMMAND_TIMEOUT_TICKS, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 * @brief Log in to an ordinary FTP server.
 * @details Uses server type zero and disables echo around credentials. On
 *          command failure recover the modem/echo state from the Chain.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Pointer to tact_mdl_ftp_config_t.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_ftp_login(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !tact_get_init(ctx).init) return false;
  const tact_mdl_ftp_config_t *cfg = (const tact_mdl_ftp_config_t *)param;
  if(!cfg || !cfg->port || !cfg->host[0]) return false;
  const char *fields[] = { cfg->host, cfg->username, cfg->password };
  const size_t capacities[] = { sizeof(cfg->host), sizeof(cfg->username), sizeof(cfg->password) };
  for(size_t field = 0U; field < sizeof(fields)/sizeof(fields[0]); field++)
  {
    const char *end = (const char *)memchr(fields[field], '\0', capacities[field]);
    if(!end) return false;
    for(const char *value = fields[field]; value < end; value++)
    {
      if((unsigned char)*value < 0x20U || (unsigned char)*value > 0x7EU || *value == '"') return false;
    }
  }
  char command[sizeof(cfg->host) + sizeof(cfg->username) + sizeof(cfg->password) + 64U] = {0};
  int written = snprintf(command, sizeof(command), "%sAT+CFTPSLOGIN=\"%s\",%u,\"%s\",\"%s\",0%s", TACT_CMD_SAVE, cfg->host, (unsigned)cfg->port, cfg->username, cfg->password, TACT_CMD_CRLF);
  if(written < 0 || (size_t)written >= sizeof(command)) return false;
  /* Echo is off while the password is transmitted; SAVE only copies the request. */
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...]
  {
    TACT_ITEM("ATE0"TACT_CMD_CRLF, TACT_CMD_OK, TACT_PARCE_RAW, 0, FTP_COMMAND_TIMEOUT_TICKS, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(command, TACT_CMD_OK, TACT_PARCE_RAW, 0, FTP_COMMAND_TIMEOUT_TICKS, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(NULL, "+CFTPSLOGIN: 0\r\n|+CFTPSLOGIN:0\r\n", TACT_PARCE_RAW, 0, FTP_COMMAND_TIMEOUT_TICKS, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("ATE1"TACT_CMD_CRLF, TACT_CMD_OK, TACT_PARCE_RAW, 0, FTP_COMMAND_TIMEOUT_TICKS, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 * @brief Select binary FTP transfer type.
 * @details Enqueues CFTPSTYPE=I after login so the server preserves the
 *          original binary payload without newline conversion.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Unused; pass NULL.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_ftp_type(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !tact_get_init(ctx).init) return false;
  (void)param;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...]
  {
    TACT_ITEM("AT+CFTPSTYPE=I"TACT_CMD_CRLF, TACT_CMD_OK, TACT_PARCE_RAW, 0, FTP_COMMAND_TIMEOUT_TICKS, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(NULL, "+CFTPSTYPE: 0\r\n|+CFTPSTYPE:0\r\n", TACT_PARCE_RAW, 0, FTP_COMMAND_TIMEOUT_TICKS, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 * @brief Request the prompt for one fixed-length upload block.
 * @details Sends CFTPSPUT with an explicit length and REST offset. The
 *          length must equal the TX stream size and be 1..2048. After the prompt,
 *          transfer exactly that many bytes: no escaping or Ctrl+Z is required.
 *          The caller owns UART scheduling and must exclude other AT commands.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Pointer to tact_mdl_ftp_upload_t.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_ftp_put_begin(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !tact_get_init(ctx).init) return false;
  const tact_mdl_ftp_upload_t *upload = (const tact_mdl_ftp_upload_t *)param;
  if(!upload || !upload->size || upload->size > TACT_MDL_FTP_PUT_MAX || upload->offset > INT32_MAX || !upload->remote_path[0]) return false;
  const char *end = (const char *)memchr(upload->remote_path, '\0', sizeof(upload->remote_path));
  if(!end) return false;
  for(const char *value = upload->remote_path; value < end; value++)
  {
    if((unsigned char)*value < 0x20U || (unsigned char)*value > 0x7EU || *value == '"') return false;
  }
  char command[sizeof(upload->remote_path) + 64U] = {0};
  int written = snprintf(command, sizeof(command), "%sAT+CFTPSPUT=\"%s\",%u,%lu%s", TACT_CMD_SAVE, upload->remote_path, (unsigned)upload->size, (unsigned long)upload->offset, TACT_CMD_CRLF);
  if(written < 0 || (size_t)written >= sizeof(command)) return false;
  /* Explicit length means every payload byte is literal, including ETX/Ctrl+Z. */
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...]
  {
    TACT_ITEM("ATE0"TACT_CMD_CRLF, TACT_CMD_OK, TACT_PARCE_RAW, 0, FTP_COMMAND_TIMEOUT_TICKS, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(command, ">", TACT_PARCE_RAW, 0, FTP_COMMAND_TIMEOUT_TICKS, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 * @brief Wait for the final result of a fixed-length upload.
 * @details Queues no data or terminator: waits for OK and successful
 *          CFTPSPUT, then restores echo. Call after the TX count reaches size.
 *          A timeout/nonzero modem result reports failure through cb.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Unused; pass NULL.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_ftp_put_end(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !tact_get_init(ctx).init) return false;
  (void)param;
  /* The fixed-length payload ends automatically; never append Ctrl+Z here. */
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...]
  {
    TACT_ITEM(NULL, TACT_CMD_OK, TACT_PARCE_RAW, 0, FTP_TRANSFER_TIMEOUT_TICKS, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(NULL, "+CFTPSPUT: 0\r\n|+CFTPSPUT:0\r\n", TACT_PARCE_RAW, 0, FTP_COMMAND_TIMEOUT_TICKS, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("ATE1"TACT_CMD_CRLF, TACT_CMD_OK, TACT_PARCE_RAW, 0, FTP_COMMAND_TIMEOUT_TICKS, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 * @brief Query a remote file size.
 * @details cb receives tact_mdl_ftp_result_t; remote_size is valid only
 *          for a successful callback and must be copied before cb returns.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Pointer to tact_mdl_ftp_path_t.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_ftp_size(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !tact_get_init(ctx).init) return false;
  const tact_mdl_ftp_path_t *path = (const tact_mdl_ftp_path_t *)param;
  if(!path || !path->remote_path[0]) return false;
  const char *end = (const char *)memchr(path->remote_path, '\0', sizeof(path->remote_path));
  if(!end) return false;
  for(const char *value = path->remote_path; value < end; value++)
  {
    if((unsigned char)*value < 0x20U || (unsigned char)*value > 0x7EU || *value == '"') return false;
  }
  char command[sizeof(path->remote_path) + 32U] = {0};
  int written = snprintf(command, sizeof(command), "%sAT+CFTPSSIZE=\"%s\"%s", TACT_CMD_SAVE, path->remote_path, TACT_CMD_CRLF);
  if(written < 0 || (size_t)written >= sizeof(command)) return false;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...]
  {
    TACT_ITEM(command, TACT_CMD_OK, TACT_PARCE_RAW, 0, FTP_COMMAND_TIMEOUT_TICKS, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(NULL, "+CFTPSSIZE:", TACT_PARCE_SIMCOM, 0, FTP_COMMAND_TIMEOUT_TICKS, 0, 0, NULL, "+CFTPSSIZE: %u", TACT_ARG(tact_mdl_ftp_result_t, remote_size)),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, sizeof(tact_mdl_ftp_result_t), meta)) return false;
  return true;
}

/*******************************************************************************
 * @brief Log out of the current FTP server.
 * @details Enqueues CFTPSLOGOUT and waits for its successful result.
 *          The caller's Chain decides whether to stop or reconnect.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Unused; pass NULL.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_ftp_logout(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !tact_get_init(ctx).init) return false;
  (void)param;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...]
  {
    TACT_ITEM("AT+CFTPSLOGOUT"TACT_CMD_CRLF, TACT_CMD_OK, TACT_PARCE_RAW, 0, FTP_COMMAND_TIMEOUT_TICKS, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(NULL, "+CFTPSLOGOUT: 0\r\n|+CFTPSLOGOUT:0\r\n", TACT_PARCE_RAW, 0, FTP_COMMAND_TIMEOUT_TICKS, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 * @brief Stop FTP and deactivate its PDP context.
 * @details Enqueues CFTPSSTOP. First release TCP and any other service
 *          sharing this PDP context so their connections are not interrupted.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Pointer to tact_mdl_ftp_config_t.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_ftp_stop(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !tact_get_init(ctx).init) return false;
  const tact_mdl_ftp_config_t *cfg = (const tact_mdl_ftp_config_t *)param;
  if(!cfg || !cfg->pdp_context_id) return false;
  char command[40] = {0};
  char answer[80] = {0};
  int written = snprintf(command, sizeof(command), "%sAT+CFTPSSTOP=%u%s", TACT_CMD_SAVE, (unsigned)cfg->pdp_context_id, TACT_CMD_CRLF);
  if(written < 0 || (size_t)written >= sizeof(command)) return false;
  written = snprintf(answer, sizeof(answer), "%s+CFTPSSTOP: 0,%u\r\n|+CFTPSSTOP:0,%u\r\n", TACT_CMD_SAVE, (unsigned)cfg->pdp_context_id, (unsigned)cfg->pdp_context_id);
  if(written < 0 || (size_t)written >= sizeof(answer)) return false;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...]
  {
    TACT_ITEM(command, TACT_CMD_OK, TACT_PARCE_RAW, 0, FTP_COMMAND_TIMEOUT_TICKS, 0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(NULL, answer, TACT_PARCE_RAW, 0, FTP_COMMAND_TIMEOUT_TICKS, 0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}

/*******************************************************************************
 * @brief Advance a caller-owned TX stream by one bounded transport write.
 * @details Sends literal bytes without AT framing or escaping. Each invocation
 *          sends at most STREAM_BLOCK bytes and advances count only by the
 *          callback's accepted count. Zero means pending progress; the callback
 *          must not return zero after partially transmitting a block.
 * @param[in] ctx Initialized TACT context with exclusively owned UART.
 * @param[in,out] param Pointer to tact_mdl_ftp_stream_t with direction TX.
 * @retval true count equals size.
 * @retval false More data remains or the parameters/transport are invalid.
 ******************************************************************************/
bool tact_mdl_ftp_stream_tx(tact_context_t* const ctx, void* const param)
{
  tact_mdl_ftp_stream_t *stream = (tact_mdl_ftp_stream_t *)param;
  if(!ctx || !stream || stream->direction != TACT_MDL_FTP_STREAM_TX || stream->count > stream->size || (stream->size && !stream->buffer)) return false;
  tact_init_t init = tact_get_init(ctx);
  if(!init.init || !init.tact_write) return false;
  if(stream->count == stream->size) return true;
  uint32_t remaining = stream->size - stream->count;
  uint16_t length = (uint16_t)(remaining > TACT_MDL_FTP_STREAM_BLOCK ? TACT_MDL_FTP_STREAM_BLOCK : remaining);
  uint16_t written = init.tact_write(&stream->buffer[stream->count], length);
  if(written > length) return false;
  stream->count += written;
  return stream->count == stream->size;
}

/*******************************************************************************
 * @brief Advance a caller-owned RX stream using the existing UART ring.
 * @details Copies at most STREAM_BLOCK payload bytes and leaves bytes beyond
 *          the expected size untouched. The Chain must already have consumed
 *          the payload header and paused all competing RX parsers/consumers.
 *          Port critical hooks must exclude the UART interrupt producer.
 * @param[in] ctx Initialized TACT context with exclusively owned RX ring.
 * @param[in,out] param Pointer to tact_mdl_ftp_stream_t with direction RX.
 * @retval true count equals size.
 * @retval false More bytes are needed or the parameters/ring are invalid.
 ******************************************************************************/
bool tact_mdl_ftp_stream_rx(tact_context_t* const ctx, void* const param)
{
  tact_mdl_ftp_stream_t *stream = (tact_mdl_ftp_stream_t *)param;
  if(!ctx || !stream || stream->direction != TACT_MDL_FTP_STREAM_RX || stream->count > stream->size || (stream->size && !stream->buffer)) return false;
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
  uint32_t remaining = stream->size - stream->count;
  uint16_t length = (uint16_t)(remaining > TACT_MDL_FTP_STREAM_BLOCK ? TACT_MDL_FTP_STREAM_BLOCK : remaining);
  if(length > ring->count) length = ring->count;
  for(uint16_t i = 0U; i < length; i++)
  {
    stream->buffer[stream->count + i] = ring->buffer[ring->tail];
    ring->tail++;
    if(ring->tail == ring->size) ring->tail = 0U;
  }
  ring->count = (uint16_t)(ring->count - length);
  stream->count += length;
  TACT_CRITICAL_EXIT
  return stream->count == stream->size;
}
