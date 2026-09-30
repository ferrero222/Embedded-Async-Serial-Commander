[English](readme.md) | [Russian](readme.ru.md)

# eASC(Embedded Async Serial Commander) documentation

The framework offers a constructor and methods for easily creating any single or grouped commands, creating and embedding automatic parsers, and building logical execution chains of grouped commands to implement various interaction scenarios with any external device, using any interfaces without being tied to hardware implementation.

## 1. Features and Properties

*   **Portability**: Completely independent of hardware and platform. The framework implements only pure logic.
*   **Asynchronicity**: No system blocking or waiting.
*   **Command Management**: API for creating and sending command groups, building complex logical chains.
*   **Flexibility**: Ability to create any type of command with various parameter combinations.
*   **Memory**: Use of a custom dynamic memory allocator with alignment support and O1heap fragmentation protection. [GitHub](https://github.com/pavel-kirienko/o1heap)
*   **Error Handling**: Design by Contract (DBC) for robust error checking. [GitHub](https://github.com/QuantumLeaps/DBC-for-embedded-C)
*   **Concurrency hooks**: Configurable critical sections for short shared-state operations; defaults are single-thread only.
*   **URC Support**: URC handling for asynchronous events.
*   **Testing**: Embedded tests from QLP. [GitHub](https://github.com/QuantumLeaps/Embedded-Test)
*   **Slices**: Use of ring slices instead of direct buffer copying. [GitHub](https://github.com/ferrero222/ringslice/tree/dev)
*   **Modularity**: A set of offered, extensible, ready-made modules of grouped commands for specific external devices.
*   **Extensibility**: Ability to adapt and integrate new parsers to support any command format.
*   **Context Awareness**: Ability to create multiple library contexts simultaneously for parallel work with several devices.

The host test suite exercises the parser, queue, modem modules, and chains. Hardware behavior still needs validation against the exact modem model and firmware used by an application.

## 2. Configuration and Prerequisites

### Basic Configuration

Allocate the backing bytes and initialize the library's ring-buffer descriptor. Keep the context zero-initialized before its first initialization:

```c
uint8_t rx_bytes[512];
asc_ring_buffer_t rx = {
  .buffer = rx_bytes,
  .size = sizeof(rx_bytes),
  .head = 0,
  .tail = 0,
  .count = 0,
};
```
Next, define the context globally:

```c
asc_context_t ctx = {0};
```

Initialize this context:

```c
if (!asc_init_ex(&ctx, your_printf_function, your_write_function, &rx)) {
  /* Invalid callbacks, ring-buffer state, or context state. */
}
```

### Configuration Parameters

The default critical-section hooks are no-ops and are suitable only when one serialized application context owns the library. If the UART receive path or another execution context shares the ring/queue state, define a project port header and provide short, nestable hooks:

```c
/* my_asc_port_config.h */
#define ASC_PORT_ENTER_CRITICAL() platform_irq_lock()
#define ASC_PORT_EXIT_CRITICAL()  platform_irq_unlock()
```

In the file `asc_core.h`, you can configure parameters:

```c
#define ASC_MAX_ITEMS_PER_ENTITY  50     //Max amount of AT cmds in one group
#define ASC_ENTITY_QUEUE_SIZE     10     //Max amount of groups
#define ASC_URC_QUEUE_SIZE        10     //Amount of handled URC
#define ASC_MEMORY_POOL_SIZE      4096   //Memory pool for custom heap
#define ASC_URC_FREQ_CHECK        10     //Check urc each ASC_URC_FREQ_CHECK*10ms

/* These macros can be overridden by the application before including the header. */
#define ASC_DEBUG_ENABLED         0
```

The main processing function must be called for each context every 10ms in the system timer:

```c
void task_10ms(void) {
  asc_core_proc(&ctx1);
  asc_core_proc(&ctx2);
  ...
}
```

## 3. Commands

The file `asc_core.h` presents the API for working with commands and the library core itself, containing:

*   `asc_entity_enqueue`
*   `asc_entity_dequeue`
*   `asc_urc_enqueue`
*   `asc_urc_dequeue`
*   `asc_core_proc`
*   `asc_get_init`
*   `asc_get_cur_time`
*   `asc_malloc`
*   `asc_free`

For more details about the functions and their parameters, see the file itself. Let's look at some examples of creating and using commands.

### Example 1, grouped AT commands for SIMCOM without response formatting

```c
asc_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
{
  ASC_ITEM("AT+CIPMODE?"ASC_CMD_CRLF,        "+CIPMODE: 0", ASC_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, ASC_NO_ARG),
  ASC_ITEM("AT+CIPMODE=0"ASC_CMD_CRLF,                NULL, ASC_PARCE_SIMCOM, 10, 100, 0, 1, NULL, NULL, ASC_NO_ARG),
  ASC_ITEM("AT+CIPMUX?"ASC_CMD_CRLF,          "+CIPMUX: 0", ASC_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, ASC_NO_ARG),
  ASC_ITEM("AT+CIPMUX=0"ASC_CMD_CRLF,                 NULL, ASC_PARCE_SIMCOM, 30, 100, 0, 1, NULL, NULL, ASC_NO_ARG),
  ASC_ITEM("AT+CIPSTATUS"ASC_CMD_CRLF,   "STATE: IP START", ASC_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, ASC_NO_ARG),
  ASC_ITEM("AT+CSTT=\"\",\"\",\"\""ASC_CMD_CRLF,      NULL, ASC_PARCE_SIMCOM, 10, 100, 0, 1, NULL, NULL, ASC_NO_ARG),
  ASC_ITEM("AT+CIPSTATUS"ASC_CMD_CRLF, "STATE: IP GPRSACT", ASC_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, ASC_NO_ARG),
  ASC_ITEM("AT+CIICR"ASC_CMD_CRLF,                    NULL, ASC_PARCE_SIMCOM, 30, 100, 0, 1, NULL, NULL, ASC_NO_ARG),
  ASC_ITEM("AT+CIPSTATUS"ASC_CMD_CRLF, "STATE: IP GPRSACT", ASC_PARCE_SIMCOM,  3, 100, 0, 1, NULL, NULL, ASC_NO_ARG),
  ASC_ITEM("AT+CIFSR"ASC_CMD_CRLF,           ASC_CMD_FORCE, ASC_PARCE_SIMCOM, 10, 100, 0, 1, NULL, NULL, ASC_NO_ARG),
  ASC_ITEM("AT+CIPHEAD?"ASC_CMD_CRLF,        "+CIPHEAD: 1", ASC_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, ASC_NO_ARG),
  ASC_ITEM("AT+CIPHEAD=1"ASC_CMD_CRLF,                NULL, ASC_PARCE_SIMCOM, 10, 100, 0, 1, NULL, NULL, ASC_NO_ARG),
  ASC_ITEM("AT+CIPSRIP?"ASC_CMD_CRLF,        "+CIPSRIP: 1", ASC_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, ASC_NO_ARG),
  ASC_ITEM("AT+CIPSRIP=1"ASC_CMD_CRLF,                NULL, ASC_PARCE_SIMCOM, 10, 100, 0, 1, NULL, NULL, ASC_NO_ARG),
  ASC_ITEM("AT+CIPSHOWTP?"ASC_CMD_CRLF,    "+CIPSHOWTP: 1", ASC_PARCE_SIMCOM,  1, 100, 1, 0, NULL, NULL, ASC_NO_ARG),
  ASC_ITEM("AT+CIPSHOWTP=1"ASC_CMD_CRLF,              NULL, ASC_PARCE_SIMCOM, 10, 100, 0, 0, NULL, NULL, ASC_NO_ARG),
};
if(!asc_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
```

### Example 2, grouped AT commands for SIMCOM with response formatting

```c
asc_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
{
  ASC_ITEM("AT+GSN"ASC_CMD_CRLF,       NULL, ASC_PARCE_SIMCOM, 10, 100, 0, 1, NULL,               "%15[^\x0d]", ASC_ARG(asc_mdl_rtd_t, modem_imei)),
  ASC_ITEM("AT+GMM"ASC_CMD_CRLF,       NULL, ASC_PARCE_SIMCOM, 10, 100, 0, 1, NULL,               "%15[^\x0d]", ASC_ARG(asc_mdl_rtd_t, modem_id)),
  ASC_ITEM("AT+GMR"ASC_CMD_CRLF,       NULL, ASC_PARCE_SIMCOM, 10, 100, 0, 1, NULL,      "Revision:%29[^\x0d]", ASC_ARG(asc_mdl_rtd_t, modem_rev)),
  ASC_ITEM("AT+CCLK?"ASC_CMD_CRLF,  "+CCLK", ASC_PARCE_SIMCOM, 10, 100, 0, 1, NULL,      "+CCLK: \"%21[^\"]\"", ASC_ARG(asc_mdl_rtd_t, modem_clock)),
  ASC_ITEM("AT+CCID"ASC_CMD_CRLF,      NULL, ASC_PARCE_SIMCOM, 10, 100, 0, 1, NULL,               "%21[^\x0d]", ASC_ARG(asc_mdl_rtd_t, sim_iccid)),
  ASC_ITEM("AT+COPS?"ASC_CMD_CRLF,  "+COPS", ASC_PARCE_SIMCOM, 20, 100, 0, 1, NULL, "+COPS: 0, 0,\"%49[^\"]\"", ASC_ARG(asc_mdl_rtd_t, sim_operator)),
  ASC_ITEM("AT+CSQ"ASC_CMD_CRLF,     "+CSQ", ASC_PARCE_SIMCOM, 10, 100, 0, 1, NULL,                 "+CSQ: %d", ASC_ARG(asc_mdl_rtd_t, sim_rssi)),
  ASC_ITEM("AT+CENG=3"ASC_CMD_CRLF,    NULL, ASC_PARCE_SIMCOM, 10, 100, 0, 1, NULL,                       NULL, ASC_NO_ARG),
  ASC_ITEM("AT+CENG?"ASC_CMD_CRLF,     NULL, ASC_PARCE_SIMCOM, 10, 100, 0, 0, asc_mdl_general_ceng_cb,    NULL, ASC_NO_ARG),
};
if(!asc_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, sizeof(asc_mdl_rtd_t), meta)) return false;
```

### Command Parameters

Let's examine each specified command parameter individually and understand what happens:

*   **ASC_ITEM** - Macro for filling the structure, use it to add a command to a group.
*   **[REQ]** - The request itself, consisting of the concatenation of the command and `ASC_CMD_CRLF`. In this case, it's a string literal, but it can also be a character array.
*   **[PREFIX]** - String literal to search for in the response. Can be omitted.
*   **[PARCE_TYPE]** - Type of parser used, read below.
*   **[FORMAT]** - Response parsing format for SSCANF, used together with `VA_ARGS`. Retrieved data will be assembled according to this format, placed into arguments, and passed to the group callback. Can be omitted.
*   **[RPT]** - Maximum command attempts, including the first send; zero fails on the first timeout.
*   **[WAIT]** - Response wait timer in 10ms units.
*   **[STEPERROR]** - In case of a command error, we can skip several steps forward or backward within the group, or do nothing. 0 terminates the entire group.
*   **[STEPOK]** - Similar to error, but here in case of success we can step to a specific command. 0 terminates the entire group.
*   **[CB]** - Callback for the command, called upon command execution result. Data obtained via the format is also passed to it. Can be omitted.
*   **[VA_ARG]** - Arguments for the format in the form of `ASC_ARG`, where we specify which structure and which field will be used to store the formatted data. Can be omitted. Maximum 6. The caller must match each format conversion to the field type and keep string widths within the field size; the core checks only that the field offset lies inside the allocated data block.

### Parameters of the asc_entity_enqueue function

```c
bool asc_entity_enqueue(asc_context_t* const ctx, const asc_item_t* const item, const uint8_t item_amount, const asc_entity_cb_t cb, uint16_t data_size, void* const meta);
```

*   **ctx** - Global context for which the group is created.
*   **item** - Command group.
*   **item_amount** - Size of the command structure.
*   **cb** - Callback for the entire group, which will be called upon execution of this group. Can be omitted.
*   **data_size** - Size of the data structure that will be created dynamically, automatically, to save data specified in the [FORMAT] and [VA_ARGS] fields. A pointer to this structure will be returned in the group callback, and after execution, its memory will be cleared. Can be omitted (0).
*   **meta** - Pointer to some additional group metadata. Passed directly to the execution callback and not processed by the library in any way. Can be omitted.

### Parameters of the callback for the entire command group:

```c
void (*asc_entity_cb_t)(bool result, void* meta, void* data);
```

*   **result** - Execution result.
*   **meta** - The same metadata we passed during creation.
*   **data** - The same data structure that was created dynamically because we specified the size, or NULL if we didn't specify anything.

### Parameters of the callback for a command:

```c
(*answ_parce_cb_t)(ringslice_t data_slice, bool result, void* data);
```

*   **data_slice** - The library works directly with the ring buffer using slices. Here, a data slice for this command is returned, if it exists. Use the ringslice library API to work with this slice.
*   **result** - Execution result.
*   **data** - Pointer to the same data structure created for formatting, if we specified it during creation.

### Peculiarities

*   If you need to get data from the response but did not pass the structure size for creation or the format, an assert will trigger during execution.
*   To retrieve useful data from the response and collect them, the library itself dynamically creates a data structure if everything was correctly specified when creating the command group, passes it to the execution callback, and then also deletes it after completion. Therefore, if the data is needed for some time, you must copy them from the callback into your static structure.
*   Sometimes the command composition is not known in advance and needs to be created in real-time on the stack or in some temporary buffer, rather than using a static string literal. Since the library works asynchronously, you must ensure that this temporary buffer exists at the moment of command execution. To avoid dealing with this each time, you can simply specify that the command should be saved to the library's memory using the `ASC_CMD_SAVE` macro, for example: `ASC_CMD_SAVE"AT+CIPMODE?"ASC_CMD_CRLF`. The same can be applied to the prefix.
*   In the [PREFIX] field, you can specify more complex constructions to check multiple lines and prefixes at once:
    *   Use `|` for OR operations: `"+CREG: 0,1|+CREG: 0,5"`
    *   Use `&` for AND operations: `"+IPD&SEND OK"`
*   Also, in the [PREFIX] field, you can specify the macro-literal `ASC_CMD_FORCE`, which indicates that this command or data should simply be sent without parsing or waiting for a response. Other fields except [STEPOK] will not be used at all, and their content can be anything.
*   The `modules` folder contains some files and implementations of ready-made AT command groups.
*   The `tests` folder contains a portable makefile. Run `make -C tests check`; use `ARCH_FLAGS=-m32` only when the toolchain has 32-bit support. Host tests use an 8 KiB pool to account for 64-bit pointer overhead; production builds keep the configurable default and must size it for their actual queue workload. `BIN_DIR` can redirect all build outputs outside the source tree.
*   The command callback receives a data slice. This is done so you can write your own data parser if the standard formatting is insufficient. An example of this can be seen in the ready-made function `asc_mdl_rtd`, where the data structure is dynamically created by the library, and in the command callback, we manually parse its data in the desired way and place them into this structure.
*   In case of processed data, the library itself moves the tail and counter of your ring buffer.
*   The `examples` folder contains usage examples.

### Parsers
`ASC_PARCE_SIMCOM`<br>
Parser for commands in SIMCOM format. This parser works with echo enabled. Usually, the response to commands comes sequentially as: ECHO → RESULT → DATA. Echo must always come first. The parser works similarly, first looking for the command echo, then the execution result, and then the useful data, i.e., it splits the response into three fields: [REQ] [RES] [DATA]. Their combination, presence, and order can vary from command to command, each differently. The internal parser handles only a few combinations and possible cases, which should be sufficient. Let's examine the parameter combinations when creating a command and what responses they support:

| Specified Command Parameters | Required Response Fields for Such Parameters |
|------------------------------|----------------------------------------------|
| [REQ] | [REQ] [RES] with OK<br>[REQ] [RES] [DATA] with OK |
| [REQ] [PREFIX] <br> [REQ] [PREFIX] [FORMAT] [ARG]| [REQ] [RES] [DATA] + prefix and format check (if specified)<br>[REQ] [DATA] + prefix and format check (if specified)<br>[DATA] + prefix and format check (if specified) |
| [PREFIX] | [DATA] + prefix check |
| [PREFIX] [FORMAT] [ARG] | [DATA] + prefix and format check |

The [DATA] field is usually framed by CRLF characters — the parser finds boundaries by them, removes them, and works with "clean" data. If [DATA] contains multi-line data with CRLF, the parser will return only the FIRST line.
For complex cases, it is recommended to write a custom parser via a callback (example: function `asc_mdl_rtd`). Use the table above to correctly create a command, knowing in advance the type of response and what fields it contains.

`ASC_PARCE_RAW`<br>
Parser for data transmission format commands. Parses raw data simply checking for a match with the [PREFIX] field if specified. Also, if the [FORMAT][ARG] field is specified, the parser will attempt to format and retrieve useful data from the very beginning of this data buffer. The data is not preprocessed but handled as-is, which must be taken into account. The [REQ] field can be omitted.

## 4. Creating Logical Chains

Based on command groups (described above), you can create your own algorithms and execution chains. An API is provided in the file `asc_chain.h`:

*   `asc_chain_destroy`
*   `asc_chain_destroy_ex`
*   `asc_chain_start`
*   `asc_chain_stop`
*   `asc_chain_reset`
*   `asc_chain_run`
*   `asc_chain_is_running`
*   `asc_chain_get_current_step`
*   `asc_chain_get_current_step_name`

### Creating Step Functions

To create a chain, each command group must be wrapped in a function of this type:

```c
bool (*function)(asc_context_t* ctx, asc_entity_cb_t cb,
                 const void* param, void* meta);
```
The function must, based on the result, execute the callback passed into it.

For example:

```c
bool asc_mdl_gprs_socket_connect(asc_context_t* const ctx, const asc_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !param) return false;
  char cipstart[320] = {0};
  const asc_mdl_gprs_server_t* tcp = (const asc_mdl_gprs_server_t*)param;
  int written = snprintf(cipstart, sizeof(cipstart), "%sAT+CIPSTART=\"%s\",\"%s\",\"%s\"%s", ASC_CMD_SAVE, tcp->mode, tcp->ip, tcp->port, ASC_CMD_CRLF);
  if(written < 0 || (size_t)written >= sizeof(cipstart)) return false;
  asc_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    ASC_ITEM("AT+CIPSTATUS"ASC_CMD_CRLF, "STATE: IP STATUS|STATE: TCP CLOSED", ASC_PARCE_SIMCOM, 10, 100,  0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM(cipstart,                           "CONNECT OK|ALREADY CONNECT", ASC_PARCE_SIMCOM,  6, 500,  0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CIPSTATUS"ASC_CMD_CRLF,                  "STATE: CONNECT OK", ASC_PARCE_SIMCOM, 10, 100,  0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CIPSEND?"ASC_CMD_CRLF,                           "+CIPSEND:", ASC_PARCE_SIMCOM, 10, 100,  0, 1, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CIPQSEND?"ASC_CMD_CRLF,                       "+CIPQSEND: 0", ASC_PARCE_SIMCOM,  1, 100,  1, 0, NULL, NULL, ASC_NO_ARG),
    ASC_ITEM("AT+CIPQSEND=0"ASC_CMD_CRLF,                                NULL, ASC_PARCE_SIMCOM, 10, 100,  0, 0, NULL, NULL, ASC_NO_ARG),
  };
  if(!asc_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
  return true;
}
```

Where:
*   **ctx** - Global context for which the group is created.
*   **cb** - Callback for the entire group.
*   **param** - Pointer to input parameters for creating the command. You must ensure that the provided structure exists all the time while this command group is executing asynchronously.
*   **meta** - Pointer to some additional group metadata. Passed directly to the execution callback.

Such functions can be used in chains or separately. Sets of ready-made functions are presented in the `modules` folder; you can use them and add your own.

### Creating a Chain

Let's create an example chain based on the modules provided in the library:

```c
chain_step_t tcp_steps[] =
{
  //Main
  ASC_CHAIN("INIT_MODEM", "NEXT", "MODEM_RESTART", asc_mdl_modem_init, NULL, NULL, NULL, 1),
  ASC_CHAIN("GPRS INIT", "NEXT", "GPRS DEINIT", asc_mdl_gprs_init, NULL, NULL, NULL, 1),
  ASC_CHAIN("SOCKET CONFIG", "NEXT", "GPRS INIT", asc_mdl_gprs_socket_config, NULL, NULL, NULL, 1),
  ASC_CHAIN("CONNECT TO SERVER", "NEXT", "SOCKET CONFIG", asc_mdl_gprs_socket_connect, NULL, &asc_server_connect, NULL, 1),
  ASC_CHAIN("GET RTD", "NEXT", "DISCONNECT FROM SERVER", asc_mdl_rtd, asc_rtd_cb, NULL, NULL, 1),
  ASC_CHAIN_EXEC("CHECK RTD", "NEXT", "GET RTD", asc_rtd_check),

  ASC_CHAIN_EXEC("CREATE WIALON LOGIN", "NEXT", "DISCONNECT FROM SERVER", asc_server_data_wialon_login),
  ASC_CHAIN("SEND WIALON LOGIN", "NEXT", "DISCONNECT FROM SERVER", asc_mdl_gprs_socket_send_recieve, NULL, &asc_server_data, NULL, 3),

  ASC_CHAIN_LOOP_START(10),
    ASC_CHAIN("GET RTD", "NEXT", "DISCONNECT FROM SERVER", asc_mdl_rtd, asc_rtd_cb, NULL, NULL, 1),
    ASC_CHAIN_EXEC("CREATE WIALON DATA", "NEXT", "DISCONNECT FROM SERVER", asc_server_data_wialon_packet),
    ASC_CHAIN("SEND WIALON DATA", "NEXT", "DISCONNECT FROM SERVER", asc_mdl_gprs_socket_send_recieve, NULL, &asc_server_data, NULL, 3),
    ASC_CHAIN_DELAY(1000),
  ASC_CHAIN_LOOP_END,

  ASC_CHAIN_EXEC("WIALON DATA CLEAN", "STOP", "HARD RESET", asc_server_data_clean),

  //Error
  ASC_CHAIN("GPRS DEINIT", "GPRS INIT", "MODEM RESTART", asc_mdl_gprs_deinit, NULL, NULL, NULL, 1),
  ASC_CHAIN("DISCONNECT FROM SERVER", "CONNECT TO SERVER", "GPRS DEINIT", asc_mdl_gprs_socket_disconnect, NULL, NULL, NULL, 1),
  ASC_CHAIN("MODEM RESTART", "GPRS INIT", "MODEM RESTART", asc_mdl_modem_reset, NULL, NULL, NULL, 1),

  //Critical
  ASC_CHAIN_EXEC("HARD RESET", "STOP", "STOP", asc_hard_reset),
};

asc_chain_t* chain = asc_chain_create("TCP", tcp_steps, sizeof(tcp_steps)/sizeof(chain_step_t), ctx);
if(!chain || !asc_chain_start(chain)) return;

while(asc_chain_is_running(chain))
{
  bool res = asc_chain_run(chain);
  if(asc_chain_is_running(chain) && !res) break;
}
/* Destroy only after any outstanding asynchronous step callback has returned. */
if(!asc_chain_destroy_ex(chain)) {
  /* Defer cleanup until no run/callback is active. */
}
```

This chain one-time configures the SIMCOM modem, context, and connection, gets real-time data, checks it, and then 10 times starts collecting and sending this data to the server with a period of 10 seconds, after preliminary authorization. In case of an error, disconnection or deinitialization occurs with an attempt to reinitialize and reconnect or a complete restart.

### Chain Parameters

So, let's look at what can be used in a chain and what parameters can be passed there:

**ASC_CHAIN** - The main macro for adding an execution step, contains:
*   **[Name]** - Step name.
*   **[Success target]** - Name of the step to go to in case of success. You can specify NULL or "NEXT" for step +1, or "PREV" for step -1, or a specific step name. "STOP" will end chain execution.
*   **[Error target]** - The same as for success, but in case of error.
*   **[Func]** - The function that will be called to execute the step.
*   **[Cb]** - Callback for the step.
*   **[Param]** - The parameters that will be passed to the specified function when it starts executing.
*   **[meta]** - The metadata that we passed during creation.
*   **[Retries]** - Maximum total attempts, including the initial call; 0 or 1 means one attempt.

**ASC_CHAIN_EXEC** - Macro for creating and executing some action, you can execute or check something. Contains:
*   **[Name]** - Step name.
*   **[True target]** - Name of the step to go to in case of success. You can specify NULL or "NEXT" for step +1, or "PREV" for step -1, or a specific step name. "STOP" will end chain execution.
*   **[False target]** - The same as for success, but in case of error.
*   **[Exec func]** - Function to execute. Must be of type `bool (*exec)(void);` Its execution result affects the next transition.

**ASC_CHAIN_LOOP_START** - Macro to indicate the start of a following loop, contains:
*   **[Iterations]** - Number of loop iterations. 0 - Infinite.

**ASC_CHAIN_LOOP_END** - Macro to indicate the end of a loop.

**ASC_CHAIN_DELAY** - Macro to create an artificial delay, contains:
*   **[ms]** - Delay time in ms.

### Parameters of the asc_entity_enqueue function

```c
asc_chain_t* asc_chain_create(const char* const name, const chain_step_t* const steps,
                              const uint32_t step_count, asc_context_t* const ctx);
```

*   **name** - Chain name.
*   **steps** - The chain structure itself.
*   **step_count** - Number of steps in the chain.

### Peculiarities

*   As can be seen from the example, nested loops can be created. The library implements and uses its own dynamic stack to work with them. There is support and the possibility of transitions through loops, from loops, into loops with or without nesting, moving both forward and backward, skipping their denoting steps. There are no restrictions, however, it should be noted that one loop iteration occurs only at the moment of executing `ASC_CHAIN_LOOP_END` for the corresponding loop. Also, ensure there are no steps with the same names; in such a case, the transition to such a step will be performed to the first one found in the array.
```
