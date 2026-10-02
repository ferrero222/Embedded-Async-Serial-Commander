# T.A.C.T. (Text Async Core Terminal) documentation

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
tact_ring_buffer_t rx = {
  .buffer = rx_bytes,
  .size = sizeof(rx_bytes),
  .head = 0,
  .tail = 0,
  .count = 0,
};
```
Next, define the context globally:

```c
tact_context_t ctx = {0};
```

Initialize this context:

```c
if (!tact_init_ex(&ctx, your_printf_function, your_write_function, &rx)) {
  /* Invalid callbacks, ring-buffer state, or context state. */
}
```

### Configuration Parameters

The default critical-section hooks are no-ops and are suitable only when one serialized application context owns the library. If the UART receive path or another execution context shares the ring/queue state, define a project port header and provide short, nestable hooks:

```c
/* my_tact_port_config.h */
#define TACT_PORT_ENTER_CRITICAL() platform_irq_lock()
#define TACT_PORT_EXIT_CRITICAL()  platform_irq_unlock()
```

In the file `tact_core.h`, you can configure parameters:

```c
#define TACT_MAX_ITEMS_PER_ENTITY  50     //Max amount of AT cmds in one group
#define TACT_ENTITY_QUEUE_SIZE     10     //Max amount of groups
#define TACT_URC_QUEUE_SIZE        10     //Amount of handled URC
#define TACT_MEMORY_POOL_SIZE      4096   //Memory pool for custom heap
#define TACT_URC_FREQ_CHECK        10     //Check urc each TACT_URC_FREQ_CHECK*10ms

