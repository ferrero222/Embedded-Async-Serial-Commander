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
#include "dbc_assert.h"
#include "tact_mdl_general.h"
#include "stdlib.h"
#include "tact_port.h"
   
/*******************************************************************************
 * Local pre-processor symbols/macros ('#define')
 ******************************************************************************/
DBC_MODULE_NAME("TACT_CORE")
#define TACT_OWN_REQ    0x01u
#define TACT_OWN_PREFIX 0x02u

/*******************************************************************************
 * Global variable definitions (declared in header file with 'extern')
 ******************************************************************************/
/*******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/
static bool tact_string_boolean_find(const ringslice_t* const rs_data, const char* const pattern, ringslice_cnt_t* const match_end);
static void tact_proc_handle_cmd_result(tact_context_t* const ctx, tact_entity_t* const entity, tact_item_t* const item, const bool success); 
static void tact_proc_handle_timeout(tact_context_t* const ctx, tact_entity_t* const entity, tact_item_t* const item);
static void tact_proc_handle_tx_timeout(tact_context_t* const ctx, tact_entity_t* const entity, tact_item_t* const item);
static void tact_proc_finish(tact_context_t* const ctx);

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
 * @brief Release all memory owned by an entity and clear its queue slot.
 *
 * Items stored in the queue own only strings marked with TACT_CMD_SAVE, the
 * copied argument descriptors, the entity data block, and the copied item
 * array. User strings, callbacks, and metadata remain borrowed.
******************************************************************************/
static void tact_entity_release(tact_context_t* const ctx, tact_entity_t* const entity)
{
  if(!ctx || !entity || !ctx->init_struct.heap) return;

  if(entity->item)
  {
    for(uint8_t i = 0; i < entity->item_cnt; ++i)
    {
      tact_item_t* item = &entity->item[i];
      if(item->owned & TACT_OWN_REQ) o1heapFree(ctx->init_struct.heap, item->req);
      if(item->owned & TACT_OWN_PREFIX) o1heapFree(ctx->init_struct.heap, item->answ.prefix);
      if(item->answ.ptrs) o1heapFree(ctx->init_struct.heap, item->answ.ptrs);
    }
    o1heapFree(ctx->init_struct.heap, entity->item);
  }
  if(entity->data) o1heapFree(ctx->init_struct.heap, entity->data);
  memset(entity, 0, sizeof(*entity));
}
 
/*******************************************************************************
** @brief Dequeues the oldest entity from the queue and releases its items
** @note This function must be called with internal locking/critical section active
** @param ctx Pointer to the application context structure
** @return true - entity successfully dequeued and released, false - queue is empty
**         or initialization check failed
******************************************************************************/
static bool tact_entity_dequeue_locked(tact_context_t* const ctx)
{
  if(!ctx || !ctx->init_struct.init || !ctx->init_struct.heap || !ctx->entity_queue.entity_cnt) return false;
  tact_entity_t* cur_entity = &ctx->entity_queue.entity[ctx->entity_queue.entity_tail];
  TACT_DEBUG(ctx, "[TACT][INFO] Dequeueing entity with %d items", cur_entity->item_cnt);
  tact_entity_release(ctx, cur_entity);
  ctx->entity_queue.entity_tail = (ctx->entity_queue.entity_tail +1) % TACT_ENTITY_QUEUE_SIZE;
  --ctx->entity_queue.entity_cnt;
  TACT_DEBUG(
    ctx, "[TACT][INFO] Entity dequeued. Queue count: %d. Memory used: %d/%d",  
    ctx->entity_queue.entity_cnt, o1heapGetDiagnostics(ctx->init_struct.heap).allocated, o1heapGetDiagnostics(ctx->init_struct.heap).capacity
  );
  return true;
}

 /*******************************************************************************
** @brief Advances the ring buffer's tail pointer to a new position, consuming data
** @param ctx Pointer to the application context structure
** @param source_buffer Pointer to the underlying memory buffer of the ring buffer
** @param source_size Total size of the underlying memory buffer
** @param new_tail Absolute index in the buffer representing the new tail position
** @return true - tail updated successfully and data consumed, false - validation failed
******************************************************************************/
static bool tact_rx_consume_to(tact_context_t* const ctx, const uint8_t* const source_buffer, const uint16_t source_size, const uint16_t new_tail)
{
  if(!ctx) return false;
  TACT_CRITICAL_ENTER
  tact_ring_buffer_t* rx = ctx->init_struct.rx_buff;
  if(!ctx->init_struct.init || !rx || !rx->buffer || !rx->size ||
     rx->buffer != source_buffer || rx->size != source_size ||
     rx->head >= rx->size || rx->tail >= rx->size || new_tail >= rx->size ||
     rx->count >= rx->size)
  {
    TACT_CRITICAL_EXIT
    return false;
  }
  uint16_t consumed = new_tail >= rx->tail ? (uint16_t)(new_tail - rx->tail) : (uint16_t)(rx->size - rx->tail + new_tail);
  if(consumed > rx->count)
  {
    TACT_CRITICAL_EXIT
    return false;
  }
  rx->tail = new_tail;
  rx->count = (uint16_t)(rx->count - consumed);
  TACT_CRITICAL_EXIT
  return true;
}


/*******************************************************************************
 ** @brief  Boolean operation for strings in TACT
 ** @param  rs_data  data ringslices
 ** @param  pattern  pattern to work
 ** @return true - success parce
 **         false/true
 ******************************************************************************/
/**
* @brief Wrapper for string boolean matching that checks if a pattern exists
* @param rs_data Pointer to the ring slice data container
* @param pattern String containing keywords separated by '|' (OR) or '&' (AND)
* @return true - the boolean pattern matches the data, false - otherwise
*/
static bool tact_string_boolean_ops(const ringslice_t* const rs_data, const char* const pattern)
{
  return tact_string_boolean_find(rs_data, pattern, NULL);
}

/** 
* @brief Searches for a boolean pattern in a ring slice and finds the match end
* @param rs_data Pointer to the ring slice data container
* @param pattern String containing keywords separated by '|' (OR) or '&' (AND)
* @param match_end Pointer to store the last position index of the overall match
* @return true - all (AND) or any (OR) terms matched, false - validation failed
*/
static bool tact_string_boolean_find(const ringslice_t* const rs_data, const char* const pattern, ringslice_cnt_t* const match_end)
{
  DBC_REQUIRE(150, rs_data); 
  DBC_REQUIRE(151, pattern);

  const char* or_sep = strchr(pattern, '|'); //Find type of boolean operation
  const char* and_sep = strchr(pattern, '&');
  
  if(or_sep && and_sep) return false; // Combination in not allowed
  
  const char sep = or_sep ? '|' : (and_sep ? '&' : '\0');
  const int require_all = (sep == '&'); // 1 for AND, 0 for OR
  bool found = false;
  uint32_t furthest_end = 0;

  for(const char* start = pattern; *start && *start != '\0';) 
  {
    const char* end = strchr(start, sep);
    size_t length = end ? (size_t)(end - start) : strlen(start);
    ringslice_t match = ringslice_strnstr(rs_data, start, length);
    if(ringslice_is_empty(&match))
    {
      if(require_all) return false;
    }
    else
    {
      found = true;
      uint32_t end_offset = ((uint32_t)match.last + rs_data->buf_size - rs_data->first) % rs_data->buf_size;
      if(end_offset > furthest_end) furthest_end = end_offset;
      if(!require_all)
      {
        if(match_end) *match_end = match.last;
        return true;
      }
    }
    start = end && *end != '\0' ? end + 1 : start + length;
  }
  if(require_all && found && match_end)
  {
    *match_end = (ringslice_cnt_t)(((uint32_t)rs_data->first + furthest_end) % rs_data->buf_size);
  }
  return require_all && found;
}

