/******************************************************************************
 *              _____      _       ____   _____  ======                      *
 *      ====== |_   _|    / \     / ___| |_   _| ======    (c)03.10.2025     *
 *      ======   | |     / _ \   | |       | |   ======        v1.0.0        *
 *      ======   | |    / ___ \  | |___    | |   ======                      *
 *      ======   |_|   /_/   \_\  \____|   |_|   ======                      *
 *                                                                           *
 ******************************************************************************/
#ifndef TACT_MDL_FTP_H
#define TACT_MDL_FTP_H

/*******************************************************************************
 * Include files
 ******************************************************************************/
#include "tact_core.h"

/*******************************************************************************
 * Module configuration
 ******************************************************************************/
#define TACT_MDL_FTP_HOST_MAX       128U
#define TACT_MDL_FTP_USER_MAX       128U
#define TACT_MDL_FTP_PASSWORD_MAX   128U
#define TACT_MDL_FTP_PATH_MAX       112U
#define TACT_MDL_FTP_STREAM_BLOCK   256U
#define TACT_MDL_FTP_PUT_MAX        2048U

/*******************************************************************************
 * Global type definitions ('typedef')
 ******************************************************************************/
typedef enum
{
  TACT_MDL_FTP_STREAM_TX = 0,
  TACT_MDL_FTP_STREAM_RX
} tact_mdl_ftp_stream_direction_t;

/* Sole stream storage is owned by the caller; there is no static FTP context. */
typedef struct tact_mdl_ftp_stream_t
{
  uint8_t *buffer;                              /* Payload, not AT framing. */
  uint32_t size;                                /* Expected payload byte count. */
  uint32_t count;                               /* Payload bytes transferred. */
  tact_mdl_ftp_stream_direction_t direction;    /* Required transfer direction. */
} tact_mdl_ftp_stream_t;

typedef struct tact_mdl_ftp_config_t
{
  char host[TACT_MDL_FTP_HOST_MAX + 1U];
  uint16_t port;
  char username[TACT_MDL_FTP_USER_MAX + 1U];
  char password[TACT_MDL_FTP_PASSWORD_MAX + 1U];
  uint8_t pdp_context_id;
} tact_mdl_ftp_config_t;

typedef struct tact_mdl_ftp_upload_t
{
  char remote_path[TACT_MDL_FTP_PATH_MAX + 1U];
  uint16_t size;                  /* 1..2048 bytes in this PUT operation. */
  uint32_t offset;                /* Remote REST offset; zero for a new file. */
} tact_mdl_ftp_upload_t;

typedef struct tact_mdl_ftp_path_t
{
  char remote_path[TACT_MDL_FTP_PATH_MAX + 1U];
} tact_mdl_ftp_path_t;

/* Entity-owned SIZE callback data; copy it before the callback returns. */
typedef struct tact_mdl_ftp_result_t
{
  unsigned int remote_size;
} tact_mdl_ftp_result_t;

/*******************************************************************************
 * Global function prototypes (definition in C source)
 ******************************************************************************/
