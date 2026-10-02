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
#include "tact_chain.h"
#include "dbc_assert.h"
#include "o1heap.h"
#include <stdio.h>
#include <string.h>
#include "tact_port.h"

/*******************************************************************************
 * Local pre-processor symbols/macros ('#define')
 ******************************************************************************/
DBC_MODULE_NAME("TACT_CHAIN")

/*******************************************************************************
 * Global variable definitions (declared in header file with 'extern')
 ******************************************************************************/
/*******************************************************************************
 * Local function prototypes ('static')
 ******************************************************************************/
static bool tact_chain_step_function_proc(tact_chain_t* chain, chain_step_t* const step);
static bool tact_chain_step_exec_proc(tact_chain_t* chain, chain_step_t* const step);
static bool tact_chain_step_loop_start_proc(tact_chain_t* chain, chain_step_t* const step);
static bool tact_chain_step_loop_end_proc(tact_chain_t* chain, chain_step_t* const step);
static bool tact_chain_step_delay_proc(tact_chain_t* chain, chain_step_t* const step);
static bool tact_chain_target_exists(const chain_step_t* const steps, const uint32_t step_count, const char* const target);

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
 ** @brief  Notify chain about step completion
 **         This function should be called from AT command callbacks
 ** @param  none
 ** @return none
 ******************************************************************************/
static void tact_chain_step_cb(const bool result, void* const meta, const void* const data) 
{
  tact_chain_t* chain = (tact_chain_t*)meta;
  if(!chain || !chain->callback_pending) return;

  uint32_t completed_step = chain->pending_step;
  if(completed_step >= chain->step_count)
  {
    chain->callback_pending = false;
    chain->pending_step = UINT32_MAX;
    return;
  }
  chain_step_t* step = &chain->steps[completed_step];
  bool current = chain->is_running && chain->current_step == completed_step && step->state == TACT_CHAIN_STEP_RUNNING;
  tact_entity_cb_t user_cb = step->action.func.cb;
  void* user_meta = step->action.func.meta;
  tact_context_t* ctx = chain->ctx;
  (void)ctx;
  /* Clear before forwarding: the user callback may destroy the chain. */
  chain->callback_pending = false;
  chain->pending_step = UINT32_MAX;
  if(current)
  {
    step->state = result ? TACT_CHAIN_STEP_SUCCESS : TACT_CHAIN_STEP_ERROR;
    if(step->execution_count < UINT8_MAX) ++step->execution_count;
    TACT_DEBUG(ctx, "[TACT][INFO] Step '%s' completed with %s", step->name, result ? "SUCCESS" : "ERROR");
  }
  else
  {
    TACT_DEBUG(ctx, "[TACT][INFO] Ignoring stale completion for step %u", (unsigned)completed_step);
  }
  if(user_cb) user_cb(result, user_meta, data);
}

/******************************************************************************* 
 ** @brief  Calculate maximum loop nesting depth in chain
 ** @param  steps Array of steps
 ** @param  step_count Number of steps
 ** @return Maximum loop nesting depth
 *******************************************************************************/
static uint32_t tact_chain_cal_max_loop_depth(const chain_step_t* const steps, const uint32_t step_count) 
{
  DBC_REQUIRE(201, steps && step_count);
  uint32_t current_depth = 0;
  uint32_t max_depth = 0;
  for(uint32_t i = 0; i < step_count; i++) 
  {
    if(steps[i].type == TACT_CHAIN_STEP_LOOP_START) 
    {
      current_depth++;
      if(current_depth > max_depth) max_depth = current_depth;
    } 
    else if(steps[i].type == TACT_CHAIN_STEP_LOOP_END) 
    {
      if(current_depth > 0) current_depth--;
    }
  }
  if(max_depth > UINT32_MAX - 2u) return 0;
  return max_depth ? (max_depth + 2u) : 3u;
}