/**
 * @brief Parses data from a ring slice into items based on a format string
* @param rs_data Pointer to the ring slice data container
* @param item Pointer to the expected response item structure
* @return true - variables parsed successfully matching format count, false - otherwise
*/
static bool tact_cmd_sscanf(const ringslice_t* const rs_data, const tact_item_t* const item) 
{
  DBC_REQUIRE(155, rs_data); 
  DBC_REQUIRE(156, item);  
  const char *format = item->answ.format;
  void** output_ptrs = item->answ.ptrs;
  if(!format || !output_ptrs) return false;
  size_t param_count = 0;
  while(param_count < 6 && output_ptrs[param_count] != TACT_NO_ARG) ++param_count;
  if(param_count == 0 || output_ptrs[param_count] != TACT_NO_ARG) return false;

  switch (param_count)
  {
    case 1:  return ringslice_scanf(rs_data, format, output_ptrs[0]) == 1;
    case 2:  return ringslice_scanf(rs_data, format, output_ptrs[0], output_ptrs[1]) == 2;
    case 3:  return ringslice_scanf(rs_data, format, output_ptrs[0], output_ptrs[1], output_ptrs[2]) == 3;
    case 4:  return ringslice_scanf(rs_data, format, output_ptrs[0], output_ptrs[1], output_ptrs[2], output_ptrs[3]) == 4;
    case 5:  return ringslice_scanf(rs_data, format, output_ptrs[0], output_ptrs[1], output_ptrs[2], output_ptrs[3], output_ptrs[4]) == 5;
    case 6:  return ringslice_scanf(rs_data, format, output_ptrs[0], output_ptrs[1], output_ptrs[2], output_ptrs[3], output_ptrs[4], output_ptrs[5]) == 6;
    default: return false;
  }
}

/*******************************************************************************
 ** @brief  Implementations for raw data parcer kind of: > RAW DATA
 ** @param  ctx     core context
 ** @param  rs_me   slice of origin buffer
 ** @param  entity  current proc entity
 ** @param  item    current proc item
 ** @return true - success parce
 **         false - failure parce and handle / or no data in buff
 ******************************************************************************/
static bool tact_raw_parcer(tact_context_t* const ctx, const ringslice_t rs_me, const tact_item_t* const item, const tact_entity_t* const entity)
{
  DBC_REQUIRE(117, ctx);
  DBC_REQUIRE(118, item);
  DBC_REQUIRE(119, entity);
  
  bool res = true;

  ringslice_cnt_t proced_data = 0;
  bool consume_data = false;
  ringslice_t rs_data = rs_me;
  
  if(item->answ.prefix)
  {
    char* prefix = strncmp(item->answ.prefix, TACT_CMD_SAVE, strlen(TACT_CMD_SAVE)) ? item->answ.prefix : item->answ.prefix +strlen(TACT_CMD_SAVE);
    ringslice_cnt_t match_end = 0;
    res = tact_string_boolean_find(&rs_data, prefix, &match_end);
    if(res && item->answ.format) res = tact_cmd_sscanf(&rs_data, item);
    if(res) {
      proced_data = item->answ.format ? rs_data.last : match_end;
      consume_data = true;
    }
  }
  else
  {
    if(item->answ.format) res = tact_cmd_sscanf(&rs_data, item);
    if(res) {
      proced_data = (uint16_t)(item->answ.format ? rs_data.last : rs_me.last);
      consume_data = true;
    }
  }

  if(consume_data) tact_rx_consume_to(ctx, rs_me.buf, rs_me.buf_size, (uint16_t)proced_data);
  if(item->answ.cb) item->answ.cb(ringslice_initializer(rs_me.buf, rs_me.buf_size, rs_data.first, rs_me.last), res, entity->data);
  return res; 
}

/** 
 * @brief Find req echo
 */
static void tact_simcom_parcer_find_rs_req(const ringslice_t* const me, ringslice_t* const rs_req, const char* req)
{
  DBC_REQUIRE(125, me); 
  DBC_REQUIRE(127, rs_req); 

  if(!req) return;

  if(strncmp(req, TACT_CMD_SAVE, strlen(TACT_CMD_SAVE)) == 0) req += strlen(TACT_CMD_SAVE);
  size_t req_len = strlen(req);
  if(!req_len) return;
  /* SIMCom echoes a CRLF-terminated request with one extra CR before the
   * response separator, so ignore only the final LF in that case. For other
   * valid non-empty requests, search the complete request instead of
   * underflowing req_len - 1 or silently dropping its last byte. */
  if(req_len >= 2u && req[req_len - 2u] == '\r' && req[req_len - 1u] == '\n') --req_len;
  *rs_req = ringslice_strnstr(me, req, req_len);
}

/** 
 * @brief Find result ring slice 
 */
static void tact_simcom_parcer_find_rs_res(const ringslice_t* const me, const ringslice_t* const rs_req, ringslice_t* const rs_res)
{
  DBC_REQUIRE(129, rs_req); 
  DBC_REQUIRE(131, rs_res); 

  if(ringslice_is_empty(rs_req)) return;

  ringslice_t tmp = ringslice_subslice_after(me, rs_req, 0);

  if(ringslice_is_empty(&tmp)) return;

  *rs_res = ringslice_strstr(&tmp, TACT_CMD_ERROR);
  if(ringslice_is_empty(rs_res)) *rs_res = ringslice_strstr(&tmp, TACT_CMD_OK);
}

/** 
 * @brief Find and data ring slice 
 */
static void tact_simcom_parcer_find_rs_data(const ringslice_t* const me, const ringslice_t* const rs_req, const ringslice_t* const rs_res, ringslice_t* const rs_data)
{
  DBC_REQUIRE(134, me); 
  DBC_REQUIRE(135, rs_req);  
  DBC_REQUIRE(136, rs_res); 
  DBC_REQUIRE(137, rs_data);

  const uint8_t crlf_len = strlen(TACT_CMD_CRLF);

  if(ringslice_is_empty(rs_req) && ringslice_is_empty(rs_res)) //No request/response, data is the entire buffer \r\nDATA\r\n
  { 
    *rs_data = *me;
  } 
  else if(!ringslice_is_empty(rs_req) && ringslice_is_empty(rs_res)) //Request but no response yet, data is after request REQ\r\r\nDATA\r\n
  {
    *rs_data = ringslice_subslice_after(me, rs_req, 0);
  } 
  else if(!ringslice_is_empty(rs_req) && !ringslice_is_empty(rs_res)) //Both request and response present
  {
    ringslice_t after_req = ringslice_subslice_after(me, rs_req, strlen(TACT_CMD_CRLF"OK"TACT_CMD_CRLF));
    *rs_data = ringslice_subslice_equals(&after_req, rs_res) 
               ? ringslice_subslice_after(me, rs_res, 0) // REQ\r\r\nOK\r\n\r\nDATA\r\n
               : ringslice_subslice_gap(rs_req, rs_res); // REQ\r\r\nDATA\r\n\r\nOK\r\n
  }

  if((ringslice_len(rs_data) <= 2 *crlf_len) || (ringslice_strncmp(rs_data, TACT_CMD_CRLF, 2))) // If data more than 2bytes and first 2 bytes its CRLF
  {
    *rs_data = (ringslice_t){0};
    return;
  }
  *rs_data = ringslice_subslice_with_suffix(rs_data, crlf_len, TACT_CMD_CRLF); 
  if(ringslice_is_empty(rs_data)) return;
  *rs_data = ringslice_subslice(rs_data, crlf_len, ringslice_len(rs_data)- crlf_len); // Extract clean data (without surrounding CRLFx2)
}

/* The extracted payload excludes its framing CRLF. Consume that terminator as
 * part of the parsed response, but only when both slices share the same RX
 * backing ring. Synthetic parser tests may intentionally use separate slices.
 */