/* These macros can be overridden by the application before including the header. */
#define TACT_DEBUG_ENABLED         0
```

The main processing function must be called for each context every 10ms in the system timer:

```c
void task_10ms(void) {
  tact_core_proc(&ctx1);
  tact_core_proc(&ctx2);
  ...
}
```

## 3. Commands

The file `tact_core.h` presents the API for working with commands and the library core itself, containing:

*   `tact_entity_enqueue`
*   `tact_entity_dequeue`
*   `tact_urc_enqueue`
*   `tact_urc_dequeue`
*   `tact_core_proc`
*   `tact_get_init`
*   `tact_get_cur_time`
*   `tact_malloc`
*   `tact_free`

For more details about the functions and their parameters, see the file itself. Let's look at some examples of creating and using commands.

### Example 1, grouped AT commands for SIMCOM without response formatting

```c
tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
{
  TACT_ITEM("AT+CIPMODE?"TACT_CMD_CRLF,        "+CIPMODE: 0", TACT_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, TACT_NO_ARG),
  TACT_ITEM("AT+CIPMODE=0"TACT_CMD_CRLF,                NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL, NULL, TACT_NO_ARG),
  TACT_ITEM("AT+CIPMUX?"TACT_CMD_CRLF,          "+CIPMUX: 0", TACT_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, TACT_NO_ARG),
  TACT_ITEM("AT+CIPMUX=0"TACT_CMD_CRLF,                 NULL, TACT_PARCE_SIMCOM, 30, 100, 0, 1, NULL, NULL, TACT_NO_ARG),
  TACT_ITEM("AT+CIPSTATUS"TACT_CMD_CRLF,   "STATE: IP START", TACT_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, TACT_NO_ARG),
  TACT_ITEM("AT+CSTT=\"\",\"\",\"\""TACT_CMD_CRLF,      NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL, NULL, TACT_NO_ARG),
  TACT_ITEM("AT+CIPSTATUS"TACT_CMD_CRLF, "STATE: IP GPRSACT", TACT_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, TACT_NO_ARG),
  TACT_ITEM("AT+CIICR"TACT_CMD_CRLF,                    NULL, TACT_PARCE_SIMCOM, 30, 100, 0, 1, NULL, NULL, TACT_NO_ARG),
  TACT_ITEM("AT+CIPSTATUS"TACT_CMD_CRLF, "STATE: IP GPRSACT", TACT_PARCE_SIMCOM,  3, 100, 0, 1, NULL, NULL, TACT_NO_ARG),
  TACT_ITEM("AT+CIFSR"TACT_CMD_CRLF,           TACT_CMD_FORCE, TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL, NULL, TACT_NO_ARG),
  TACT_ITEM("AT+CIPHEAD?"TACT_CMD_CRLF,        "+CIPHEAD: 1", TACT_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, TACT_NO_ARG),
  TACT_ITEM("AT+CIPHEAD=1"TACT_CMD_CRLF,                NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL, NULL, TACT_NO_ARG),
  TACT_ITEM("AT+CIPSRIP?"TACT_CMD_CRLF,        "+CIPSRIP: 0", TACT_PARCE_SIMCOM,  1, 100, 1, 2, NULL, NULL, TACT_NO_ARG),
  TACT_ITEM("AT+CIPSRIP=0"TACT_CMD_CRLF,                NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL, NULL, TACT_NO_ARG),
  TACT_ITEM("AT+CIPSHOWTP?"TACT_CMD_CRLF,    "+CIPSHOWTP: 1", TACT_PARCE_SIMCOM,  1, 100, 1, 0, NULL, NULL, TACT_NO_ARG),
  TACT_ITEM("AT+CIPSHOWTP=1"TACT_CMD_CRLF,              NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 0, NULL, NULL, TACT_NO_ARG),
};
if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
```

### Example 2, grouped AT commands for SIMCOM with response formatting

```c
tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
{
  TACT_ITEM("AT+GSN"TACT_CMD_CRLF,       NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL,               "%15[^\x0d]", TACT_ARG(tact_mdl_rtd_t, modem_imei)),
  TACT_ITEM("AT+GMM"TACT_CMD_CRLF,       NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL,               "%15[^\x0d]", TACT_ARG(tact_mdl_rtd_t, modem_id)),
  TACT_ITEM("AT+GMR"TACT_CMD_CRLF,       NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL,      "Revision:%29[^\x0d]", TACT_ARG(tact_mdl_rtd_t, modem_rev)),
  TACT_ITEM("AT+CCLK?"TACT_CMD_CRLF,  "+CCLK", TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL,      "+CCLK: \"%21[^\"]\"", TACT_ARG(tact_mdl_rtd_t, modem_clock)),
  TACT_ITEM("AT+CCID"TACT_CMD_CRLF,      NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL,               "%21[^\x0d]", TACT_ARG(tact_mdl_rtd_t, sim_iccid)),
  TACT_ITEM("AT+COPS?"TACT_CMD_CRLF,  "+COPS", TACT_PARCE_SIMCOM, 20, 100, 0, 1, NULL, "+COPS: 0, 0,\"%49[^\"]\"", TACT_ARG(tact_mdl_rtd_t, sim_operator)),
  TACT_ITEM("AT+CSQ"TACT_CMD_CRLF,     "+CSQ", TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL,                 "+CSQ: %d", TACT_ARG(tact_mdl_rtd_t, sim_rssi)),
  TACT_ITEM("AT+CENG=3"TACT_CMD_CRLF,    NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 1, NULL,                       NULL, TACT_NO_ARG),
  TACT_ITEM("AT+CENG?"TACT_CMD_CRLF,     NULL, TACT_PARCE_SIMCOM, 10, 100, 0, 0, tact_mdl_general_ceng_cb,    NULL, TACT_NO_ARG),
};
if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, sizeof(tact_mdl_rtd_t), meta)) return false;
```

### Command Parameters

Let's examine each specified command parameter individually and understand what happens:

*   **TACT_ITEM** - Macro for filling the structure, use it to add a command to a group.
*   **[REQ]** - The request itself, consisting of the concatenation of the command and `TACT_CMD_CRLF`. In this case, it's a string literal, but it can also be a character array.
*   **[PREFIX]** - String literal to search for in the response. Can be omitted.
*   **[PARCE_TYPE]** - Type of parser used, read below.
*   **[FORMAT]** - Response parsing format for SSCANF, used together with `VA_ARGS`. Retrieved data will be assembled according to this format, placed into arguments, and passed to the group callback. Can be omitted.
*   **[RPT]** - Maximum command attempts, including the first send; zero fails on the first timeout.
*   **[WAIT]** - Response wait timer in 10ms units.
*   **[STEPERROR]** - In case of a command error, we can skip several steps forward or backward within the group, or do nothing. 0 terminates the entire group.
*   **[STEPOK]** - Similar to error, but here in case of success we can step to a specific command. 0 terminates the entire group.
*   **[CB]** - Callback for the command, called upon command execution result. Data obtained via the format is also passed to it. Can be omitted.
*   **[VA_ARG]** - Arguments for the format in the form of `TACT_ARG`, where we specify which structure and which field will be used to store the formatted data. Can be omitted. Maximum 6. The caller must match each format conversion to the field type and keep string widths within the field size; the core checks only that the field offset lies inside the allocated data block.

### Parameters of the tact_entity_enqueue function

```c
bool tact_entity_enqueue(tact_context_t* const ctx, const tact_item_t* const item, const uint8_t item_amount, const tact_entity_cb_t cb, uint16_t data_size, void* const meta);
```

*   **ctx** - Global context for which the group is created.
*   **item** - Command group.
*   **item_amount** - Size of the command structure.
*   **cb** - Callback for the entire group, which will be called upon execution of this group. Can be omitted.
*   **data_size** - Size of the data structure that will be created dynamically, automatically, to save data specified in the [FORMAT] and [VA_ARGS] fields. A pointer to this structure will be returned in the group callback, and after execution, its memory will be cleared. Can be omitted (0).
*   **meta** - Pointer to some additional group metadata. Passed directly to the execution callback and not processed by the library in any way. Can be omitted.

### Parameters of the callback for the entire command group:

```c
void (*tact_entity_cb_t)(bool result, void* meta, void* data);
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
*   Sometimes the command composition is not known in advance and needs to be created in real-time on the stack or in some temporary buffer, rather than using a static string literal. Since the library works asynchronously, you must ensure that this temporary buffer exists at the moment of command execution. To avoid dealing with this each time, you can simply specify that the command should be saved to the library's memory using the `TACT_CMD_SAVE` macro, for example: `TACT_CMD_SAVE"AT+CIPMODE?"TACT_CMD_CRLF`. The same can be applied to the prefix.
*   In the [PREFIX] field, you can specify more complex constructions to check multiple lines and prefixes at once:
    *   Use `|` for OR operations: `"+CREG: 0,1|+CREG: 0,5"`
    *   Use `&` for AND operations: `"+IPD&SEND OK"`