/*******************************************************************************
 * @brief Start FTP on the selected PDP context.
 * @details Configure APN/network beforehand. Enqueues CFTPSSTART and waits
 *          for its successful result. Session ordering belongs to the Chain.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Pointer to tact_mdl_ftp_config_t.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_ftp_start(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 * @brief Select passive FTP data connections.
 * @details Enqueues CFTPSMODE=1. Call after service start and before login;
 *          the modem establishes the data connection outbound.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Unused; pass NULL.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_ftp_mode(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 * @brief Log in to an ordinary FTP server.
 * @details Uses server type zero and disables echo around credentials. On
 *          command failure recover the modem/echo state from the Chain.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Pointer to tact_mdl_ftp_config_t.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_ftp_login(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 * @brief Select binary FTP transfer type.
 * @details Enqueues CFTPSTYPE=I after login so the server preserves the
 *          original binary payload without newline conversion.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Unused; pass NULL.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_ftp_type(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 * @brief Request the prompt for one fixed-length upload block.
 * @details Sends CFTPSPUT with an explicit length and REST offset. The
 *          length must equal the TX stream size and be 1..2048. After the prompt,
 *          transfer exactly that many bytes: no escaping or Ctrl+Z is required.
 *          The caller owns UART scheduling and must exclude other AT commands.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Pointer to tact_mdl_ftp_upload_t.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_ftp_put_begin(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 * @brief Wait for the final result of a fixed-length upload.
 * @details Queues no data or terminator: waits for OK and successful
 *          CFTPSPUT, then restores echo. Call after the TX count reaches size.
 *          A timeout/nonzero modem result reports failure through cb.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Unused; pass NULL.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_ftp_put_end(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 * @brief Query a remote file size.
 * @details cb receives tact_mdl_ftp_result_t; remote_size is valid only
 *          for a successful callback and must be copied before cb returns.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Pointer to tact_mdl_ftp_path_t.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_ftp_size(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 * @brief Log out of the current FTP server.
 * @details Enqueues CFTPSLOGOUT and waits for its successful result.
 *          The caller's Chain decides whether to stop or reconnect.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Unused; pass NULL.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_ftp_logout(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 * @brief Stop FTP and deactivate its PDP context.
 * @details Enqueues CFTPSSTOP. First release TCP and any other service
 *          sharing this PDP context so their connections are not interrupted.
 * @param[in] ctx Initialized TACT context.
 * @param[in] cb Completion callback, or NULL.
 * @param[in] param Pointer to tact_mdl_ftp_config_t.
 * @param[in] meta Caller value forwarded to cb.
 * @retval true The command sequence was queued.
 * @retval false Parameters are invalid or TACT rejected the sequence.
 ******************************************************************************/
bool tact_mdl_ftp_stop(tact_context_t* const ctx, const tact_entity_cb_t cb, const void* const param, void* const meta);

/*******************************************************************************
 * @brief Advance a raw transmit stream by one bounded write.
 * @details Use as TACT_CHAIN_EXEC in a loop. Sends at most STREAM_BLOCK bytes
 *          through the context write callback and advances count by the accepted
 *          count. The buffer stays caller-owned. No allocation, escaping, file
 *          reads, terminators, or session state are hidden in this function.
 *          The write callback must report the exact accepted byte count; zero
 *          must mean no bytes sent. Timeouts/recovery belong to the Chain.
 * @param[in] ctx Initialized TACT context with an exclusively owned UART.
 * @param[in,out] param Pointer to tact_mdl_ftp_stream_t with direction TX.
 * @retval true count equals size; the full payload has been transmitted.
 * @retval false The stream is pending or its context/transport is invalid.
 ******************************************************************************/
bool tact_mdl_ftp_stream_tx(tact_context_t* const ctx, void* const param);

/*******************************************************************************
 * @brief Advance a raw receive stream by one bounded ring-buffer read.
 * @details Copies at most STREAM_BLOCK bytes from the existing TACT/UART ring
 *          and stops at size, leaving subsequent protocol bytes in the ring.
 *          The payload header must already be consumed. Exclude tact_core_proc
 *          and other RX consumers during this raw phase; configure the TACT
 *          port critical hooks to protect the ring from its interrupt producer.
 *          FTP headers, chunk preparation, deadlines, and recovery belong to
 *          the caller's Chain, not to this byte-copy operation.
 * @param[in] ctx Initialized TACT context with an exclusively owned RX ring.
 * @param[in,out] param Pointer to tact_mdl_ftp_stream_t with direction RX.
 * @retval true count equals size; all expected payload bytes have been received.
 * @retval false More bytes are needed or the context/ring is invalid.
 ******************************************************************************/
bool tact_mdl_ftp_stream_rx(tact_context_t* const ctx, void* const param);

#endif /* TACT_MDL_FTP_H */