static ringslice_cnt_t tact_simcom_line_end(const ringslice_t* const me, const ringslice_t* const data)
{
  if(!me || !data || me->buf != data->buf || me->buf_size != data->buf_size) return data ? data->last : 0;
  ringslice_t suffix = ringslice_initializer(me->buf, me->buf_size, data->last, me->last);
  ringslice_t terminator = ringslice_strstr(&suffix, TACT_CMD_CRLF);
  return ringslice_is_empty(&terminator) ? data->last : terminator.last;
}

/** 
 * @brief Proc all found slices 
 * @note [REQ][RES][DATA]
 *        NULL NULL NULL  - unhandle state
 *        REQ  NULL NULL  - unhandle state
 *        REQ  RES  NULL  - handle state
 *        REQ  NULL DATA  - handle state
 *        REQ  RES  DATA  - handle state
 *        NULL RES  NULL  - unhandle state
 *        NULL RES  DATA  - unhandle state
 *        NULL NULL DATA  - handle state
 * @return  false - failure / true - success
 */
static bool tact_simcom_parcer_post_proc(tact_context_t* const ctx, const ringslice_t* const me, const ringslice_t* const rs_req, const ringslice_t* const rs_res, 
                                        const ringslice_t* const rs_data, const tact_item_t* const item, const tact_entity_t* const entity)
{
  DBC_REQUIRE(139, ctx); 
  DBC_REQUIRE(140, me); 
  DBC_REQUIRE(141, rs_req);  
  DBC_REQUIRE(142, rs_res); 
  DBC_REQUIRE(143, rs_data);
  DBC_REQUIRE(144, item);
  DBC_REQUIRE(145, entity);
  
  int res = false;  

  bool rs_req_exist  = !ringslice_is_empty(rs_req);
  bool rs_res_exist  = !ringslice_is_empty(rs_res);
  bool rs_data_exist = !ringslice_is_empty(rs_data);

  ringslice_cnt_t proced_data = 0;
  bool consume_data = false;
  
  char* prefix = item->answ.prefix;
  if(prefix && !strncmp(prefix, TACT_CMD_SAVE, strlen(TACT_CMD_SAVE))) prefix += strlen(TACT_CMD_SAVE);

  switch((rs_req_exist << 2) | (rs_res_exist << 1) | rs_data_exist) // Bitmask: REQ[bit2] RES[bit1] DATA[bit0]
  {
      case 0x01: //0b001 - NULL NULL DATA (PREFIX)
           if(!prefix) break;
           res = tact_string_boolean_ops(rs_data, prefix);
           if(res && item->answ.format) res = tact_cmd_sscanf(rs_data, item);
           if(res) { proced_data = tact_simcom_line_end(me, rs_data); consume_data = true; }
           break;
      case 0x05: //0b101 - REQ NULL DATA (REQ +PREFIX)
           if(!item->req || !prefix) break;
           res = tact_string_boolean_ops(rs_data, prefix);
           if(res && item->answ.format) res = tact_cmd_sscanf(rs_data, item);
           if(res) { proced_data = tact_simcom_line_end(me, rs_data); consume_data = true; }
           break;
      case 0x06: //0b110 - REQ RES NULL (REQ, NO PREFIX, NO FORMAT)
           if(!item->req || ringslice_strcmp(rs_res, TACT_CMD_ERROR) == 0) res = false;
           else if(prefix || item->answ.format) res = false;
           else res = true;
           if(res) { proced_data = rs_res->last; consume_data = true; }
           break;
      case 0x07: //0b111 - REQ RES DATA (REQ)
           if(!item->req) break;
           if(rs_res->buf != rs_data->buf || rs_res->buf_size != rs_data->buf_size) break;
           else if(ringslice_strcmp(rs_res, TACT_CMD_ERROR) == 0) res = false;
           else res = true;
           if(res){
             if(prefix) res = tact_string_boolean_ops(rs_data, prefix);
             if(res && item->answ.format) res = tact_cmd_sscanf(rs_data, item);
           }
           if(ringslice_is_later_than(rs_res, rs_data)) proced_data = (uint16_t)rs_res->last;
           else                                         proced_data = (uint16_t)tact_simcom_line_end(me, rs_data);
           consume_data = true;
           break;
      default: 
           break;
  }
  if(consume_data) tact_rx_consume_to(ctx, me->buf, me->buf_size, (uint16_t)proced_data);
  if(item->answ.cb) 
  {
    ringslice_t cb_rs_data = rs_data_exist ? ringslice_initializer(me->buf, me->buf_size, rs_data->first, me->last) : (ringslice_t){0};
    item->answ.cb(cb_rs_data, res, entity->data);
  }
  return res; 
}

/*******************************************************************************
 ** @brief  Implementations for simcom data parcer kind of: ECHO\r\r\nRES\r\nDATA\r\n
 ** @param  ctx     core context
 ** @param  rs_me   slice of origin buffer
 ** @param  entity  current proc entity
 ** @param  item    current proc item
 ** @return true - success parce
 **         false - failure parce and handle / or no data in buff
 ******************************************************************************/
static bool tact_simcom_parcer(tact_context_t* const ctx, const ringslice_t rs_me, const tact_item_t* const item, const tact_entity_t* const entity)
{
  DBC_REQUIRE(120, ctx);
  DBC_REQUIRE(121, item);
  DBC_REQUIRE(122, entity);

  bool res = false;

  ringslice_t rs_req = {0};
  ringslice_t rs_res = {0}; 
  ringslice_t rs_data = {0};

  tact_simcom_parcer_find_rs_req(&rs_me, &rs_req, item->req); // Find request and response in buffer
  tact_simcom_parcer_find_rs_res(&rs_me, &rs_req, &rs_res);
  tact_simcom_parcer_find_rs_data(&rs_me, &rs_req, &rs_res, &rs_data); // Extract data section
  res = tact_simcom_parcer_post_proc(ctx, &rs_me, &rs_req, &rs_res, &rs_data, item, entity); // Proc data
  return res;
}

/*******************************************************************************
 ** @brief  Function to parce the RX ring buffer for proc with existing entities in queue.
 **         Using choosen parcer for item.
 ** @param  ctx     core context
 ** @param  entity  current proc entity
 ** @param  item    current proc item
 ** @return true  - success parce
 **         false - failure parce and handle / or no data in buff
 ******************************************************************************/
static bool tact_cmd_ring_parcer(tact_context_t* const ctx, const tact_entity_t* const entity, const tact_item_t* const item, const ringslice_t rs_me)
{
  DBC_REQUIRE(100, ctx);
  DBC_REQUIRE(101, ctx->init_struct.init);
  DBC_REQUIRE(102, item);
  DBC_REQUIRE(103, entity);

  bool res = false;
  
  if(item->answ.prefix && strncmp(item->answ.prefix, TACT_CMD_FORCE, strlen(TACT_CMD_FORCE)) == 0) res = true;
  if(res) return true;
  if(ringslice_is_empty(&rs_me)) return 0; // no data

  if(!res)
  {
    switch(item->parce_type)
    {
      case TACT_PARCE_SIMCOM: res = tact_simcom_parcer(ctx, rs_me, item, entity); break;
      case TACT_PARCE_RAW:    res = tact_raw_parcer(ctx, rs_me, item, entity);    break;
      default: break;
    }
  }

  #ifndef TACT_TEST
  if(res > 0) tact_printf_from_ring(ctx, rs_me, "[RX]");
  #endif
  return res;
}

