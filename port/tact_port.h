/******************************************************************************
 *                              _    ____   ____                              *
 *                   ======    / \  / ___| / ___| ======       (c)03.10.2025  *
 *                   ======   / _ \ \___ \| |     ======           v1.0.0     *
 *                   ======  / ___ \ ___) | |___  ======                      *
 *                   ====== /_/   \_\____/ \____| ======                      *  
 *                                                                            *
 ******************************************************************************/
#ifndef __TACT_PORT_H
#define __TACT_PORT_H

/*******************************************************************************
 * Include files
 ******************************************************************************/
#include "ringslice.h"
#include <stdint.h> 
#include <stdbool.h> 
#include <string.h>
#include <assert.h>
#include "dbc_assert.h"
#include "tact_core.h"

#ifndef TACT_PORT_ENTER_CRITICAL
  #define TACT_PORT_ENTER_CRITICAL() ((void)0)
#endif
#ifndef TACT_PORT_EXIT_CRITICAL
  #define TACT_PORT_EXIT_CRITICAL() ((void)0)
#endif

/*******************************************************************************
 * Global pre-processor symbols/macros ('#define')
 ******************************************************************************/
#if TACT_DEBUG_ENABLED
  #define TACT_DEBUG(ctx, fmt, ...) tact_printf_safe(ctx, fmt, __VA_ARGS__)
#else
  #define TACT_DEBUG(ctx, fmt, ...) ((void)0)
#endif

/*******************************************************************************
 * Global type definitions ('typedef')
 ******************************************************************************/
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
 ** @brief  DBC fault override
 ** @param  none
 ** @return none
 ******************************************************************************/
DBC_NORETURN void DBC_fault_handler(char const* module, int label); 

/*******************************************************************************
 ** @brief  Platform hook to enter into a critical section
 ** @param  none
 ** @return none
 ******************************************************************************/
void _tact_crit_enter(void); 

/*******************************************************************************
 ** @brief  Platform hook to exit from a critical section
 ** @param  none
 ** @return none
 ******************************************************************************/
void _tact_crit_exit(void);

/*******************************************************************************
 ** @brief  Printf
 ** @param  none
 ** @return none
 ******************************************************************************/
void tact_printf_safe(tact_context_t* const ctx, const char *fmt, ...);

/*******************************************************************************
 ** @brief  Printf
 ** @param  none
 ** @return none
 ******************************************************************************/
void tact_printf_from_ring(tact_context_t* const ctx, ringslice_t rs_me, char* text);

#endif
