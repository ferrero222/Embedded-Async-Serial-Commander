/******************************************************************************
 *              _____      _       ____   _____  ======                      *
 *      ====== |_   _|    / \     / ___| |_   _| ======    (c)03.10.2025     *
 *      ======   | |     / _ \   | |       | |   ======        v1.0.0        *
 *      ======   | |    / ___ \  | |___    | |   ======                      *
 *      ======   |_|   /_/   \_\  \____|   |_|   ======                      *
 *                                                                           *
 ******************************************************************************/
#ifndef __TACT_CORE_H
#define __TACT_CORE_H

/*******************************************************************************
 * Include files
 ******************************************************************************/
#include <stdint.h> 
#include <stdbool.h> 
#include <string.h>
#include <assert.h>
#include <stddef.h>
#include "o1heap.h"
#include "ringslice.h"

/*******************************************************************************
 * Config
 ******************************************************************************/
#ifndef TACT_MAX_ITEMS_PER_ENTITY
  #define TACT_MAX_ITEMS_PER_ENTITY   50u
#endif
#ifndef TACT_ENTITY_QUEUE_SIZE
  #define TACT_ENTITY_QUEUE_SIZE      10u
#endif
#ifndef TACT_URC_QUEUE_SIZE
  #define TACT_URC_QUEUE_SIZE         10u
#endif
#ifndef TACT_URC_FREQ_CHECK
  #define TACT_URC_FREQ_CHECK         10u
#endif
#ifndef TACT_MEMORY_POOL_SIZE
  #define TACT_MEMORY_POOL_SIZE       4096u
#endif
#ifndef TACT_URC_PREFIX_MAX_LEN
  #define TACT_URC_PREFIX_MAX_LEN     63u
#endif
#ifndef TACT_MAX_URCS_PER_PROC
  #define TACT_MAX_URCS_PER_PROC      4u
#endif
#ifndef TACT_DEBUG_ENABLED
  #ifdef TACT_TEST
    #define TACT_DEBUG_ENABLED        0
  #else
    #define TACT_DEBUG_ENABLED        1
  #endif
#endif

#if TACT_MAX_ITEMS_PER_ENTITY < 1 || TACT_MAX_ITEMS_PER_ENTITY > UINT8_MAX
  #error "TACT_MAX_ITEMS_PER_ENTITY must fit in tact_item_t::item_cnt"
#endif
#if TACT_ENTITY_QUEUE_SIZE < 1 || TACT_ENTITY_QUEUE_SIZE > UINT8_MAX
  #error "TACT_ENTITY_QUEUE_SIZE must be in range 1..255"
#endif
#if TACT_URC_QUEUE_SIZE < 1 || TACT_URC_QUEUE_SIZE > UINT8_MAX
  #error "TACT_URC_QUEUE_SIZE must be in range 1..255"
#endif
#if TACT_URC_FREQ_CHECK < 1
  #error "TACT_URC_FREQ_CHECK must be greater than zero"
#endif
#if TACT_MAX_URCS_PER_PROC < 1 || TACT_MAX_URCS_PER_PROC > UINT8_MAX
  #error "TACT_MAX_URCS_PER_PROC must be in range 1..255"
#endif
#if TACT_URC_PREFIX_MAX_LEN < 1 || TACT_URC_PREFIX_MAX_LEN > UINT16_MAX
  #error "TACT_URC_PREFIX_MAX_LEN must fit within the ring-slice length type"
#endif
#if TACT_MEMORY_POOL_SIZE < 256
  #error "TACT_MEMORY_POOL_SIZE is too small for O1Heap"
#endif

/*******************************************************************************
 * Global pre-processor symbols/macros ('#define')
 ******************************************************************************/
#define TACT_CMD_SAVE             "<S>"
#define TACT_CMD_FORCE            "<F>"
#define TACT_CMD_CRLF             "\r\n"
#define TACT_CMD_CR               "\r"
#define TACT_CMD_LF               "\n"
#define TACT_CMD_CTRL_Z           "\x1a"
#define TACT_CMD_OK               TACT_CMD_CRLF"OK"TACT_CMD_CRLF
#define TACT_CMD_ERROR            TACT_CMD_CRLF"ERROR"TACT_CMD_CRLF

#define TACT_ITEM_SIZE            sizeof(tact_item_t)
#define TACT_URC_SIZE             sizeof(tact_urc_queue_t)

#define TACT_ITEM(req_, prefix_, parce_type_, retries_, timeout_, err_step_, ok_step_, cb_, format_, ...) \
{                                                                                                        \
  .req = req_,                                                                                           \
  .parce_type = parce_type_,                                                                             \
  .answ =                                                                                                \
  {                                                                                                      \
    .prefix = prefix_,                                                                                   \
    .format = format_,                                                                                   \
    .ptrs = (void*[]){__VA_ARGS__, TACT_NO_ARG},                                                          \
    .cb = cb_                                                                                            \
  },                                                                                                     \
  .meta =                                                                                                \
  {                                                                                                      \
    .wait = timeout_,                                                                                    \
    .rpt_cnt = retries_,                                                                                 \
    .err_step = err_step_,                                                                               \
    .ok_step = ok_step_                                                                                  \
  }                                                                                                      \
}