/*******************************************************************************
** @brief Removes a range of data from the RX ring buffer and shifts remaining bytes
** @param ctx Pointer to the application context structure
** @param source_buffer Pointer to the underlying memory buffer of the ring buffer
** @param source_size Total size of the underlying memory buffer
** @param start Absolute index in the buffer indicating the start of the range
** @param end Absolute index in the buffer indicating the end of the range
** @return true - range removed successfully and buffer updated, false - validation failed
******************************************************************************/
static bool tact_rx_remove_range(tact_context_t* const ctx, const uint8_t* const source_buffer, const uint16_t source_size, const uint16_t start, const uint16_t end)
{
  if(!ctx || !source_buffer || !source_size) return false;
  TACT_CRITICAL_ENTER
  tact_ring_buffer_t* rx = ctx->init_struct.rx_buff;
  if(!ctx->init_struct.init || !rx || rx->buffer != source_buffer || rx->size != source_size ||
     rx->head >= rx->size || rx->tail >= rx->size || start >= rx->size || end >= rx->size ||
     rx->count >= rx->size)
  {
    TACT_CRITICAL_EXIT
    return false;
  }
  uint16_t start_offset = start >= rx->tail ? (uint16_t)(start - rx->tail) : (uint16_t)(rx->size - rx->tail + start);
  uint16_t end_offset = end >= rx->tail ? (uint16_t)(end - rx->tail) : (uint16_t)(rx->size - rx->tail + end);
  uint16_t remove_len = end_offset > start_offset ? (uint16_t)(end_offset - start_offset) : (uint16_t)(rx->size - start_offset + end_offset);
  if(!remove_len || start_offset >= rx->count || remove_len > (uint16_t)(rx->count - start_offset))
  {
    TACT_CRITICAL_EXIT
    return false;
  }
  uint16_t bytes_after = (uint16_t)(rx->count - start_offset - remove_len);
  for(uint16_t i = 0; i < bytes_after; ++i)
  {
    uint16_t src = (uint16_t)(((uint32_t)rx->tail + start_offset + remove_len + i) % rx->size);
    uint16_t dst = (uint16_t)(((uint32_t)rx->tail + start_offset + i) % rx->size);
    rx->buffer[dst] = rx->buffer[src];
  }
  rx->count = (uint16_t)(rx->count - remove_len);
  rx->head = (uint16_t)(((uint32_t)rx->tail + rx->count) % rx->size);
  TACT_CRITICAL_EXIT
  return true;
}

/*******************************************************************************
 ** @brief  Find and proc standart URC 
 ** @param  ctx     core context
 ** @param  me  slice of origin buffer
 ** @return None
 ******************************************************************************/
static void tact_process_urcs(tact_context_t* const ctx, const ringslice_t* me)
{
  if(!ctx || !me || !me->buf || !me->buf_size || ringslice_is_empty(me)) return;
  tact_urc_queue_t registrations[TACT_URC_QUEUE_SIZE] = {0};
  char prefix_storage[TACT_URC_QUEUE_SIZE][TACT_URC_PREFIX_MAX_LEN + 1u] = {{0}};
  TACT_CRITICAL_ENTER
  if(!ctx->init_struct.init)
  {
    TACT_CRITICAL_EXIT
    return;
  }
  for(uint8_t i = 0; i < TACT_URC_QUEUE_SIZE; ++i)
  {
    const char* registered_prefix = ctx->urc_queue[i].prefix;
    if(!registered_prefix || !ctx->urc_queue[i].cb) continue;
    size_t len = 0;
    while(len <= TACT_URC_PREFIX_MAX_LEN && registered_prefix[len]) ++len;
    if(!len || len > TACT_URC_PREFIX_MAX_LEN) continue;
    memcpy(prefix_storage[i], registered_prefix, len + 1u);
    registrations[i].prefix = prefix_storage[i];
    registrations[i].cb = ctx->urc_queue[i].cb;
  }
  TACT_CRITICAL_EXIT
  bool owns_rx_slice = ctx->init_struct.rx_buff && me->buf == ctx->init_struct.rx_buff->buffer && me->buf_size == ctx->init_struct.rx_buff->size;
  ringslice_t current = *me;
  for(uint8_t dispatched = 0; dispatched < TACT_MAX_URCS_PER_PROC; )
  {
    bool found_urc = false;
    for(uint8_t i = 0; i < TACT_URC_QUEUE_SIZE && !found_urc; ++i)
    {
      if(!registrations[i].prefix || !registrations[i].cb) continue;
      ringslice_t search = current;
      while(!ringslice_is_empty(&search))
      {
        ringslice_t prefix = ringslice_strstr(&search, registrations[i].prefix);
        if(ringslice_is_empty(&prefix)) break;
        bool at_line_start = prefix.first == current.first;
        if(!at_line_start)
        {
          ringslice_cnt_t before1 = prefix.first ? (ringslice_cnt_t)(prefix.first - 1u) : (ringslice_cnt_t)(prefix.buf_size - 1u);
          ringslice_cnt_t before2 = before1 ? (ringslice_cnt_t)(before1 - 1u) : (ringslice_cnt_t)(prefix.buf_size - 1u);
          at_line_start = prefix.buf[before2] == '\r' && prefix.buf[before1] == '\n';
        }
        if(!at_line_start)
        {
          search = ringslice_subslice_after(&search, &prefix, 0);
          continue;
        }
        ringslice_t suffix = ringslice_subslice_after(&search, &prefix, 0);
        ringslice_t terminator = ringslice_strstr(&suffix, TACT_CMD_CRLF);
        if(ringslice_is_empty(&terminator)) break; // Keep fragmented URC until its CRLF arrives.
        ringslice_t line = ringslice_initializer(current.buf, current.buf_size, prefix.first, terminator.last);
        TACT_DEBUG(ctx, "[TACT][INFO] Found URC: %s", registrations[i].prefix);
        registrations[i].cb(line); // RX bytes remain owned until the callback returns.
        if(!owns_rx_slice) return; // External test slices cannot mutate the context RX ring.
        if(!tact_rx_remove_range(ctx, current.buf, current.buf_size, prefix.first, terminator.last)) return;
        TACT_CRITICAL_ENTER
        if(!ctx->init_struct.init || !ctx->init_struct.rx_buff)
        {
          TACT_CRITICAL_EXIT
          return;
        }
        tact_ring_buffer_t* rx = ctx->init_struct.rx_buff;
        current = ringslice_initializer(rx->buffer, rx->size, rx->tail, rx->head);
        TACT_CRITICAL_EXIT
        ++dispatched;
        found_urc = true;
        break;
      }
    }
    if(!found_urc) break;
  }
}

/*******************************************************************************
 ** @brief  Init tact lib  
 ** @param  ctx        core context
 ** @param  tact_printf pointer to user func of printf
 ** @param  tact_write  pointer to user func of write to uart
 ** @param  rx_buff    struct to ring buffer
 ** @return none
 ******************************************************************************/
bool tact_init_ex(tact_context_t* const ctx, const tact_printf_t tact_printf, const tact_write_t tact_write, tact_ring_buffer_t* rx_buff)
{
  if(!ctx || !tact_printf || !tact_write || !rx_buff || !rx_buff->buffer || rx_buff->size < 2u || rx_buff->head >= rx_buff->size || rx_buff->tail >= rx_buff->size || rx_buff->count >= rx_buff->size) return false;
  uint16_t queued = rx_buff->head >= rx_buff->tail ? (uint16_t)(rx_buff->head - rx_buff->tail) : (uint16_t)(rx_buff->size - rx_buff->tail + rx_buff->head);
  if(queued != rx_buff->count) return false;
  TACT_CRITICAL_ENTER
  if(ctx->init_struct.init) { TACT_CRITICAL_EXIT return false; }
  O1HeapInstance* heap = o1heapInit(ctx->mem_pool, sizeof(ctx->mem_pool));
  if(!heap) { TACT_CRITICAL_EXIT return false; }
  memset(&ctx->entity_queue, 0, sizeof(ctx->entity_queue));
  memset(ctx->urc_queue, 0, sizeof(ctx->urc_queue));
  memset(&ctx->init_struct, 0, sizeof(ctx->init_struct));
  ctx->time = 0;
  ctx->init_struct.heap = heap;
  ctx->init_struct.tact_write = tact_write;
  ctx->init_struct.tact_printf = tact_printf;
  ctx->init_struct.rx_buff = rx_buff;
  ctx->init_struct.init = true;
  TACT_DEBUG(ctx, "[TACT][INFO] TACT library initialized successfully", NULL);
  TACT_DEBUG(ctx, "[TACT][INFO] Memory pool size: %u bytes", (unsigned)sizeof(ctx->mem_pool));
  TACT_DEBUG(ctx, "[TACT][INFO] Memory overhead: %u/%u", (unsigned)o1heapGetDiagnostics(ctx->init_struct.heap).allocated, (unsigned)o1heapGetDiagnostics(ctx->init_struct.heap).capacity);
  TACT_DEBUG(ctx, "[TACT][INFO] Entity queue size: %u", (unsigned)TACT_ENTITY_QUEUE_SIZE);
  TACT_CRITICAL_EXIT
  return true;
}