*   Also, in the [PREFIX] field, you can specify the macro-literal `TACT_CMD_FORCE`, which indicates that this command or data should simply be sent without parsing or waiting for a response. Other fields except [STEPOK] will not be used at all, and their content can be anything.
*   The `modules` folder contains some files and implementations of ready-made AT command groups.
*   The `tests` folder contains a portable makefile. Run `make -C tests check`; use `ARCH_FLAGS=-m32` only when the toolchain has 32-bit support. Host tests use an 8 KiB pool to account for 64-bit pointer overhead; production builds keep the configurable default and must size it for their actual queue workload. `BIN_DIR` can redirect all build outputs outside the source tree.
*   The command callback receives a data slice. This is done so you can write your own data parser if the standard formatting is insufficient. An example of this can be seen in the ready-made function `tact_mdl_rtd`, where the data structure is dynamically created by the library, and in the command callback, we manually parse its data in the desired way and place them into this structure.
*   In case of processed data, the library itself moves the tail and counter of your ring buffer.
*   The `examples` folder contains usage examples.

### Parsers
`TACT_PARCE_SIMCOM`<br>
Parser for commands in SIMCOM format. This parser works with echo enabled. Usually, the response to commands comes sequentially as: ECHO → RESULT → DATA. Echo must always come first. The parser works similarly, first looking for the command echo, then the execution result, and then the useful data, i.e., it splits the response into three fields: [REQ] [RES] [DATA]. Their combination, presence, and order can vary from command to command, each differently. The internal parser handles only a few combinations and possible cases, which should be sufficient. Let's examine the parameter combinations when creating a command and what responses they support:

