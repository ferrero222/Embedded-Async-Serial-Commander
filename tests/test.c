//============================================================================
// ET: embedded test; very simple test example
//============================================================================
#include "et.h"  // ET: embedded test

#include "tact_core.h" // 
#include "o1heap.h" // 
#include "tact_port.h" // 
#include "tact_chain.h"  // ET: embedded test
#include "tact_mdl_general.h"
#include "tact_mdl_sms.h"
#include "tact_mdl_gprs.h"
#include "tact_mdl_gprs_server.h"
#include "tact_mdl_mqtt.h"
#include <stdio.h>

static tact_context_t test_ctx = {0};

_Alignas(O1HEAP_ALIGNMENT) uint8_t test_buffer[2048] = {0};
const uint16_t test_buffer_tail = 0;

tact_ring_buffer_t tact_ring_buffer = {
  .buffer = test_buffer,
  .count = 0,
  .head = test_buffer_tail,
  .tail = test_buffer_tail,
  .size = 2048,
};

static uint8_t captured_tcp_packet[32];
static uint16_t captured_tcp_packet_len;
static uint8_t captured_uart_tx[512];
static uint16_t captured_uart_tx_len;
static uint8_t urc_callback_count;
static uint8_t partial_tx_capture[64];
static uint16_t partial_tx_capture_len;
static uint8_t entity_callback_count;
static bool entity_callback_result;
static bool callback_dequeue_result;
static bool callback_deinit_result;
static tact_entity_cb_t deferred_chain_cb;
static void* deferred_chain_meta;
static uint8_t deferred_function_calls;
static tact_chain_t* chain_to_destroy_from_callback;
static bool chain_user_callback_called;
static bool chain_destroy_from_callback_result;

static void capture_tcp_packet(uint8_t* data, uint16_t len)
{
  VERIFY(len <= sizeof(captured_tcp_packet));
  if(len) memcpy(captured_tcp_packet, data, len);
  captured_tcp_packet_len = len;
}

uint16_t test_write(uint8_t* buff, uint16_t len) {
  (void)buff;
  return len;
}

static uint16_t capture_uart_write(uint8_t* buff, uint16_t len)
{
  if(!buff || (uint32_t)captured_uart_tx_len + len > sizeof(captured_uart_tx)) return 0;
  memcpy(captured_uart_tx + captured_uart_tx_len, buff, len);
  captured_uart_tx_len = (uint16_t)(captured_uart_tx_len + len);
  return len;
}

static uint16_t partial_test_write(uint8_t* buff, uint16_t len)
{
  uint16_t accepted = len > 2u ? 2u : len;
  if((uint32_t)partial_tx_capture_len + accepted > sizeof(partial_tx_capture)) return 0;
  memcpy(partial_tx_capture + partial_tx_capture_len, buff, accepted);
  partial_tx_capture_len = (uint16_t)(partial_tx_capture_len + accepted);
  return accepted;
}

static uint16_t stall_after_two_test_write(uint8_t* buff, uint16_t len)
{
  if(partial_tx_capture_len >= 2u) return 0;
  return partial_test_write(buff, len);
}

void test_printf(const char* string) {
  (void)string;
  return;
}

void setup(void) {
    // executed before *every* non-skipped test
}

void teardown(void) {
    // executed after *every* non-skipped and non-failing test
}

void testItemCB(ringslice_t data_slice, bool result, void* const data)
{
  (void)data_slice;
  VERIFY(result);
  tact_mdl_rtd_t* real_data = (tact_mdl_rtd_t*)data;
  VERIFY(strcmp(real_data->modem_imei, "5235") == 0);
}

void testEntityCB(const bool result, void* const meta, const void* const data)
{
  VERIFY(result);
  VERIFY(meta == test_buffer);
  tact_mdl_rtd_t* real_data = (tact_mdl_rtd_t*)data;
  VERIFY(strcmp(real_data->modem_imei, "5235") == 0);
}

void testUrcCB(ringslice_t urc_slice)
{
  VERIFY(ringslice_strncmp(&urc_slice, "+TEST", strlen("+TEST")) == 0);
  ++urc_callback_count;
}

static void testLifecycleEntityCB(const bool result, void* const meta, const void* const data)
{
  (void)result;
  (void)meta;
  (void)data;
  callback_dequeue_result = tact_entity_dequeue(&test_ctx);
  callback_deinit_result = tact_deinit_ex(&test_ctx);
}

static void testTxEntityCB(const bool result, void* const meta, const void* const data)
{
  (void)meta;
  (void)data;
  ++entity_callback_count;
  entity_callback_result = result;
}

bool testChainFunc(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  VERIFY(param == test_buffer);

  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    TACT_ITEM(NULL, "+TEST", TACT_PARCE_SIMCOM, 2, 150, 0, 1, testItemCB,"+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
  };
  bool res = tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, sizeof(tact_mdl_rtd_t), meta);
  VERIFY(res);
  tact_ring_buffer_t* rx = tact_get_init(ctx).rx_buff;
  static const uint8_t response[] = "\r\n+TEST: 523566, text\r\n";
  if(rx && rx->count == 0)
    VERIFY(tact_rx_push(ctx, response, sizeof(response) - 1u));
  tact_entity_queue_t* queue =_tact_get_entity_queue(ctx);
  while(queue->entity_cnt)
  {
    _tact_core_proc(ctx);
  }
  return res;
}

bool testChainCond(void)
{
  return true;
}

static bool testDeferredChainFunc(tact_context_t* const ctx, const tact_entity_cb_t cb,
                                 const void* const param, void* const meta)
{
  (void)ctx;
  (void)param;
  ++deferred_function_calls;
  deferred_chain_cb = cb;
  deferred_chain_meta = meta;
  return true;
}

static void testChainUserCallback(const bool result, void* const meta, const void* const data)
{
  (void)result;
  (void)meta;
  (void)data;
  chain_user_callback_called = true;
}

static void testChainDestroyFromCallback(const bool result, void* const meta, const void* const data)
{
  (void)result;
  (void)meta;
  (void)data;
  chain_destroy_from_callback_result = tact_chain_destroy_ex(chain_to_destroy_from_callback);
  chain_to_destroy_from_callback = NULL;
}