void tact_init(tact_context_t* const ctx, const tact_printf_t tact_printf, const tact_write_t tact_write, tact_ring_buffer_t* rx_buff)
{
  (void)tact_init_ex(ctx, tact_printf, tact_write, rx_buff);
}

/*******************************************************************************
 ** @brief  DeInit tact lib  
 ** @param  ctx core context
 ** @return none
 ******************************************************************************/
/** 
* @brief Deinitializes the TACT library and releases all allocated resources
* @param ctx Pointer to the application context structure
* @return true - library deinitialized successfully, false - core validation failed
*         or active processing is running
*/
void tact_deinit(tact_context_t* const ctx)
{
  (void)tact_deinit_ex(ctx);
}

bool tact_deinit_ex(tact_context_t* const ctx)
{
  if(!ctx) return false;
  TACT_CRITICAL_ENTER
  if(!ctx->init_struct.init || !ctx->init_struct.heap || ctx->proc_active)
  {
    TACT_CRITICAL_EXIT
    return false;
  }
  TACT_DEBUG(ctx, "[TACT][INFO] Deinitializing TACT library", NULL);
  for(uint8_t i = 0; i < TACT_ENTITY_QUEUE_SIZE; ++i) tact_entity_release(ctx, &ctx->entity_queue.entity[i]);
  for(uint8_t i = 0; i < TACT_URC_QUEUE_SIZE; ++i) if(ctx->urc_queue[i].prefix) o1heapFree(ctx->init_struct.heap, ctx->urc_queue[i].prefix);
  ctx->init_struct.init = false;
  memset(&ctx->entity_queue, 0, sizeof(tact_entity_queue_t));
  memset(ctx->urc_queue, 0, sizeof(ctx->urc_queue));
  ctx->init_struct.tact_printf = NULL;
  ctx->init_struct.tact_write = NULL;
  ctx->init_struct.rx_buff = NULL;
  ctx->init_struct.heap = NULL;
  ctx->time = 0;
  ctx->proc_active = false;
  TACT_DEBUG(ctx, "[TACT][INFO] TACT library deinitialized", NULL);
  TACT_CRITICAL_EXIT
  return true;
}

/*******************************************************************************
 ** @brief  Function to append main queue with new group of at cmds
 ** @param  ctx          core context
 ** @param  item         ptr to your group of at cmds.
 ** @param  item_amount  amount  of your at cms in group 
 ** @param  cb           ur callback function for the whole group.
 ** @param  data_size    If you`r expecting some usefull data while execution pass here size of them.
 **                      And pass the ptr of this data to VA ARGS of each item with proper format to
 **                      get this data. If no need pass the 0.
 ** @param  meta         Ptr to some meta data of execution. Will be called in CB. Can be NULL.
 ** @return true: ok false: error while trying to append
 ******************************************************************************/
bool tact_entity_enqueue(tact_context_t* const ctx, const tact_item_t* const item, const uint8_t item_amount, const tact_entity_cb_t cb, uint16_t data_size, void* const meta)
{
  if(!ctx || !item || !item_amount || item_amount > TACT_MAX_ITEMS_PER_ENTITY) return false;
  TACT_CRITICAL_ENTER
  if(!ctx->init_struct.init || !ctx->init_struct.heap ||
     ctx->entity_queue.entity_cnt > TACT_ENTITY_QUEUE_SIZE ||
     ctx->entity_queue.entity_head >= TACT_ENTITY_QUEUE_SIZE ||
     ctx->entity_queue.entity_tail >= TACT_ENTITY_QUEUE_SIZE)
  {
    TACT_CRITICAL_EXIT
    return false;
  }
  TACT_DEBUG(ctx, "[TACT][INFO] Enqueueing entity with %d items", item_amount);
  if(ctx->entity_queue.entity_cnt >= TACT_ENTITY_QUEUE_SIZE)
  {
    TACT_DEBUG(ctx, "[TACT][ERROR] Queue is full", NULL);
    TACT_CRITICAL_EXIT
    return false;
  }
  tact_entity_t* cur_entity = &ctx->entity_queue.entity[ctx->entity_queue.entity_head];
  memset(cur_entity, 0, sizeof(*cur_entity));
  cur_entity->data = data_size ? tact_malloc(ctx, data_size) : NULL;
  if(data_size && !cur_entity->data) goto error_exit;
  cur_entity->data_size = data_size;
  if(cur_entity->data) memset(cur_entity->data, 0, data_size);
  cur_entity->item = tact_malloc(ctx, item_amount * sizeof(tact_item_t));
  if(!cur_entity->item) goto error_exit;
  memset(cur_entity->item, 0, item_amount * sizeof(tact_item_t));
  cur_entity->item_cnt = item_amount;
  for(int i = 0; i < item_amount; i++)
  {
    if(!item[i].req && !item[i].answ.prefix) goto error_exit; //unhandle comb
    if(item[i].parce_type != TACT_PARCE_SIMCOM && item[i].parce_type != TACT_PARCE_RAW) goto error_exit;
    if(item[i].req)
    {
      const char* command = item[i].req;
      size_t marker_len = strlen(TACT_CMD_SAVE);
      if(strncmp(command, TACT_CMD_SAVE, marker_len) == 0) command += marker_len;
      if(!command[0]) goto error_exit;
    }
    if(item[i].answ.prefix)
    {
      const char* prefix = item[i].answ.prefix;
      size_t marker_len = strlen(TACT_CMD_SAVE);
      if(strncmp(prefix, TACT_CMD_SAVE, marker_len) == 0) prefix += marker_len;
      if(!prefix[0]) goto error_exit;
    }
    memcpy(&cur_entity->item[i], &item[i], sizeof(tact_item_t));
    tact_item_t* cur_item = &cur_entity->item[i];
    cur_item->owned = 0;
    char* source_req = item[i].req;
    char* source_prefix = item[i].answ.prefix;
    void** source_ptrs = item[i].answ.ptrs;
    bool save_req = source_req && strncmp(source_req, TACT_CMD_SAVE, strlen(TACT_CMD_SAVE)) == 0;
    bool save_prefix = source_prefix && strncmp(source_prefix, TACT_CMD_SAVE, strlen(TACT_CMD_SAVE)) == 0;

    // Start with only borrowed pointers installed. Owned copies are attached
    // after allocation so the rollback path never frees caller-owned memory.
    cur_item->req = save_req ? NULL : source_req;
    cur_item->answ.prefix = save_prefix ? NULL : source_prefix;
    cur_item->answ.ptrs = NULL;

    if(source_ptrs && source_ptrs[0] != TACT_NO_ARG)
    {
      if(!item[i].answ.format || !data_size || !cur_entity->data) goto error_exit;
      size_t ptr_count = 0;
      while(ptr_count < 6 && source_ptrs[ptr_count] != TACT_NO_ARG) ++ptr_count;
      if(ptr_count == 0 || source_ptrs[ptr_count] != TACT_NO_ARG) goto error_exit;
      cur_item->answ.ptrs = tact_malloc(ctx, (ptr_count + 1u) * sizeof(void*));
      if(!cur_item->answ.ptrs) goto error_exit;
      for(size_t j = 0; j < ptr_count; ++j)
      {
        size_t offset = (size_t)source_ptrs[j];
        if(offset >= data_size) goto error_exit;
        cur_item->answ.ptrs[j] = (char*)cur_entity->data + offset;
        TACT_DEBUG(ctx, "[TACT][INFO] User data for item[%d] ARG[%u]: offset=%zu, ptr=%p", i, (unsigned)j,
                  offset, cur_item->answ.ptrs[j]);
      }
      cur_item->answ.ptrs[ptr_count] = TACT_NO_ARG;
    }
    else if(item[i].answ.format) goto error_exit;
    if(save_req)
    {
      size_t length = strlen(source_req) + 1;
      cur_item->req = tact_malloc(ctx, length);
      if(!cur_item->req) goto error_exit;
      cur_item->owned |= TACT_OWN_REQ;
      memcpy(cur_item->req, source_req, length);
    }
    if(save_prefix)
    {
      size_t length = strlen(source_prefix) + 1;
      cur_item->answ.prefix = tact_malloc(ctx, length);
      if(!cur_item->answ.prefix) goto error_exit;
      cur_item->owned |= TACT_OWN_PREFIX;
      memcpy(cur_item->answ.prefix, source_prefix, length);
    }
  }
  cur_entity->cb = cb;
  cur_entity->meta = meta;
  cur_entity->state = TACT_STATE_WRITE;
  ctx->entity_queue.entity_head = (ctx->entity_queue.entity_head + 1) % TACT_ENTITY_QUEUE_SIZE;
  ++ctx->entity_queue.entity_cnt;
  TACT_DEBUG(ctx, "[TACT][INFO] Entity enqueued. Queue count: %d. Memory used: %u/%u. User data at %p. ",
             ctx->entity_queue.entity_cnt, (unsigned)o1heapGetDiagnostics(ctx->init_struct.heap).allocated,
             (unsigned)o1heapGetDiagnostics(ctx->init_struct.heap).capacity, cur_entity->data);
  TACT_CRITICAL_EXIT
  return true;

  error_exit:
    TACT_DEBUG(ctx, "[TACT][ERROR] Queue failed", NULL); 
    tact_entity_release(ctx, cur_entity);
    TACT_CRITICAL_EXIT
    return false; 
}