/*******************************************************************************
** @brief Validates an array of chain steps for structural and logical correctness
** @param steps Pointer to the array of execution chain steps
** @param step_count Total number of steps in the array
** @return true - all steps are valid, targets exist, and loops are balanced,
**         false - validation failed or structural error detected
******************************************************************************/
static bool tact_chain_validate_steps(const chain_step_t* const steps, const uint32_t step_count)
{
  uint32_t loop_depth = 0;
  for(uint32_t i = 0; i < step_count; ++i)
  {
    const chain_step_t* step = &steps[i];
    if(!step->name || !step->name[0] || strcmp(step->name, "STOP") == 0 || strcmp(step->name, "NEXT") == 0 || strcmp(step->name, "PREV") == 0) return false;
    switch(step->type)
    {
      case TACT_CHAIN_STEP_FUNCTION:
           if(!step->action.func.function ||
              !tact_chain_target_exists(steps, step_count, step->action.func.success_target) ||
              !tact_chain_target_exists(steps, step_count, step->action.func.error_target)
           ){
             return false;
           }
           break;
      case TACT_CHAIN_STEP_EXEC:
           if(!step->action.exec.function ||
              !tact_chain_target_exists(steps, step_count, step->action.exec.true_target) ||
              !tact_chain_target_exists(steps, step_count, step->action.exec.false_target)
           ){ 
             return false;
           }
           break;
      case TACT_CHAIN_STEP_LOOP_START:
           ++loop_depth;
           break;
      case TACT_CHAIN_STEP_LOOP_END:
           if(!loop_depth) return false;
           --loop_depth;
           break;
      case TACT_CHAIN_STEP_DELAY:
           break;
      default:
           return false;
    }
  }
  return loop_depth == 0;
}

/** 
 * @brief Chain step function proc
 */
static bool tact_chain_target_exists(const chain_step_t* const steps, const uint32_t step_count, const char* const target)
{
  if(!target || strcmp(target, "STOP") == 0 || strcmp(target, "NEXT") == 0 || strcmp(target, "PREV") == 0) return true;
  uint32_t matches = 0;
  for(uint32_t i = 0; i < step_count; ++i)
  {
    if(steps[i].name && strcmp(steps[i].name, target) == 0)
    {
      ++matches;
      if(matches > 1u) return false;
    }
  }
  return matches == 1u;
}

/*******************************************************************************
 ** @brief  Find step index by name
 ** @param  none
 ** @retval none
 *******************************************************************************/
static uint32_t tact_chain_find_step_index_by_name(const tact_chain_t* const chain, const char* const step_name) 
{
  DBC_REQUIRE(301, chain);
  DBC_REQUIRE(302, step_name);
  for(uint32_t i = 0; i < chain->step_count; i++) 
  {
    if(chain->steps[i].name && strcmp(chain->steps[i].name, step_name) == 0) return i;
  }
  TACT_DEBUG(chain->ctx, "[TACT][ERROR] Step '%s' not found!", step_name);
  return UINT32_MAX;
}

/*******************************************************************************
 ** @brief  Find step index by name
 ** @param  none
 ** @retval none
 *******************************************************************************/
static bool tact_chain_execute_step_prepare(tact_chain_t* const chain, const uint32_t target_index)
{
  if(target_index != chain->current_step) //check loops
  {
    if(target_index > chain->current_step) //forward dest
    {
      for(uint32_t i = chain->current_step; i < target_index; i++) 
      {
        if(chain->steps[i].type == TACT_CHAIN_STEP_LOOP_START)
        {
          if(chain->loop_stack_ptr >= chain->loop_stack_size) return false;
          chain->loop_stack[chain->loop_stack_ptr].start_step_index = i;
          chain->loop_stack[chain->loop_stack_ptr].iteration_count = 0;
          chain->loop_stack_ptr++;
          chain->steps[i].state = TACT_CHAIN_STEP_SUCCESS;
        }
        else if(chain->steps[i].type == TACT_CHAIN_STEP_LOOP_END)
        {
          if(!chain->loop_stack_ptr) return false;
          tact_loop_stack_item_t *current_loop = &chain->loop_stack[chain->loop_stack_ptr - 1];
          chain_step_t *loop_start = &chain->steps[current_loop->start_step_index];
          chain->loop_stack_ptr--;
          loop_start->state = TACT_CHAIN_STEP_IDLE;
          current_loop->iteration_count = 0;
        }
      }
    }
    else //backward dest
    {
      for(uint32_t i = chain->current_step; i > target_index; i--) 
      {
        if(chain->steps[i].type == TACT_CHAIN_STEP_LOOP_START)
        {
          if(chain->steps[i].state == TACT_CHAIN_STEP_IDLE) continue;
          if(!chain->loop_stack_ptr) return false;
          tact_loop_stack_item_t *current_loop = &chain->loop_stack[chain->loop_stack_ptr - 1];
          chain_step_t *loop_start = &chain->steps[current_loop->start_step_index];
          chain->loop_stack_ptr--;
          loop_start->state = TACT_CHAIN_STEP_IDLE;
          current_loop->iteration_count = 0;
        }
      }
      for(uint32_t i = 0; i < target_index; i++) 
      {
        if(chain->steps[i].type == TACT_CHAIN_STEP_LOOP_START)
        {
          if(chain->steps[i].state != TACT_CHAIN_STEP_IDLE) continue;
          if(chain->loop_stack_ptr >= chain->loop_stack_size) return false;
          chain->loop_stack[chain->loop_stack_ptr].start_step_index = i;
          chain->loop_stack[chain->loop_stack_ptr].iteration_count = 0;
          chain->loop_stack_ptr++;
          chain->steps[i].state = TACT_CHAIN_STEP_SUCCESS;
        }
      }
    } 
  }
  return true;
}