// test group ----------------------------------------------------------------
TEST_GROUP("TACT") {

  { //TACT_CORE=====================================================================
    TEST("o1heap lib") {
      O1HeapInstance* heap = o1heapInit(test_buffer, TACT_MEMORY_POOL_SIZE); 
      bool res = o1heapDoInvariantsHold(heap);
      VERIFY(res);
      void* ptr1 = o1heapAllocate(heap, 111);
      VERIFY(ptr1);
      void* ptr2 = o1heapAllocate(heap, 345);
      VERIFY(ptr2);
      o1heapFree(heap, ptr1);
      o1heapFree(heap, ptr2);
      VERIFY(o1heapGetDiagnostics(heap).allocated == 0);
    }

    TEST("tact_init()/tact_deinit() testing") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      VERIFY(_tact_get_init(&test_ctx).init);
      VERIFY(tact_deinit_ex(&test_ctx));
      VERIFY(!tact_deinit_ex(&test_ctx));
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("core callback cannot externally dequeue or deinitialize its active context") {
      char rx_data[] = "ACK";
      tact_ring_buffer_t ring = {
        .buffer = (uint8_t*)rx_data,
        .count = sizeof(rx_data) - 1u,
        .head = sizeof(rx_data) - 1u,
        .tail = 0,
        .size = sizeof(rx_data),
      };
      VERIFY(tact_init_ex(&test_ctx, test_printf, test_write, &ring));
      tact_item_t item = TACT_ITEM(NULL, "ACK", TACT_PARCE_RAW, 0, 10, 0, 0,
                                 NULL, NULL, TACT_NO_ARG);
      callback_dequeue_result = true;
      callback_deinit_result = true;
      VERIFY(tact_entity_enqueue(&test_ctx, &item, 1, testLifecycleEntityCB, 0, NULL));
      _tact_core_proc(&test_ctx); // move from WRITE to READ
      _tact_core_proc(&test_ctx); // callback attempts forbidden external mutation
      VERIFY(!callback_dequeue_result);
      VERIFY(!callback_deinit_result);
      VERIFY(_tact_get_entity_queue(&test_ctx)->entity_cnt == 0);
      VERIFY(_tact_get_init(&test_ctx).init);
      VERIFY(tact_deinit_ex(&test_ctx));
    }

    TEST("tact_entity_enqueue()/tact_entity_dequeue() simple AT`s") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT"TACT_CMD_CRLF,   NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL, NULL, TACT_NO_ARG),
        TACT_ITEM("AT"TACT_CMD_CRLF,   NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL, NULL, TACT_NO_ARG),  
        TACT_ITEM("ATE1"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 2, 600, 0, 0, NULL, NULL, TACT_NO_ARG),  
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, 0, NULL);
      VERIFY(res);
      res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, 0, NULL);
      VERIFY(res);
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      VERIFY(queue->entity_cnt == 2);
      VERIFY(queue->entity[0].item_cnt == sizeof(items)/sizeof(items[0]));
      VERIFY(queue->entity[1].item_cnt == sizeof(items)/sizeof(items[0]));
      tact_entity_dequeue(&test_ctx);
      VERIFY(queue->entity_cnt == 1);
      tact_entity_dequeue(&test_ctx);
      VERIFY(queue->entity_cnt == 0);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_entity_enqueue() full queue leaves context active") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      tact_item_t item = {
        .req = "AT"TACT_CMD_CRLF,
        .parce_type = TACT_PARCE_SIMCOM,
        .meta = {.wait = 1, .rpt_cnt = 1},
      };
      for(uint8_t i = 0; i < TACT_ENTITY_QUEUE_SIZE; ++i)
        VERIFY(tact_entity_enqueue(&test_ctx, &item, 1, NULL, 0, NULL));

      VERIFY(!tact_entity_enqueue(&test_ctx, &item, 1, NULL, 0, NULL));
      VERIFY(_tact_get_init(&test_ctx).init);
      VERIFY(_tact_get_entity_queue(&test_ctx)->entity_cnt == TACT_ENTITY_QUEUE_SIZE);

      O1HeapInstance* heap = _tact_get_init(&test_ctx).heap;
      tact_deinit(&test_ctx);
      VERIFY(o1heapGetDiagnostics(heap).allocated == 0);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_entity_enqueue() allocation failure rolls back partial entity") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      char saved_req_a[2400];
      char saved_req_b[2400];
      memset(saved_req_a, 'A', sizeof(saved_req_a));
      memset(saved_req_b, 'B', sizeof(saved_req_b));
      memcpy(saved_req_a, TACT_CMD_SAVE, strlen(TACT_CMD_SAVE));
      memcpy(saved_req_b, TACT_CMD_SAVE, strlen(TACT_CMD_SAVE));
      saved_req_a[sizeof(saved_req_a) - 1] = '\0';
      saved_req_b[sizeof(saved_req_b) - 1] = '\0';
      tact_item_t items[2] = {
        {.req = saved_req_a, .parce_type = TACT_PARCE_SIMCOM},
        {.req = saved_req_b, .parce_type = TACT_PARCE_SIMCOM},
      };

      VERIFY(!tact_entity_enqueue(&test_ctx, items, 2, NULL, 0, NULL));
      VERIFY(_tact_get_init(&test_ctx).init);
      VERIFY(_tact_get_entity_queue(&test_ctx)->entity_cnt == 0);
      VERIFY(o1heapGetDiagnostics(_tact_get_init(&test_ctx).heap).allocated == 0);

      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("entity release uses captured string ownership") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      char borrowed_req[] = "AT+X\r\n";
      char borrowed_prefix[] = "READY";
      tact_item_t borrowed = TACT_ITEM(borrowed_req, borrowed_prefix, TACT_PARCE_SIMCOM,
                                     1, 10, 0, 0, NULL, NULL, TACT_NO_ARG);
      VERIFY(tact_entity_enqueue(&test_ctx, &borrowed, 1, NULL, 0, NULL));
      memcpy(borrowed_req, TACT_CMD_SAVE, strlen(TACT_CMD_SAVE));
      memcpy(borrowed_prefix, TACT_CMD_SAVE, strlen(TACT_CMD_SAVE));
      tact_entity_dequeue(&test_ctx);
      VERIFY(o1heapGetDiagnostics(_tact_get_init(&test_ctx).heap).allocated == 0);

      tact_item_t saved = TACT_ITEM(TACT_CMD_SAVE"AT+X\r\n", TACT_CMD_SAVE"READY",
                                  TACT_PARCE_SIMCOM, 1, 10, 0, 0, NULL, NULL, TACT_NO_ARG);
      VERIFY(tact_entity_enqueue(&test_ctx, &saved, 1, NULL, 0, NULL));
      tact_entity_queue_t* queue = _tact_get_entity_queue(&test_ctx);
      tact_entity_t* queued = &queue->entity[queue->entity_tail];
      queued->item[0].req[0] = 'A';
      queued->item[0].answ.prefix[0] = 'R';
      tact_entity_dequeue(&test_ctx);
      VERIFY(o1heapGetDiagnostics(_tact_get_init(&test_ctx).heap).allocated == 0);
      tact_deinit(&test_ctx);
    }

    TEST("tact_entity_enqueue() rejects offsets outside entity data") {
      typedef struct { char short_text[4]; int number; } small_output_t;
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      tact_item_t item[] = {
        TACT_ITEM("AT+X"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 0, 10, 0, 0, NULL,
                 "%d", TACT_ARG(small_output_t, number)),
      };
      VERIFY(!tact_entity_enqueue(&test_ctx, item, 1, NULL, offsetof(small_output_t, number), NULL));
      VERIFY(_tact_get_entity_queue(&test_ctx)->entity_cnt == 0);
      VERIFY(tact_entity_enqueue(&test_ctx, item, 1, NULL, sizeof(small_output_t), NULL));
      tact_entity_dequeue(&test_ctx);
      VERIFY(_tact_get_init(&test_ctx).init);
      tact_deinit(&test_ctx);
    }

    TEST("tact_entity_enqueue() rejects empty saved commands and prefixes") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      tact_item_t empty_command = TACT_ITEM("", NULL, TACT_PARCE_SIMCOM, 1, 10, 0, 0, NULL, NULL, TACT_NO_ARG);
      tact_item_t marker_only_command = TACT_ITEM(TACT_CMD_SAVE, NULL, TACT_PARCE_SIMCOM, 1, 10, 0, 0, NULL, NULL, TACT_NO_ARG);
      tact_item_t marker_only_prefix = TACT_ITEM(NULL, TACT_CMD_SAVE, TACT_PARCE_RAW, 1, 10, 0, 0, NULL, NULL, TACT_NO_ARG);
      VERIFY(!tact_entity_enqueue(&test_ctx, &empty_command, 1, NULL, 0, NULL));
      VERIFY(!tact_entity_enqueue(&test_ctx, &marker_only_command, 1, NULL, 0, NULL));
      VERIFY(!tact_entity_enqueue(&test_ctx, &marker_only_prefix, 1, NULL, 0, NULL));
      VERIFY(_tact_get_entity_queue(&test_ctx)->entity_cnt == 0);
      VERIFY(o1heapGetDiagnostics(_tact_get_init(&test_ctx).heap).allocated == 0);
      tact_deinit(&test_ctx);
    }

    TEST("tact_entity_enqueue()/tact_entity_dequeue() composite AT`s") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM(TACT_CMD_SAVE"AT+GSN"TACT_CMD_CRLF,       NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL,               "%15[^\x0d]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
        TACT_ITEM(TACT_CMD_SAVE"AT+GMM"TACT_CMD_CRLF,       NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL,               "%15[^\x0d]", TACT_ARG(tact_mdl_rtd_t, modem_id)),  
        TACT_ITEM("AT+GMR"TACT_CMD_CRLF,                   NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL,      "Revision:%29[^\x0d]", TACT_ARG(tact_mdl_rtd_t, modem_rev)),                
        TACT_ITEM("AT+CCLK?"TACT_CMD_CRLF,                 NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL,      "+CCLK: \"%21[^\"]\"", TACT_ARG(tact_mdl_rtd_t, modem_clock)),          
        TACT_ITEM("AT+CCID"TACT_CMD_CRLF,   TACT_CMD_SAVE"+CCLK", TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL,               "%21[^\x0d]", TACT_ARG(tact_mdl_rtd_t, sim_iccid)),             
        TACT_ITEM("AT+COPS?"TACT_CMD_CRLF,  TACT_CMD_SAVE"+COPS", TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL, "+COPS: 0, 0,\"%49[^\"]\"", TACT_ARG(tact_mdl_rtd_t, sim_operator)),            
        TACT_ITEM("AT+CSQ"TACT_CMD_CRLF,     TACT_CMD_SAVE"+CSQ", TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL,                 "+CSQ: %d", TACT_ARG(tact_mdl_rtd_t, sim_rssi)),             
        TACT_ITEM("AT+CENG=3"TACT_CMD_CRLF,                NULL, TACT_PARCE_SIMCOM, 2, 600, 0, 1, NULL,                       NULL, TACT_NO_ARG),                
        TACT_ITEM("AT+CENG?"TACT_CMD_CRLF,                 NULL, TACT_PARCE_SIMCOM, 2, 600, 0, 0, NULL,                       NULL, TACT_NO_ARG),  
      };         
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), NULL);
      VERIFY(res);
      res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), NULL);
      VERIFY(res);
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      VERIFY(queue->entity_cnt == 2);
      VERIFY(queue->entity[0].item_cnt == sizeof(items)/sizeof(items[0]));
      VERIFY(queue->entity[1].item_cnt == sizeof(items)/sizeof(items[0]));
      VERIFY(queue->entity[0].data);
      VERIFY(queue->entity[1].data);
      VERIFY((tact_mdl_rtd_t*){queue->entity[0].data}->modem_clock == queue->entity[0].item[3].answ.ptrs[0]);
      VERIFY(queue->entity[0].item[0].answ.ptrs[1] == TACT_NO_ARG);
      VERIFY((tact_mdl_rtd_t*){queue->entity[1].data}->sim_iccid == queue->entity[1].item[4].answ.ptrs[0]);
      VERIFY(queue->entity[1].item[0].answ.ptrs[1] == TACT_NO_ARG);
      tact_entity_dequeue(&test_ctx);
      VERIFY(queue->entity_cnt == 1);
      tact_entity_dequeue(&test_ctx);
      VERIFY(queue->entity_cnt == 0);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_urc_enqueue()/tact_urc_dequeue()") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      tact_urc_queue_t urc = {"+SMS", testUrcCB};
      VERIFY(tact_urc_enqueue(&test_ctx, &urc));
      tact_urc_queue_t* urc_queue = _tact_get_urc_queue(&test_ctx);  
      VERIFY(strcmp(urc_queue->prefix, urc.prefix) == 0);
      urc = (tact_urc_queue_t){"+CMD", testUrcCB};
      VERIFY(tact_urc_enqueue(&test_ctx, &urc));
      VERIFY(strcmp(urc_queue[1].prefix, urc.prefix) == 0);
      VERIFY(tact_urc_dequeue(&test_ctx, "+SMS"));
      VERIFY(urc_queue[0].prefix == NULL);
      VERIFY(tact_urc_dequeue(&test_ctx, "+CMD"));
      VERIFY(urc_queue[1].prefix == NULL);

      static const char* prefixes[TACT_URC_QUEUE_SIZE] = {
        "+U0", "+U1", "+U2", "+U3", "+U4",
        "+U5", "+U6", "+U7", "+U8", "+U9",
      };
      for(uint8_t i = 0; i < TACT_URC_QUEUE_SIZE; ++i) {
        urc.prefix = (char*)prefixes[i];
        VERIFY(tact_urc_enqueue(&test_ctx, &urc));
      }
      urc.prefix = "+OVERFLOW";
      VERIFY(!tact_urc_enqueue(&test_ctx, &urc));
      VERIFY(_tact_get_init(&test_ctx).init);
      for(uint8_t i = 0; i < TACT_URC_QUEUE_SIZE; ++i)
        VERIFY(strcmp(urc_queue[i].prefix, prefixes[i]) == 0);

      tact_deinit(&test_ctx);
      for(uint8_t i = 0; i < TACT_URC_QUEUE_SIZE; ++i)
        VERIFY(urc_queue[i].prefix == NULL);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_cmd_sscanf()") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      char test[128] = "123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT+GSN"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL,"%15[^\x0d]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), NULL);
      VERIFY(res);
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      ringslice_t data = ringslice_initializer((uint8_t*)test, 128, 0, sizeof(test)-1);
      int res_d = _tact_cmd_sscanf(&data, queue->entity[0].item);
      VERIFY(res_d);
      VERIFY(strncmp((tact_mdl_rtd_t*){queue->entity[0].data}->modem_imei, test, 15) == 0);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_string_boolean_ops()") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);      char test[] = "+GSN1234+TRASH+END";
      ringslice_t data = ringslice_initializer((uint8_t*)test, sizeof(test), 0, strlen(test));
      int res_d = _tact_string_boolean_ops(&data, "+GSN1234");
      VERIFY(res_d);
      res_d = _tact_string_boolean_ops(&data, "+GSN1234");
      VERIFY(res_d);
      res_d = _tact_string_boolean_ops(&data, "EMPTY|+GSN1234");
      VERIFY(res_d);
      res_d = _tact_string_boolean_ops(&data, "+GSN1234&+END");
      VERIFY(res_d);
      res_d = _tact_string_boolean_ops(&data, "+GSN1234&EMPTY");
      VERIFY(res_d <= 0);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_simcom_parcer_post_proc(),  NULL NULL NULL  - unhandle state") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);      
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT+TEST?"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL,"+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), NULL);
      VERIFY(res);
      ringslice_t rs_data = {0};
      ringslice_t rs_req  = {0};
      ringslice_t rs_res  = {0};
      ringslice_t rs_me   = {0};
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      int res_d = _tact_simcom_parcer_post_proc(&test_ctx, &rs_me, &rs_req, &rs_res, &rs_data, &queue->entity[0].item[0], &queue->entity[0]);
      VERIFY(res_d <= 0);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_simcom_parcer_post_proc(), REQ NULL NULL  - unhandle state") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);      
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT+TEST?"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL,"+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), NULL);
      VERIFY(res);
      char rs_req_impl[128]  = "AT+TEST?";
      ringslice_t rs_data = {0};
      ringslice_t rs_req  = ringslice_initializer((uint8_t*)rs_req_impl, 128, 0, strlen(rs_req_impl));
      ringslice_t rs_res  = {0};
      ringslice_t rs_me   = {0};
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      int res_d = _tact_simcom_parcer_post_proc(&test_ctx, &rs_me, &rs_req, &rs_res, &rs_data, &queue->entity[0].item[0], &queue->entity[0]);
      VERIFY(res_d <= 0);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_simcom_parcer_post_proc(), REQ RES NULL(expect) - handle state") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);      
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT+TEST?"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL,"+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), NULL);
      VERIFY(res);
      char rs_req_impl[128]  = "AT+TEST?";
      char rs_res_impl[128]  = "\r\nOK\r\n";
      char rs_me_impl[128]   = "EMPTY";
      ringslice_t rs_data = {0};
      ringslice_t rs_req  = ringslice_initializer((uint8_t*)rs_req_impl, 128, 0, strlen(rs_req_impl));
      ringslice_t rs_res  = ringslice_initializer((uint8_t*)rs_res_impl, 128, 0, strlen(rs_res_impl));
      ringslice_t rs_me   = ringslice_initializer((uint8_t*)rs_me_impl, 128, 0, strlen(rs_me_impl));
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      int res_d = _tact_simcom_parcer_post_proc(&test_ctx, &rs_me, &rs_req, &rs_res, &rs_data, &queue->entity[0].item[0], &queue->entity[0]);
      VERIFY(res_d <= 0);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_simcom_parcer_post_proc(), REQ RES NULL(no expect) - handle state") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);      
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT+TEST?"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL, NULL, TACT_NO_ARG),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, 0, NULL);
      VERIFY(res);
      char rs_req_impl[128]  = "AT+TEST?";
      char rs_res_impl[128]  = "\r\nOK\r\n";
      char rs_me_impl[128]   = "EMPTY";
      ringslice_t rs_data = {0};
      ringslice_t rs_req  = ringslice_initializer((uint8_t*)rs_req_impl, 128, 0, strlen(rs_req_impl));
      ringslice_t rs_res  = ringslice_initializer((uint8_t*)rs_res_impl, 128, 0, strlen(rs_res_impl));
      ringslice_t rs_me   = ringslice_initializer((uint8_t*)rs_me_impl, 128, 0, strlen(rs_me_impl));
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      int res_d = _tact_simcom_parcer_post_proc(&test_ctx, &rs_me, &rs_req, &rs_res, &rs_data, &queue->entity[0].item[0], &queue->entity[0]);
      VERIFY(res_d);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_simcom_parcer_post_proc(), REQ NULL DATA - handle state") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);      
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM(TACT_CMD_SAVE"AT+TEST?"TACT_CMD_CRLF, TACT_CMD_SAVE"+TEST", TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL,"+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), NULL);
      VERIFY(res);
      char rs_req_impl[128]  = "AT+TEST?";
      char rs_me_impl[128]   = "EMPTY";
      char rs_data_impl[128] = "+TEST: 523566, text";
      ringslice_t rs_data = ringslice_initializer((uint8_t*)rs_data_impl, 128, 0, strlen(rs_data_impl));
      ringslice_t rs_req  = ringslice_initializer((uint8_t*)rs_req_impl, 128, 0, strlen(rs_req_impl));
      ringslice_t rs_res  = {0};
      ringslice_t rs_me   = ringslice_initializer((uint8_t*)rs_me_impl, 128, 0, strlen(rs_me_impl));
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      int res_d = _tact_simcom_parcer_post_proc(&test_ctx, &rs_me, &rs_req, &rs_res, &rs_data, &queue->entity[0].item[0], &queue->entity[0]);
      VERIFY(res_d);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_simcom_parcer_post_proc(), REQ RES(OK) DATA - handle state") {
      char rx_data[] = "AT+TEST?\r\n+TEST: 523566, text\r\n\r\nOK\r\n";
      uint16_t rx_len = (uint16_t)strlen(rx_data);
      tact_ring_buffer_t rx = {
        .buffer = (uint8_t*)rx_data,
        .count = rx_len,
        .head = rx_len,
        .tail = 0,
        .size = sizeof(rx_data),
      };
      tact_init(&test_ctx, test_printf, test_write, &rx);
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT+TEST?"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL,"+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), NULL);
      VERIFY(res);
      ringslice_t rs_me = ringslice_initializer((uint8_t*)rx_data, sizeof(rx_data), 0, rx_len);
      ringslice_t rs_req = ringslice_initializer((uint8_t*)rx_data, sizeof(rx_data), 0, strlen("AT+TEST?\r\n"));
      ringslice_t rs_data = ringslice_initializer((uint8_t*)rx_data, sizeof(rx_data), rs_req.last,
                                                   (uint16_t)(rs_req.last + strlen("+TEST: 523566, text\r\n")));
      ringslice_t rs_res = ringslice_initializer((uint8_t*)rx_data, sizeof(rx_data), rs_data.last, rx_len);
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      int res_d = _tact_simcom_parcer_post_proc(&test_ctx, &rs_me, &rs_req, &rs_res, &rs_data, &queue->entity[0].item[0], &queue->entity[0]);
      VERIFY(res_d);
      VERIFY(strcmp((tact_mdl_rtd_t*){queue->entity[0].data}->modem_imei, "5235") == 0);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_simcom_parcer_post_proc(), REQ RES(ERROR) DATA  - handle state") {
      char rx_data[] = "AT+TEST?\r\n+TEST: 523566, text\r\n\r\nERROR\r\n";
      uint16_t rx_len = (uint16_t)strlen(rx_data);
      tact_ring_buffer_t rx = {
        .buffer = (uint8_t*)rx_data,
        .count = rx_len,
        .head = rx_len,
        .tail = 0,
        .size = sizeof(rx_data),
      };
      tact_init(&test_ctx, test_printf, test_write, &rx);
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT+TEST?"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL,"+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), NULL);
      VERIFY(res);
      ringslice_t rs_me = ringslice_initializer((uint8_t*)rx_data, sizeof(rx_data), 0, rx_len);
      ringslice_t rs_req = ringslice_initializer((uint8_t*)rx_data, sizeof(rx_data), 0, strlen("AT+TEST?\r\n"));
      ringslice_t rs_data = ringslice_initializer((uint8_t*)rx_data, sizeof(rx_data), rs_req.last,
                                                   (uint16_t)(rs_req.last + strlen("+TEST: 523566, text\r\n")));
      ringslice_t rs_res = ringslice_initializer((uint8_t*)rx_data, sizeof(rx_data), rs_data.last, rx_len);
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      int res_d = _tact_simcom_parcer_post_proc(&test_ctx, &rs_me, &rs_req, &rs_res, &rs_data, &queue->entity[0].item[0], &queue->entity[0]);
      VERIFY(res_d <= 0);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_simcom_parcer_post_proc(), NULL RES NULL  - unhandle state") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);      
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT+TEST?"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL,"+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), NULL);
      VERIFY(res);
      char rs_res_impl[128]  = "\r\nOK\r\n";
      ringslice_t rs_data = {0};
      ringslice_t rs_req  = {0};
      ringslice_t rs_res  = ringslice_initializer((uint8_t*)rs_res_impl, 128, 0, strlen(rs_res_impl));
      ringslice_t rs_me   = {0};
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      int res_d = _tact_simcom_parcer_post_proc(&test_ctx, &rs_me, &rs_req, &rs_res, &rs_data, &queue->entity[0].item[0], &queue->entity[0]);
      VERIFY(res_d <= 0);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_simcom_parcer_post_proc(), NULL RES DATA  - unhandle state") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);      
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT+TEST?"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL,"+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), NULL);
      VERIFY(res);
      char rs_res_impl[128]  = "\r\nOK\r\n";
      char rs_data_impl[128] = "+TEST: 523566, text";
      ringslice_t rs_data = ringslice_initializer((uint8_t*)rs_data_impl, 128, 0, strlen(rs_data_impl));
      ringslice_t rs_req  = {0};
      ringslice_t rs_res  = ringslice_initializer((uint8_t*)rs_res_impl, 128, 0, strlen(rs_res_impl));
      ringslice_t rs_me   = {0};
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      int res_d = _tact_simcom_parcer_post_proc(&test_ctx, &rs_me, &rs_req, &rs_res, &rs_data, &queue->entity[0].item[0], &queue->entity[0]);
      VERIFY(res_d <= 0);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_simcom_parcer_post_proc(), NULL NULL DATA(expect)  - handle state") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);      
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM(NULL, "+TEST", TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL,"+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), NULL);
      VERIFY(res);
      char rs_data_impl[128] = "+TEST: 523566, text";
      ringslice_t rs_data = ringslice_initializer((uint8_t*)rs_data_impl, 128, 0, strlen(rs_data_impl));
      ringslice_t rs_req  = {0};
      ringslice_t rs_res  = {0};
      ringslice_t rs_me   = {0};
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      int res_d = _tact_simcom_parcer_post_proc(&test_ctx, &rs_me, &rs_req, &rs_res, &rs_data, &queue->entity[0].item[0], &queue->entity[0]);
      VERIFY(res_d);
      VERIFY(strcmp((tact_mdl_rtd_t*){queue->entity[0].data}->modem_imei, "5235") == 0);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_simcom_parcer_post_proc(), NULL NULL DATA(no expect)  - handle state") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);      
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT+TEST?"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL, NULL, TACT_NO_ARG),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, 0, NULL);
      VERIFY(res);
      char rs_data_impl[128] = "+TEST: 523566, text";
      ringslice_t rs_data = ringslice_initializer((uint8_t*)rs_data_impl, 128, 0, strlen(rs_data_impl));
      ringslice_t rs_req  = {0};
      ringslice_t rs_res  = {0};
      ringslice_t rs_me   = {0};
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      int res_d = _tact_simcom_parcer_post_proc(&test_ctx, &rs_me, &rs_req, &rs_res, &rs_data, &queue->entity[0].item[0], &queue->entity[0]);
      VERIFY(res_d <= 0);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_simcom_parcer_find_rs_data() RES before DATA") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);      
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT+TEST?"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL,"+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), NULL);
      VERIFY(res);
      char rs_me_impl[128]   = "FFFFAT+TEST?\r\r\nOK\r\n\r\n+TEST: 523566, text\r\nFFFFFFF";
      ringslice_t rs_data = {0};
      ringslice_t rs_req  = ringslice_initializer((uint8_t*)rs_me_impl, 128, 4, 4+strlen("AT+TEST?\r"));
      ringslice_t rs_res  = ringslice_initializer((uint8_t*)rs_me_impl, 128, 4+strlen("AT+TEST?\r"), 4+strlen("AT+TEST?\r")+strlen("\r\nOK\r\n"));
      ringslice_t rs_me   = ringslice_initializer((uint8_t*)rs_me_impl, 128, 0, strlen(rs_me_impl));
      _tact_simcom_parcer_find_rs_data(&rs_me, &rs_req, &rs_res, &rs_data);
      VERIFY(ringslice_is_empty(&rs_data) != 1);
      VERIFY(ringslice_strncmp(&rs_data, "+TEST: 523566", strlen("+TEST: 523566")) == 0);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_simcom_parcer_find_rs_data() RES after DATA") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);      
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT+TEST?"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL,"+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), NULL);
      VERIFY(res);
      char rs_me_impl[128]   = "FFFFAT+TEST?\r\r\n+TEST: 523566, text\r\n\r\nOK\r\nFFFFFFF";
      ringslice_t rs_data = {0};
      ringslice_t rs_req  = ringslice_initializer((uint8_t*)rs_me_impl, 128, 4, 4+strlen("AT+TEST?\r"));
      ringslice_t rs_res  = ringslice_initializer((uint8_t*)rs_me_impl, 128, 
                                                  4+strlen("AT+TEST?\r")+strlen("\r\n+TEST: 523566, text\r\n"),
                                                  4+strlen("AT+TEST?\r")+strlen("\r\n+TEST: 523566, text\r\n")+strlen("\r\nOK\r\n"));
      ringslice_t rs_me   = ringslice_initializer((uint8_t*)rs_me_impl, 128, 0, strlen(rs_me_impl));
      _tact_simcom_parcer_find_rs_data(&rs_me, &rs_req, &rs_res, &rs_data);
      VERIFY(ringslice_is_empty(&rs_data) != 1);
      VERIFY(ringslice_strncmp(&rs_data, "+TEST: 523566", strlen("+TEST: 523566")) == 0);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_simcom_parcer_find_rs_data() no RES and REQ") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);      
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT+TEST?"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL,"+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), NULL);
      VERIFY(res);
      char rs_me_impl[128]   = "\r\n+TEST: 523566, text\r\nFFFFFFF";
      ringslice_t rs_data = {0};
      ringslice_t rs_req  = {0};
      ringslice_t rs_res  = {0};
      ringslice_t rs_me   = ringslice_initializer((uint8_t*)rs_me_impl, 128, 0, strlen(rs_me_impl));
      _tact_simcom_parcer_find_rs_data(&rs_me, &rs_req, &rs_res, &rs_data);
      VERIFY(ringslice_is_empty(&rs_data) != 1);
      VERIFY(ringslice_strncmp(&rs_data, "+TEST: 523566", strlen("+TEST: 523566")) == 0);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_simcom_parcer_find_rs_data() no RES") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);      
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT+TEST?"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL,"+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), NULL);
      VERIFY(res);
      char rs_me_impl[128]   = "FFFFAT+TEST?\r\r\n+TEST: 523566, text\r\nFFFFFFF";
      ringslice_t rs_data = {0};
      ringslice_t rs_req  = ringslice_initializer((uint8_t*)rs_me_impl, 128, 4, 4+strlen("AT+TEST?\r"));
      ringslice_t rs_res  = {0};
      ringslice_t rs_me   = ringslice_initializer((uint8_t*)rs_me_impl, 128, 0, strlen(rs_me_impl));
      _tact_simcom_parcer_find_rs_data(&rs_me, &rs_req, &rs_res, &rs_data);
      VERIFY(ringslice_is_empty(&rs_data) != 1);
      VERIFY(ringslice_strncmp(&rs_data, "+TEST: 523566", strlen("+TEST: 523566")) == 0);   
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_simcom_parcer_find_rs_res() OK") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);      
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT+TEST?"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL,"+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), NULL);
      VERIFY(res);
      char rs_me_impl[128]   = "FFFFAT+TEST?\r\r\n+TEST: 523566, text\r\n\r\nOK\r\nFFFFFFF";
      ringslice_t rs_req  = ringslice_initializer((uint8_t*)rs_me_impl, 128, 4, 4+strlen("AT+TEST?\r"));
      ringslice_t rs_res  = {0};
      ringslice_t rs_me   = ringslice_initializer((uint8_t*)rs_me_impl, 128, 0, strlen(rs_me_impl));
      _tact_simcom_parcer_find_rs_res(&rs_me, &rs_req, &rs_res);
      VERIFY(ringslice_is_empty(&rs_res) != 1);
      VERIFY(ringslice_strcmp(&rs_res, "\r\nOK\r\n") == 0);   
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

  TEST("tact_simcom_parcer_find_rs_res() ERROR") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);      
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT+TEST?"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL,"+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), NULL);
      VERIFY(res);
      char rs_me_impl[128]   = "FFFFAT+TEST?\r\r\n+TEST: 523566, text\r\n\r\nERROR\r\nFFFFFFF";
      ringslice_t rs_req  = ringslice_initializer((uint8_t*)rs_me_impl, 128, 4, 4+strlen("AT+TEST?\r"));
      ringslice_t rs_res  = {0};
      ringslice_t rs_me   = ringslice_initializer((uint8_t*)rs_me_impl, 128, 0, strlen(rs_me_impl));
      _tact_simcom_parcer_find_rs_res(&rs_me, &rs_req, &rs_res);
      VERIFY(ringslice_is_empty(&rs_res) != 1);
      VERIFY(ringslice_strcmp(&rs_res, "\r\nERROR\r\n") == 0);   
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

  TEST("tact_simcom_parcer_find_rs_req()") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);      
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT+TEST?"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL,"+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), NULL);
      VERIFY(res);
      char rs_me_impl[128] = "FFFFAT+TEST?\r\r\n+TEST: 523566, text\r\n\r\nOK\r\nFFFFFFF";
      ringslice_t rs_req  = {0};
      ringslice_t rs_me   = ringslice_initializer((uint8_t*)rs_me_impl, 128, 0, strlen(rs_me_impl));
      _tact_simcom_parcer_find_rs_req(&rs_me, &rs_req, items[0].req);
      VERIFY(ringslice_is_empty(&rs_req) != 1);
      VERIFY(ringslice_strcmp(&rs_req, "AT+TEST?\r") == 0);   
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

  TEST("tact_cmd_ring_parcer() SIMCOM format RES after data") {
      char parce_buffer[2048] = "FFFFAT+TEST?\r\r\n+TEST: 523566, text\r\n\r\nOK\r\nFFFFFFF";
      uint16_t parce_buffer_tail = 0;
      uint16_t parce_buffer_head = strlen(parce_buffer);

      tact_ring_buffer_t ring = {
        .buffer = (uint8_t*)parce_buffer,
        .count = parce_buffer_head - parce_buffer_tail,
        .head = parce_buffer_head,
        .tail = parce_buffer_tail,
        .size = 2048,
      };

      tact_init(&test_ctx, test_printf, test_write, &ring);
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT+TEST?"TACT_CMD_CRLF, "+TEST", TACT_PARCE_SIMCOM, 2, 150, 0, 1, testItemCB,"+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), test_buffer);
      VERIFY(res);
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      ringslice_t rs_me = ringslice_initializer((uint8_t*)parce_buffer, 2048, parce_buffer_tail, parce_buffer_head);
      int res_p = _tact_cmd_ring_parcer(&test_ctx, &queue->entity[0], &queue->entity->item[0], rs_me);
      VERIFY(res_p);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

  TEST("tact_cmd_ring_parcer() SIMCOM format RES before data") {
      char parce_buffer[2048] = "FFFFAT+TEST?\r\r\nOK\r\n\r\n+TEST: 523566, text\r\nFFFFFFF";
      uint16_t parce_buffer_tail = 0;
      uint16_t parce_buffer_head = strlen(parce_buffer);

      tact_ring_buffer_t ring = {
        .buffer = (uint8_t*)parce_buffer,
        .count = parce_buffer_head - parce_buffer_tail,
        .head = parce_buffer_head,
        .tail = parce_buffer_tail,
        .size = 2048,
      };

      tact_init(&test_ctx, test_printf, test_write, &ring);
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT+TEST?"TACT_CMD_CRLF, "+TEST", TACT_PARCE_SIMCOM, 2, 150, 0, 1, testItemCB,"+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), test_buffer);
      VERIFY(res);
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      ringslice_t rs_me = ringslice_initializer((uint8_t*)parce_buffer, 2048, parce_buffer_tail, parce_buffer_head);
      int res_p = _tact_cmd_ring_parcer(&test_ctx, &queue->entity[0], &queue->entity->item[0], rs_me);
      VERIFY(res_p);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

  TEST("tact_cmd_ring_parcer() SIMCOM format RES before data without prefix check") {
      char parce_buffer[2048] = "FFFFAT+TEST?\r\r\nOK\r\n\r\n+TEST: 523566, text\r\nFFFFFFF";
      uint16_t parce_buffer_tail = 0;
      uint16_t parce_buffer_head = strlen(parce_buffer);

      tact_ring_buffer_t ring = {
        .buffer = (uint8_t*)parce_buffer,
        .count = parce_buffer_head - parce_buffer_tail,
        .head = parce_buffer_head,
        .tail = parce_buffer_tail,
        .size = 2048,
      };
      tact_init(&test_ctx, test_printf, test_write, &ring);
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT+TEST?"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, testItemCB,"+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), test_buffer);
      VERIFY(res);
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      ringslice_t rs_me = ringslice_initializer((uint8_t*)parce_buffer, 2048, parce_buffer_tail, parce_buffer_head);
      int res_p = _tact_cmd_ring_parcer(&test_ctx, &queue->entity[0], &queue->entity->item[0], rs_me);
      VERIFY(res_p);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

  TEST("tact_cmd_ring_parcer() SIMCOM no format no prefix, just sending and wait echo with OK") {
      char parce_buffer[2048] = "FFFFAT+TEST?\r\r\nOK\r\n\r\n+TEST: 523566, text\r\nFFFFFFF";
      uint16_t parce_buffer_tail = 0;
      uint16_t parce_buffer_head = strlen(parce_buffer);

      tact_ring_buffer_t ring = {
        .buffer = (uint8_t*)parce_buffer,
        .count = parce_buffer_head - parce_buffer_tail,
        .head = parce_buffer_head,
        .tail = parce_buffer_tail,
        .size = 2048,
      };
      tact_init(&test_ctx, test_printf, test_write, &ring);
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT+TEST?"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 2, 150, 0, 1, NULL, NULL, TACT_NO_ARG),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, 0, test_buffer);
      VERIFY(res);
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      ringslice_t rs_me = ringslice_initializer((uint8_t*)parce_buffer, 2048, parce_buffer_tail, parce_buffer_head);
      int res_p = _tact_cmd_ring_parcer(&test_ctx, &queue->entity[0], &queue->entity->item[0], rs_me);
      VERIFY(res_p);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

  TEST("tact_cmd_ring_parcer() SIMCOM full check without RES") {
      char parce_buffer[2048] = "FFFFAT+TEST?\r\r\n+TEST: 523566, text\r\nFFFFFFF";
      uint16_t parce_buffer_tail = 0;
      uint16_t parce_buffer_head = strlen(parce_buffer);;

      tact_ring_buffer_t ring = {
        .buffer = (uint8_t*)parce_buffer,
        .count = parce_buffer_head - parce_buffer_tail,
        .head = parce_buffer_head,
        .tail = parce_buffer_tail,
        .size = 2048,
      };
      tact_init(&test_ctx, test_printf, test_write, &ring);
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT+TEST?"TACT_CMD_CRLF, "+TEST", TACT_PARCE_SIMCOM, 2, 150, 0, 1, testItemCB,"+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), test_buffer);
      VERIFY(res);
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      ringslice_t rs_me = ringslice_initializer((uint8_t*)parce_buffer, 2048, parce_buffer_tail, parce_buffer_head);
      int res_p = _tact_cmd_ring_parcer(&test_ctx, &queue->entity[0], &queue->entity->item[0], rs_me);
      VERIFY(res_p);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

  TEST("tact_cmd_ring_parcer() SIMCOM full check without RES and REQ") {
      char parce_buffer[2048] = "\r\n+TEST: 523566, text\r\nFFFFFFFFFFF";
      uint16_t parce_buffer_tail = 0;
      uint16_t parce_buffer_head = strlen(parce_buffer);

      tact_ring_buffer_t ring = {
        .buffer = (uint8_t*)parce_buffer,
        .count = parce_buffer_head - parce_buffer_tail,
        .head = parce_buffer_head,
        .tail = parce_buffer_tail,
        .size = 2048,
      };
      tact_init(&test_ctx, test_printf, test_write, &ring);
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM(NULL, "+TEST", TACT_PARCE_SIMCOM, 2, 150, 0, 1, testItemCB,"+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), test_buffer);
      VERIFY(res);
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      ringslice_t rs_me = ringslice_initializer((uint8_t*)parce_buffer, 2048, parce_buffer_tail, parce_buffer_head);
      int res_p = _tact_cmd_ring_parcer(&test_ctx, &queue->entity[0], &queue->entity->item[0], rs_me);
      VERIFY(res_p);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

  TEST("tact_cmd_ring_parcer() SIMCOM no Item Req, only answer") {
      char parce_buffer[2048] = "\r\n+TEST: 523566, text\r\nFFFFFFFFFFF";
      uint16_t parce_buffer_tail = 0;
      uint16_t parce_buffer_head = strlen(parce_buffer);

      tact_ring_buffer_t ring = {
        .buffer = (uint8_t*)parce_buffer,
        .count = parce_buffer_head - parce_buffer_tail,
        .head = parce_buffer_head,
        .tail = parce_buffer_tail,
        .size = 2048,
      };
      tact_init(&test_ctx, test_printf, test_write, &ring);
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM(NULL, "+TEST", TACT_PARCE_SIMCOM, 2, 150, 0, 1, testItemCB,"+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), test_buffer);
      VERIFY(res);
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      ringslice_t rs_me = ringslice_initializer((uint8_t*)parce_buffer, 2048, parce_buffer_tail, parce_buffer_head);
      int res_p = _tact_cmd_ring_parcer(&test_ctx, &queue->entity[0], &queue->entity->item[0], rs_me);
      VERIFY(res_p);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

  TEST("tact_cmd_ring_parcer() RAW with req") {
      char parce_buffer[2048] = "\r\n+TEST: 523566, text\r\nFFFFFFFFFFF";
      uint16_t parce_buffer_tail = 0;
      uint16_t parce_buffer_head = strlen(parce_buffer);

      tact_ring_buffer_t ring = {
        .buffer = (uint8_t*)parce_buffer,
        .count = parce_buffer_head - parce_buffer_tail,
        .head = parce_buffer_head,
        .tail = parce_buffer_tail,
        .size = 2048,
      };
      tact_init(&test_ctx, test_printf, test_write, &ring);
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT+TEST=1", "+TEST", TACT_PARCE_RAW, 2, 150, 0, 1, testItemCB, "\r\n+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), test_buffer);
      VERIFY(res);
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      ringslice_t rs_me = ringslice_initializer((uint8_t*)parce_buffer, 2048, parce_buffer_tail, parce_buffer_head);
      int res_p = _tact_cmd_ring_parcer(&test_ctx, &queue->entity[0], &queue->entity->item[0], rs_me);
      VERIFY(res_p);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

  TEST("tact_cmd_ring_parcer() RAW no req") {
      char parce_buffer[2048] = "\r\n+TEST: 523566, text\r\nFFFFFFFFFFF";
      uint16_t parce_buffer_tail = 0;
      uint16_t parce_buffer_head = strlen(parce_buffer);

      tact_ring_buffer_t ring = {
        .buffer = (uint8_t*)parce_buffer,
        .count = parce_buffer_head - parce_buffer_tail,
        .head = parce_buffer_head,
        .tail = parce_buffer_tail,
        .size = 2048,
      };
      tact_init(&test_ctx, test_printf, test_write, &ring);
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM(NULL, "+TEST", TACT_PARCE_RAW, 2, 150, 0, 1, testItemCB,"\r\n+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), test_buffer);
      VERIFY(res);
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      ringslice_t rs_me = ringslice_initializer((uint8_t*)parce_buffer, 2048, parce_buffer_tail, parce_buffer_head);
      int res_p = _tact_cmd_ring_parcer(&test_ctx, &queue->entity[0], &queue->entity->item[0], rs_me);
      VERIFY(res_p);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }  

  TEST("tact_cmd_ring_parcer() RAW No prefix and arg") {
      char parce_buffer[2048] = "\r\n+TEST: 523566, text\r\nFFFFFFFFFFF";
      uint16_t parce_buffer_tail = 0;
      uint16_t parce_buffer_head = strlen(parce_buffer);

      tact_ring_buffer_t ring = {
        .buffer = (uint8_t*)parce_buffer,
        .count = parce_buffer_head - parce_buffer_tail,
        .head = parce_buffer_head,
        .tail = parce_buffer_tail,
        .size = 2048,
      };
      tact_init(&test_ctx, test_printf, test_write, &ring);
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM("AT+TEST=1", NULL, TACT_PARCE_RAW, 2, 150, 0, 1, NULL, NULL, TACT_NO_ARG),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), NULL, sizeof(tact_mdl_rtd_t), test_buffer);
      VERIFY(res);
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      ringslice_t rs_me = ringslice_initializer((uint8_t*)parce_buffer, 2048, parce_buffer_tail, parce_buffer_head);
      int res_p = _tact_cmd_ring_parcer(&test_ctx, &queue->entity[0], &queue->entity->item[0], rs_me);
      VERIFY(res_p);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }   

  TEST("tact_process_urcs()") {
      char parce_buffer[2048] = "\r\n+TEST: 523566, text\r\nFFFFFFFFFFF";
      uint16_t parce_buffer_tail = 0;
      uint16_t parce_buffer_head = strlen(parce_buffer);

      tact_ring_buffer_t ring = {
        .buffer = (uint8_t*)parce_buffer,
        .count = parce_buffer_head - parce_buffer_tail,
        .head = parce_buffer_head,
        .tail = parce_buffer_tail,
        .size = 2048,
      };
      tact_init(&test_ctx, test_printf, test_write, &ring);
      tact_urc_queue_t urc = {"+TEST", testUrcCB};
      tact_urc_enqueue(&test_ctx, &urc);
      ringslice_t rs_me   = ringslice_initializer((uint8_t*)parce_buffer, 2048, 0, strlen(parce_buffer));
      _tact_process_urcs(&test_ctx, &rs_me);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_core_proc() first cmd fail, second success") {
      char parce_buffer[2048] = "\r\n+TEST: 523566, text\r\nFFFFFFFFFFF";
      uint16_t parce_buffer_tail = 0;
      uint16_t parce_buffer_head = strlen(parce_buffer);

      tact_ring_buffer_t ring = {
        .buffer = (uint8_t*)parce_buffer,
        .count = parce_buffer_head - parce_buffer_tail,
        .head = parce_buffer_head,
        .tail = parce_buffer_tail,
        .size = 2048,
      };
      tact_init(&test_ctx, test_printf, test_write, &ring);
      tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
      {
        TACT_ITEM(TACT_CMD_SAVE"AT+GSN"TACT_CMD_CRLF, NULL, TACT_PARCE_SIMCOM, 2, 150, 1, 1, NULL, NULL, TACT_NO_ARG),
        TACT_ITEM(NULL, "+TEST", TACT_PARCE_SIMCOM, 2, 150, 0, 1, testItemCB,"+TEST: %4[^,]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
      };
      bool res = tact_entity_enqueue(&test_ctx, items, sizeof(items)/sizeof(items[0]), testEntityCB, sizeof(tact_mdl_rtd_t), test_buffer);
      VERIFY(res);
      tact_entity_queue_t* queue =_tact_get_entity_queue(&test_ctx);
      while(queue->entity_cnt)
      {
        _tact_core_proc(&test_ctx);
      }
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

  TEST("tact_process_urcs() removes only the framed URC and preserves adjacent response bytes") {
      char rx_bytes[128] = {0};
      tact_ring_buffer_t ring = {
        .buffer = (uint8_t*)rx_bytes,
        .count = 0,
        .head = 0,
        .tail = 0,
        .size = sizeof(rx_bytes),
      };
      VERIFY(tact_init_ex(&test_ctx, test_printf, test_write, &ring));
      tact_urc_queue_t urc = {"+TEST", testUrcCB};
      VERIFY(tact_urc_enqueue(&test_ctx, &urc));
      static const uint8_t stream[] = "\r\n+RSP: 1\r\n\r\n+TEST: 1\r\n+TAIL: 2\r\n";
      VERIFY(tact_rx_push(&test_ctx, stream, sizeof(stream) - 1u));
      urc_callback_count = 0;

      ringslice_t input = ringslice_initializer(ring.buffer, ring.size, ring.tail, ring.head);
      _tact_process_urcs(&test_ctx, &input);
      VERIFY(urc_callback_count == 1);
      ringslice_t remaining = ringslice_initializer(ring.buffer, ring.size, ring.tail, ring.head);
      ringslice_t found = ringslice_strnstr(&remaining, "+TEST", strlen("+TEST"));
      VERIFY(ringslice_is_empty(&found));
      found = ringslice_strnstr(&remaining, "+RSP", strlen("+RSP"));
      VERIFY(!ringslice_is_empty(&found));
      found = ringslice_strnstr(&remaining, "+TAIL", strlen("+TAIL"));
      VERIFY(!ringslice_is_empty(&found));
      VERIFY(ring.count == sizeof(stream) - 1u - strlen("+TEST: 1\r\n"));
      tact_deinit(&test_ctx);
    }

  TEST("tact_core_proc() rejects signed steps outside entity bounds") {
      for(uint8_t failure_case = 0; failure_case < 3; ++failure_case)
      {
        char rx_data[8] = {0};
        uint16_t response_len = 0;
        if(!failure_case) {
          memcpy(rx_data, "MATCH", sizeof("MATCH") - 1);
          response_len = sizeof("MATCH") - 1;
        }
        tact_ring_buffer_t ring = {
          .buffer = (uint8_t*)rx_data,
          .count = response_len,
          .head = response_len,
          .tail = 0,
          .size = sizeof(rx_data),
        };
        tact_init(&test_ctx, test_printf, test_write, &ring);
        tact_item_t items[] = {
          TACT_ITEM(NULL, "MATCH", TACT_PARCE_RAW, failure_case == 2 ? 0 : 1, 1, -127, 127, NULL, NULL, TACT_NO_ARG),
          TACT_ITEM(NULL, "OTHER", TACT_PARCE_RAW, 1, 1, 0, 0, NULL, NULL, TACT_NO_ARG),
        };
        VERIFY(tact_entity_enqueue(&test_ctx, items, sizeof(items) / sizeof(items[0]), NULL, 0, NULL));

        _tact_core_proc(&test_ctx); // enter read state
        _tact_core_proc(&test_ctx); // complete with +127 or timeout with -127
        VERIFY(_tact_get_entity_queue(&test_ctx)->entity_cnt == 0);

        tact_deinit(&test_ctx);
        VERIFY(!_tact_get_init(&test_ctx).init);
      }
    }

  TEST("tact_rx_push() preserves ring ordering across wraparound") {
      uint8_t rx_data[8] = {0};
      tact_ring_buffer_t ring = {
        .buffer = rx_data,
        .count = 0,
        .head = 0,
        .tail = 0,
        .size = sizeof(rx_data),
      };
      tact_init(&test_ctx, test_printf, test_write, &ring);
      VERIFY(tact_rx_push(&test_ctx, (const uint8_t*)"abcdef", 6));
      VERIFY(ring.head == 6 && ring.tail == 0 && ring.count == 6);

      tact_item_t item = TACT_ITEM(NULL, "abcd", TACT_PARCE_RAW, 1, 20, 0, 0, NULL, NULL, TACT_NO_ARG);
      VERIFY(tact_entity_enqueue(&test_ctx, &item, 1, NULL, 0, NULL));
      _tact_core_proc(&test_ctx);
      _tact_core_proc(&test_ctx);
      VERIFY(_tact_get_entity_queue(&test_ctx)->entity_cnt == 0);
      VERIFY(ring.head == 6 && ring.tail == 4 && ring.count == 2);

      VERIFY(tact_rx_push(&test_ctx, (const uint8_t*)"ghijk", 5));
      VERIFY(ring.head == 3 && ring.tail == 4 && ring.count == 7);
      VERIFY(!tact_rx_push(&test_ctx, (const uint8_t*)"x", 1));
      VERIFY(ring.head == 3 && ring.tail == 4 && ring.count == 7);

      char ordered[7];
      for(uint16_t i = 0; i < sizeof(ordered); ++i)
        ordered[i] = (char)rx_data[(ring.tail + i) % ring.size];
      VERIFY(memcmp(ordered, "efghijk", sizeof(ordered)) == 0);

      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

  TEST("TACT_CMD_FORCE completes without waiting for RX bytes") {
      uint8_t rx_data[8] = {0};
      tact_ring_buffer_t ring = {
        .buffer = rx_data,
        .count = 0,
        .head = 0,
        .tail = 0,
        .size = sizeof(rx_data),
      };
      tact_init(&test_ctx, test_printf, test_write, &ring);
      tact_item_t item = TACT_ITEM("AT+CFUN=1"TACT_CMD_CRLF, TACT_CMD_FORCE, TACT_PARCE_SIMCOM,
                                 1, 20, 0, 0, NULL, NULL, TACT_NO_ARG);
      VERIFY(tact_entity_enqueue(&test_ctx, &item, 1, NULL, 0, NULL));
      _tact_core_proc(&test_ctx);
      _tact_core_proc(&test_ctx);
      VERIFY(_tact_get_entity_queue(&test_ctx)->entity_cnt == 0);
      VERIFY(ring.count == 0);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

  TEST("tact_core_proc() resumes short UART writes without losing request bytes") {
      uint8_t rx_data[64] = {0};
      tact_ring_buffer_t ring = {
        .buffer = rx_data,
        .count = 0,
        .head = 0,
        .tail = 0,
        .size = sizeof(rx_data),
      };
      partial_tx_capture_len = 0;
      memset(partial_tx_capture, 0, sizeof(partial_tx_capture));
      VERIFY(tact_init_ex(&test_ctx, test_printf, partial_test_write, &ring));
      tact_item_t item = TACT_ITEM("AT+X"TACT_CMD_CRLF, "DONE", TACT_PARCE_RAW,
                                 0, 20, 0, 0, NULL, NULL, TACT_NO_ARG);
      VERIFY(tact_entity_enqueue(&test_ctx, &item, 1, NULL, 0, NULL));

      _tact_core_proc(&test_ctx);
      tact_entity_t* current = &_tact_get_entity_queue(&test_ctx)->entity[0];
      VERIFY(current->state == TACT_STATE_WRITE);
      VERIFY(current->tx_offset == 2u);
      _tact_core_proc(&test_ctx);
      VERIFY(current->state == TACT_STATE_WRITE);
      VERIFY(current->tx_offset == 4u);
      _tact_core_proc(&test_ctx);
      VERIFY(current->state == TACT_STATE_READ);
      VERIFY(partial_tx_capture_len == strlen("AT+X"TACT_CMD_CRLF));
      VERIFY(memcmp(partial_tx_capture, "AT+X"TACT_CMD_CRLF, partial_tx_capture_len) == 0);

      VERIFY(tact_rx_push(&test_ctx, (const uint8_t*)"DONE", 4));
      _tact_core_proc(&test_ctx);
      VERIFY(_tact_get_entity_queue(&test_ctx)->entity_cnt == 0);
      VERIFY(tact_deinit_ex(&test_ctx));
    }

  TEST("tact_core_proc() does not retry a request after a partial UART write stalls") {
      uint8_t rx_data[32] = {0};
      tact_ring_buffer_t ring = {
        .buffer = rx_data,
        .count = 0,
        .head = 0,
        .tail = 0,
        .size = sizeof(rx_data),
      };
      partial_tx_capture_len = 0;
      memset(partial_tx_capture, 0, sizeof(partial_tx_capture));
      entity_callback_count = 0;
      entity_callback_result = true;
      VERIFY(tact_init_ex(&test_ctx, test_printf, stall_after_two_test_write, &ring));
      tact_item_t item = TACT_ITEM("AT+PARTIAL"TACT_CMD_CRLF, "OK", TACT_PARCE_RAW,
                                 3, 3, 0, 0, NULL, NULL, TACT_NO_ARG);
      VERIFY(tact_entity_enqueue(&test_ctx, &item, 1, testTxEntityCB, 0, NULL));

      _tact_core_proc(&test_ctx);
      _tact_core_proc(&test_ctx);
      VERIFY(_tact_get_entity_queue(&test_ctx)->entity_cnt == 1);
      _tact_core_proc(&test_ctx);
      VERIFY(_tact_get_entity_queue(&test_ctx)->entity_cnt == 0);
      VERIFY(partial_tx_capture_len == 2u);
      VERIFY(memcmp(partial_tx_capture, "AT", 2u) == 0);
      VERIFY(entity_callback_count == 1u);
      VERIFY(!entity_callback_result);
      VERIFY(tact_deinit_ex(&test_ctx));
    }

    TEST("GPRS stream parser accepts fragmented non-NUL IPD headers") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      tact_gprs_stream_ctx_t stream = TACT_GPRS_STREAM_CTX_INITIALIZER;
      VERIFY(tact_gprs_stream_ctx_init(&test_ctx, &stream, 16));
      captured_tcp_packet_len = 0;
      memset(captured_tcp_packet, 0, sizeof(captured_tcp_packet));

      uint8_t part1[] = {'+', 'I', 'P'};
      uint8_t part2[] = {'D', ',', '5', ',', 'T'};
      uint8_t part3[] = {'C', 'P', ':', 'h', 'e', 'l', 'l', 'o'};
      VERIFY(tact_mld_gprs_server_stream_data_handler(&test_ctx, &stream, part1, sizeof(part1), capture_tcp_packet));
      VERIFY(stream.data_len == sizeof(part1));
      VERIFY(captured_tcp_packet_len == 0);
      VERIFY(tact_mld_gprs_server_stream_data_handler(&test_ctx, &stream, part2, sizeof(part2), capture_tcp_packet));
      VERIFY(stream.data_len == sizeof(part1) + sizeof(part2));
      VERIFY(captured_tcp_packet_len == 0);
      VERIFY(tact_mld_gprs_server_stream_data_handler(&test_ctx, &stream, part3, sizeof(part3), capture_tcp_packet));
      VERIFY(captured_tcp_packet_len == 5);
      VERIFY(memcmp(captured_tcp_packet, "hello", 5) == 0);
      VERIFY(stream.data_len == 0);

      tact_gprs_stream_ctx_cleanup(&test_ctx, &stream);
      VERIFY(o1heapGetDiagnostics(_tact_get_init(&test_ctx).heap).allocated == 0);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

  TEST("SMS send preserves a full 160 character message") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      tact_mdl_sms_msg_t sms = {0};
      memcpy(sms.num, "+1234567890", sizeof("+1234567890"));
      memset(sms.msg, 'X', 160);
      sms.msg[160] = '\0';
      VERIFY(tact_mdl_sms_send_text(&test_ctx, NULL, &sms, NULL));

      tact_entity_t* entity = &_tact_get_entity_queue(&test_ctx)->entity[0];
      VERIFY(entity->item_cnt == 7u);
      VERIFY(strstr(entity->item[1].req, "AT+CSCS=\"GSM\"") != NULL);
      const char* text = entity->item[3].req;
      VERIFY(text != NULL);
      VERIFY(strncmp(text, TACT_CMD_SAVE, strlen(TACT_CMD_SAVE)) == 0);
      VERIFY(strlen(text) == strlen(TACT_CMD_SAVE) + 160u + 1u);
      VERIFY((uint8_t)text[strlen(TACT_CMD_SAVE) + 160u] == 0x1a);
      VERIFY(strcmp(entity->item[3].answ.prefix, TACT_CMD_FORCE) == 0);
      VERIFY(entity->item[3].meta.rpt_cnt == 1u);
      VERIFY(entity->item[4].req == NULL);
      VERIFY(strcmp(entity->item[4].answ.prefix, "+CMGS:") == 0);
      VERIFY(entity->item[4].meta.wait == 4000u);
      VERIFY(entity->item[4].meta.rpt_cnt == 1u);
      VERIFY(strcmp(entity->item[5].answ.prefix, "OK") == 0);
      VERIFY(entity->item[5].meta.wait == 100u);
      VERIFY(entity->item[5].meta.err_step == 1 && entity->item[5].meta.ok_step == 1);
      VERIFY(strcmp(entity->item[6].answ.prefix, TACT_CMD_FORCE) == 0);

      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

  TEST("SMS send consumes prompt padding, waits for +CMGS, and submits once") {
      memset(test_buffer, 0, sizeof(test_buffer));
      tact_ring_buffer.head = 0;
      tact_ring_buffer.tail = 0;
      tact_ring_buffer.count = 0;
      captured_uart_tx_len = 0;
      memset(captured_uart_tx, 0, sizeof(captured_uart_tx));
      entity_callback_count = 0;
      entity_callback_result = false;
      VERIFY(tact_init_ex(&test_ctx, test_printf, capture_uart_write, &tact_ring_buffer));

      tact_mdl_sms_msg_t sms = {0};
      memcpy(sms.num, "+1234567890", sizeof("+1234567890"));
      memcpy(sms.msg, "HELLO", sizeof("HELLO"));
      VERIFY(tact_mdl_sms_send_text(&test_ctx, testTxEntityCB, &sms, NULL));

      static const char cmgf_reply[] = "AT+CMGF=1\r\r\nOK\r\n";
      tact_core_proc(&test_ctx);
      VERIFY(tact_rx_push(&test_ctx, (const uint8_t*)cmgf_reply, sizeof(cmgf_reply) - 1u));
      tact_core_proc(&test_ctx);

      static const char charset_reply[] = "AT+CSCS=\"GSM\"\r\r\nOK\r\n";
      tact_core_proc(&test_ctx);
      VERIFY(tact_rx_push(&test_ctx, (const uint8_t*)charset_reply, sizeof(charset_reply) - 1u));
      tact_core_proc(&test_ctx);

      static const char prompt[] = "\r\n> ";
      tact_core_proc(&test_ctx);
      VERIFY(tact_rx_push(&test_ctx, (const uint8_t*)prompt, sizeof(prompt) - 1u));
      tact_core_proc(&test_ctx);
      tact_core_proc(&test_ctx); /* Send the payload and Ctrl-Z once. */

      static const char cmgs_reply[] = "\r\n+CMGS: 12\r\n\r\nOK\r\n";
      VERIFY(tact_rx_push(&test_ctx, (const uint8_t*)cmgs_reply, sizeof(cmgs_reply) - 1u));
      tact_core_proc(&test_ctx); /* Process prompt completion and advance toward +CMGS. */
      tact_core_proc(&test_ctx);
      tact_entity_t* response_entity = &_tact_get_entity_queue(&test_ctx)->entity[0];
      ringslice_t response_slice = ringslice_initializer(tact_ring_buffer.buffer, tact_ring_buffer.size,
                                                          tact_ring_buffer.tail, tact_ring_buffer.head);
      VERIFY(response_entity->item_id == 4u);
      VERIFY(_tact_string_boolean_ops(&response_slice, "+CMGS:"));
      VERIFY(_tact_cmd_ring_parcer(&test_ctx, response_entity, &response_entity->item[4], response_slice));
      tact_core_proc(&test_ctx); /* Complete only after +CMGS arrives. */
      for(uint8_t i = 0; i < 4u; ++i) tact_core_proc(&test_ctx);

      static const char expected_tx[] = "AT+CMGF=1\r\nAT+CSCS=\"GSM\"\r\nAT+CMGS=\"+1234567890\"\r\nHELLO\x1A";
      VERIFY(captured_uart_tx_len == sizeof(expected_tx) - 1u);
      VERIFY(memcmp(captured_uart_tx, expected_tx, sizeof(expected_tx) - 1u) == 0);
      VERIFY(entity_callback_count == 1u && entity_callback_result);
      VERIFY(_tact_get_entity_queue(&test_ctx)->entity_cnt == 0u);
      VERIFY(tact_deinit_ex(&test_ctx));
    }

  TEST("SMS module quotes service-centre address and uses the documented CMGR syntax") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      tact_mdl_sms_msg_t sms = {0};
      memcpy(sms.num, "+1234567890", sizeof("+1234567890"));
      VERIFY(tact_mdl_sms_sc_set(&test_ctx, NULL, &sms, NULL));
      tact_entity_t* entity = &_tact_get_entity_queue(&test_ctx)->entity[0];
      VERIFY(strcmp(entity->item[0].req, TACT_CMD_SAVE "AT+CSCA=\"+1234567890\"\r\n") == 0);
      tact_deinit(&test_ctx);

      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      memset(&sms, 0, sizeof(sms));
      memcpy(sms.num, "+1234567890", sizeof("+1234567890"));
      sms.index = 1u;
      VERIFY(tact_mdl_sms_read(&test_ctx, NULL, &sms, NULL));
      entity = &_tact_get_entity_queue(&test_ctx)->entity[0];
      VERIFY(strcmp(entity->item[1].req, TACT_CMD_SAVE "AT+CMGR=1\r\n") == 0);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

  TEST("SMS module passes numeric options through to the modem") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      tact_mdl_sms_msg_t sms = {0};
      VERIFY(tact_mdl_sms_read(&test_ctx, NULL, &sms, NULL));
      VERIFY(tact_mdl_sms_delete(&test_ctx, NULL, &sms, NULL));
      sms.mode = 4u;
      VERIFY(tact_mdl_sms_indicate(&test_ctx, NULL, &sms, NULL));
      VERIFY(_tact_get_entity_queue(&test_ctx)->entity_cnt == 3u);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

  TEST("SMS send checks string bounds but not message contents") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      tact_mdl_sms_msg_t sms = {0};
      memcpy(sms.num, "+1234567890", sizeof("+1234567890"));
      sms.msg[0] = (char)0x80;
      sms.msg[1] = '\x1A';
      sms.msg[2] = '\0';
      VERIFY(tact_mdl_sms_send_text(&test_ctx, NULL, &sms, NULL));
      VERIFY(_tact_get_entity_queue(&test_ctx)->entity_cnt == 1u);
      tact_deinit(&test_ctx);

      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      memset(sms.msg, 'X', sizeof(sms.msg));
      VERIFY(!tact_mdl_sms_send_text(&test_ctx, NULL, &sms, NULL));
      sms.msg[0] = 'X';
      sms.msg[1] = '\0';
      memset(sms.num, '1', sizeof(sms.num));
      VERIFY(!tact_mdl_sms_send_text(&test_ctx, NULL, &sms, NULL));
      VERIFY(_tact_get_entity_queue(&test_ctx)->entity_cnt == 0u);

      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

  TEST("GPRS send copies transient payload and response prefix") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      char payload[] = "PING";
      char answer[] = "SEND OK";
      tact_mdl_gprs_data_t tcp = {.data = payload, .answ = answer};
      VERIFY(tact_mdl_gprs_socket_send_recieve(&test_ctx, NULL, &tcp, NULL));

      payload[0] = 'X';
      answer[0] = 'X';
      tact_entity_t* entity = &_tact_get_entity_queue(&test_ctx)->entity[0];
      VERIFY(strcmp(entity->item[1].req + strlen(TACT_CMD_SAVE), "AT+CIPSEND=5\r\n") == 0);
      VERIFY(memcmp(entity->item[2].req + strlen(TACT_CMD_SAVE), "PING", 4) == 0);
      VERIFY((uint8_t)entity->item[2].req[strlen(TACT_CMD_SAVE) + 4u] == 0x1a);
      VERIFY(strcmp(entity->item[2].answ.prefix + strlen(TACT_CMD_SAVE), "SEND OK") == 0);

      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

  TEST("GPRS send allocation failure preserves core context") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      char payload[5000];
      memset(payload, 'D', sizeof(payload) - 1u);
      payload[sizeof(payload) - 1u] = '\0';
      tact_mdl_gprs_data_t tcp = {.data = payload, .answ = NULL};
      VERIFY(!tact_mdl_gprs_socket_send_recieve(&test_ctx, NULL, &tcp, NULL));
      VERIFY(_tact_get_init(&test_ctx).init);
      VERIFY(_tact_get_entity_queue(&test_ctx)->entity_cnt == 0);
      VERIFY(o1heapGetDiagnostics(_tact_get_init(&test_ctx).heap).allocated == 0);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

  TEST("GPRS socket checks field bounds but passes values through") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      tact_mdl_gprs_server_t server = {0};
      memcpy(server.mode, "RAW", sizeof("RAW"));
      memcpy(server.ip, "host\"x", sizeof("host\"x"));
      memcpy(server.port, "oops", sizeof("oops"));
      VERIFY(tact_mdl_gprs_socket_connect(&test_ctx, NULL, &server, NULL));
      tact_entity_t* entity = &_tact_get_entity_queue(&test_ctx)->entity[0];
      VERIFY(strstr(entity->item[1].req, "AT+CIPSTART=\"RAW\",\"host\"x\",\"oops\"") != NULL);
      tact_deinit(&test_ctx);

      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      memset(server.mode, 'X', sizeof(server.mode));
      VERIFY(!tact_mdl_gprs_socket_connect(&test_ctx, NULL, &server, NULL));
      VERIFY(_tact_get_entity_queue(&test_ctx)->entity_cnt == 0u);
      tact_deinit(&test_ctx);
    }

    TEST("MQTT config checks used string bounds but not values") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      tact_mdl_mqtt_cfg_t cfg = {0};
      cfg.client_index = 9u;
      cfg.clean_session = 3u;
      memcpy(cfg.client_id, "id\"x", sizeof("id\"x"));
      memcpy(cfg.server_addr, "odd://host", sizeof("odd://host"));
      VERIFY(tact_mdl_mqtt_acquire(&test_ctx, NULL, &cfg, NULL));
      VERIFY(tact_mdl_mqtt_connect(&test_ctx, NULL, &cfg, NULL));
      VERIFY(_tact_get_entity_queue(&test_ctx)->entity_cnt == 2u);
      tact_deinit(&test_ctx);

      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      memset(cfg.server_addr, 'X', sizeof(cfg.server_addr));
      VERIFY(!tact_mdl_mqtt_connect(&test_ctx, NULL, &cfg, NULL));
      memset(cfg.client_id, 'X', sizeof(cfg.client_id));
      VERIFY(!tact_mdl_mqtt_acquire(&test_ctx, NULL, &cfg, NULL));
      VERIFY(_tact_get_entity_queue(&test_ctx)->entity_cnt == 0u);
      tact_deinit(&test_ctx);
    }

    TEST("MQTT checks bounds and owns copied publish strings") {
      uint8_t rx_data[256] = {0};
      tact_ring_buffer_t ring = {
        .buffer = rx_data,
        .count = 0,
        .head = 0,
        .tail = 0,
        .size = sizeof(rx_data),
      };
      VERIFY(tact_init_ex(&test_ctx, test_printf, test_write, &ring));
      tact_mdl_mqtt_msg_t msg = {
        .client_index = 0,
        .qos = 1,
        .topic = NULL,
        .payload = "hello",
        .retained = 0,
        .pub_timeout = 30,
      };
      VERIFY(!tact_mdl_mqtt_publish(&test_ctx, NULL, &msg, NULL));
      char unterminated_topic[TACT_MDL_MQTT_TOPIC_MAX + 1u];
      memset(unterminated_topic, 'X', sizeof(unterminated_topic));
      msg.topic = unterminated_topic;
      VERIFY(!tact_mdl_mqtt_publish(&test_ctx, NULL, &msg, NULL));
      VERIFY(_tact_get_entity_queue(&test_ctx)->entity_cnt == 0);
      VERIFY(o1heapGetDiagnostics(_tact_get_init(&test_ctx).heap).allocated == 0);

      char topic[] = "/demo/foo+";
      char payload[] = "short payload";
      msg.topic = topic;
      msg.payload = payload;
      msg.qos = 3;
      msg.pub_timeout = 0;
      VERIFY(tact_mdl_mqtt_publish(&test_ctx, NULL, &msg, NULL));
      topic[0] = 'X';
      payload[0] = 'X';
      tact_entity_t* entity = &_tact_get_entity_queue(&test_ctx)->entity[0];
      VERIFY(entity->item_cnt == 5);
      VERIFY(strcmp(entity->item[1].req, TACT_CMD_SAVE"/demo/foo+") == 0);
      VERIFY(strcmp(entity->item[3].req, TACT_CMD_SAVE"short payload") == 0);
      VERIFY(entity->item[4].meta.wait == 1000u);

      O1HeapInstance* heap = _tact_get_init(&test_ctx).heap;
      tact_deinit(&test_ctx);
      VERIFY(o1heapGetDiagnostics(heap).allocated == 0);
      VERIFY(!_tact_get_init(&test_ctx).init);

      VERIFY(tact_init_ex(&test_ctx, test_printf, test_write, &ring));
      msg.topic = "/demo/foo+/status";
      VERIFY(tact_mdl_mqtt_subscribe(&test_ctx, NULL, &msg, NULL));
      VERIFY(_tact_get_entity_queue(&test_ctx)->entity_cnt == 1u);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }
  } //TACT_CORE=====================================================================

  { //TACT_CHAIN====================================================================
    TEST("tact_chain_create() rejects empty names and ambiguous transition targets") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      chain_step_t empty_name[] = { TACT_CHAIN_DELAY(0) };
      empty_name[0].name = "";
      chain_step_t ambiguous_target[] = {
        TACT_CHAIN_EXEC("ROUTE", "DUP", "STOP", testChainCond),
        TACT_CHAIN_DELAY(0),
        TACT_CHAIN_DELAY(0),
      };
      ambiguous_target[1].name = "DUP";
      ambiguous_target[2].name = "DUP";
      VERIFY(tact_chain_create("", empty_name, 1, &test_ctx) == NULL);
      VERIFY(tact_chain_create("empty-step", empty_name, 1, &test_ctx) == NULL);
      VERIFY(tact_chain_create("ambiguous", ambiguous_target, 3, &test_ctx) == NULL);
      VERIFY(o1heapGetDiagnostics(_tact_get_init(&test_ctx).heap).allocated == 0);
      tact_deinit(&test_ctx);
    }

    TEST("TACT_CHAIN retry limit counts total attempts including the first") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      deferred_chain_cb = NULL;
      deferred_chain_meta = NULL;
      deferred_function_calls = 0;
      chain_step_t steps[] = {
        TACT_CHAIN("ASYNC", "STOP", "STOP", testDeferredChainFunc, NULL, NULL, NULL, 3),
      };
      tact_chain_t* chain = tact_chain_create("retry", steps, 1, &test_ctx);
      VERIFY(chain != NULL);
      VERIFY(tact_chain_start(chain));
      VERIFY(tact_chain_run(chain));
      VERIFY(deferred_function_calls == 1u && deferred_chain_cb != NULL);
      deferred_chain_cb(false, deferred_chain_meta, NULL);
      VERIFY(tact_chain_run(chain));
      VERIFY(tact_chain_run(chain));
      VERIFY(deferred_function_calls == 2u && deferred_chain_cb != NULL);
      deferred_chain_cb(false, deferred_chain_meta, NULL);
      VERIFY(tact_chain_run(chain));
      VERIFY(tact_chain_run(chain));
      VERIFY(deferred_function_calls == 3u && deferred_chain_cb != NULL);
      deferred_chain_cb(false, deferred_chain_meta, NULL);
      VERIFY(!tact_chain_run(chain));
      VERIFY(!tact_chain_is_running(chain));
      VERIFY(deferred_function_calls == 3u);
      VERIFY(tact_chain_destroy_ex(chain));
      tact_deinit(&test_ctx);
    }

    TEST("tact_chain_create() allocation failure preserves core context") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      chain_step_t steps[256];
      memset(steps, 0, sizeof(steps));
      for(uint16_t i = 0; i < 256; ++i) {
        steps[i].type = TACT_CHAIN_STEP_DELAY;
        steps[i].name = "DELAY";
      }

      VERIFY(tact_chain_create("too-large", steps, 256, &test_ctx) == NULL);
      VERIFY(_tact_get_init(&test_ctx).init);
      VERIFY(o1heapGetDiagnostics(_tact_get_init(&test_ctx).heap).allocated == 0);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_chain_delay() observes elapsed 10 ms ticks") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      chain_step_t steps[] = { TACT_CHAIN_DELAY(30) };
      tact_chain_t* chain = tact_chain_create("delay", steps, 1, &test_ctx);
      VERIFY(chain != NULL);
      VERIFY(tact_chain_start(chain));
      VERIFY(tact_chain_run(chain));
      VERIFY(tact_chain_get_current_step(chain) == 0);

      _tact_core_proc(&test_ctx);
      _tact_core_proc(&test_ctx);
      VERIFY(tact_chain_run(chain));
      VERIFY(tact_chain_get_current_step(chain) == 0);

      _tact_core_proc(&test_ctx);
      VERIFY(tact_chain_run(chain));
      VERIFY(tact_chain_get_current_step(chain) == 1);
      VERIFY(!tact_chain_run(chain));
      VERIFY(!tact_chain_is_running(chain));

      tact_chain_destroy(chain);
      VERIFY(o1heapGetDiagnostics(_tact_get_init(&test_ctx).heap).allocated == 0);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_chain_create() simple straight chain") {
      char parce_buffer[2048] = "\r\n+TEST: 523566, text\r\n";
      uint16_t parce_buffer_tail = 0;
      uint16_t parce_buffer_head = strlen(parce_buffer);

      tact_ring_buffer_t ring = {
        .buffer = (uint8_t*)parce_buffer,
        .count = parce_buffer_head - parce_buffer_tail,
        .head = parce_buffer_head,
        .tail = parce_buffer_tail,
        .size = 2048,
      };
      tact_init(&test_ctx, test_printf, test_write, &ring);

      chain_step_t server_steps[] = 
      {   
        TACT_CHAIN("MODEM INIT",        "NEXT", "MODEM RESET", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
        TACT_CHAIN("GPRS INIT",         "NEXT", "MODEM RESET", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
        TACT_CHAIN("SOCKET CONFIG",     "NEXT", "GPRS DEINIT", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
        TACT_CHAIN("SOCKET CONNECT",    "NEXT", "GPRS DEINIT", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
        TACT_CHAIN("SOCKET DISCONNECT", "NEXT", "MODEM RESET", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
        TACT_CHAIN("GPRS DEINIT",       "NEXT", "MODEM RESET", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
        TACT_CHAIN("MODEM RESET",       "NEXT", "MODEM RESET", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
      };
      tact_chain_t* chain = tact_chain_create("TCP", server_steps, sizeof(server_steps)/sizeof(chain_step_t), &test_ctx);
      VERIFY(chain->step_count == sizeof(server_steps)/sizeof(chain_step_t));
      tact_chain_start(chain);
      VERIFY(chain->is_running);
      while(tact_chain_is_running(chain))
      {
        bool res = tact_chain_run(chain);
        if(tact_chain_is_running(chain)) VERIFY(res);
        _tact_core_proc(&test_ctx);
      }
      tact_chain_destroy(chain);
      tact_deinit(&test_ctx);
      VERIFY(!_tact_get_init(&test_ctx).init);
    }

    TEST("tact_chain_create() one loop") {
      char parce_buffer[2048] = "\r\n+TEST: 523566, text\r\n";
      uint16_t parce_buffer_tail = 0;
      uint16_t parce_buffer_head = strlen(parce_buffer);

      tact_ring_buffer_t ring = {
        .buffer = (uint8_t*)parce_buffer,
        .count = parce_buffer_head - parce_buffer_tail,
        .head = parce_buffer_head,
        .tail = parce_buffer_tail,
        .size = 2048,
      };
      tact_init(&test_ctx, test_printf, test_write, &ring);

      chain_step_t server_steps[] = 
      {   
        TACT_CHAIN("MODEM INIT",     "NEXT", "MODEM RESET", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
        TACT_CHAIN("GPRS INIT",      "NEXT", "MODEM RESET", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
        TACT_CHAIN("SOCKET CONFIG",  "NEXT", "GPRS DEINIT", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
        TACT_CHAIN("SOCKET CONNECT", "NEXT", "GPRS DEINIT", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),

        TACT_CHAIN_LOOP_START(5), 
          TACT_CHAIN("MODEM RTD",          "NEXT", "SOCKET DISCONNECT", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
          TACT_CHAIN("SOCKET SEND RECIVE", "NEXT", "SOCKET DISCONNECT", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
        TACT_CHAIN_LOOP_END,

        TACT_CHAIN("SOCKET DISCONNECT", "NEXT", "MODEM RESET", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
        TACT_CHAIN("GPRS DEINIT",       "NEXT", "MODEM RESET", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
        TACT_CHAIN("MODEM RESET",       "NEXT", "MODEM RESET", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
      };
      tact_chain_t* chain = tact_chain_create("TCP", server_steps, sizeof(server_steps)/sizeof(chain_step_t), &test_ctx);
      VERIFY(chain->step_count == sizeof(server_steps)/sizeof(chain_step_t));
      tact_chain_start(chain);
      VERIFY(chain->is_running);
      while(tact_chain_is_running(chain))
      {
        bool res = tact_chain_run(chain);
        if(tact_chain_is_running(chain)) VERIFY(res);
      }
      tact_chain_destroy(chain);
      tact_deinit(&test_ctx);
      VERIFY(!tact_get_init(&test_ctx).init);
    }

    TEST("tact_chain_create() nested loops with delay, exec and prev") {
      char parce_buffer[2048] = "\r\n+TEST: 523566, text\r\n";
      uint16_t parce_buffer_tail = 0;
      uint16_t parce_buffer_head = strlen(parce_buffer);

      tact_ring_buffer_t ring = {
        .buffer = (uint8_t*)parce_buffer,
        .count = parce_buffer_head - parce_buffer_tail,
        .head = parce_buffer_head,
        .tail = parce_buffer_tail,
        .size = 2048,
      };
      tact_init(&test_ctx, test_printf, test_write, &ring);

      chain_step_t server_steps[] = 
      {   
        TACT_CHAIN("MODEM INIT",     "NEXT", "MODEM RESET", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
        TACT_CHAIN("GPRS INIT",      "NEXT", "MODEM RESET", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
        TACT_CHAIN("SOCKET CONFIG",  "NEXT", "GPRS DEINIT", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
        TACT_CHAIN("SOCKET CONNECT", "NEXT", "GPRS DEINIT", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),

        TACT_CHAIN_LOOP_START(3), 
            TACT_CHAIN("MODEM RTD", "NEXT", "SOCKET DISCONNECT", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
            TACT_CHAIN_LOOP_START(3), 
                TACT_CHAIN("MODEM RTD", "NEXT", "SOCKET DISCONNECT", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
                TACT_CHAIN_DELAY(50),
                TACT_CHAIN("SOCKET SEND RECIVE", "NEXT", "SOCKET DISCONNECT", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
            TACT_CHAIN_LOOP_END,
            TACT_CHAIN("SOCKET SEND RECIVE", "NEXT", "SOCKET DISCONNECT", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
        TACT_CHAIN_LOOP_END,

        TACT_CHAIN_EXEC("CHECK CONN", "NEXT", "MODEM RESET", testChainCond),
        TACT_CHAIN("SOCKET DISCONNECT", "NEXT", "PREV", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
        TACT_CHAIN("GPRS DEINIT",       "NEXT", "MODEM RESET", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
        TACT_CHAIN("MODEM RESET",       "NEXT", "MODEM RESET", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
      };

      tact_chain_t* chain = tact_chain_create("TCP", server_steps, sizeof(server_steps)/sizeof(chain_step_t), &test_ctx);
      VERIFY(chain->step_count == sizeof(server_steps)/sizeof(chain_step_t));
      tact_chain_start(chain);
      VERIFY(chain->is_running);
      while(tact_chain_is_running(chain))
      {
        bool res = tact_chain_run(chain);
        if(tact_chain_is_running(chain)) VERIFY(res);
        _tact_core_proc(&test_ctx);
      }
      tact_chain_destroy(chain);
      tact_deinit(&test_ctx);
      VERIFY(!tact_get_init(&test_ctx).init);
    }

    TEST("tact_chain_create() nested loop with backward and forward falltrough") {
      char parce_buffer[2048] = "\r\n+TEST: 523566, text\r\n";
      uint16_t parce_buffer_tail = 0;
      uint16_t parce_buffer_head = strlen(parce_buffer);

      tact_ring_buffer_t ring = {
        .buffer = (uint8_t*)parce_buffer,
        .count = parce_buffer_head - parce_buffer_tail,
        .head = parce_buffer_head,
        .tail = parce_buffer_tail,
        .size = 2048,
      };
      tact_init(&test_ctx, test_printf, test_write, &ring);

      chain_step_t server_steps[] = 
      {   
        TACT_CHAIN("1",     "4", "STOP", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
        TACT_CHAIN("2",      "3", "STOP", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),

        TACT_CHAIN_LOOP_START(3), 
            TACT_CHAIN("3", "5", "STOP", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
            TACT_CHAIN_LOOP_START(3), 
                TACT_CHAIN("4", "2", "STOP", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
            TACT_CHAIN_LOOP_END,
        TACT_CHAIN_LOOP_END,

        TACT_CHAIN_EXEC("5",  "NEXT", "STOP", testChainCond),
        TACT_CHAIN("6",       "NEXT", "PREV", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
        TACT_CHAIN("7",       "NEXT", "STOP", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
        TACT_CHAIN("8",       "NEXT", "STOP", testChainFunc, testEntityCB, test_buffer, test_buffer, 3),
      };

      tact_chain_t* chain = tact_chain_create("TCP", server_steps, sizeof(server_steps)/sizeof(chain_step_t), &test_ctx);
      VERIFY(chain->step_count == sizeof(server_steps)/sizeof(chain_step_t));
      tact_chain_start(chain);
      VERIFY(chain->is_running);
      while(tact_chain_is_running(chain))
      {
        bool res = tact_chain_run(chain);
        if(tact_chain_is_running(chain)) VERIFY(res);
      }
      VERIFY(chain->loop_stack_ptr == 0);
      tact_chain_destroy(chain);
      tact_deinit(&test_ctx);
      VERIFY(!tact_get_init(&test_ctx).init);
    }

    TEST("chain ignores stale callbacks and blocks restart/destruction while pending") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      chain_step_t steps[] = {
        TACT_CHAIN("ASYNC", "STOP", "STOP", testDeferredChainFunc,
                  testChainUserCallback, NULL, NULL, 0),
      };
      tact_chain_t* chain = tact_chain_create("ASYNC", steps, 1, &test_ctx);
      VERIFY(chain != NULL);
      VERIFY(tact_chain_start(chain));
      deferred_chain_cb = NULL;
      deferred_chain_meta = NULL;
      chain_user_callback_called = false;
      VERIFY(tact_chain_run(chain));
      VERIFY(deferred_chain_cb != NULL);
      VERIFY(chain->callback_pending);
      tact_chain_stop(chain);
      VERIFY(!tact_chain_start(chain));
      VERIFY(!tact_chain_destroy_ex(chain));

      deferred_chain_cb(true, deferred_chain_meta, NULL);
      VERIFY(chain_user_callback_called);
      VERIFY(!chain->callback_pending);
      VERIFY(tact_chain_start(chain));
      tact_chain_stop(chain);
      VERIFY(tact_chain_destroy_ex(chain));
      tact_deinit(&test_ctx);
    }

    TEST("chain may be destroyed by its asynchronous completion callback") {
      tact_init(&test_ctx, test_printf, test_write, &tact_ring_buffer);
      chain_step_t steps[] = {
        TACT_CHAIN("ASYNC DESTROY", "STOP", "STOP", testDeferredChainFunc,
                  testChainDestroyFromCallback, NULL, NULL, 0),
      };
      tact_chain_t* chain = tact_chain_create("ASYNC DESTROY", steps, 1, &test_ctx);
      VERIFY(chain != NULL);
      chain_to_destroy_from_callback = chain;
      chain_destroy_from_callback_result = false;
      VERIFY(tact_chain_start(chain));
      VERIFY(tact_chain_run(chain));
      VERIFY(deferred_chain_cb != NULL);
      deferred_chain_cb(true, deferred_chain_meta, NULL);
      VERIFY(chain_destroy_from_callback_result);
      VERIFY(chain_to_destroy_from_callback == NULL);
      tact_deinit(&test_ctx);
    }

  } //TACT_CHAIN====================================================================

} // TEST_GROUP()
