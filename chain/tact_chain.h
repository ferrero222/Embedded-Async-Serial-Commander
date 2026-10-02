/******************************************************************************
 *              _____      _       ____   _____  ======                      *
 *      ====== |_   _|    / \     / ___| |_   _| ======    (c)03.10.2025     *
 *      ======   | |     / _ \   | |       | |   ======        v1.0.0        *
 *      ======   | |    / ___ \  | |___    | |   ======                      *
 *      ======   |_|   /_/   \_\  \____|   |_|   ======                      *
 *                                                                           *
 ******************************************************************************/
#ifndef TACT_CHAIN_H
#define TACT_CHAIN_H

/*******************************************************************************
 * Include files
 ******************************************************************************/
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include "tact_core.h"

/*******************************************************************************
 * Global pre-processor symbols/macros ('#define')
 ******************************************************************************/
/**
 * @brief Create function step with success and error targets
 * @param name Step name
 * @param success_target Target step name on success (NULL for next step)
 * @param error_target Target step name on error
 * @param func Function to execute
 * @param retries Maximum total attempts, including the initial call (0 means one attempt)
 */
#define TACT_CHAIN(name_, success_target_, error_target_, func_, cb_, param_, meta_, retries_) \
{                                                                                             \
  .type = TACT_CHAIN_STEP_FUNCTION,                                                            \
  .name = name_,                                                                              \
  .action.func.function = func_,                                                              \
  .action.func.cb = cb_,                                                                      \
  .action.func.param = param_,                                                                \
  .action.func.meta = meta_,                                                                  \
  .action.func.success_target = success_target_,                                              \
  .action.func.error_target = error_target_,                                                  \
  .action.func.max_retries = retries_,                                                        \
  .state = TACT_CHAIN_STEP_IDLE,                                                               \
  .execution_count = 0,                                                                       \
}

/*******************************************************************************
 * @brief Create a synchronous EXEC step with caller-owned parameters.
 * @details The function receives the chain's TACT context and param unchanged.
 *          The parameter remains borrowed and must outlive all step executions.
 *          False may mean pending progress, depending on the chosen loop targets.
 * @param[in] name_ Step name.
 * @param[in] true_target_ Target when the function returns true.
 * @param[in] false_target_ Target when the function returns false.
 * @param[in] exec_func_ Synchronous function to execute.
 * @param[in,out] param_ Caller-owned parameters, or NULL.
 ******************************************************************************/
#define TACT_CHAIN_EXEC(name_, true_target_, false_target_, exec_func_, param_) \
{                                                                      \
  .type = TACT_CHAIN_STEP_EXEC,                                         \
  .name = name_,                                                       \
  .action.exec.function = exec_func_,                                  \
  .action.exec.param = param_,                                         \
  .action.exec.true_target = true_target_,                             \
  .action.exec.false_target = false_target_,                           \
  .state = TACT_CHAIN_STEP_IDLE,                                        \
  .execution_count = 0,                                                \
}

/**
 * @brief Create loop start step
 * @param iterations Number of iterations (0 = infinite loop)
 */
#define TACT_CHAIN_LOOP_START(iterations_) \
{                                         \
  .type = TACT_CHAIN_STEP_LOOP_START,      \
  .name = "LOOP_START",                   \
  .action.loop_count = iterations_,       \
  .state = TACT_CHAIN_STEP_IDLE,           \
  .execution_count = 0,                   \
}

/**
 * @brief Create loop end step
 */
#define TACT_CHAIN_LOOP_END         \
{                                  \
  .type = TACT_CHAIN_STEP_LOOP_END, \
  .name = "LOOP_END",              \
  .action.loop_count = 0,          \
  .state = TACT_CHAIN_STEP_IDLE,    \
  .execution_count = 0,            \
}

/**
 * @brief Create delay step
 * @param ms Delay in milliseconds
 */
#define TACT_CHAIN_DELAY(ms_)    \
{                               \
  .type = TACT_CHAIN_STEP_DELAY, \
  .name = "DELAY",              \
  .action.delay.start = 0,      \
  .action.delay.value = ms_,    \
  .state = TACT_CHAIN_STEP_IDLE, \
  .execution_count = 0,         \
}

/*******************************************************************************
 * Global type definitions ('typedef')
 ******************************************************************************/    
typedef uint8_t tact_step_exec_state_t;
enum {        
  TACT_CHAIN_STEP_IDLE,       // Step is not executing
  TACT_CHAIN_STEP_RUNNING,    // Function started, waiting for callback
  TACT_CHAIN_STEP_SUCCESS,    // Step completed successfully
  TACT_CHAIN_STEP_ERROR,      // Step completed with error
};

typedef uint8_t tact_chain_step_type_t;
enum {              
  TACT_CHAIN_STEP_FUNCTION,   // Function call with transitions
  TACT_CHAIN_STEP_EXEC,       // Exec jump
  TACT_CHAIN_STEP_LOOP_START, // Loop start
  TACT_CHAIN_STEP_LOOP_END,   // Loop end
  TACT_CHAIN_STEP_DELAY,      // Delay
};