| Specified Command Parameters | Required Response Fields for Such Parameters |
|------------------------------|----------------------------------------------|
| [REQ] | [REQ] [RES] with OK<br>[REQ] [RES] [DATA] with OK |
| [REQ] [PREFIX] <br> [REQ] [PREFIX] [FORMAT] [ARG]| [REQ] [RES] [DATA] + prefix and format check (if specified)<br>[REQ] [DATA] + prefix and format check (if specified)<br>[DATA] + prefix and format check (if specified) |
| [PREFIX] | [DATA] + prefix check |
| [PREFIX] [FORMAT] [ARG] | [DATA] + prefix and format check |

The [DATA] field is usually framed by CRLF characters — the parser finds boundaries by them, removes them, and works with "clean" data. If [DATA] contains multi-line data with CRLF, the parser will return only the FIRST line.
For complex cases, it is recommended to write a custom parser via a callback (example: function `tact_mdl_rtd`). Use the table above to correctly create a command, knowing in advance the type of response and what fields it contains.

`TACT_PARCE_RAW`<br>
Parser for data transmission format commands. Parses raw data simply checking for a match with the [PREFIX] field if specified. Also, if the [FORMAT][ARG] field is specified, the parser will attempt to format and retrieve useful data from the very beginning of this data buffer. The data is not preprocessed but handled as-is, which must be taken into account. The [REQ] field can be omitted.

## 4. Creating Logical Chains

Based on command groups (described above), you can create your own algorithms and execution chains. An API is provided in the file `tact_chain.h`:

*   `tact_chain_destroy`
*   `tact_chain_destroy_ex`
*   `tact_chain_start`
*   `tact_chain_stop`
*   `tact_chain_reset`
*   `tact_chain_run`
*   `tact_chain_is_running`
*   `tact_chain_get_current_step`
*   `tact_chain_get_current_step_name`

### Creating Step Functions

To create a chain, each command group must be wrapped in a function of this type:

```c
bool (*function)(tact_context_t* ctx, tact_entity_cb_t cb,
                 const void* param, void* meta);
```
The function must, based on the result, execute the callback passed into it.

For example:

