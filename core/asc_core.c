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
#include "dbc_assert.h"
#include "asc_mdl_general.h"
#include "stdlib.h"
#include "asc_port.h"
   
/*******************************************************************************
 * Local pre-processor symbols/macros ('#define')
 ******************************************************************************/
DBC_MODULE_NAME("ASC_CORE")
#define ASC_OWN_REQ    0x01u
#define ASC_OWN_PREFIX 0x02u

/*******************************************************************************
 * Global variable definitions (declared in header file with 'extern')
 ******************************************************************************/
/*******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/
static bool asc_string_boolean_find(const ringslice_t* const rs_data, const char* const pattern, ringslice_cnt_t* const match_end);
static void asc_proc_handle_cmd_result(asc_context_t* const ctx, asc_entity_t* const entity, asc_item_t* const item, const bool success); 
static void asc_proc_handle_timeout(asc_context_t* const ctx, asc_entity_t* const entity, asc_item_t* const item);
static void asc_proc_handle_tx_timeout(asc_context_t* const ctx, asc_entity_t* const entity, asc_item_t* const item);
static void asc_proc_finish(asc_context_t* const ctx);

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
 * Items stored in the queue own only strings marked with ASC_CMD_SAVE, the
 * copied argument descriptors, the entity data block, and the copied item
 * array. User strings, callbacks, and metadata remain borrowed.
******************************************************************************/
static void asc_entity_release(asc_context_t* const ctx, asc_entity_t* const entity)
{
  if(!ctx || !entity || !ctx->init_struct.heap) return;

  if(entity->item)
  {
    for(uint8_t i = 0; i < entity->item_cnt; ++i)
    {
      asc_item_t* item = &entity->item[i];
      if(item->owned & ASC_OWN_REQ) o1heapFree(ctx->init_struct.heap, item->req);
      if(item->owned & ASC_OWN_PREFIX) o1heapFree(ctx->init_struct.heap, item->answ.prefix);
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
static bool asc_entity_dequeue_locked(asc_context_t* const ctx)
{
  if(!ctx || !ctx->init_struct.init || !ctx->init_struct.heap || !ctx->entity_queue.entity_cnt) return false;
  asc_entity_t* cur_entity = &ctx->entity_queue.entity[ctx->entity_queue.entity_tail];
  ASC_DEBUG(ctx, "[ASC][INFO] Dequeueing entity with %d items", cur_entity->item_cnt);
  asc_entity_release(ctx, cur_entity);
  ctx->entity_queue.entity_tail = (ctx->entity_queue.entity_tail +1) % ASC_ENTITY_QUEUE_SIZE;
  --ctx->entity_queue.entity_cnt;
  ASC_DEBUG(
    ctx, "[ASC][INFO] Entity dequeued. Queue count: %d. Memory used: %d/%d",  
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
static bool asc_rx_consume_to(asc_context_t* const ctx, const uint8_t* const source_buffer, const uint16_t source_size, const uint16_t new_tail)
{
  if(!ctx) return false;
  ASC_CRITICAL_ENTER
  asc_ring_buffer_t* rx = ctx->init_struct.rx_buff;
  if(!ctx->init_struct.init || !rx || !rx->buffer || !rx->size ||
     rx->buffer != source_buffer || rx->size != source_size ||
     rx->head >= rx->size || rx->tail >= rx->size || new_tail >= rx->size ||
     rx->count >= rx->size)
  {
    ASC_CRITICAL_EXIT
    return false;
  }
  uint16_t consumed = new_tail >= rx->tail ? (uint16_t)(new_tail - rx->tail) : (uint16_t)(rx->size - rx->tail + new_tail);
  if(consumed > rx->count)
  {
    ASC_CRITICAL_EXIT
    return false;
  }
  rx->tail = new_tail;
  rx->count = (uint16_t)(rx->count - consumed);
  ASC_CRITICAL_EXIT
  return true;
}


/*******************************************************************************
 ** @brief  Boolean operation for strings in ATL
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
static bool asc_string_boolean_ops(const ringslice_t* const rs_data, const char* const pattern)
{
  return asc_string_boolean_find(rs_data, pattern, NULL);
}

/** 
* @brief Searches for a boolean pattern in a ring slice and finds the match end
* @param rs_data Pointer to the ring slice data container
* @param pattern String containing keywords separated by '|' (OR) or '&' (AND)
* @param match_end Pointer to store the last position index of the overall match
* @return true - all (AND) or any (OR) terms matched, false - validation failed
*/
static bool asc_string_boolean_find(const ringslice_t* const rs_data, const char* const pattern, ringslice_cnt_t* const match_end)
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
static bool asc_cmd_sscanf(const ringslice_t* const rs_data, const asc_item_t* const item) 
{
  DBC_REQUIRE(155, rs_data); 
  DBC_REQUIRE(156, item);  
  const char *format = item->answ.format;
  void** output_ptrs = item->answ.ptrs;
  if(!format || !output_ptrs) return false;
  size_t param_count = 0;
  while(param_count < 6 && output_ptrs[param_count] != ASC_NO_ARG) ++param_count;
  if(param_count == 0 || output_ptrs[param_count] != ASC_NO_ARG) return false;

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
static bool asc_raw_parcer(asc_context_t* const ctx, const ringslice_t rs_me, const asc_item_t* const item, const asc_entity_t* const entity)
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
    char* prefix = strncmp(item->answ.prefix, ASC_CMD_SAVE, strlen(ASC_CMD_SAVE)) ? item->answ.prefix : item->answ.prefix +strlen(ASC_CMD_SAVE);
    ringslice_cnt_t match_end = 0;
    res = asc_string_boolean_find(&rs_data, prefix, &match_end);
    if(res && item->answ.format) res = asc_cmd_sscanf(&rs_data, item);
    if(res) {
      proced_data = item->answ.format ? rs_data.last : match_end;
      consume_data = true;
    }
  }
  else
  {
    if(item->answ.format) res = asc_cmd_sscanf(&rs_data, item);
    if(res) {
      proced_data = (uint16_t)(item->answ.format ? rs_data.last : rs_me.last);
      consume_data = true;
    }
  }

  if(consume_data) asc_rx_consume_to(ctx, rs_me.buf, rs_me.buf_size, (uint16_t)proced_data);
  if(item->answ.cb) item->answ.cb(ringslice_initializer(rs_me.buf, rs_me.buf_size, rs_data.first, rs_me.last), res, entity->data);
  return res; 
}

/** 
 * @brief Find req echo
 */
static void asc_simcom_parcer_find_rs_req(const ringslice_t* const me, ringslice_t* const rs_req, const char* req)
{
  DBC_REQUIRE(125, me); 
  DBC_REQUIRE(127, rs_req); 

  if(!req) return;

  if(strncmp(req, ASC_CMD_SAVE, strlen(ASC_CMD_SAVE)) == 0) req += strlen(ASC_CMD_SAVE);
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
static void asc_simcom_parcer_find_rs_res(const ringslice_t* const me, const ringslice_t* const rs_req, ringslice_t* const rs_res)
{
  DBC_REQUIRE(129, rs_req); 
  DBC_REQUIRE(131, rs_res); 

  if(ringslice_is_empty(rs_req)) return;

  ringslice_t tmp = ringslice_subslice_after(me, rs_req, 0);

  if(ringslice_is_empty(&tmp)) return;

  *rs_res = ringslice_strstr(&tmp, ASC_CMD_ERROR);
  if(ringslice_is_empty(rs_res)) *rs_res = ringslice_strstr(&tmp, ASC_CMD_OK);
}

/** 
 * @brief Find and data ring slice 
 */
static void asc_simcom_parcer_find_rs_data(const ringslice_t* const me, const ringslice_t* const rs_req, const ringslice_t* const rs_res, ringslice_t* const rs_data)
{
  DBC_REQUIRE(134, me); 
  DBC_REQUIRE(135, rs_req);  
  DBC_REQUIRE(136, rs_res); 
  DBC_REQUIRE(137, rs_data);

  const uint8_t crlf_len = strlen(ASC_CMD_CRLF);

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
    ringslice_t after_req = ringslice_subslice_after(me, rs_req, strlen(ASC_CMD_CRLF"OK"ASC_CMD_CRLF));
    *rs_data = ringslice_subslice_equals(&after_req, rs_res) 
               ? ringslice_subslice_after(me, rs_res, 0) // REQ\r\r\nOK\r\n\r\nDATA\r\n
               : ringslice_subslice_gap(rs_req, rs_res); // REQ\r\r\nDATA\r\n\r\nOK\r\n
  }

  if((ringslice_len(rs_data) <= 2 *crlf_len) || (ringslice_strncmp(rs_data, ASC_CMD_CRLF, 2))) // If data more than 2bytes and first 2 bytes its CRLF
  {
    *rs_data = (ringslice_t){0};
    return;
  }
  *rs_data = ringslice_subslice_with_suffix(rs_data, crlf_len, ASC_CMD_CRLF); 
  if(ringslice_is_empty(rs_data)) return;
  *rs_data = ringslice_subslice(rs_data, crlf_len, ringslice_len(rs_data)- crlf_len); // Extract clean data (without surrounding CRLFx2)
}

/* The extracted payload excludes its framing CRLF. Consume that terminator as
 * part of the parsed response, but only when both slices share the same RX
 * backing ring. Synthetic parser tests may intentionally use separate slices.
 */
static ringslice_cnt_t asc_simcom_line_end(const ringslice_t* const me, const ringslice_t* const data)
{
  if(!me || !data || me->buf != data->buf || me->buf_size != data->buf_size) return data ? data->last : 0;
  ringslice_t suffix = ringslice_initializer(me->buf, me->buf_size, data->last, me->last);
  ringslice_t terminator = ringslice_strstr(&suffix, ASC_CMD_CRLF);
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
static bool asc_simcom_parcer_post_proc(asc_context_t* const ctx, const ringslice_t* const me, const ringslice_t* const rs_req, const ringslice_t* const rs_res, 
                                        const ringslice_t* const rs_data, const asc_item_t* const item, const asc_entity_t* const entity)
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
  if(prefix && !strncmp(prefix, ASC_CMD_SAVE, strlen(ASC_CMD_SAVE))) prefix += strlen(ASC_CMD_SAVE);

  switch((rs_req_exist << 2) | (rs_res_exist << 1) | rs_data_exist) // Bitmask: REQ[bit2] RES[bit1] DATA[bit0]
  {
      case 0x01: //0b001 - NULL NULL DATA (PREFIX)
           if(!prefix) break;
           res = asc_string_boolean_ops(rs_data, prefix);
           if(res && item->answ.format) res = asc_cmd_sscanf(rs_data, item);
           if(res) { proced_data = asc_simcom_line_end(me, rs_data); consume_data = true; }
           break;
      case 0x05: //0b101 - REQ NULL DATA (REQ +PREFIX)
           if(!item->req || !prefix) break;
           res = asc_string_boolean_ops(rs_data, prefix);
           if(res && item->answ.format) res = asc_cmd_sscanf(rs_data, item);
           if(res) { proced_data = asc_simcom_line_end(me, rs_data); consume_data = true; }
           break;
      case 0x06: //0b110 - REQ RES NULL (REQ, NO PREFIX, NO FORMAT)
           if(!item->req || ringslice_strcmp(rs_res, ASC_CMD_ERROR) == 0) res = false;
           else if(prefix || item->answ.format) res = false;
           else res = true;
           if(res) { proced_data = rs_res->last; consume_data = true; }
           break;
      case 0x07: //0b111 - REQ RES DATA (REQ)
           if(!item->req) break;
           if(rs_res->buf != rs_data->buf || rs_res->buf_size != rs_data->buf_size) break;
           else if(ringslice_strcmp(rs_res, ASC_CMD_ERROR) == 0) res = false;
           else res = true;
           if(res){
             if(prefix) res = asc_string_boolean_ops(rs_data, prefix);
             if(res && item->answ.format) res = asc_cmd_sscanf(rs_data, item);
           }
           if(ringslice_is_later_than(rs_res, rs_data)) proced_data = (uint16_t)rs_res->last;
           else                                         proced_data = (uint16_t)asc_simcom_line_end(me, rs_data);
           consume_data = true;
           break;
      default: 
           break;
  }
  if(consume_data) asc_rx_consume_to(ctx, me->buf, me->buf_size, (uint16_t)proced_data);
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
static bool asc_simcom_parcer(asc_context_t* const ctx, const ringslice_t rs_me, const asc_item_t* const item, const asc_entity_t* const entity)
{
  DBC_REQUIRE(120, ctx);
  DBC_REQUIRE(121, item);
  DBC_REQUIRE(122, entity);

  bool res = false;

  ringslice_t rs_req = {0};
  ringslice_t rs_res = {0}; 
  ringslice_t rs_data = {0};

  asc_simcom_parcer_find_rs_req(&rs_me, &rs_req, item->req); // Find request and response in buffer
  asc_simcom_parcer_find_rs_res(&rs_me, &rs_req, &rs_res);
  asc_simcom_parcer_find_rs_data(&rs_me, &rs_req, &rs_res, &rs_data); // Extract data section
  res = asc_simcom_parcer_post_proc(ctx, &rs_me, &rs_req, &rs_res, &rs_data, item, entity); // Proc data
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
static bool asc_cmd_ring_parcer(asc_context_t* const ctx, const asc_entity_t* const entity, const asc_item_t* const item, const ringslice_t rs_me)
{
  DBC_REQUIRE(100, ctx);
  DBC_REQUIRE(101, ctx->init_struct.init);
  DBC_REQUIRE(102, item);
  DBC_REQUIRE(103, entity);

  bool res = false;
  
  if(item->answ.prefix && strncmp(item->answ.prefix, ASC_CMD_FORCE, strlen(ASC_CMD_FORCE)) == 0) res = true;
  if(res) return true;
  if(ringslice_is_empty(&rs_me)) return 0; // no data

  if(!res)
  {
    switch(item->parce_type)
    {
      case ASC_PARCE_SIMCOM: res = asc_simcom_parcer(ctx, rs_me, item, entity); break;
      case ASC_PARCE_RAW:    res = asc_raw_parcer(ctx, rs_me, item, entity);    break;
      default: break;
    }
  }

  #ifndef ASC_TEST
  if(res > 0) asc_printf_from_ring(ctx, rs_me, "[RX]");
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
static bool asc_rx_remove_range(asc_context_t* const ctx, const uint8_t* const source_buffer, const uint16_t source_size, const uint16_t start, const uint16_t end)
{
  if(!ctx || !source_buffer || !source_size) return false;
  ASC_CRITICAL_ENTER
  asc_ring_buffer_t* rx = ctx->init_struct.rx_buff;
  if(!ctx->init_struct.init || !rx || rx->buffer != source_buffer || rx->size != source_size ||
     rx->head >= rx->size || rx->tail >= rx->size || start >= rx->size || end >= rx->size ||
     rx->count >= rx->size)
  {
    ASC_CRITICAL_EXIT
    return false;
  }
  uint16_t start_offset = start >= rx->tail ? (uint16_t)(start - rx->tail) : (uint16_t)(rx->size - rx->tail + start);
  uint16_t end_offset = end >= rx->tail ? (uint16_t)(end - rx->tail) : (uint16_t)(rx->size - rx->tail + end);
  uint16_t remove_len = end_offset > start_offset ? (uint16_t)(end_offset - start_offset) : (uint16_t)(rx->size - start_offset + end_offset);
  if(!remove_len || start_offset >= rx->count || remove_len > (uint16_t)(rx->count - start_offset))
  {
    ASC_CRITICAL_EXIT
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
  ASC_CRITICAL_EXIT
  return true;
}

/*******************************************************************************
 ** @brief  Find and proc standart URC 
 ** @param  ctx     core context
 ** @param  me  slice of origin buffer
 ** @return None
 ******************************************************************************/
static void asc_process_urcs(asc_context_t* const ctx, const ringslice_t* me)
{
  if(!ctx || !me || !me->buf || !me->buf_size || ringslice_is_empty(me)) return;
  asc_urc_queue_t registrations[ASC_URC_QUEUE_SIZE] = {0};
  char prefix_storage[ASC_URC_QUEUE_SIZE][ASC_URC_PREFIX_MAX_LEN + 1u] = {{0}};
  ASC_CRITICAL_ENTER
  if(!ctx->init_struct.init)
  {
    ASC_CRITICAL_EXIT
    return;
  }
  for(uint8_t i = 0; i < ASC_URC_QUEUE_SIZE; ++i)
  {
    const char* registered_prefix = ctx->urc_queue[i].prefix;
    if(!registered_prefix || !ctx->urc_queue[i].cb) continue;
    size_t len = 0;
    while(len <= ASC_URC_PREFIX_MAX_LEN && registered_prefix[len]) ++len;
    if(!len || len > ASC_URC_PREFIX_MAX_LEN) continue;
    memcpy(prefix_storage[i], registered_prefix, len + 1u);
    registrations[i].prefix = prefix_storage[i];
    registrations[i].cb = ctx->urc_queue[i].cb;
  }
  ASC_CRITICAL_EXIT
  bool owns_rx_slice = ctx->init_struct.rx_buff && me->buf == ctx->init_struct.rx_buff->buffer && me->buf_size == ctx->init_struct.rx_buff->size;
  ringslice_t current = *me;
  for(uint8_t dispatched = 0; dispatched < ASC_MAX_URCS_PER_PROC; )
  {
    bool found_urc = false;
    for(uint8_t i = 0; i < ASC_URC_QUEUE_SIZE && !found_urc; ++i)
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
        ringslice_t terminator = ringslice_strstr(&suffix, ASC_CMD_CRLF);
        if(ringslice_is_empty(&terminator)) break; // Keep fragmented URC until its CRLF arrives.
        ringslice_t line = ringslice_initializer(current.buf, current.buf_size, prefix.first, terminator.last);
        ASC_DEBUG(ctx, "[ASC][INFO] Found URC: %s", registrations[i].prefix);
        registrations[i].cb(line); // RX bytes remain owned until the callback returns.
        if(!owns_rx_slice) return; // External test slices cannot mutate the context RX ring.
        if(!asc_rx_remove_range(ctx, current.buf, current.buf_size, prefix.first, terminator.last)) return;
        ASC_CRITICAL_ENTER
        if(!ctx->init_struct.init || !ctx->init_struct.rx_buff)
        {
          ASC_CRITICAL_EXIT
          return;
        }
        asc_ring_buffer_t* rx = ctx->init_struct.rx_buff;
        current = ringslice_initializer(rx->buffer, rx->size, rx->tail, rx->head);
        ASC_CRITICAL_EXIT
        ++dispatched;
        found_urc = true;
        break;
      }
    }
    if(!found_urc) break;
  }
}

/*******************************************************************************
 ** @brief  Init atl lib  
 ** @param  ctx        core context
 ** @param  asc_printf pointer to user func of printf
 ** @param  asc_write  pointer to user func of write to uart
 ** @param  rx_buff    struct to ring buffer
 ** @return none
 ******************************************************************************/
bool asc_init_ex(asc_context_t* const ctx, const asc_printf_t asc_printf, const asc_write_t asc_write, asc_ring_buffer_t* rx_buff)
{
  if(!ctx || !asc_printf || !asc_write || !rx_buff || !rx_buff->buffer || rx_buff->size < 2u || rx_buff->head >= rx_buff->size || rx_buff->tail >= rx_buff->size || rx_buff->count >= rx_buff->size) return false;
  uint16_t queued = rx_buff->head >= rx_buff->tail ? (uint16_t)(rx_buff->head - rx_buff->tail) : (uint16_t)(rx_buff->size - rx_buff->tail + rx_buff->head);
  if(queued != rx_buff->count) return false;
  ASC_CRITICAL_ENTER
  if(ctx->init_struct.init) { ASC_CRITICAL_EXIT return false; }
  O1HeapInstance* heap = o1heapInit(ctx->mem_pool, sizeof(ctx->mem_pool));
  if(!heap) { ASC_CRITICAL_EXIT return false; }
  memset(&ctx->entity_queue, 0, sizeof(ctx->entity_queue));
  memset(ctx->urc_queue, 0, sizeof(ctx->urc_queue));
  memset(&ctx->init_struct, 0, sizeof(ctx->init_struct));
  ctx->time = 0;
  ctx->init_struct.heap = heap;
  ctx->init_struct.asc_write = asc_write;
  ctx->init_struct.asc_printf = asc_printf;
  ctx->init_struct.rx_buff = rx_buff;
  ctx->init_struct.init = true;
  ASC_DEBUG(ctx, "[ASC][INFO] ATL library initialized successfully", NULL);
  ASC_DEBUG(ctx, "[ASC][INFO] Memory pool size: %u bytes", (unsigned)sizeof(ctx->mem_pool));
  ASC_DEBUG(ctx, "[ASC][INFO] Memory overhead: %u/%u", (unsigned)o1heapGetDiagnostics(ctx->init_struct.heap).allocated, (unsigned)o1heapGetDiagnostics(ctx->init_struct.heap).capacity);
  ASC_DEBUG(ctx, "[ASC][INFO] Entity queue size: %u", (unsigned)ASC_ENTITY_QUEUE_SIZE);
  ASC_CRITICAL_EXIT
  return true;
}

void asc_init(asc_context_t* const ctx, const asc_printf_t asc_printf, const asc_write_t asc_write, asc_ring_buffer_t* rx_buff)
{
  (void)asc_init_ex(ctx, asc_printf, asc_write, rx_buff);
}

/*******************************************************************************
 ** @brief  DeInit atl lib  
 ** @param  ctx core context
 ** @return none
 ******************************************************************************/
/** 
* @brief Deinitializes the ATL library and releases all allocated resources
* @param ctx Pointer to the application context structure
* @return true - library deinitialized successfully, false - core validation failed
*         or active processing is running
*/
void asc_deinit(asc_context_t* const ctx)
{
  (void)asc_deinit_ex(ctx);
}

bool asc_deinit_ex(asc_context_t* const ctx)
{
  if(!ctx) return false;
  ASC_CRITICAL_ENTER
  if(!ctx->init_struct.init || !ctx->init_struct.heap || ctx->proc_active)
  {
    ASC_CRITICAL_EXIT
    return false;
  }
  ASC_DEBUG(ctx, "[ASC][INFO] Deinitializing ATL library", NULL);
  for(uint8_t i = 0; i < ASC_ENTITY_QUEUE_SIZE; ++i) asc_entity_release(ctx, &ctx->entity_queue.entity[i]);
  for(uint8_t i = 0; i < ASC_URC_QUEUE_SIZE; ++i) if(ctx->urc_queue[i].prefix) o1heapFree(ctx->init_struct.heap, ctx->urc_queue[i].prefix);
  ctx->init_struct.init = false;
  memset(&ctx->entity_queue, 0, sizeof(asc_entity_queue_t));
  memset(ctx->urc_queue, 0, sizeof(ctx->urc_queue));
  ctx->init_struct.asc_printf = NULL;
  ctx->init_struct.asc_write = NULL;
  ctx->init_struct.rx_buff = NULL;
  ctx->init_struct.heap = NULL;
  ctx->time = 0;
  ctx->proc_active = false;
  ASC_DEBUG(ctx, "[ASC][INFO] ATL library deinitialized", NULL);
  ASC_CRITICAL_EXIT
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
bool asc_entity_enqueue(asc_context_t* const ctx, const asc_item_t* const item, const uint8_t item_amount, const asc_entity_cb_t cb, uint16_t data_size, void* const meta)
{
  if(!ctx || !item || !item_amount || item_amount > ASC_MAX_ITEMS_PER_ENTITY) return false;
  ASC_CRITICAL_ENTER
  if(!ctx->init_struct.init || !ctx->init_struct.heap ||
     ctx->entity_queue.entity_cnt > ASC_ENTITY_QUEUE_SIZE ||
     ctx->entity_queue.entity_head >= ASC_ENTITY_QUEUE_SIZE ||
     ctx->entity_queue.entity_tail >= ASC_ENTITY_QUEUE_SIZE)
  {
    ASC_CRITICAL_EXIT
    return false;
  }
  ASC_DEBUG(ctx, "[ASC][INFO] Enqueueing entity with %d items", item_amount);
  if(ctx->entity_queue.entity_cnt >= ASC_ENTITY_QUEUE_SIZE)
  {
    ASC_DEBUG(ctx, "[ASC][ERROR] Queue is full", NULL);
    ASC_CRITICAL_EXIT
    return false;
  }
  asc_entity_t* cur_entity = &ctx->entity_queue.entity[ctx->entity_queue.entity_head];
  memset(cur_entity, 0, sizeof(*cur_entity));
  cur_entity->data = data_size ? asc_malloc(ctx, data_size) : NULL;
  if(data_size && !cur_entity->data) goto error_exit;
  cur_entity->data_size = data_size;
  if(cur_entity->data) memset(cur_entity->data, 0, data_size);
  cur_entity->item = asc_malloc(ctx, item_amount * sizeof(asc_item_t));
  if(!cur_entity->item) goto error_exit;
  memset(cur_entity->item, 0, item_amount * sizeof(asc_item_t));
  cur_entity->item_cnt = item_amount;
  for(int i = 0; i < item_amount; i++)
  {
    if(!item[i].req && !item[i].answ.prefix) goto error_exit; //unhandle comb
    if(item[i].parce_type != ASC_PARCE_SIMCOM && item[i].parce_type != ASC_PARCE_RAW) goto error_exit;
    if(item[i].req)
    {
      const char* command = item[i].req;
      size_t marker_len = strlen(ASC_CMD_SAVE);
      if(strncmp(command, ASC_CMD_SAVE, marker_len) == 0) command += marker_len;
      if(!command[0]) goto error_exit;
    }
    if(item[i].answ.prefix)
    {
      const char* prefix = item[i].answ.prefix;
      size_t marker_len = strlen(ASC_CMD_SAVE);
      if(strncmp(prefix, ASC_CMD_SAVE, marker_len) == 0) prefix += marker_len;
      if(!prefix[0]) goto error_exit;
    }
    memcpy(&cur_entity->item[i], &item[i], sizeof(asc_item_t));
    asc_item_t* cur_item = &cur_entity->item[i];
    cur_item->owned = 0;
    char* source_req = item[i].req;
    char* source_prefix = item[i].answ.prefix;
    void** source_ptrs = item[i].answ.ptrs;
    bool save_req = source_req && strncmp(source_req, ASC_CMD_SAVE, strlen(ASC_CMD_SAVE)) == 0;
    bool save_prefix = source_prefix && strncmp(source_prefix, ASC_CMD_SAVE, strlen(ASC_CMD_SAVE)) == 0;

    // Start with only borrowed pointers installed. Owned copies are attached
    // after allocation so the rollback path never frees caller-owned memory.
    cur_item->req = save_req ? NULL : source_req;
    cur_item->answ.prefix = save_prefix ? NULL : source_prefix;
    cur_item->answ.ptrs = NULL;

    if(source_ptrs && source_ptrs[0] != ASC_NO_ARG)
    {
      if(!item[i].answ.format || !data_size || !cur_entity->data) goto error_exit;
      size_t ptr_count = 0;
      while(ptr_count < 6 && source_ptrs[ptr_count] != ASC_NO_ARG) ++ptr_count;
      if(ptr_count == 0 || source_ptrs[ptr_count] != ASC_NO_ARG) goto error_exit;
      cur_item->answ.ptrs = asc_malloc(ctx, (ptr_count + 1u) * sizeof(void*));
      if(!cur_item->answ.ptrs) goto error_exit;
      for(size_t j = 0; j < ptr_count; ++j)
      {
        size_t offset = (size_t)source_ptrs[j];
        if(offset >= data_size) goto error_exit;
        cur_item->answ.ptrs[j] = (char*)cur_entity->data + offset;
        ASC_DEBUG(ctx, "[ASC][INFO] User data for item[%d] ARG[%u]: offset=%zu, ptr=%p", i, (unsigned)j,
                  offset, cur_item->answ.ptrs[j]);
      }
      cur_item->answ.ptrs[ptr_count] = ASC_NO_ARG;
    }
    else if(item[i].answ.format) goto error_exit;
    if(save_req)
    {
      size_t length = strlen(source_req) + 1;
      cur_item->req = asc_malloc(ctx, length);
      if(!cur_item->req) goto error_exit;
      cur_item->owned |= ASC_OWN_REQ;
      memcpy(cur_item->req, source_req, length);
    }
    if(save_prefix)
    {
      size_t length = strlen(source_prefix) + 1;
      cur_item->answ.prefix = asc_malloc(ctx, length);
      if(!cur_item->answ.prefix) goto error_exit;
      cur_item->owned |= ASC_OWN_PREFIX;
      memcpy(cur_item->answ.prefix, source_prefix, length);
    }
  }
  cur_entity->cb = cb;
  cur_entity->meta = meta;
  cur_entity->state = ASC_STATE_WRITE;
  ctx->entity_queue.entity_head = (ctx->entity_queue.entity_head + 1) % ASC_ENTITY_QUEUE_SIZE;
  ++ctx->entity_queue.entity_cnt;
  ASC_DEBUG(ctx, "[ASC][INFO] Entity enqueued. Queue count: %d. Memory used: %u/%u. User data at %p. ",
             ctx->entity_queue.entity_cnt, (unsigned)o1heapGetDiagnostics(ctx->init_struct.heap).allocated,
             (unsigned)o1heapGetDiagnostics(ctx->init_struct.heap).capacity, cur_entity->data);
  ASC_CRITICAL_EXIT
  return true;

  error_exit:
    ASC_DEBUG(ctx, "[ASC][ERROR] Queue failed", NULL); 
    asc_entity_release(ctx, cur_entity);
    ASC_CRITICAL_EXIT
    return false; 
}

/*******************************************************************************
 ** @brief  Clear first entity from the queue 
 ** @param  ctx core context
 ** @return false: some errors; true: ok
 ******************************************************************************/
bool asc_entity_dequeue(asc_context_t* const ctx)
{
  if(!ctx) return false;
  ASC_CRITICAL_ENTER
  if(!ctx->init_struct.init || !ctx->init_struct.heap || ctx->proc_active)
  {
    ASC_CRITICAL_EXIT
    return false;
  }
  bool result = asc_entity_dequeue_locked(ctx);
  ASC_CRITICAL_EXIT
  return result;
}

/*******************************************************************************
 ** @brief  Function to append URC queue
 ** @param  ctx  core context
 ** @param  urc  ptr to your URC.
 ** @return true/false
 ******************************************************************************/
bool asc_urc_enqueue(asc_context_t* const ctx, const asc_urc_queue_t* const urc)  
{
  if(!ctx || !urc || !urc->prefix || !urc->cb || !urc->prefix[0]) return false;
  size_t prefix_len = 0;
  while(prefix_len <= ASC_URC_PREFIX_MAX_LEN && urc->prefix[prefix_len]) ++prefix_len;
  if(!prefix_len || prefix_len > ASC_URC_PREFIX_MAX_LEN) return false;

  ASC_CRITICAL_ENTER
  if(!ctx->init_struct.init || !ctx->init_struct.heap)
  {
    ASC_CRITICAL_EXIT
    return false;
  }
  asc_urc_queue_t* empty_slot = NULL;
  for(uint8_t i = 0; i < ASC_URC_QUEUE_SIZE; ++i)
  {
    if(ctx->urc_queue[i].prefix && strcmp(ctx->urc_queue[i].prefix, urc->prefix) == 0)
    {
      ctx->urc_queue[i].cb = urc->cb;
      ASC_CRITICAL_EXIT
      return true;
    }
    if(!ctx->urc_queue[i].prefix && !empty_slot) empty_slot = &ctx->urc_queue[i];
  }
  if(!empty_slot)
  {
    ASC_DEBUG(ctx, "[ASC][ERROR] URC queue is full", NULL);
    ASC_CRITICAL_EXIT
    return false;
  }

  char* prefix_copy = asc_malloc(ctx, prefix_len + 1u);
  if(!prefix_copy)
  {
    ASC_CRITICAL_EXIT
    return false;
  }
  memcpy(prefix_copy, urc->prefix, prefix_len + 1u);
  empty_slot->prefix = prefix_copy;
  empty_slot->cb = urc->cb;
  ASC_DEBUG(ctx, "[ASC][INFO] URC enqueued successfully", NULL);
  ASC_CRITICAL_EXIT
  return true;
}

/*******************************************************************************
 ** @brief  Function to delete URC from queue
 ** @param  ctx  core context
 ** @param  prefix  prefix of your URC.
 ** @return true/false
 ******************************************************************************/
bool asc_urc_dequeue(asc_context_t* const ctx, const char* prefix)
{
  if(!ctx || !prefix) return false;
  ASC_CRITICAL_ENTER
  if(!ctx->init_struct.init || !ctx->init_struct.heap)
  {
    ASC_CRITICAL_EXIT
    return false;
  }
  for(uint8_t i = 0; i < ASC_URC_QUEUE_SIZE; ++i)
  {
    if(!ctx->urc_queue[i].prefix) continue;
    if(strcmp(ctx->urc_queue[i].prefix, prefix) == 0)
    {
      o1heapFree(ctx->init_struct.heap, ctx->urc_queue[i].prefix);
      memset(&ctx->urc_queue[i], 0, ASC_URC_SIZE);
      ASC_DEBUG(ctx,"[ASC][INFO] URC dequeued successfully", NULL);
      ASC_CRITICAL_EXIT
      return true;
    }
  }
  ASC_DEBUG(ctx, "[ASC][INFO] URC dequeued fail", NULL);
  ASC_CRITICAL_EXIT
  return false;
}

/*******************************************************************************
 ** @brief  Function get init. 
 ** @param  ctx  core context
 ** @return @asc_init_t
 ******************************************************************************/
asc_init_t asc_get_init(asc_context_t* const ctx)
{
  asc_init_t res = {0};
  if(!ctx) return res;
  ASC_CRITICAL_ENTER
  res = ctx->init_struct;
  ASC_CRITICAL_EXIT
  return res;
}

/*******************************************************************************
 ** @brief  Function get time in 10ms. 
 ** @param  ctx  core context
 ** @return time
 ******************************************************************************/
uint32_t asc_get_cur_time(asc_context_t* const ctx)
{
  if(!ctx) return 0;
  ASC_CRITICAL_ENTER
  uint32_t res = ctx->time;
  ASC_CRITICAL_EXIT
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
bool asc_rx_push(asc_context_t* const ctx, const uint8_t* const data, uint16_t len)
{
  if(!ctx || (len && !data)) return false;
  ASC_CRITICAL_ENTER
  asc_ring_buffer_t* rx = ctx->init_struct.rx_buff;
  if(!ctx->init_struct.init || !rx || !rx->buffer || !rx->size ||
     rx->head >= rx->size || rx->tail >= rx->size || rx->count >= rx->size)
  {
    ASC_CRITICAL_EXIT
    return false;
  }
  uint16_t free_space = (uint16_t)(rx->size - 1u - rx->count);
  if(len > free_space)
  {
    ASC_CRITICAL_EXIT
    return false;
  }
  uint16_t first_chunk = (uint16_t)(rx->size - rx->head);
  if(first_chunk > len) first_chunk = len;
  if(first_chunk) memcpy(rx->buffer + rx->head, data, first_chunk);
  uint16_t second_chunk = (uint16_t)(len - first_chunk);
  if(second_chunk) memcpy(rx->buffer, data + first_chunk, second_chunk);
  rx->head = (uint16_t)(((uint32_t)rx->head + len) % rx->size);
  rx->count = (uint16_t)(rx->count + len);
  ASC_CRITICAL_EXIT
  return true;
}

/*******************************************************************************
 ** @brief  Function to custom malloc. Handle the concurrent state
 ** @param  ctx  core context
 ** @param  size amount to alloc
 ** @return ptr to new memory
 ******************************************************************************/
void* asc_malloc(asc_context_t* const ctx, size_t size)
{
  if(!ctx || !size) return NULL;
  ASC_CRITICAL_ENTER
  if(!ctx->init_struct.init || !ctx->init_struct.heap) { ASC_CRITICAL_EXIT return NULL; }
  void* res = o1heapAllocate(ctx->init_struct.heap, size);
  ASC_CRITICAL_EXIT
  return res;
}

/*******************************************************************************
 ** @brief  Function to custom free. Handle the concurrent state
 ** @param  ctx  core context
 ** @param  ptr  ptr to delete
 ** @return none
 ******************************************************************************/
void asc_free(asc_context_t* ctx, void* ptr)
{
  if(!ctx || !ptr) return;
  ASC_CRITICAL_ENTER
  if(!ctx->init_struct.init || !ctx->init_struct.heap) { ASC_CRITICAL_EXIT return; }
  o1heapFree(ctx->init_struct.heap, ptr);
  ASC_CRITICAL_EXIT
}

/*******************************************************************************
** @brief Executes the main state machine loop for processing transmissions and responses
** @note This function should be called periodically (e.g., every 10ms)
** @param ctx Pointer to the application context structure
******************************************************************************/
void asc_core_proc(asc_context_t* const ctx)
{
  if(!ctx) return;
  ASC_CRITICAL_ENTER
  asc_ring_buffer_t* rx = ctx->init_struct.rx_buff;
  if(!ctx->init_struct.init || !rx || !rx->buffer || !rx->size || rx->head >= rx->size || rx->tail >= rx->size || ctx->proc_active)
  {
    ASC_CRITICAL_EXIT
    return;
  }
  ctx->proc_active = true;
  ++ctx->time; // unsigned wraparound is defined
  bool check_urcs = ctx->time % ASC_URC_FREQ_CHECK == 0;
  ringslice_t rs_me = ringslice_initializer(rx->buffer, rx->size, rx->tail, rx->head);
  ASC_CRITICAL_EXIT

  if(check_urcs) asc_process_urcs(ctx, &rs_me); // check URCs outside the critical section

  ASC_CRITICAL_ENTER
  if(!ctx->init_struct.init || !ctx->init_struct.rx_buff)
  {
    ASC_CRITICAL_EXIT
    asc_proc_finish(ctx);
    return;
  }
  rx = ctx->init_struct.rx_buff;
  rs_me = ringslice_initializer(rx->buffer, rx->size, rx->tail, rx->head);
  if(!ctx->entity_queue.entity_cnt)
  {
    ASC_CRITICAL_EXIT
    asc_proc_finish(ctx);
    return;
  }
  asc_entity_t* entity = &ctx->entity_queue.entity[ctx->entity_queue.entity_tail];
  if(!entity->item || entity->item_id >= entity->item_cnt)
  {
    ASC_CRITICAL_EXIT
    asc_proc_finish(ctx);
    return;
  }
  asc_item_t* item = &entity->item[entity->item_id];
  asc_write_t write_fn = ctx->init_struct.asc_write;
  ASC_CRITICAL_EXIT //we work with exclusive memory field for this entity bcs of ring buffer
  switch(entity->state)
  {
    case ASC_STATE_WRITE:
         if(!item->req)
         {
           entity->tx_offset = 0;
           entity->tx_timer = 0;
           entity->tx_started = false;
           entity->timer = item->meta.wait;
           entity->state = ASC_STATE_READ;
           break;
         }
         {
           size_t marker_len = strlen(ASC_CMD_SAVE);
           size_t request_offset = strncmp(item->req, ASC_CMD_SAVE, marker_len) == 0 ? marker_len : 0u;
           const char* request = item->req + request_offset;
           size_t request_len = strlen(request);
           if(request_len > UINT16_MAX || entity->tx_offset > request_len || !write_fn)
           {
             entity->state = ASC_STATE_WRITE;
             asc_proc_handle_cmd_result(ctx, entity, item, false);
             break;
           }
           if(!entity->tx_started)
           {
             entity->tx_offset = 0;
             entity->tx_timer = item->meta.wait;
             entity->tx_started = true;
             ASC_DEBUG(ctx, "[ASC][INFO] [TX] request length=%u", (unsigned)request_len);
           }
           size_t remaining = request_len - entity->tx_offset;
           if(!remaining)
           {
             entity->tx_offset = 0;
             entity->tx_timer = 0;
             entity->tx_started = false;
             entity->timer = item->meta.wait;
             entity->state = ASC_STATE_READ;
             break;
           }
           uint16_t accepted = write_fn((uint8_t*)request + entity->tx_offset, (uint16_t)remaining);
           if(accepted > remaining)
           {
             ASC_DEBUG(ctx, "[ASC][ERROR] asc_write returned more bytes than requested", NULL);
             entity->tx_offset = 0;
             entity->tx_timer = 0;
             entity->tx_started = false;
             asc_proc_handle_cmd_result(ctx, entity, item, false);
             break;
           }
           entity->tx_offset = (uint16_t)(entity->tx_offset + accepted);
           if(entity->tx_offset == request_len)
           {
             entity->tx_offset = 0;
             entity->tx_timer = 0;
             entity->tx_started = false;
             entity->timer = item->meta.wait;
             entity->state = ASC_STATE_READ;
           }
           else
           {
             if(entity->tx_timer) --entity->tx_timer;
             if(!entity->tx_timer) asc_proc_handle_tx_timeout(ctx, entity, item);
           }
         }
         break;
    case ASC_STATE_READ:
         if(entity->timer) --entity->timer;
         if(asc_cmd_ring_parcer(ctx, entity, item, rs_me) == 1) 
         {
           entity->state = ASC_STATE_WRITE;
           entity->timer = 0;
           ASC_DEBUG(ctx, "[ASC][INFO] Successful entity cmd %d/%d", entity->item_id+1, entity->item_cnt);
           asc_proc_handle_cmd_result(ctx, entity, item, true);  
         } 
         else if(!entity->timer)
         {
           asc_proc_handle_timeout(ctx, entity, item);
         }
         else
         {
           if(entity->timer == item->meta.wait/2) ASC_DEBUG(ctx, "[ASC][INFO] Waiting......", NULL);
         }
         break;
    default: 
         ASC_DEBUG(ctx, "[ASC][INFO] Unknown state: %d", entity->state);
         break;
  }
  asc_proc_finish(ctx);
}

/**
** @brief Handles the execution outcome of an individual item command within an entity
** @param ctx Pointer to the application context structure
** @param entity Pointer to the active entity containing the command list
** @param item Pointer to the executed command item structure
** @param success True if the command succeeded, false if it failed
**/
static void asc_proc_handle_cmd_result(asc_context_t* const ctx, asc_entity_t* const entity, asc_item_t* const item, const bool success) 
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
      ASC_DEBUG(ctx, "[ASC][INFO] Next cmd of entity", NULL);
      entity->item_id = (uint8_t)next_item_id;
      return;
    }
  }  
  //Step outside of cmd range or last cmd or step == 0
  ASC_DEBUG(ctx, "[ASC][INFO] End of entity", NULL);
  #ifndef ASC_TEST
  if(!success) //debug
  {
    asc_ring_buffer_t* rx = ctx->init_struct.rx_buff;
    if(rx && rx->buffer && rx->size && rx->head < rx->size && rx->count < rx->size)
    {
      uint16_t dump_len = rx->count < 250u ? rx->count : 250u;
      uint16_t data_start = rx->head >= dump_len ? (uint16_t)(rx->head - dump_len) : (uint16_t)(rx->size - (dump_len - rx->head));
      ringslice_t rs_me = ringslice_initializer(rx->buffer, rx->size, data_start, rx->head);
      asc_printf_from_ring(ctx, rs_me, "Failed last RX bytes: ");
    }
  }
  #endif
  if(entity->cb) entity->cb(success, entity->meta, entity->data);
  ASC_CRITICAL_ENTER
  /* Public dequeue is blocked while proc_active; this is the sole internal
   * release path for the entity currently owned by this invocation. */
  if(ctx->init_struct.init && ctx->entity_queue.entity_cnt &&
     &ctx->entity_queue.entity[ctx->entity_queue.entity_tail] == entity
  ){
    (void)asc_entity_dequeue_locked(ctx);
  }
  ASC_CRITICAL_EXIT
}

/**
** @brief Processes a response timeout event for an item, managing retry counts
** @param ctx Pointer to the application context structure
** @param entity Pointer to the active entity containing the command list
** @param item Pointer to the timed-out command item structure
**/
static void asc_proc_handle_timeout(asc_context_t* const ctx, asc_entity_t* const entity, asc_item_t* const item)
{
  entity->tx_offset = 0;
  entity->tx_timer = 0;
  entity->tx_started = false;
  entity->timer = 0;
  entity->state = ASC_STATE_WRITE;
  if(item->meta.rpt_cnt) --item->meta.rpt_cnt;
  if(item->meta.rpt_cnt)
  {
    ASC_DEBUG(ctx, "[ASC][INFO] Timeout, retries left: %d", item->meta.rpt_cnt);
    return;
  }
  ASC_DEBUG(ctx, "[ASC][INFO] Failure entity cmd %d/%d", entity->item_id+1, entity->item_cnt);
  asc_proc_handle_cmd_result(ctx, entity, item, false);
}

/**
** @brief Processes a transmission timeout event, preventing retries if a partial request was sent
** @param ctx Pointer to the application context structure
** @param entity Pointer to the active entity containing the command list
** @param item Pointer to the timed-out transmission command item structure
**/
static void asc_proc_handle_tx_timeout(asc_context_t* const ctx, asc_entity_t* const entity, asc_item_t* const item)
{
  bool partial_request_sent = entity->tx_offset != 0u;
  entity->tx_offset = 0;
  entity->tx_timer = 0;
  entity->tx_started = false;
  entity->timer = 0;
  entity->state = ASC_STATE_WRITE;
  if(partial_request_sent)
  {
    ASC_DEBUG(ctx, "[ASC][ERROR] UART write timed out after a partial request; automatic retry suppressed", NULL);
    asc_proc_handle_cmd_result(ctx, entity, item, false);
    return;
  }
  asc_proc_handle_timeout(ctx, entity, item);
}

/**
** @brief Finalizes the core processing cycle by resetting the active flag
** @param ctx Pointer to the application context structure
**/
static void asc_proc_finish(asc_context_t* const ctx)
{
  ASC_CRITICAL_ENTER
  if(ctx) ctx->proc_active = false;
  ASC_CRITICAL_EXIT
}


#ifdef ASC_TEST
/*******************************************************************************
 ** @brief  TEST implementations
 ** @param  none
 ** @return none
 ******************************************************************************/
static bool asc_test_rx_snapshot(asc_context_t* const ctx, uint16_t* const tail, uint16_t* const count)
{
  if(!ctx || !tail || !count || !ctx->init_struct.rx_buff) return false;
  *tail = ctx->init_struct.rx_buff->tail;
  *count = ctx->init_struct.rx_buff->count;
  return true;
}

static void asc_test_rx_restore(asc_context_t* const ctx, const bool saved, const uint16_t tail, const uint16_t count)
{
  if(!saved || !ctx || !ctx->init_struct.rx_buff) return;
  ctx->init_struct.rx_buff->tail = tail;
  ctx->init_struct.rx_buff->count = count;
}

void _asc_core_proc(asc_context_t* const ctx) { 
  asc_core_proc(ctx);
}

int _asc_cmd_ring_parcer(asc_context_t* const ctx, const asc_entity_t* const entity, const asc_item_t* const item, const ringslice_t rs_me) { 
  uint16_t tail = 0;
  uint16_t count = 0;
  bool saved = asc_test_rx_snapshot(ctx, &tail, &count);
  int result = asc_cmd_ring_parcer(ctx, entity,item, rs_me);
  asc_test_rx_restore(ctx, saved, tail, count);
  return result;
}

void _asc_simcom_parcer_find_rs_req(const ringslice_t* const me, ringslice_t* const rs_req, const char* const req) { 
  asc_simcom_parcer_find_rs_req(me, rs_req, req); 
}

void _asc_simcom_parcer_find_rs_res(const ringslice_t* const me, const ringslice_t* const rs_req, ringslice_t* const rs_res) { 
  asc_simcom_parcer_find_rs_res(me, rs_req, rs_res); 
}

void _asc_simcom_parcer_find_rs_data(const ringslice_t* const me, const ringslice_t* const rs_req, const ringslice_t* const rs_res, ringslice_t* const rs_data) { 
  asc_simcom_parcer_find_rs_data(me, rs_req, rs_res, rs_data); 
}

int _asc_simcom_parcer_post_proc(asc_context_t* const ctx, const ringslice_t* const me, const ringslice_t* const rs_req, const ringslice_t* const rs_res, 
                                 const ringslice_t* const rs_data, const asc_item_t* const item, const asc_entity_t* const entity) { 
  uint16_t tail = 0;
  uint16_t count = 0;
  bool saved = asc_test_rx_snapshot(ctx, &tail, &count);
  int result = asc_simcom_parcer_post_proc(ctx, me, rs_req, rs_res, rs_data, item, entity);
  asc_test_rx_restore(ctx, saved, tail, count);
  return result;
}

int _asc_string_boolean_ops(const ringslice_t* const rs_data, const char* const pattern) { 
  return asc_string_boolean_ops(rs_data, pattern); 
}

int _asc_cmd_sscanf(const ringslice_t* const rs_data, const asc_item_t* const item) {
  return asc_cmd_sscanf(rs_data, item);
}

void _asc_process_urcs(asc_context_t* const ctx, const ringslice_t* me)
{
  asc_process_urcs(ctx, me);
}


asc_entity_queue_t* _asc_get_entity_queue(asc_context_t* const ctx) {
  return &ctx->entity_queue;
}

asc_urc_queue_t* _asc_get_urc_queue(asc_context_t* const ctx) {
  return ctx->urc_queue;
}

asc_init_t _asc_get_init(asc_context_t* const ctx) {
  return asc_get_init(ctx);
}

#endif
