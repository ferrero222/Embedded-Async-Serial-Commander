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
#include "asc_port.h"
#include "asc_core.h"

#ifndef ASC_TEST
  #include <stdarg.h>
  #include <stdio.h>
#endif

/*******************************************************************************
 * Config
 ******************************************************************************/
/*******************************************************************************
 * Global pre-processor symbols/macros ('#define')
 ******************************************************************************/
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
DBC_NORETURN void DBC_fault_handler(char const* module, int label)
{
  (void)module;
  (void)label;
  for(;;) { /* Override or replace this port for a target-specific safe state. */ }
}

/*******************************************************************************
 ** @brief  Weak function to enter into critical section
 ** @param  none
 ** @return none
 ******************************************************************************/
#ifndef ASC_TEST
static void asc_crit_enter(void)
{
  ASC_PORT_ENTER_CRITICAL();
}

/*******************************************************************************
 ** @brief  Weak function to exit critical section
 ** @param  none
 ** @return none
 ******************************************************************************/
static void asc_crit_exit(void)
{
  ASC_PORT_EXIT_CRITICAL();
}
#endif

/*******************************************************************************
 ** @brief  Printf
 ** @param  none
 ** @return none
 ******************************************************************************/
#ifndef ASC_TEST
void asc_printf_safe(asc_context_t* const ctx, const char *fmt, ...) 
{
  if(!ctx || !fmt) return;
  asc_init_t atl = asc_get_init(ctx); 
  if(!atl.asc_printf) return;

  va_list args;
  va_start(args, fmt);
  char buffer[512];
  int formatted_len = vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  if(formatted_len < 0) return;

  size_t input_len = 0;
  while(input_len < sizeof(buffer) && buffer[input_len] != '\0') ++input_len;
  bool truncated = (size_t)formatted_len >= sizeof(buffer);
  char escaped[sizeof(buffer) * 4u + 8u];
  size_t escaped_pos = 0;
  static const char hex[] = "0123456789ABCDEF";
  for(size_t i = 0; i < input_len; ++i)
  {
    uint8_t c = (uint8_t)buffer[i];
    const char* replacement = NULL;
    switch(c)
    {
      case '\r': replacement = "\\r"; break;
      case '\n': replacement = "\\n"; break;
      case '\t': replacement = "\\t"; break;
      case '\\': replacement = "\\\\"; break;
      default: break;
    }
    if(replacement)
    {
      escaped[escaped_pos++] = replacement[0];
      escaped[escaped_pos++] = replacement[1];
    }
    else if(c >= 32u && c <= 126u)
    {
      escaped[escaped_pos++] = (char)c;
    }
    else
    {
      escaped[escaped_pos++] = '\\';
      escaped[escaped_pos++] = 'x';
      escaped[escaped_pos++] = hex[c >> 4];
      escaped[escaped_pos++] = hex[c & 0x0fu];
    }
  }
  if(truncated)
  {
    escaped[escaped_pos++] = '.';
    escaped[escaped_pos++] = '.';
    escaped[escaped_pos++] = '.';
  }
  escaped[escaped_pos++] = '\n';
  escaped[escaped_pos] = '\0';
  atl.asc_printf(escaped);
}

/*******************************************************************************
 ** @brief  Printf
 ** @param  none
 ** @return none
 ******************************************************************************/
void asc_printf_from_ring(asc_context_t* const ctx, ringslice_t rs_me, char* text)
{
  int data_len = ringslice_len(&rs_me);
  int wrap_len = (rs_me.first + data_len > rs_me.buf_size) ? rs_me.first + data_len - rs_me.buf_size : 0;
  int first_len = data_len - wrap_len;
  ASC_DEBUG(ctx, "[ASC][INFO] %s %.*s%.*s", text, first_len, &rs_me.buf[rs_me.first], wrap_len, &rs_me.buf[0]);
}

#endif

/*******************************************************************************
 ** @brief  Critical handlers
 ** @param  none
 ** @return none
 ******************************************************************************/
#ifndef ASC_TEST
static volatile uint32_t asc_crit_counter = 0;
#endif

void _asc_crit_enter(void) 
{ 
  #ifndef ASC_TEST
  if(asc_crit_counter == 0) asc_crit_enter();
  if(asc_crit_counter < UINT32_MAX) ++asc_crit_counter;
  #endif
}

void _asc_crit_exit(void)  
{
  #ifndef ASC_TEST
  if(asc_crit_counter > 0) 
  {
    --asc_crit_counter;
    if(asc_crit_counter == 0) asc_crit_exit();
  }
  #endif
}