/*******************************************************************************
 ** @brief  Execute jump to target step
 ** @param  none
 ** @retval none
 *******************************************************************************/
static bool tact_chain_execute_step_jump(tact_chain_t* const chain, const char* const target_name) 
{
  if(target_name && strcmp(target_name, "STOP") == 0)
  {
    return false;
  }
  if(!target_name || strcmp(target_name, "NEXT") == 0) 
  {
    chain->current_step++;
    return true;
  }
  if(strcmp(target_name, "PREV") == 0) 
  {
    if(chain->current_step) chain->current_step--; //not first
    else return false;
    return true;
  }
  uint32_t target_index = tact_chain_find_step_index_by_name(chain, target_name);
  if(target_index == UINT32_MAX) return false;
  if(!tact_chain_execute_step_prepare(chain, target_index)) return false;
  chain->current_step = target_index;
  return true;
}

/*******************************************************************************
 ** @brief  Reset internal chain state
 ** @param  none
 ** @retval none
 *******************************************************************************/
static void tact_chain_reset_state(tact_chain_t* const chain) 
{
  DBC_REQUIRE(401, chain);
  chain->current_step = 0;
  chain->loop_stack_ptr = 0;
  for(uint32_t i = 0; i < chain->step_count; i++) //Reset all steps to initial state
  {
    chain->steps[i].state = TACT_CHAIN_STEP_IDLE;
    chain->steps[i].execution_count = 0;
    if(chain->steps[i].type == TACT_CHAIN_STEP_DELAY)
    {
      chain->steps[i].action.delay.start = 0;
      chain->steps[i].action.delay.started = false;
    }
  }
}

/*******************************************************************************
 ** @brief  Process one chain step based on current state
 ** @param  none
 ** @retval none
 *******************************************************************************/
static bool tact_chain_process_step(tact_chain_t* const chain) 
{
  DBC_REQUIRE(501, chain);
  bool res = true;
  if(chain->current_step >= chain->step_count) // Check if chain completed
  { 
    chain->is_running = false;
    TACT_DEBUG(chain->ctx, "[TACT][INFO] Chain '%s' completed", chain->name);
    res = false;
    return res;
  }
  chain_step_t *step = &chain->steps[chain->current_step];
  DBC_ASSERT(502, step);
  switch(step->type) 
  {
    case TACT_CHAIN_STEP_FUNCTION:   res = tact_chain_step_function_proc(chain, step);   break;
    case TACT_CHAIN_STEP_EXEC:       res = tact_chain_step_exec_proc(chain, step);       break;
    case TACT_CHAIN_STEP_LOOP_START: res = tact_chain_step_loop_start_proc(chain, step); break;
    case TACT_CHAIN_STEP_LOOP_END:   res = tact_chain_step_loop_end_proc(chain, step);   break;
    case TACT_CHAIN_STEP_DELAY:      res = tact_chain_step_delay_proc(chain, step);      break;
    default: 
      TACT_DEBUG(chain->ctx, "[TACT][ERROR] Unknown step type: %d", step->type);
      chain->is_running = false;
      return false;
  }
  return res;
}

/** 
 * @brief Chain step function proc
 */