/*******************************************************************************
 ** @brief  Clear first entity from the queue 
 ** @param  ctx core context
 ** @return false: some errors; true: ok
 ******************************************************************************/
bool tact_entity_dequeue(tact_context_t* const ctx)
{
  if(!ctx) return false;
  TACT_CRITICAL_ENTER
  if(!ctx->init_struct.init || !ctx->init_struct.heap || ctx->proc_active)
  {
    TACT_CRITICAL_EXIT
    return false;
  }
  bool result = tact_entity_dequeue_locked(ctx);
  TACT_CRITICAL_EXIT
  return result;
}

/*******************************************************************************
 ** @brief  Function to append URC queue
 ** @param  ctx  core context
 ** @param  urc  ptr to your URC.
 ** @return true/false
 ******************************************************************************/
bool tact_urc_enqueue(tact_context_t* const ctx, const tact_urc_queue_t* const urc)  
{
  if(!ctx || !urc || !urc->prefix || !urc->cb || !urc->prefix[0]) return false;
  size_t prefix_len = 0;
  while(prefix_len <= TACT_URC_PREFIX_MAX_LEN && urc->prefix[prefix_len]) ++prefix_len;
  if(!prefix_len || prefix_len > TACT_URC_PREFIX_MAX_LEN) return false;

  TACT_CRITICAL_ENTER
  if(!ctx->init_struct.init || !ctx->init_struct.heap)
  {
    TACT_CRITICAL_EXIT
    return false;
  }
  tact_urc_queue_t* empty_slot = NULL;
  for(uint8_t i = 0; i < TACT_URC_QUEUE_SIZE; ++i)
  {
    if(ctx->urc_queue[i].prefix && strcmp(ctx->urc_queue[i].prefix, urc->prefix) == 0)
    {
      ctx->urc_queue[i].cb = urc->cb;
      TACT_CRITICAL_EXIT
      return true;
    }
    if(!ctx->urc_queue[i].prefix && !empty_slot) empty_slot = &ctx->urc_queue[i];
  }
  if(!empty_slot)
  {
    TACT_DEBUG(ctx, "[TACT][ERROR] URC queue is full", NULL);
    TACT_CRITICAL_EXIT
    return false;
  }

  char* prefix_copy = tact_malloc(ctx, prefix_len + 1u);
  if(!prefix_copy)
  {
    TACT_CRITICAL_EXIT
    return false;
  }
  memcpy(prefix_copy, urc->prefix, prefix_len + 1u);
  empty_slot->prefix = prefix_copy;
  empty_slot->cb = urc->cb;
  TACT_DEBUG(ctx, "[TACT][INFO] URC enqueued successfully", NULL);
  TACT_CRITICAL_EXIT
  return true;
}

/*******************************************************************************
 ** @brief  Function to delete URC from queue
 ** @param  ctx  core context
 ** @param  prefix  prefix of your URC.
 ** @return true/false
 ******************************************************************************/
bool tact_urc_dequeue(tact_context_t* const ctx, const char* prefix)
{
  if(!ctx || !prefix) return false;
  TACT_CRITICAL_ENTER
  if(!ctx->init_struct.init || !ctx->init_struct.heap)
  {
    TACT_CRITICAL_EXIT
    return false;
  }
  for(uint8_t i = 0; i < TACT_URC_QUEUE_SIZE; ++i)
  {
    if(!ctx->urc_queue[i].prefix) continue;
    if(strcmp(ctx->urc_queue[i].prefix, prefix) == 0)
    {
      o1heapFree(ctx->init_struct.heap, ctx->urc_queue[i].prefix);
      memset(&ctx->urc_queue[i], 0, TACT_URC_SIZE);
      TACT_DEBUG(ctx,"[TACT][INFO] URC dequeued successfully", NULL);
      TACT_CRITICAL_EXIT
      return true;
    }
  }
  TACT_DEBUG(ctx, "[TACT][INFO] URC dequeued fail", NULL);
  TACT_CRITICAL_EXIT
  return false;
}

/*******************************************************************************
 ** @brief  Function get init. 
 ** @param  ctx  core context
 ** @return @tact_init_t
 ******************************************************************************/
tact_init_t tact_get_init(tact_context_t* const ctx)
{
  tact_init_t res = {0};
  if(!ctx) return res;
  TACT_CRITICAL_ENTER
  res = ctx->init_struct;
  TACT_CRITICAL_EXIT
  return res;
}

/*******************************************************************************
 ** @brief  Function get time in 10ms. 
 ** @param  ctx  core context
 ** @return time
 ******************************************************************************/
uint32_t tact_get_cur_time(tact_context_t* const ctx)
{
  if(!ctx) return 0;
  TACT_CRITICAL_ENTER
  uint32_t res = ctx->time;
  TACT_CRITICAL_EXIT
  return res;
}