```c
bool tact_mdl_gprs_socket_connect(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta)
{
  if(!ctx || !param) return false;
  char cipstart[320] = {0};
  const tact_mdl_gprs_server_t* tcp = (const tact_mdl_gprs_server_t*)param;
  int written = snprintf(cipstart, sizeof(cipstart), "%sAT+CIPSTART=\"%s\",\"%s\",\"%s\"%s", TACT_CMD_SAVE, tcp->mode, tcp->ip, tcp->port, TACT_CMD_CRLF);
  if(written < 0 || (size_t)written >= sizeof(cipstart)) return false;
  tact_item_t items[] = //[REQ][PREFIX][PARCE_TYPE][RPT][WAIT][STEPERROR][STEPOK][CB][FORMAT][...##VA_ARGS]
  {
    TACT_ITEM("AT+CIPSTATUS"TACT_CMD_CRLF, "STATE: IP STATUS|STATE: TCP CLOSED", TACT_PARCE_SIMCOM, 10, 100,  0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM(cipstart,                           "CONNECT OK|ALREADY CONNECT", TACT_PARCE_SIMCOM,  6, 500,  0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIPSTATUS"TACT_CMD_CRLF,                  "STATE: CONNECT OK", TACT_PARCE_SIMCOM, 10, 100,  0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIPSEND?"TACT_CMD_CRLF,                           "+CIPSEND:", TACT_PARCE_SIMCOM, 10, 100,  0, 1, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIPQSEND?"TACT_CMD_CRLF,                       "+CIPQSEND: 0", TACT_PARCE_SIMCOM,  1, 100,  1, 0, NULL, NULL, TACT_NO_ARG),
    TACT_ITEM("AT+CIPQSEND=0"TACT_CMD_CRLF,                                NULL, TACT_PARCE_SIMCOM, 10, 100,  0, 0, NULL, NULL, TACT_NO_ARG),
  };
  if(!tact_entity_enqueue(ctx, items, sizeof(items)/sizeof(items[0]), cb, 0, meta)) return false;
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
  /* Caller owns TX/RX descriptors and pauses AT/URC processing in raw loops.
   * See examples/gprs.c for message preparation and reply validation. */
  //Main
  TACT_CHAIN("INIT_MODEM", "NEXT", "MODEM RESTART", tact_mdl_modem_init, NULL, NULL, NULL, 1),
  TACT_CHAIN("GPRS INIT", "NEXT", "GPRS DEINIT", tact_mdl_gprs_init, NULL, NULL, NULL, 1),
  TACT_CHAIN("SOCKET CONFIG", "NEXT", "GPRS INIT", tact_mdl_gprs_socket_config, NULL, NULL, NULL, 1),
  TACT_CHAIN("CONNECT TO SERVER", "NEXT", "SOCKET CONFIG", tact_mdl_gprs_socket_connect, NULL, &tact_server_connect, NULL, 1),
  TACT_CHAIN("GET RTD", "NEXT", "DISCONNECT FROM SERVER", tact_mdl_rtd, tact_rtd_cb, NULL, NULL, 1),
  TACT_CHAIN_EXEC("CHECK RTD", "NEXT", "GET RTD", tact_rtd_check, NULL),

  TACT_CHAIN_EXEC("CREATE WIALON LOGIN", "NEXT", "DISCONNECT FROM SERVER", tact_server_data_wialon_login, NULL),
  TACT_CHAIN("SEND WIALON LOGIN", "NEXT", "DISCONNECT FROM SERVER", tact_mdl_gprs_socket_send_recieve, NULL, &tact_server_tx, NULL, 1),
  TACT_CHAIN_LOOP_START(0),
    TACT_CHAIN_EXEC("LOGIN TX", "LOGIN SEND RESULT", "NEXT", tact_mdl_gprs_stream_tx, &tact_server_tx),
  TACT_CHAIN_LOOP_END,
  TACT_CHAIN("LOGIN SEND RESULT", "NEXT", "DISCONNECT FROM SERVER", tact_mdl_gprs_socket_send_end, NULL, NULL, NULL, 1),
  TACT_CHAIN_LOOP_START(0),
    TACT_CHAIN_EXEC("LOGIN RX", "LOGIN CHECK REPLY", "NEXT", tact_mdl_gprs_stream_rx, &tact_server_rx),
  TACT_CHAIN_LOOP_END,
  TACT_CHAIN_EXEC("LOGIN CHECK REPLY", "NEXT", "DISCONNECT FROM SERVER", tact_server_answer_check, NULL),

  TACT_CHAIN_LOOP_START(10),
    TACT_CHAIN("GET RTD LOOP", "NEXT", "DISCONNECT FROM SERVER", tact_mdl_rtd, tact_rtd_cb, NULL, NULL, 1),
    TACT_CHAIN_EXEC("CREATE WIALON DATA", "NEXT", "DISCONNECT FROM SERVER", tact_server_data_wialon_packet, NULL),
    TACT_CHAIN("SEND WIALON DATA", "NEXT", "DISCONNECT FROM SERVER", tact_mdl_gprs_socket_send_recieve, NULL, &tact_server_tx, NULL, 1),
    TACT_CHAIN_LOOP_START(0),
      TACT_CHAIN_EXEC("DATA TX", "DATA SEND RESULT", "NEXT", tact_mdl_gprs_stream_tx, &tact_server_tx),
    TACT_CHAIN_LOOP_END,
    TACT_CHAIN("DATA SEND RESULT", "NEXT", "DISCONNECT FROM SERVER", tact_mdl_gprs_socket_send_end, NULL, NULL, NULL, 1),
    TACT_CHAIN_LOOP_START(0),
      TACT_CHAIN_EXEC("DATA RX", "DATA CHECK REPLY", "NEXT", tact_mdl_gprs_stream_rx, &tact_server_rx),
    TACT_CHAIN_LOOP_END,
    TACT_CHAIN_EXEC("DATA CHECK REPLY", "NEXT", "DISCONNECT FROM SERVER", tact_server_answer_check, NULL),
    TACT_CHAIN_DELAY(1000),
  TACT_CHAIN_LOOP_END,

  TACT_CHAIN_EXEC("WIALON DATA CLEAN", "STOP", "HARD RESET", tact_server_data_clean, NULL),

  //Error
  TACT_CHAIN("GPRS DEINIT", "GPRS INIT", "MODEM RESTART", tact_mdl_gprs_deinit, NULL, NULL, NULL, 1),
  TACT_CHAIN("DISCONNECT FROM SERVER", "CONNECT TO SERVER", "GPRS DEINIT", tact_mdl_gprs_socket_disconnect, NULL, NULL, NULL, 1),
  TACT_CHAIN("MODEM RESTART", "GPRS INIT", "MODEM RESTART", tact_mdl_modem_reset, NULL, NULL, NULL, 1),

  //Critical
  TACT_CHAIN_EXEC("HARD RESET", "STOP", "STOP", tact_hard_reset, NULL),
};

tact_chain_t* chain = tact_chain_create("TCP", tcp_steps, sizeof(tcp_steps)/sizeof(chain_step_t), ctx);
if(!chain || !tact_chain_start(chain)) return;

while(tact_chain_is_running(chain))
{
  bool res = tact_chain_run(chain);
  if(tact_chain_is_running(chain) && !res) break;
}
/* Destroy only after any outstanding asynchronous step callback has returned. */
if(!tact_chain_destroy_ex(chain)) {
  /* Defer cleanup until no run/callback is active. */
}
```

