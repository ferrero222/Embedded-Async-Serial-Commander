/******************************************************************************
 *              _____      _       ____   _____  ======                      *
 *      ====== |_   _|    / \     / ___| |_   _| ======    (c)03.10.2025     *
 *      ======   | |     / _ \   | |       | |   ======        v1.0.0        *
 *      ======   | |    / ___ \  | |___    | |   ======                      *
 *      ======   |_|   /_/   \_\  \____|   |_|   ======                      *
 *                                                                           *
 ******************************************************************************/
#ifndef __TACT_MDL_GPRS_H
#define __TACT_MDL_GPRS_H

/*******************************************************************************
 * Include files
 ******************************************************************************/
#include "tact_core.h"

/*******************************************************************************
 * Module configuration
 ******************************************************************************/
#define TACT_MDL_GPRS_STREAM_BLOCK 256U
#define TACT_MDL_GPRS_SEND_MAX     1460U

/*******************************************************************************
 * Global type definitions ('typedef')
 ******************************************************************************/
typedef struct tact_mdl_gprs_server_t
{
  char mode[4];
  char ip[256];
  char port[6];
} tact_mdl_gprs_server_t;

typedef enum
{
  TACT_MDL_GPRS_STREAM_TX = 0,
  TACT_MDL_GPRS_STREAM_RX
} tact_mdl_gprs_stream_direction_t;

/* Caller-owned storage; RX uses only the existing TACT/UART ring. */
typedef struct tact_mdl_gprs_stream_t
{
  uint8_t *buffer;                               /* Payload, not AT framing. */
  uint32_t size;                                 /* Expected payload byte count. */
  uint32_t count;                                /* Payload bytes transferred. */
  tact_mdl_gprs_stream_direction_t direction;     /* Required transfer direction. */
  uint32_t remaining;                            /* Unread bytes in the current +IPD. */
} tact_mdl_gprs_stream_t;

/*******************************************************************************
 * Global function prototypes (definition in C source)
 ******************************************************************************/
/*******************************************************************************
 * @brief Initialize GPRS registration.
 * @details Selects the operator automatically and waits for registration and packet
 *          attachment. Connection ordering and recovery belong to the Chain.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Unused; pass NULL.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_gprs_init(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 * @brief Configure one non-transparent socket.
 * @details Enables automatic RX with +IPD,<length>,TCP/UDP: framing, without a
 *          remote-address prompt. Enables the send prompt and normal SEND OK.
 *          Configure before connecting; this is the legacy SIM800/SIM868 module.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Unused; pass NULL.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_gprs_socket_config(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 * @brief Connect the configured socket to a server.
 * @details Copies the formatted CIPSTART command into the queued entity. Server
 *          strings need to remain valid only until this function returns.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Pointer to tact_mdl_gprs_server_t.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_gprs_socket_connect(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 * @brief Request the prompt for a fixed-length socket transmission.
 * @details Disables echo, queues CIPSEND=size, and completes at >. Echo remains
 *          disabled; reconfigure it separately if needed. No payload, Ctrl+Z, or server
 *          response is handled here. Call stream_tx afterwards with this same
 *          descriptor; its count must initially be zero. The size must fit the
 *          modem's CIPSEND? limit and SEND_MAX. Exclude concurrent AT commands.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Pointer to tact_mdl_gprs_stream_t with direction TX.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_gprs_socket_send_recieve(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 * @brief Wait for the modem's transmission result.
 * @details Queues no command or payload; waits for SEND OK after stream_tx has
 *          transferred exactly size bytes. This is not an application reply.
 *          Non-success results fail by timeout; recovery belongs to the Chain.
 *          Do not run the text parser over unsolicited binary RX traffic.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Unused; pass NULL.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_gprs_socket_send_end(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 * @brief Close the current socket.
 * @details Enqueues CIPCLOSE and waits for CLOSE OK. Release raw stream ownership
 *          before allowing the command worker to process this sequence.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Unused; pass NULL.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_gprs_socket_disconnect(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 * @brief Deactivate the GPRS connection.
 * @details Enqueues CIPSHUT and waits for SHUT OK. Discard any unfinished stream
 *          context when the connection is closed or the modem is reset.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Unused; pass NULL.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_gprs_deinit(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 * @brief Advance a raw socket transmit stream by one bounded write.
 * @details Use as TACT_CHAIN_EXEC in a loop after the > prompt. Transfers at most
 *          STREAM_BLOCK bytes unchanged through the context write callback.
 *          No allocation, escaping, or terminator is added. The callback must
 *          report exactly the accepted byte count; zero must mean nothing sent.
 *          Deadlines, UART ownership, and recovery belong to the caller's Chain.
 * @param[in] ctx Initialized TACT context with an exclusively owned UART.
 * @param[in,out] param Pointer to tact_mdl_gprs_stream_t with direction TX.
 * @retval true count equals size; all payload bytes have been transmitted.
 * @retval false The stream is pending or its context/transport is invalid.
 ******************************************************************************/
bool tact_mdl_gprs_stream_tx(tact_context_t* const ctx, void* const param);

/*******************************************************************************
 * @brief Collect binary socket payload from length-delimited +IPD blocks.
 * @details Reads directly from the existing UART ring, at most STREAM_BLOCK
 *          wire bytes per call. Partial headers stay in the ring. After a header,
 *          remaining tracks the payload until it is consumed; embedded NUL,
 *          CR/LF, or +IPD bytes are copied literally. Multiple blocks may fill
 *          the requested size. Returns at count == size, preserving unread
 *          payload and subsequent headers. To continue with another buffer,
 *          reset count and update buffer/size, but preserve remaining.
 *          Initialize remaining to zero only for a new connection/stream.
 *          Exclude tact_core_proc and other RX consumers; configure the TACT
 *          critical hooks to protect the ring from its ISR producer. Text outside
 *          +IPD is skipped, not dispatched as URCs. Timeouts belong to the Chain.
 * @param[in] ctx Initialized TACT context with an exclusively owned RX ring.
 * @param[in,out] param Pointer to tact_mdl_gprs_stream_t with direction RX.
 * @retval true count equals size; all requested payload bytes were received.
 * @retval false More bytes are needed or the parameters/ring are invalid.
 ******************************************************************************/
bool tact_mdl_gprs_stream_rx(tact_context_t* const ctx, void* const param);

#endif /* __TACT_MDL_GPRS_H */
