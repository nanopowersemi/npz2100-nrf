/**
 * @file npz2100_datalog.h
 * @brief nPZ2100 SRAM data-log extraction and parsing API.
 *
 * The nPZ2100 autonomously logs peripheral sensor readings into its 256-byte
 * internal SRAM while the host is powered off.  This module provides the
 * host-side API to read, parse, and clear that log after waking up.
 *
 * Log memory layout (set up via LOGCFG / LOGSADDR registers):
 *
 *   SRAM
 *   ┌──────────────────────────────────────┐  ← LOG_START_ADDR
 *   │  Idle-entry timestamp  (5 bytes)     │    GCT_MS, GCT_0..3
 *   ├──────────────────────────────────────┤  ← LOG_START_ADDR + 5
 *   │  Log entry 0  (2–7 bytes)            │
 *   │  Log entry 1  (2–7 bytes)            │
 *   │  ...                                 │
 *   ├──────────────────────────────────────┤  ← LOGCADDR  (next write ptr)
 *   │  0xFF  (unwritten / erased)          │
 *   └──────────────────────────────────────┘  ← 0xFF
 *
 * Each log entry:
 *   [header 1B] [timestamp 4B if PLOGTS=1] [value 1B or 2B per DTYPE]
 *
 * Header byte:
 *   bit7     = TRIG  - peripheral threshold was met
 *   bits6:4  = PER   - peripheral index (0–5)
 *   bits3:0  = GCT_MS - global counter milliseconds (0–15)
 *
 * Log rotation (LOG_ROT=1):
 *   When SRAM fills, logging wraps to LOG_START_ADDR+5, overwriting oldest
 *   entries.  LOG_IS_ROT flag is set after first rotation.
 *   On rotation: oldest entry at LOGCADDR, newest at LOGCADDR-1.
 *
 * @version 0.8
 * @date    2026-09-11
 * @author  Nanopower Semiconductor AS
 */

#ifndef NPZ2100_DATALOG_H_
#define NPZ2100_DATALOG_H_

#include "npz2100_lib.h"   /* npz2100_hal_t, npz2100_config_t, npz2100_err_t */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Constants
 * ======================================================================= */

/** Byte offset from LOG_START_ADDR where the first log entry begins.
 *  The first 5 bytes hold the idle-entry timestamp written by the nPZ2100. */
#define NPZ2100_LOG_ENTRY_OFFSET    (5u)

/** Maximum SRAM address available for log data (inclusive). */
#define NPZ2100_LOG_SRAM_MAX        (0xFFu)

/** Size of the idle-entry timestamp block at LOG_START_ADDR. */
#define NPZ2100_LOG_TIMESTAMP_SIZE  (5u)

/** Header byte masks. */
#define NPZ2100_LOG_HDR_TRIG_MSK    (0x80u)   /**< Threshold-trigger flag.     */
#define NPZ2100_LOG_HDR_PER_MSK     (0x70u)   /**< Peripheral index field.     */
#define NPZ2100_LOG_HDR_PER_POS     (4u)
#define NPZ2100_LOG_HDR_GCTMS_MSK   (0x0Fu)   /**< GCT milliseconds nibble.    */

/* =========================================================================
 * Structs
 * ======================================================================= */

/**
 * @brief Metadata about the current SRAM log state.
 *
 * Populated by npz2100_datalog_read_info().  Must be called before any
 * read or parse operation so that start/current addresses and rotation
 * state are known.
 */
typedef struct {
    uint8_t  start_addr;    /**< LOG_START_ADDR from LOGSADDR register.        */
    uint8_t  cur_addr;      /**< LOGCADDR - address of next write (read-only). */
    bool     log_enabled;   /**< LOG_EN bit from LOGCFG.                       */
    bool     rotation;      /**< LOG_ROT bit - rotation is configured.         */
    bool     rotated;       /**< LOG_IS_ROT - at least one rotation occurred.  */
    uint8_t  ts_gct_ms;     /**< Idle-entry timestamp: GCT milliseconds byte.  */
    uint8_t  ts_gct[4];     /**< Idle-entry timestamp: GCT_0..GCT_3 (32-bit). */
} npz2100_datalog_info_t;

/**
 * @brief A single parsed SRAM log entry.
 */
typedef struct {
    uint8_t  peripheral;    /**< Peripheral index (0–5).                       */
    bool     triggered;     /**< TRIG bit - threshold condition was met.       */
    uint8_t  gct_ms;        /**< Header GCT milliseconds nibble (0–15).        */
    bool     has_timestamp; /**< true if this entry includes a 4-byte GCT.    */
    uint32_t timestamp;     /**< GCT_0..3 packed as 32-bit (valid if has_ts). */
    uint16_t value;         /**< Sensor value (8-bit zero-extended or 16-bit). */
    uint8_t  entry_size;    /**< Total bytes consumed by this entry in SRAM.   */
} npz2100_datalog_entry_t;

/* =========================================================================
 * API
 * ======================================================================= */

/**
 * @brief Read SRAM log metadata from the device.
 *
 * Reads LOGCFG, LOGSADDR, LOGCADDR registers and the 5-byte idle-entry
 * timestamp from SRAM at LOG_START_ADDR.  Must be called before any other
 * datalog function.
 *
 * @param[in]  hal   Pointer to initialised HAL descriptor.
 * @param[in]  cfg   Config shadow (for SRAM bank handling).
 * @param[out] info  Populated with current log state.
 * @return NPZ2100_OK on success, NPZ2100_ERR_IO on I²C error.
 */