This chain one-time configures the SIMCOM modem, context, and connection, gets real-time data, checks it, and then 10 times starts collecting and sending this data to the server with a period of 10 seconds, after preliminary authorization. In case of an error, disconnection or deinitialization occurs with an attempt to reinitialize and reconnect or a complete restart.

### Raw stream EXEC parameters

EXEC actions receive the Chain context and a caller-owned mutable parameter:

```c
bool action(tact_context_t *ctx, void *param);
```

FTP provides two raw byte operations, `tact_mdl_ftp_stream_tx` and
`tact_mdl_ftp_stream_rx`. Both use the same descriptor, owned by the caller:

```c
uint8_t payload[512] = {0}; /* Fill before starting the upload. */
tact_mdl_ftp_stream_t tx =
{
  .buffer = payload,
  .size = sizeof(payload),
  .count = 0,
  .direction = TACT_MDL_FTP_STREAM_TX
};
tact_mdl_ftp_upload_t upload =
{
  .remote_path = "can1.bin",
  .size = sizeof(payload),
  .offset = 0
};

/* Prerequisites: FTP service/login/binary type are ready, and this Chain
 * exclusively owns the UART. The surrounding worker must not process AT/URCs
 * during the raw loop; resume AT processing for FTP END afterwards. */
chain_step_t put_steps[] =
{
  TACT_CHAIN("FTP BEGIN", "NEXT", "STOP", tact_mdl_ftp_put_begin,
             NULL, &upload, NULL, 1),
  TACT_CHAIN_LOOP_START(0),
    TACT_CHAIN_EXEC("FTP TX", "FTP END", "NEXT", tact_mdl_ftp_stream_tx, &tx),
    /* Caller EXEC steps may inspect progress or handle other work here. */
  TACT_CHAIN_LOOP_END,
  TACT_CHAIN("FTP END", "STOP", "STOP", tact_mdl_ftp_put_end,
             NULL, NULL, NULL, 1),
};
```

Each call transfers at most `TACT_MDL_FTP_STREAM_BLOCK` bytes and returns true
when `count == size`. False means pending progress or invalid input. Deadlines
and recovery belong to the Chain: add a caller-defined timeout branch if the
peer may stop making progress. Do not busy-wait an infinite loop without yielding.
Use the surrounding task's scheduler and monotonic clock for raw-phase yields
and deadlines. Do not use TACT_CHAIN_DELAY while tact_core_proc is paused:
its tick counter advances only when the core is serviced.