typedef struct {
  uint32_t start_step_index;    // index of LOOP_START step
  uint32_t iteration_count;     // current loop iteration
} tact_loop_stack_item_t;

typedef struct {
  union
  {
    struct {
      bool (*function)(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta); // Function to execute
      tact_entity_cb_t cb;          // Callback for function
      void* param;                 // Pass params to function
      void* meta;                  // meta for function executing
      const char *success_target;  // Target step on success
      const char *error_target;    // Target step on error
      uint8_t max_retries;         // Maximum total attempts, including the initial call (0 means one)
    } func;   
    struct {   
      bool (*function)(tact_context_t* const ctx, void* const param); // Synchronous EXEC function
      void *param;                // Borrowed mutable EXEC parameters
      const char *true_target;     // Target step when true
      const char *false_target;    // Target step when false
    } exec;
    struct {   
      uint32_t start;              // Start moment
      uint32_t value;              // Delay in milliseconds
      bool started;                // Distinguishes a start time of zero from idle
    } delay;    
    uint8_t loop_count;            // Loop iterations (0 = infinite)
  } action;      
  const char *name;                // Step name for identification
  tact_chain_step_type_t type;      // Step type           
  tact_step_exec_state_t state;     // Current execution state
  uint8_t execution_count;         // Number of execution attempts
} chain_step_t;

typedef struct tact_chain_t {
  const char *name;                   // Chain name
  chain_step_t *steps;                // Array of steps 
  uint32_t loop_stack_ptr;            // Loop stack pointer
  uint32_t step_count;                // Number of steps
  uint32_t current_step;              // Current step index
  uint32_t loop_stack_size;           // Maximum loop stack size
  tact_loop_stack_item_t* loop_stack;  // Loop stack for nested loops
  tact_context_t* ctx;                 // Core context
  bool is_running;                    // Chain execution flag
  uint32_t pending_step;              // Step index awaiting its single async callback
  bool callback_pending;              // Prevents reset/restart/destroy while callback is outstanding
  bool function_active;               // Protects synchronous function invocation
  bool run_active;                    // Prevents reentrant chain_run/destroy
} tact_chain_t;

/*******************************************************************************
 * Local type definitions ('typedef')
 ******************************************************************************/
/*******************************************************************************
 * Global variable definitions ('extern')
 ******************************************************************************/
/*******************************************************************************
 * Global function prototypes (definition in C source)
 ******************************************************************************/
/*******************************************************************************
 ** @brief Create a new chain with copied steps (heap allocated)
 ** @note Step function pointers, names, transition strings, and param/meta pointers
 **       remain borrowed and must outlive the chain.
 ** @param name       Chain name
 ** @param steps      Array of steps (will be copied)
 ** @param step_count Number of steps
 ** @param ctx        core context
 ** @return Pointer to created chain, NULL on error
 *******************************************************************************/
tact_chain_t* tact_chain_create(const char* const name, const chain_step_t* const steps, const uint32_t step_count, tact_context_t* const ctx);

/*******************************************************************************
 ** @brief  Destroy chain and all resources
 ** @param  chain Chain to destroy
 ** @retval none
 *******************************************************************************/
void tact_chain_destroy(tact_chain_t* const chain);
bool tact_chain_destroy_ex(tact_chain_t* const chain);

/*******************************************************************************
 ** @brief Start chain execution
 ** @param chain Chain to start
 ** @return true if started successfully, false otherwise
 *******************************************************************************/
bool tact_chain_start(tact_chain_t* const chain);

/*******************************************************************************
 ** @brief Stop chain execution
 ** @param chain Chain to stop
 ** @retval none
 *******************************************************************************/
void tact_chain_stop(tact_chain_t* const chain);

/*******************************************************************************
 ** @brief Reset chain state (steps, counters, etc.)
 ** @param chain Chain to reset
 ** @retval none
 *******************************************************************************/
void tact_chain_reset(tact_chain_t* const chain);

/*******************************************************************************
 ** @brief Execute one step of the chain (non-blocking)
 ** @param chain Chain to execute
 ** @return true if chain should continue, false if completed or error
 *******************************************************************************/
bool tact_chain_run(tact_chain_t* const chain);

/*******************************************************************************
 ** @brief Check if chain is currently running
 ** @param chain Chain to check
 ** @return true if running, false otherwise
 *******************************************************************************/
bool tact_chain_is_running(const tact_chain_t* const chain);

/*******************************************************************************
 ** @brief Get current step index
 ** @param chain Chain
 ** @return Current step index
 *******************************************************************************/
uint32_t tact_chain_get_current_step(const tact_chain_t* const chain);

/*******************************************************************************
 ** @brief Get current step name
 ** @param chain Chain
 ** @return Current step name or NULL if not available
 *******************************************************************************/
const char* tact_chain_get_current_step_name(const tact_chain_t* const chain);

#endif // TACT_CHAIN_H