/*******************************************************************************
** @brief Pushes a block of data into the RX ring buffer with thread safety
** @param ctx Pointer to the application context structure
** @param data Pointer to the source byte array to be added
** @param len Number of bytes to copy from the source array
** @return true - data successfully appended to the buffer, false - validation failed
**         or insufficient free space available
******************************************************************************/
bool tact_rx_push(tact_context_t* const ctx, const uint8_t* const data, uint16_t len)
{
  if(!ctx || (len && !data)) return false;
  TACT_CRITICAL_ENTER
  tact_ring_buffer_t* rx = ctx->init_struct.rx_buff;
  if(!ctx->init_struct.init || !rx || !rx->buffer || !rx->size ||
     rx->head >= rx->size || rx->tail >= rx->size || rx->count >= rx->size)
  {
    TACT_CRITICAL_EXIT
    return false;
  }
  uint16_t free_space = (uint16_t)(rx->size - 1u - rx->count);
  if(len > free_space)
  {
    TACT_CRITICAL_EXIT
    return false;
  }
  uint16_t first_chunk = (uint16_t)(rx->size - rx->head);
  if(first_chunk > len) first_chunk = len;
  if(first_chunk) memcpy(rx->buffer + rx->head, data, first_chunk);
  uint16_t second_chunk = (uint16_t)(len - first_chunk);
  if(second_chunk) memcpy(rx->buffer, data + first_chunk, second_chunk);
  rx->head = (uint16_t)(((uint32_t)rx->head + len) % rx->size);
  rx->count = (uint16_t)(rx->count + len);
  TACT_CRITICAL_EXIT
  return true;
}

/*******************************************************************************
 ** @brief  Function to custom malloc. Handle the concurrent state
 ** @param  ctx  core context
 ** @param  size amount to alloc
 ** @return ptr to new memory
 ******************************************************************************/
void* tact_malloc(tact_context_t* const ctx, size_t size)
{
  if(!ctx || !size) return NULL;
  TACT_CRITICAL_ENTER
  if(!ctx->init_struct.init || !ctx->init_struct.heap) { TACT_CRITICAL_EXIT return NULL; }
  void* res = o1heapAllocate(ctx->init_struct.heap, size);
  TACT_CRITICAL_EXIT
  return res;
}

/*******************************************************************************
 ** @brief  Function to custom free. Handle the concurrent state
 ** @param  ctx  core context
 ** @param  ptr  ptr to delete
 ** @return none
 ******************************************************************************/
void tact_free(tact_context_t* ctx, void* ptr)
{
  if(!ctx || !ptr) return;
  TACT_CRITICAL_ENTER
  if(!ctx->init_struct.init || !ctx->init_struct.heap) { TACT_CRITICAL_EXIT return; }
  o1heapFree(ctx->init_struct.heap, ptr);
  TACT_CRITICAL_EXIT
}

/*******************************************************************************
** @brief Executes the main state machine loop for processing transmissions and responses
** @note This function should be called periodically (e.g., every 10ms)
** @param ctx Pointer to the application context structure
******************************************************************************/
void tact_core_proc(tact_context_t* const ctx)
{
  if(!ctx) return;
  TACT_CRITICAL_ENTER
  tact_ring_buffer_t* rx = ctx->init_struct.rx_buff;
  if(!ctx->init_struct.init || !rx || !rx->buffer || !rx->size || rx->head >= rx->size || rx->tail >= rx->size || ctx->proc_active)
  {
    TACT_CRITICAL_EXIT
    return;
  }
  ctx->proc_active = true;
  ++ctx->time; // unsigned wraparound is defined
  bool check_urcs = ctx->time % TACT_URC_FREQ_CHECK == 0;
  ringslice_t rs_me = ringslice_initializer(rx->buffer, rx->size, rx->tail, rx->head);
  TACT_CRITICAL_EXIT

  if(check_urcs) tact_process_urcs(ctx, &rs_me); // check URCs outside the critical section

  TACT_CRITICAL_ENTER
  if(!ctx->init_struct.init || !ctx->init_struct.rx_buff)
  {
    TACT_CRITICAL_EXIT
    tact_proc_finish(ctx);
    return;
  }
  rx = ctx->init_struct.rx_buff;
  rs_me = ringslice_initializer(rx->buffer, rx->size, rx->tail, rx->head);
  if(!ctx->entity_queue.entity_cnt)
  {
    TACT_CRITICAL_EXIT
    tact_proc_finish(ctx);
    return;
  }
  tact_entity_t* entity = &ctx->entity_queue.entity[ctx->entity_queue.entity_tail];
  if(!entity->item || entity->item_id >= entity->item_cnt)
  {
    TACT_CRITICAL_EXIT
    tact_proc_finish(ctx);
    return;
  }
  tact_item_t* item = &entity->item[entity->item_id];
  tact_write_t write_fn = ctx->init_struct.tact_write;
  TACT_CRITICAL_EXIT //we work with exclusive memory field for this entity bcs of ring buffer
  switch(entity->state)
  {
    case TACT_STATE_WRITE:
         if(!item->req)
         {
           entity->tx_offset = 0;
           entity->tx_timer = 0;
           entity->tx_started = false;
           entity->timer = item->meta.wait;
           entity->state = TACT_STATE_READ;
           break;
         }
         {
           size_t marker_len = strlen(TACT_CMD_SAVE);
           size_t request_offset = strncmp(item->req, TACT_CMD_SAVE, marker_len) == 0 ? marker_len : 0u;
           const char* request = item->req + request_offset;
           size_t request_len = strlen(request);
           if(request_len > UINT16_MAX || entity->tx_offset > request_len || !write_fn)
           {
             entity->state = TACT_STATE_WRITE;
             tact_proc_handle_cmd_result(ctx, entity, item, false);
             break;
           }
           if(!entity->tx_started)
           {
             entity->tx_offset = 0;
             entity->tx_timer = item->meta.wait;
             entity->tx_started = true;
             TACT_DEBUG(ctx, "[TACT][INFO] [TX] request length=%u", (unsigned)request_len);
           }
           size_t remaining = request_len - entity->tx_offset;
           if(!remaining)
           {
             entity->tx_offset = 0;
             entity->tx_timer = 0;
             entity->tx_started = false;
             entity->timer = item->meta.wait;
             entity->state = TACT_STATE_READ;
             break;
           }
           uint16_t accepted = write_fn((uint8_t*)request + entity->tx_offset, (uint16_t)remaining);
           if(accepted > remaining)
           {
             TACT_DEBUG(ctx, "[TACT][ERROR] tact_write returned more bytes than requested", NULL);
             entity->tx_offset = 0;
             entity->tx_timer = 0;
             entity->tx_started = false;
             tact_proc_handle_cmd_result(ctx, entity, item, false);
             break;
           }
           entity->tx_offset = (uint16_t)(entity->tx_offset + accepted);
           if(entity->tx_offset == request_len)
           {
             entity->tx_offset = 0;
             entity->tx_timer = 0;
             entity->tx_started = false;
             entity->timer = item->meta.wait;
             entity->state = TACT_STATE_READ;
           }
           else
           {
             if(entity->tx_timer) --entity->tx_timer;
             if(!entity->tx_timer) tact_proc_handle_tx_timeout(ctx, entity, item);
           }
         }
         break;
    case TACT_STATE_READ:
         if(entity->timer) --entity->timer;
         if(tact_cmd_ring_parcer(ctx, entity, item, rs_me) == 1) 
         {
           entity->state = TACT_STATE_WRITE;
           entity->timer = 0;
           TACT_DEBUG(ctx, "[TACT][INFO] Successful entity cmd %d/%d", entity->item_id+1, entity->item_cnt);
           tact_proc_handle_cmd_result(ctx, entity, item, true);  
         } 
         else if(!entity->timer)
         {
           tact_proc_handle_timeout(ctx, entity, item);
         }
         else
         {
           if(entity->timer == item->meta.wait/2) TACT_DEBUG(ctx, "[TACT][INFO] Waiting......", NULL);
         }
         break;
    default: 
         TACT_DEBUG(ctx, "[TACT][INFO] Unknown state: %d", entity->state);
         break;
  }
  tact_proc_finish(ctx);
}