static bool tact_chain_step_function_proc(tact_chain_t* chain, chain_step_t* const step)
{
  switch(step->state) 
  {
     case TACT_CHAIN_STEP_IDLE: 
          {
          TACT_DEBUG(chain->ctx, "[TACT][INFO] Starting step '%s'", step->name); // Start executing the function
          step->state = TACT_CHAIN_STEP_RUNNING;
          chain->pending_step = chain->current_step;
          chain->callback_pending = true;
          chain->function_active = true;
          bool started = step->action.func.function(chain->ctx, tact_chain_step_cb, step->action.func.param, chain);
          chain->function_active = false;
          if(!started && chain->callback_pending && chain->pending_step == chain->current_step)
          {
            chain->callback_pending = false;
            chain->pending_step = UINT32_MAX;
            TACT_DEBUG(chain->ctx, "[TACT][ERROR] Failed to start step '%s'", step->name);
            step->state = TACT_CHAIN_STEP_ERROR;
            if(step->execution_count < UINT8_MAX) ++step->execution_count;
          }
          break; 
          }
     case TACT_CHAIN_STEP_RUNNING: // Waiting for callback - do nothing this cycle
          break;
     case TACT_CHAIN_STEP_SUCCESS: 
          step->state = TACT_CHAIN_STEP_IDLE;
          step->execution_count = 0; 
          if(!tact_chain_execute_step_jump(chain, step->action.func.success_target)) // Jump to success target
          {
            chain->is_running = false;
            return false;
          }
          break;
      case TACT_CHAIN_STEP_ERROR:
          {
          uint8_t max_attempts = step->action.func.max_retries ? step->action.func.max_retries : 1u;
          TACT_DEBUG(chain->ctx, "[TACT][ERROR] Step '%s' failed (attempt %u/%u)", step->name, step->execution_count, max_attempts);
          if(step->execution_count < max_attempts)
          { 
            step->state = TACT_CHAIN_STEP_IDLE;
            TACT_DEBUG(chain->ctx, "[TACT][INFO] Retrying '%s'", step->name);
          } 
          else 
          {
            step->state = TACT_CHAIN_STEP_IDLE;
            step->execution_count = 0; 
            if(!tact_chain_execute_step_jump(chain, step->action.func.error_target)) 
            {
              chain->is_running = false;
              return false;
            }
          }
          break;
          }
      default:
          chain->is_running = false;
          return false;
  }
  return true;
}

/*******************************************************************************
 * @brief Execute one synchronous action and follow its result target.
 * @details Passes the chain's TACT context and the borrowed parameter pointer
 *          directly to the action. The action may update its parameter between
 *          loop iterations; no copy or asynchronous callback is introduced.
 * @param[in,out] chain Running chain whose current step is being executed.
 * @param[in,out] step EXEC step with its function, parameter, and targets.
 * @retval true The action ran and its transition was accepted.
 * @retval false The function is missing or the transition failed.
 ******************************************************************************/
static bool tact_chain_step_exec_proc(tact_chain_t* chain, chain_step_t* const step)
{
  if(step->action.exec.function) // Exec are executed synchronously
  {
    bool exec_result = step->action.exec.function(chain->ctx, step->action.exec.param);
    TACT_DEBUG(chain->ctx, "[TACT][INFO] Execution '%s': %s", step->name, exec_result ? "true" : "false");
    const char *target = exec_result ? step->action.exec.true_target : step->action.exec.false_target;  // Jump based on exec result
    if(!tact_chain_execute_step_jump(chain, target)) 
    {
      chain->is_running = false;
      return false;
    }
  } 
  else 
  {
    TACT_DEBUG(chain->ctx, "[TACT][INFO] Execution '%s' has no function", step->name);
    chain->is_running = false;
    return false;
  }  
  return true;
}

/** 
 * @brief Chain step loop start proc
 */
static bool tact_chain_step_loop_start_proc(tact_chain_t* chain, chain_step_t* const step)
{
  if(chain->loop_stack_ptr < chain->loop_stack_size) // Start of loop - push current position to stack
  {
    if(step->state == TACT_CHAIN_STEP_IDLE) 
    {
      chain->loop_stack[chain->loop_stack_ptr].start_step_index = chain->current_step;
      chain->loop_stack[chain->loop_stack_ptr].iteration_count = 0;
      chain->loop_stack_ptr++;
      step->state = TACT_CHAIN_STEP_SUCCESS;
      TACT_DEBUG(chain->ctx, "[TACT][INFO] Loop start, iterations: %u", step->action.loop_count);
    }
    chain->current_step++;
  } 
  else 
  {
    TACT_DEBUG(chain->ctx, "[TACT][ERROR] Loop stack overflow!", NULL);
    chain->is_running = false;
    return false;
  }
  return true;
}

/** 
 * @brief Chain step loop end proc
 */