PUT specifies the data length (1..2048 bytes) explicitly; `tx.size` must match
`upload.size`. Payload bytes are sent unchanged, including NUL, ETX, and Ctrl+Z.
Do not append a terminator. For the next file block, prepare its buffer, reset
`count`, and update `upload.size` and the remote REST `upload.offset`. The server
result is obtained by FTP END; a completed TX counter alone does not prove upload
success.

For receive, set direction to `TACT_MDL_FTP_STREAM_RX`, set `size` to the expected
payload length, reset `count`, and use `tact_mdl_ftp_stream_rx` in the same loop
pattern. Consume the text payload header first. RX reads only the expected bytes
from the existing UART ring and leaves trailing protocol bytes untouched. FTP
download headers and chunk boundaries are handled by the caller, not by this
byte-copy operation.

The raw operations create no tasks, suspend no workers, allocate no buffers,
and store no global transfer state. The current GSM worker is not automatically
paused by them. The surrounding integration must exclude `tact_core_proc` and
other RX consumers during raw receive, and prevent AT commands during raw TX.
Configure the TACT port critical-section hooks to protect the ring from its UART
ISR producer; the default hooks are no-ops. The transport write callback must
return the exact accepted byte count, with zero meaning no bytes sent. Returning
zero after a partial physical transmission does not satisfy this contract and
must not be treated as a safe retry.

### GPRS binary stream

The legacy SIM800/SIM868 GPRS module uses the same caller-owned stream layout as
FTP, plus `remaining`: bytes left in the current `+IPD` payload. There is no
extra ring, heap-allocated packet, callback, or hidden transfer state in the new
EXEC operations. The older `tact_mdl_gprs_server` chunk-handler API remains for
compatibility; do not use it as a second consumer of the same RX ring.

`tact_mdl_gprs_socket_send_recieve` now takes `tact_mdl_gprs_stream_t`, not the
old text/answer descriptor. It disables echo, queues `AT+CIPSEND=size`, and
completes at `>`. Echo stays disabled; reconfigure it separately if needed.
Send exactly `size` bytes using `tact_mdl_gprs_stream_tx`; no Ctrl+Z or CR/LF is
appended. `tact_mdl_gprs_socket_send_end` separately waits for `SEND OK`.
A completed TX counter means UART transfer, not modem/server acknowledgement.
One send is limited to `TACT_MDL_GPRS_SEND_MAX` and must fit the modem's current
`AT+CIPSEND?` allowance.

```c
uint8_t request[] = {'P', 0, 0x1A, 'G'};
uint8_t reply[8] = {0};
tact_mdl_gprs_stream_t tx =
{
  .buffer = request,
  .size = sizeof(request),
  .direction = TACT_MDL_GPRS_STREAM_TX
};
tact_mdl_gprs_stream_t rx =
{
  .buffer = reply,
  .size = sizeof(reply),
  .direction = TACT_MDL_GPRS_STREAM_RX,
  .remaining = 0
};

chain_step_t socket_steps[] =
{
  TACT_CHAIN("GPRS PROMPT", "NEXT", "STOP", tact_mdl_gprs_socket_send_recieve,
             NULL, &tx, NULL, 1),
  TACT_CHAIN_LOOP_START(0),
    TACT_CHAIN_EXEC("GPRS TX", "GPRS SEND RESULT", "NEXT", tact_mdl_gprs_stream_tx, &tx),
  TACT_CHAIN_LOOP_END,
  TACT_CHAIN("GPRS SEND RESULT", "NEXT", "STOP", tact_mdl_gprs_socket_send_end,
             NULL, NULL, NULL, 1),
  TACT_CHAIN_LOOP_START(0),
    TACT_CHAIN_EXEC("GPRS RX", "STOP", "NEXT", tact_mdl_gprs_stream_rx, &rx),
  TACT_CHAIN_LOOP_END,
};
```

Run after socket configuration/connection. Raw loops require the same UART
ownership, critical hooks, scheduler yields, and monotonic deadlines described
for FTP. The sample assumes a request/reply peer that does not deliver unrelated
binary traffic while the text worker waits for SEND OK. An asynchronous socket
requires caller-controlled text/binary RX routing: do not run the generic AT/URC
parser through an unsolicited binary packet, including while awaiting SEND OK.