/**
** @brief Handles the execution outcome of an individual item command within an entity
** @param ctx Pointer to the application context structure
** @param entity Pointer to the active entity containing the command list
** @param item Pointer to the executed command item structure
** @param success True if the command succeeded, false if it failed
**/
static void tact_proc_handle_cmd_result(tact_context_t* const ctx, tact_entity_t* const entity, tact_item_t* const item, const bool success) 
{
  DBC_REQUIRE(801, ctx);
  DBC_REQUIRE(802, entity);
  DBC_REQUIRE(803, item);
  int step = success ? item->meta.ok_step : item->meta.err_step;
  if(entity->item_cnt && step != 0)
  {
    int next_item_id = (int)entity->item_id + step;
    if(next_item_id >= 0 && next_item_id < entity->item_cnt)
    {
      TACT_DEBUG(ctx, "[TACT][INFO] Next cmd of entity", NULL);
      entity->item_id = (uint8_t)next_item_id;
      return;
    }
  }  
  //Step outside of cmd range or last cmd or step == 0
  TACT_DEBUG(ctx, "[TACT][INFO] End of entity", NULL);
  #ifndef TACT_TEST
  if(!success) //debug
  {
    tact_ring_buffer_t* rx = ctx->init_struct.rx_buff;
    if(rx && rx->buffer && rx->size && rx->head < rx->size && rx->count < rx->size)
    {
      uint16_t dump_len = rx->count < 250u ? rx->count : 250u;
      uint16_t data_start = rx->head >= dump_len ? (uint16_t)(rx->head - dump_len) : (uint16_t)(rx->size - (dump_len - rx->head));
      ringslice_t rs_me = ringslice_initializer(rx->buffer, rx->size, data_start, rx->head);
      tact_printf_from_ring(ctx, rs_me, "Failed last RX bytes: ");
    }
  }
  #endif
  if(entity->cb) entity->cb(success, entity->meta, entity->data);
  TACT_CRITICAL_ENTER
  /* Public dequeue is blocked while proc_active; this is the sole internal
   * release path for the entity currently owned by this invocation. */
  if(ctx->init_struct.init && ctx->entity_queue.entity_cnt &&
     &ctx->entity_queue.entity[ctx->entity_queue.entity_tail] == entity
  ){
    (void)tact_entity_dequeue_locked(ctx);
  }
  TACT_CRITICAL_EXIT
}

/**
** @brief Processes a response timeout event for an item, managing retry counts
** @param ctx Pointer to the application context structure
** @param entity Pointer to the active entity containing the command list
** @param item Pointer to the timed-out command item structure
**/
static void tact_proc_handle_timeout(tact_context_t* const ctx, tact_entity_t* const entity, tact_item_t* const item)
{
  entity->tx_offset = 0;
  entity->tx_timer = 0;
  entity->tx_started = false;
  entity->timer = 0;
  entity->state = TACT_STATE_WRITE;
  if(item->meta.rpt_cnt) --item->meta.rpt_cnt;
  if(item->meta.rpt_cnt)
  {
    TACT_DEBUG(ctx, "[TACT][INFO] Timeout, retries left: %d", item->meta.rpt_cnt);
    return;
  }
  TACT_DEBUG(ctx, "[TACT][INFO] Failure entity cmd %d/%d", entity->item_id+1, entity->item_cnt);
  tact_proc_handle_cmd_result(ctx, entity, item, false);
}

/**
** @brief Processes a transmission timeout event, preventing retries if a partial request was sent
** @param ctx Pointer to the application context structure
** @param entity Pointer to the active entity containing the command list
** @param item Pointer to the timed-out transmission command item structure
**/
static void tact_proc_handle_tx_timeout(tact_context_t* const ctx, tact_entity_t* const entity, tact_item_t* const item)
{
  bool partial_request_sent = entity->tx_offset != 0u;
  entity->tx_offset = 0;
  entity->tx_timer = 0;
  entity->tx_started = false;
  entity->timer = 0;
  entity->state = TACT_STATE_WRITE;
  if(partial_request_sent)
  {
    TACT_DEBUG(ctx, "[TACT][ERROR] UART write timed out after a partial request; automatic retry suppressed", NULL);
    tact_proc_handle_cmd_result(ctx, entity, item, false);
    return;
  }
  tact_proc_handle_timeout(ctx, entity, item);
}

/**
** @brief Finalizes the core processing cycle by resetting the active flag
** @param ctx Pointer to the application context structure
**/
static void tact_proc_finish(tact_context_t* const ctx)
{
  TACT_CRITICAL_ENTER
  if(ctx) ctx->proc_active = false;
  TACT_CRITICAL_EXIT
}


#ifdef TACT_TEST
/*******************************************************************************
 ** @brief  TEST implementations
 ** @param  none
 ** @return none
 ******************************************************************************/
static bool tact_test_rx_snapshot(tact_context_t* const ctx, uint16_t* const tail, uint16_t* const count)
{
  if(!ctx || !tail || !count || !ctx->init_struct.rx_buff) return false;
  *tail = ctx->init_struct.rx_buff->tail;
  *count = ctx->init_struct.rx_buff->count;
  return true;
}

static void tact_test_rx_restore(tact_context_t* const ctx, const bool saved, const uint16_t tail, const uint16_t count)
{
  if(!saved || !ctx || !ctx->init_struct.rx_buff) return;
  ctx->init_struct.rx_buff->tail = tail;
  ctx->init_struct.rx_buff->count = count;
}

void _tact_core_proc(tact_context_t* const ctx) { 
  tact_core_proc(ctx);
}

int _tact_cmd_ring_parcer(tact_context_t* const ctx, const tact_entity_t* const entity, const tact_item_t* const item, const ringslice_t rs_me) { 
  uint16_t tail = 0;
  uint16_t count = 0;
  bool saved = tact_test_rx_snapshot(ctx, &tail, &count);
  int result = tact_cmd_ring_parcer(ctx, entity,item, rs_me);
  tact_test_rx_restore(ctx, saved, tail, count);
  return result;
}

void _tact_simcom_parcer_find_rs_req(const ringslice_t* const me, ringslice_t* const rs_req, const char* const req) { 
  tact_simcom_parcer_find_rs_req(me, rs_req, req); 
}

void _tact_simcom_parcer_find_rs_res(const ringslice_t* const me, const ringslice_t* const rs_req, ringslice_t* const rs_res) { 
  tact_simcom_parcer_find_rs_res(me, rs_req, rs_res); 
}

void _tact_simcom_parcer_find_rs_data(const ringslice_t* const me, const ringslice_t* const rs_req, const ringslice_t* const rs_res, ringslice_t* const rs_data) { 
  tact_simcom_parcer_find_rs_data(me, rs_req, rs_res, rs_data); 
}

int _tact_simcom_parcer_post_proc(tact_context_t* const ctx, const ringslice_t* const me, const ringslice_t* const rs_req, const ringslice_t* const rs_res, 
                                 const ringslice_t* const rs_data, const tact_item_t* const item, const tact_entity_t* const entity) { 
  uint16_t tail = 0;
  uint16_t count = 0;
  bool saved = tact_test_rx_snapshot(ctx, &tail, &count);
  int result = tact_simcom_parcer_post_proc(ctx, me, rs_req, rs_res, rs_data, item, entity);
  tact_test_rx_restore(ctx, saved, tail, count);
  return result;
}

int _tact_string_boolean_ops(const ringslice_t* const rs_data, const char* const pattern) { 
  return tact_string_boolean_ops(rs_data, pattern); 
}

int _tact_cmd_sscanf(const ringslice_t* const rs_data, const tact_item_t* const item) {
  return tact_cmd_sscanf(rs_data, item);
}

void _tact_process_urcs(tact_context_t* const ctx, const ringslice_t* me)
{
  tact_process_urcs(ctx, me);
}


tact_entity_queue_t* _tact_get_entity_queue(tact_context_t* const ctx) {
  return &ctx->entity_queue;
}

tact_urc_queue_t* _tact_get_urc_queue(tact_context_t* const ctx) {
  return ctx->urc_queue;
}

tact_init_t _tact_get_init(tact_context_t* const ctx) {
  return tact_get_init(ctx);
}

#endif