static bool tact_chain_step_loop_end_proc(tact_chain_t* chain, chain_step_t* const step)
{
  (void)step;
  if(chain->loop_stack_ptr > 0) // End of loop - check if we should continue looping
  {
    tact_loop_stack_item_t *current_loop = &chain->loop_stack[chain->loop_stack_ptr - 1];
    chain_step_t *loop_start = &chain->steps[current_loop->start_step_index];
    current_loop->iteration_count++;

    if(loop_start->action.loop_count == 0 || current_loop->iteration_count < loop_start->action.loop_count) // 0 = infinite loop, otherwise check iteration count
    {
      chain->current_step = current_loop->start_step_index;
      TACT_DEBUG(chain->ctx, "[TACT][INFO] Loop iteration %u", current_loop->iteration_count);
    } 
    else 
    {
      chain->loop_stack_ptr--;
      loop_start->state = TACT_CHAIN_STEP_IDLE;
      current_loop->iteration_count = 0;
      chain->current_step++;
      TACT_DEBUG(chain->ctx, "[TACT][INFO] Loop completed after %u iterations", current_loop->iteration_count);
    }
  }
  else 
  {
    TACT_DEBUG(chain->ctx, "[TACT][ERROR] Loop end without start!", NULL);
    chain->is_running = false;
    return false;
  }
  return true;
}

/** 
 * @brief Chain step delay proc
 */
static bool tact_chain_step_delay_proc(tact_chain_t* chain, chain_step_t* const step)
{
  uint32_t now = tact_get_cur_time(chain->ctx);
  uint32_t ticks = step->action.delay.value / 10u + (step->action.delay.value % 10u != 0u);
  if(!step->action.delay.started)
  {
    TACT_DEBUG(chain->ctx, "[TACT][INFO] Chain step delay %d ms", step->action.delay.value);
    TACT_DEBUG(chain->ctx, "[TACT][INFO] Wait....", NULL);
    step->action.delay.start = now;
    step->action.delay.started = true;
  }
  if(!ticks || (uint32_t)(now - step->action.delay.start) >= ticks)
  {
    step->state = TACT_CHAIN_STEP_IDLE;
    step->action.delay.start = 0;
    step->action.delay.started = false;
    chain->current_step++;
  }
  return true;
}

/*******************************************************************************
 ** @brief Create a new chain with copied steps (heap allocated)
 ** @param name       Chain name
 ** @param steps      Array of steps (will be copied)
 ** @param step_count Number of steps
 ** @param ctx        core context
 ** @return Pointer to created chain, NULL on error
 *******************************************************************************/
tact_chain_t* tact_chain_create(const char* const name, const chain_step_t* const steps, const uint32_t step_count, tact_context_t* const ctx) 
{
  if(!ctx || !tact_get_init(ctx).init || !name || !name[0] || !steps || !step_count) return NULL;
  #if SIZE_MAX < UINT64_MAX
  if((uint64_t)step_count > SIZE_MAX / sizeof(*steps)) return NULL;
  #endif
  if(!tact_chain_validate_steps(steps, step_count)) return NULL;
    
  // Calculate required loop stack size based on actual loop nesting
  uint32_t required_stack_size = tact_chain_cal_max_loop_depth(steps, step_count);
  if(!required_stack_size) return NULL;
  #if SIZE_MAX < UINT64_MAX
  if((uint64_t)required_stack_size > SIZE_MAX / sizeof(tact_loop_stack_item_t)) return NULL;
  #endif
  
  // Allocate memory for chain structure
  tact_chain_t *chain = (tact_chain_t*)tact_malloc(ctx, sizeof(tact_chain_t));
  if(!chain) 
  {
    return NULL;
  }
  
  // Allocate memory for step copy (ensures steps survive function return)
  chain_step_t *steps_copy = (chain_step_t*)tact_malloc(ctx, step_count * sizeof(chain_step_t));
  if(!steps_copy) 
  {
    tact_free(ctx, chain);
    return NULL;
  }
  
  // Copy steps to heap
  memcpy(steps_copy, steps, step_count * sizeof(chain_step_t));
  
  // Initialize chain structure
  memset(chain, 0, sizeof(tact_chain_t));
  chain->name = name;
  chain->steps = steps_copy;
  chain->step_count = step_count;
  chain->loop_stack_size = required_stack_size; // Dynamic size based on actual needs
  chain->ctx = ctx;
  chain->pending_step = UINT32_MAX;

  // Allocate loop stack with calculated size
  chain->loop_stack = (tact_loop_stack_item_t*)tact_malloc(ctx, chain->loop_stack_size * sizeof(tact_loop_stack_item_t));  
  if(!chain->loop_stack) 
  {
    tact_free(ctx, chain->steps);
    tact_free(ctx, chain);
    return NULL;
  }

  memset(chain->loop_stack, 0, chain->loop_stack_size * sizeof(tact_loop_stack_item_t));

  TACT_DEBUG(
    chain->ctx, "[TACT][INFO] Created chain '%s' with %u steps, loop stack: %u, memory used: %d/%d",        
    name, step_count, required_stack_size, o1heapGetDiagnostics(tact_get_init(ctx).heap).allocated, o1heapGetDiagnostics(tact_get_init(ctx).heap).capacity
  );
  return chain;
}