RX expects `+IPD,<length>,TCP:<payload>` (or `UDP`) with CIPMUX=0, CIPHEAD=1,
CIPSHOWTP=1 and automatic reception (CIPRXGET=0). Socket configuration sets these
options, disables the remote-address prompt (CIPSRIP=0), and enables the send
prompt/result (CIPSPRT=1). The format follows the
[SIMCom AT command manual](https://simcom.ee/documents/SIM868/SIM800%20Series_AT%20Command%20Manual_V1.10.pdf),
sections 8.2.3, 8.2.17, 8.2.20, 8.2.24 and 8.2.26. This is not the future A76xx TCP module.

Set RX `size` to the expected **payload** length, excluding +IPD headers. It
may arrive in several UART fragments or several +IPD blocks. Only payload bytes
increment `count`; NUL, CR/LF, and +IPD inside a payload remain literal bytes.
Completion is `count == size`, not a newline or TCP packet boundary. If the
application reply length is unknown, read bounded chunks and let the application
protocol determine its complete-message length.

After one RX buffer fills, replace `buffer`/`size` and reset `count`, preserving
`remaining` so the next call continues an unfinished +IPD block. Reset the whole
RX context only for a new connection or after explicitly discarding its old ring
data. Text between payload blocks is skipped, not reported as URCs; the caller
must separately route status/errors if they are required.

### Chain Parameters

So, let's look at what can be used in a chain and what parameters can be passed there:

**TACT_CHAIN** - The main macro for adding an execution step, contains:
*   **[Name]** - Step name.
*   **[Success target]** - Name of the step to go to in case of success. You can specify NULL or "NEXT" for step +1, or "PREV" for step -1, or a specific step name. "STOP" will end chain execution.
*   **[Error target]** - The same as for success, but in case of error.
*   **[Func]** - The function that will be called to execute the step.
*   **[Cb]** - Callback for the step.
*   **[Param]** - The parameters that will be passed to the specified function when it starts executing.
*   **[meta]** - The metadata that we passed during creation.
*   **[Retries]** - Maximum total attempts, including the initial call; 0 or 1 means one attempt.

**TACT_CHAIN_EXEC** - Macro for creating and executing some action, you can execute or check something. Contains:
*   **[Name]** - Step name.
*   **[True target]** - Name of the step to go to in case of success. You can specify NULL or "NEXT" for step +1, or "PREV" for step -1, or a specific step name. "STOP" will end chain execution.
*   **[False target]** - The same as for success, but in case of error.
*   **[Exec func]** - Function to execute. Must be of type `bool (*exec)(tact_context_t *ctx, void *param);` The Chain passes its own TACT context automatically; the result selects the next transition.
*   **[Param]** - Borrowed mutable parameter pointer passed unchanged to the EXEC function; use NULL when unused. The referenced object must outlive all executions.

**TACT_CHAIN_LOOP_START** - Macro to indicate the start of a following loop, contains:
*   **[Iterations]** - Number of loop iterations. 0 - Infinite.

**TACT_CHAIN_LOOP_END** - Macro to indicate the end of a loop.

**TACT_CHAIN_DELAY** - Macro to create an artificial delay, contains:
*   **[ms]** - Delay time in ms.

### Parameters of the tact_entity_enqueue function

```c
tact_chain_t* tact_chain_create(const char* const name, const chain_step_t* const steps,
                              const uint32_t step_count, tact_context_t* const ctx);
```

*   **name** - Chain name.
*   **steps** - The chain structure itself.
*   **step_count** - Number of steps in the chain.

### Peculiarities

*   As can be seen from the example, nested loops can be created. The library implements and uses its own dynamic stack to work with them. There is support and the possibility of transitions through loops, from loops, into loops with or without nesting, moving both forward and backward, skipping their denoting steps. There are no restrictions, however, it should be noted that one loop iteration occurs only at the moment of executing `TACT_CHAIN_LOOP_END` for the corresponding loop. Also, ensure there are no steps with the same names; in such a case, the transition to such a step will be performed to the first one found in the array.
```