npz2100_err_t npz2100_datalog_read_info(const npz2100_hal_t    *hal,
                                         npz2100_config_t       *cfg,
                                         npz2100_datalog_info_t *info);

/**
 * @brief Compute the byte size of one log entry for a given peripheral slot.
 *
 * Uses the shadow config (CFGP for PLOGTS, MODP for DTYPE) to determine
 * whether a timestamp is present and whether the value is 1 or 2 bytes.
 *
 * Entry size = 1 (header) + 4 (timestamp, if PLOGTS=1) + 1 or 2 (value)
 *
 * @param[in] cfg      Config shadow.
 * @param[in] slot     Peripheral index (0–5).
 * @return Total entry size in bytes (2–7), or 0 if slot is invalid.
 */
uint8_t npz2100_datalog_entry_size(const npz2100_config_t *cfg, uint8_t slot);

/**
 * @brief Parse a single log entry from a raw byte buffer.
 *
 * Pure function - no I²C transaction.  The caller supplies the raw bytes
 * and the peripheral slot index (read from the previous header or known
 * from context) so that has_timestamp and value width can be derived.
 *
 * On success, entry->entry_size tells the caller how many bytes to advance
 * the buffer pointer before calling this function again.
 *
 * @param[in]  buf     Pointer to the start of the entry (first byte = header).
 * @param[in]  len     Number of bytes available in buf.
 * @param[in]  cfg     Config shadow (for PLOGTS and DTYPE of each peripheral).
 * @param[out] entry   Populated with parsed entry data.
 * @return NPZ2100_OK on success.
 *         NPZ2100_ERR_ARG if buf is NULL, len is 0, or not enough bytes.
 */
npz2100_err_t npz2100_datalog_parse_entry(const uint8_t           *buf,
                                           size_t                   len,
                                           const npz2100_config_t  *cfg,
                                           npz2100_datalog_entry_t *entry);

/**
 * @brief Read the raw SRAM log bytes into a caller-supplied buffer.
 *
 * Handles log rotation transparently:
 *   - No rotation: reads from start_addr+5 to cur_addr-1 (linear).
 *   - Rotation occurred: reads from cur_addr to 0xFF, then from
 *     start_addr+5 to cur_addr-1, so the buffer is always oldest-first.
 *
 * The caller must supply a buffer large enough for the entire log region
 * (up to 251 bytes: 0xFF - (LOG_START_ADDR+5) + 1).
 *
 * @param[in]  hal         Pointer to initialised HAL descriptor.
 * @param[in]  cfg         Config shadow (for SRAM bank handling).
 * @param[in]  info        Log info struct populated by npz2100_datalog_read_info().
 * @param[out] buf         Destination buffer (caller-allocated).
 * @param[in]  buf_len     Size of buf in bytes.
 * @param[out] bytes_read  Number of bytes written to buf.
 * @return NPZ2100_OK on success, NPZ2100_ERR_ARG if buf too small, NPZ2100_ERR_IO on bus error.
 */
npz2100_err_t npz2100_datalog_read_raw(const npz2100_hal_t          *hal,
                                        npz2100_config_t             *cfg,
                                        const npz2100_datalog_info_t *info,
                                        uint8_t                      *buf,
                                        size_t                        buf_len,
                                        size_t                       *bytes_read);

/**
 * @brief Read and parse all log entries from SRAM, oldest-first.
 *
 * Internally calls npz2100_datalog_read_raw() then iterates
 * npz2100_datalog_parse_entry() across the buffer.
 *
 * Partial entries at the rotation boundary (possible when different
 * peripherals have different entry sizes) are skipped silently - the
 * parser advances by 1 byte and re-tries alignment on the next header.
 *
 * @param[in]  hal          Pointer to initialised HAL descriptor.
 * @param[in]  cfg          Config shadow.
 * @param[in]  info         Log info struct from npz2100_datalog_read_info().
 * @param[out] entries      Caller-allocated array of npz2100_datalog_entry_t.
 * @param[in]  max_entries  Capacity of the entries array.
 * @param[out] count        Number of entries successfully parsed.
 * @return NPZ2100_OK on success, NPZ2100_ERR_ARG on bad arguments, NPZ2100_ERR_IO on bus error.
 */
npz2100_err_t npz2100_datalog_read_entries(const npz2100_hal_t          *hal,
                                            npz2100_config_t             *cfg,
                                            const npz2100_datalog_info_t *info,
                                            npz2100_datalog_entry_t      *entries,
                                            size_t                        max_entries,
                                            size_t                       *count);

/**
 * @brief Erase the SRAM log region and reset the write pointer.
 *
 * Writes 0xFF to every byte from LOG_START_ADDR to 0xFF and resets
 * the LOGCADDR pointer to LOG_START_ADDR + NPZ2100_LOG_ENTRY_OFFSET.
 * The LOGCFG, LOGSADDR registers are not modified.
 *
 * Call this after successfully extracting all entries if you want the
 * log to start fresh on the next idle cycle.
 *
 * @param[in] hal   Pointer to initialised HAL descriptor.
 * @param[in] cfg   Config shadow.
 * @param[in] info  Log info struct from npz2100_datalog_read_info().
 * @return NPZ2100_OK on success, NPZ2100_ERR_IO on I²C error.
 */
npz2100_err_t npz2100_datalog_clear(const npz2100_hal_t          *hal,
                                     npz2100_config_t             *cfg,
                                     const npz2100_datalog_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* NPZ2100_DATALOG_H_ */