#define TACT_ARG(src, field) ((void*)offsetof(src, field))
#define TACT_NO_ARG          (void*)0xFFFF

#define TACT_CRITICAL_ENTER  _tact_crit_enter();
#define TACT_CRITICAL_EXIT   _tact_crit_exit();

/*******************************************************************************
 * Global type definitions ('typedef')
 ******************************************************************************/
typedef void (*answ_parce_cb_t)(ringslice_t data_slice, bool result, void* const data);
typedef void (*tact_urc_cb)(ringslice_t urc_slice);   //urc callback type

typedef uint8_t tact_parce_type_t;
enum
{
  TACT_PARCE_SIMCOM = 1,  //ECHO\r\r\nRES\r\nDATA\r\n
  TACT_PARCE_RAW,         //> RAW DATA
};

typedef struct tact_urc_queue_t{
  char* prefix;  // Char prefix to find the URC
  tact_urc_cb cb; // Callback for this URC
} tact_urc_queue_t;

typedef struct tact_item_t
{
  char* req;  //Sended request, could be string or literal
  tact_parce_type_t parce_type;
  uint8_t owned; //Internal ownership flags for copied req and prefix
  struct{
    char *prefix;        //Prefix to find in answer
    char *format;        //format for parcing answer
    void **ptrs;         //Offsets to output fields, resolved when enqueued
    answ_parce_cb_t cb;  //Callback by the end
  } answ;
  struct {
    uint16_t wait;    // wait time (up to 65535) in 10ms
    uint8_t rpt_cnt;  // repeat counter (up to 255)
    int8_t err_step;  // error step (-127 to 127)  
    int8_t ok_step;   // success step (-127 to 127)
  } meta;
} tact_item_t;

/*******************************************************************************
 * Local type definitions ('typedef')
 ******************************************************************************/
typedef void (*tact_printf_t)(const char *string);

typedef uint16_t (*tact_write_t)(uint8_t* buff, //buff where data will be written
                                uint16_t len); //len of data

typedef void (*tact_entity_cb_t)(const bool result,       //result
                                void* const meta,        //passed meta from @tact_entity_t
                                const void* const data); //data ptr from @tact_entity_t

typedef uint8_t tact_proc_states_t;
enum
{
  TACT_STATE_READ = 1,
  TACT_STATE_WRITE,
};

typedef struct {
  uint8_t *buffer;      
  uint16_t size;   
  uint16_t head;       
  uint16_t tail;       
  uint16_t count;     
} tact_ring_buffer_t;

typedef struct tact_init_t{
  tact_printf_t tact_printf;    //custom printf fucntion
  tact_write_t tact_write;      //custom write function
  O1HeapInstance* heap;       //instance for custom heap
  tact_ring_buffer_t* rx_buff; //rx ring buffer 
  bool init;                  //init flag
} tact_init_t; 

typedef struct tact_entity_t{
  tact_item_t*       item;       //list of items
  uint8_t           item_cnt;   //amount of items
  uint8_t           item_id;    //current id of executionable items
  uint16_t          timer;      //timer
  tact_entity_cb_t   cb;         //cb for item
  void*             data;       //usefull data from execution
  void*             meta;       //meta data
  uint16_t          data_size;  //usefull data size
  uint16_t          tx_offset;  //request bytes already accepted by tact_write
  uint16_t          tx_timer;   //timeout budget while a partial request is being written
  bool              tx_started; //request transmission has started
  tact_proc_states_t state;      //state
} tact_entity_t;

typedef struct tact_entity_queue_t{
  tact_entity_t entity[TACT_ENTITY_QUEUE_SIZE]; //entity queue
  uint8_t entity_head;  //entitiy head
  uint8_t entity_tail;  //entity tail
  uint8_t entity_cnt;   //entity counter
} tact_entity_queue_t;

typedef struct tact_context_t {
  tact_entity_queue_t entity_queue; //entity queue
  tact_urc_queue_t urc_queue[TACT_URC_QUEUE_SIZE]; //urc queue
  tact_init_t init_struct; //init struct
  #ifdef __cplusplus
  alignas(O1HEAP_ALIGNMENT) uint8_t mem_pool[TACT_MEMORY_POOL_SIZE];
  #else
  _Alignas(O1HEAP_ALIGNMENT) uint8_t mem_pool[TACT_MEMORY_POOL_SIZE];
  #endif
  uint32_t time;
  bool proc_active;  //guards the active entity against reentrant proc/dequeue/deinit
} tact_context_t;

/*******************************************************************************
 * Global variable definitions ('extern')
 ******************************************************************************/
/*******************************************************************************
 * Global function prototypes (definition in C source)
 ******************************************************************************/
/*******************************************************************************
 ** @brief  Init tact lib  
 ** @param  ctx        core context
 ** @param  tact_printf pointer to user func of printf
 ** @param  tact_write  pointer to user func of write to uart
 ** @param  rx_buff    struct to ring buffer
 ** @return none
 ******************************************************************************/