/*******************************************************************************
 ** @brief  Destroy chain and all resources
 ** @param  chain Chain to destroy
 ** @retval none
 *******************************************************************************/
void tact_chain_destroy(tact_chain_t* const chain) 
{
  (void)tact_chain_destroy_ex(chain);
}

bool tact_chain_destroy_ex(tact_chain_t* const chain)
{
  if(!chain || chain->callback_pending || chain->function_active || chain->run_active) return false;
  tact_context_t* ctx = chain->ctx;
  if(!ctx || !tact_get_init(ctx).init) return false;
  TACT_DEBUG(ctx, "[TACT][INFO] Destroying chain '%s'", chain->name);
  if(chain->steps) tact_free(ctx, chain->steps);
  if(chain->loop_stack) tact_free(ctx, chain->loop_stack);
  tact_free(ctx, chain);
  return true;
}

/*******************************************************************************
 ** @brief Start chain execution
 ** @param chain Chain to start
 ** @return true if started successfully, false otherwise
 *******************************************************************************/
bool tact_chain_start(tact_chain_t* const chain) 
{
  if(!chain || chain->callback_pending || chain->function_active || chain->run_active)
    return false;
  tact_chain_reset_state(chain);
  chain->is_running = true;
  TACT_DEBUG(chain->ctx, "[TACT][INFO] Chain '%s' started", chain->name);
  return true;
}

/*******************************************************************************
 ** @brief Stop chain execution
 ** @param chain Chain to stop
 ** @retval none
 *******************************************************************************/
void tact_chain_stop(tact_chain_t* const chain) 
{
  if(!chain) return;
  chain->is_running = false;
  TACT_DEBUG(chain->ctx, "[TACT][INFO] Chain '%s' stopped", chain->name);
}

/*******************************************************************************
 ** @brief Reset chain state (steps, counters, etc.)
 ** @param chain Chain to reset
 ** @retval none
 *******************************************************************************/
void tact_chain_reset(tact_chain_t* const chain) 
{
  if(!chain || chain->callback_pending || chain->function_active || chain->run_active) return;
  tact_chain_reset_state(chain);
  TACT_DEBUG(chain->ctx, "[TACT][INFO] Chain '%s' reset", chain->name);
}

/*******************************************************************************
 ** @brief Execute one step of the chain (non-blocking)
 ** @param chain Chain to execute
 ** @return true if chain should continue, false if completed or error
 *******************************************************************************/
bool tact_chain_run(tact_chain_t* const chain) 
{
  if(!chain || !chain->is_running || chain->run_active) return false;
  chain->run_active = true;
  bool res = tact_chain_process_step(chain);
  chain->run_active = false;
  return res;
}

/*******************************************************************************
 ** @brief Check if chain is currently running
 ** @param chain Chain to check
 ** @return true if running, false otherwise
 *******************************************************************************/
bool tact_chain_is_running(const tact_chain_t* const chain) 
{
  if(!chain) return false;
  bool res = chain->is_running;
  return res;
}

/*******************************************************************************
 ** @brief Get current step index
 ** @param chain Chain
 ** @return Current step index
 *******************************************************************************/
uint32_t tact_chain_get_current_step(const tact_chain_t* const chain) 
{
  if(!chain) return 0;
  uint32_t res = chain->current_step;
  return res;
}

/*******************************************************************************
 ** @brief Get current step name
 ** @param chain Chain
 ** @return Current step name or NULL if not available
 *******************************************************************************/
const char* tact_chain_get_current_step_name(const tact_chain_t* const chain) 
{
  if(!chain || chain->current_step >= chain->step_count) return NULL;
  const char* res = chain->steps[chain->current_step].name;
  return res;
}