/* The context must be zero-initialized before its first initialization. */
bool tact_init_ex(tact_context_t* const ctx, const tact_printf_t tact_printf, const tact_write_t tact_write, tact_ring_buffer_t* rx_buff);
void tact_init(tact_context_t* const ctx, const tact_printf_t tact_printf, const tact_write_t tact_write, tact_ring_buffer_t* rx_buff);

/*******************************************************************************
 ** @brief  DeInit tact lib  
 ** @param  ctx core context
 ** @return none
 ******************************************************************************/
void tact_deinit(tact_context_t* const ctx);
/** Return false without changing the context if processing is currently active. */
bool tact_deinit_ex(tact_context_t* const ctx);

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
bool tact_entity_enqueue(tact_context_t* const ctx, const tact_item_t* const item, const uint8_t item_amount, const tact_entity_cb_t cb, uint16_t data_size, void* const meta);

/*******************************************************************************
 ** @brief  Clear first entity from the queue 
 ** @param  ctx core context
 ** @return false: some errors; true: ok
 ******************************************************************************/
bool tact_entity_dequeue(tact_context_t* const ctx);

/*******************************************************************************
 ** @brief  Function to append URC queue
 ** @param  ctx  core context
 ** @param  urc  ptr to your URC.
 ** @return true/false
 ******************************************************************************/
bool tact_urc_enqueue(tact_context_t* const ctx, const tact_urc_queue_t* const urc);

/*******************************************************************************
 ** @brief  Function to delete URC from queue
 ** @param  ctx  core context
 ** @param  prefix  prefix of your URC.
 ** @return true/false
 ******************************************************************************/
bool tact_urc_dequeue(tact_context_t* const ctx, const char* prefix);

/*******************************************************************************
 ** @brief  Function to proc TACT core proccesses. 
 ** @param  ctx  core context
 ** @return none
 ******************************************************************************/
void tact_core_proc(tact_context_t* const ctx);

/*******************************************************************************
 ** @brief Append received bytes to the core RX ring buffer.
 ** @param ctx Core context
 ** @param data Received bytes
 ** @param len Number of bytes
 ** @return true if every byte was accepted; false if input is invalid or the
 **         ring buffer does not have enough free space.
 ******************************************************************************/
bool tact_rx_push(tact_context_t* const ctx, const uint8_t* const data, uint16_t len);

/*******************************************************************************
 ** @brief  Function get time in 10ms. 
 ** @param  ctx  core context
 ** @return time
 ******************************************************************************/
uint32_t tact_get_cur_time(tact_context_t* const ctx);

/*******************************************************************************
 ** @brief  Function get init. 
 ** @param  ctx  core context
 ** @return @tact_init_t
 ******************************************************************************/
tact_init_t tact_get_init(tact_context_t* const ctx);

/*******************************************************************************
 ** @brief  Function to custom malloc. Handle the concurrent state
 ** @param  ctx  core context
 ** @param  size amount to alloc
 ** @return ptr to new memory
 ******************************************************************************/
void* tact_malloc(tact_context_t* const ctx, size_t size);

/*******************************************************************************
 ** @brief  Function to custom free. Handle the concurrent state
 ** @param  ctx  core context
 ** @param  ptr  ptr to delete
 ** @return none
 ******************************************************************************/
void tact_free(tact_context_t* const ctx, void* ptr);

#ifdef TACT_TEST
void _tact_core_proc(tact_context_t* const ctx);
int _tact_cmd_ring_parcer(tact_context_t* const ctx, const tact_entity_t* const entity, const tact_item_t* const item, const ringslice_t rs_me);
void _tact_simcom_parcer_find_rs_req(const ringslice_t* const me, ringslice_t* const rs_req, const char* const req); 
void _tact_simcom_parcer_find_rs_res(const ringslice_t* const me, const ringslice_t* const rs_req, ringslice_t* const rs_res);
void _tact_simcom_parcer_find_rs_data(const ringslice_t* const me, const ringslice_t* const rs_req, const ringslice_t* const rs_res, ringslice_t* const rs_data); 
int _tact_simcom_parcer_post_proc(tact_context_t* const ctx, const ringslice_t* const me, const ringslice_t* const rs_req, const ringslice_t* const rs_res, const ringslice_t* const rs_data, const tact_item_t* const item, const tact_entity_t* const entity); 
int _tact_string_boolean_ops(const ringslice_t* const rs_data, const char* const pattern); 
int _tact_cmd_sscanf(const ringslice_t* const rs_data, const tact_item_t* const item); 
void _tact_process_urcs(tact_context_t* const ctx, const ringslice_t* me);
tact_entity_queue_t* _tact_get_entity_queue(tact_context_t* const ctx); 
tact_urc_queue_t* _tact_get_urc_queue(tact_context_t* const ctx); 
tact_init_t _tact_get_init(tact_context_t* const ctx);
#endif

#endif //__TACT_CORE_H

