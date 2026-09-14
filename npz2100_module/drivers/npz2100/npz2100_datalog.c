/**
 * @file npz2100_datalog.c
 * @brief nPZ2100 SRAM data-log extraction and parsing implementation.
 *
 * Platform-agnostic - no OS, no HAL, no standard library beyond <string.h>.
 * All I²C operations go through the npz2100_hal_t callbacks.
 *
 * Log entry byte layout (produced autonomously by the nPZ2100 while the
 * host is powered off):
 *
 *   Byte 0      : header
 *                   bit7     = TRIG   (threshold was met)
 *                   bits6:4  = PER    (peripheral index 0–5)
 *                   bits3:0  = GCT_MS (global counter milliseconds nibble)
 *   Bytes 1–4   : GCT timestamp (GCT_0..GCT_3, LSB first) - only if PLOGTS=1
 *   Byte(s) N   : peripheral value - 1 byte (DTYPE=UINT8) or 2 bytes (UINT16/INT16)
 *
 * Idle-entry timestamp at LOG_START_ADDR (written by nPZ2100 on every idle entry):
 *   Byte 0      : GCT_MS
 *   Bytes 1–4   : GCT_0..GCT_3
 *
 * @version 0.8
 * @date    2026-09-11
 * @author  Nanopower Semiconductor AS
 */

#include "npz2100_datalog.h"
#include "npz2100_regs_adc_log.h"
#include "npz2100_regs_periph.h"
#include <string.h>  /* memset */

/* =========================================================================
 * Internal helpers
 * ======================================================================= */

/**
 * @brief Return true if the peripheral slot's DTYPE indicates an 8-bit value.
 */
static bool is_8bit(const npz2100_config_t *cfg, uint8_t slot)
{
    uint8_t dtype = NPZ2100_MODP_DTYPE_GET(cfg->periph[slot].modp);
    return (dtype == NPZ2100_DTYPE_UINT8);
}

/**
 * @brief Return true if the peripheral slot has PLOGTS set (timestamp in log).
 */
static bool has_ts(const npz2100_config_t *cfg, uint8_t slot)
{
    return (cfg->periph[slot].cfgp & NPZ2100_CFGP_PLOGTS_MSK) != 0u;
}

/**
 * @brief Compute log entry size for a given peripheral slot.
 *
 * 1 (header) + 4 (timestamp, if PLOGTS) + 1 or 2 (value per DTYPE).
 */
static uint8_t entry_size_for_slot(const npz2100_config_t *cfg, uint8_t slot)
{
    uint8_t sz = 1u;                       /* header always present */
    if (has_ts(cfg, slot))  { sz += 4u; } /* optional 32-bit GCT  */
    if (is_8bit(cfg, slot)) { sz += 1u; } /* value: 8-bit         */
    else                    { sz += 2u; } /* value: 16-bit        */
    return sz;
}

/**
 * @brief Validate that a header byte encodes a legal peripheral index (0–5).
 *
 * Used to detect misalignment when scanning for entries after rotation.
 */
static bool header_valid(uint8_t hdr)
{
    uint8_t per = (hdr & NPZ2100_LOG_HDR_PER_MSK) >> NPZ2100_LOG_HDR_PER_POS;
    return (per <= NPZ2100_P_BANK_MAX);
}

/* =========================================================================
 * npz2100_datalog_entry_size
 * ======================================================================= */

uint8_t npz2100_datalog_entry_size(const npz2100_config_t *cfg, uint8_t slot)
{
    if (cfg == NULL || slot > NPZ2100_P_BANK_MAX) {
        return 0u;
    }
    return entry_size_for_slot(cfg, slot);
}

/* =========================================================================
 * npz2100_datalog_read_info
 * ======================================================================= */

npz2100_err_t npz2100_datalog_read_info(const npz2100_hal_t    *hal,
                                         npz2100_config_t       *cfg,
                                         npz2100_datalog_info_t *info)
{
    if (hal == NULL || cfg == NULL || info == NULL) {
        return NPZ2100_ERR_ARG;
    }

    memset(info, 0, sizeof(*info));

    /* Read LOGCFG, LOGSADDR, LOGCADDR in a single burst. */
    uint8_t regs[3];
    npz2100_err_t err = npz2100_reg_burst_read(hal,
                                                NPZ2100_REG_LOGCFG,
                                                regs, sizeof(regs));
    if (err != NPZ2100_OK) {
        return err;
    }

    info->log_enabled = (regs[0] & NPZ2100_LOGCFG_LOG_EN_MSK)     != 0u;
    info->rotation    = (regs[0] & NPZ2100_LOGCFG_LOG_ROT_MSK)    != 0u;
    info->rotated     = (regs[0] & NPZ2100_LOGCFG_LOG_IS_ROT_MSK) != 0u;
    info->start_addr  = regs[1];   /* LOGSADDR */
    info->cur_addr    = regs[2];   /* LOGCADDR */

    /* Read the 5-byte idle-entry timestamp from SRAM at start_addr. */
    uint8_t ts_buf[NPZ2100_LOG_TIMESTAMP_SIZE];
    err = npz2100_sram_read_ll(hal, cfg, info->start_addr,
                                ts_buf, sizeof(ts_buf));
    if (err != NPZ2100_OK) {
        return err;
    }

    info->ts_gct_ms  = ts_buf[0];
    info->ts_gct[0]  = ts_buf[1];   /* GCT_0 - LSB */
    info->ts_gct[1]  = ts_buf[2];
    info->ts_gct[2]  = ts_buf[3];
    info->ts_gct[3]  = ts_buf[4];   /* GCT_3 - MSB */

    return NPZ2100_OK;
}

/* =========================================================================
 * npz2100_datalog_parse_entry
 * ======================================================================= */

npz2100_err_t npz2100_datalog_parse_entry(const uint8_t           *buf,
                                           size_t                   len,
                                           const npz2100_config_t  *cfg,
                                           npz2100_datalog_entry_t *entry)
{
    if (buf == NULL || cfg == NULL || entry == NULL || len == 0u) {
        return NPZ2100_ERR_ARG;
    }

    memset(entry, 0, sizeof(*entry));

    /* Decode header. */
    uint8_t hdr = buf[0];
    entry->triggered  = (hdr & NPZ2100_LOG_HDR_TRIG_MSK) != 0u;
    entry->peripheral = (hdr & NPZ2100_LOG_HDR_PER_MSK) >> NPZ2100_LOG_HDR_PER_POS;
    entry->gct_ms     = (hdr & NPZ2100_LOG_HDR_GCTMS_MSK);

    if (entry->peripheral > NPZ2100_P_BANK_MAX) {
        return NPZ2100_ERR_ARG;
    }

    /* Determine expected size from peripheral shadow config. */
    uint8_t slot       = entry->peripheral;
    entry->has_timestamp = has_ts(cfg, slot);
    bool    val_8bit   = is_8bit(cfg, slot);
    uint8_t needed     = entry_size_for_slot(cfg, slot);

    if (len < (size_t)needed) {
        return NPZ2100_ERR_ARG;  /* not enough bytes */
    }

    size_t pos = 1u;  /* advance past header */

    /* Optional 4-byte timestamp (GCT_0..3, LSB first). */
    if (entry->has_timestamp) {
        entry->timestamp  = (uint32_t)buf[pos];
        entry->timestamp |= (uint32_t)buf[pos + 1u] << 8u;
        entry->timestamp |= (uint32_t)buf[pos + 2u] << 16u;
        entry->timestamp |= (uint32_t)buf[pos + 3u] << 24u;
        pos += 4u;
    }

    /* Value - 1 or 2 bytes. */
    if (val_8bit) {
        entry->value = buf[pos];
        pos += 1u;
    } else {
        entry->value = (uint16_t)buf[pos] | ((uint16_t)buf[pos + 1u] << 8u);
        pos += 2u;
    }

    entry->entry_size = (uint8_t)pos;

    return NPZ2100_OK;
}

/* =========================================================================
 * npz2100_datalog_read_raw
 * ======================================================================= */

npz2100_err_t npz2100_datalog_read_raw(const npz2100_hal_t          *hal,
                                        npz2100_config_t             *cfg,
                                        const npz2100_datalog_info_t *info,
                                        uint8_t                      *buf,
                                        size_t                        buf_len,
                                        size_t                       *bytes_read)
{
    if (hal == NULL || cfg == NULL || info == NULL ||
        buf == NULL || bytes_read == NULL) {
        return NPZ2100_ERR_ARG;
    }

    *bytes_read = 0u;

    /* First log entry starts at start_addr + NPZ2100_LOG_ENTRY_OFFSET. */
    uint8_t entry_start = info->start_addr + NPZ2100_LOG_ENTRY_OFFSET;

    /* Sanity: if cur_addr has not advanced past the timestamp, nothing logged. */
    if (info->cur_addr <= entry_start && !info->rotated) {
        return NPZ2100_OK;  /* empty log - nothing to read */
    }

    npz2100_err_t err = NPZ2100_OK;
    size_t        written = 0u;

    if (!info->rotated) {
        /* ---- Linear (no rotation) ---------------------------------------- */
        /* Read from entry_start to cur_addr - 1.                             */
        if (info->cur_addr <= entry_start) {
            return NPZ2100_OK;  /* nothing written yet */
        }
        size_t sz = (size_t)(info->cur_addr - entry_start);
        if (sz > buf_len) {
            return NPZ2100_ERR_ARG;  /* buffer too small */
        }
        err = npz2100_sram_read_ll(hal, cfg, entry_start, buf, sz);
        if (err != NPZ2100_OK) {
            return err;
        }
        written = sz;

    } else {
        /* ---- Rotation occurred ------------------------------------------- */
        /* Oldest data: cur_addr → 0xFF                                       */
        /* Newest data: entry_start → cur_addr - 1                            */
        /*                                                                     */
        /* If cur_addr <= entry_start (wrapped all the way around), the full   */
        /* region from entry_start to 0xFF contains valid entries.             */

        size_t seg1_sz = 0u;
        size_t seg2_sz = 0u;

        /* Segment 1: cur_addr to 0xFF (oldest entries, if cur_addr > entry_start) */
        if (info->cur_addr > entry_start) {
            seg1_sz = (size_t)(NPZ2100_LOG_SRAM_MAX - info->cur_addr + 1u);
        }

        /* Segment 2: entry_start to cur_addr - 1 (newest entries) */
        if (info->cur_addr > entry_start) {
            seg2_sz = (size_t)(info->cur_addr - entry_start);
        } else {
            /* cur_addr wrapped to or before entry_start: full region valid */
            seg2_sz = (size_t)(NPZ2100_LOG_SRAM_MAX - entry_start + 1u);
        }

        if ((seg1_sz + seg2_sz) > buf_len) {
            return NPZ2100_ERR_ARG;  /* buffer too small */
        }

        /* Read segment 1 - oldest entries at the end of SRAM. */
        if (seg1_sz > 0u) {
            err = npz2100_sram_read_ll(hal, cfg, info->cur_addr,
                                       buf, seg1_sz);
            if (err != NPZ2100_OK) {
                return err;
            }
            written += seg1_sz;
        }

        /* Read segment 2 - newer entries at the start of the log region. */
        if (seg2_sz > 0u) {
            err = npz2100_sram_read_ll(hal, cfg, entry_start,
                                       buf + written, seg2_sz);
            if (err != NPZ2100_OK) {
                return err;
            }
            written += seg2_sz;
        }
    }

    *bytes_read = written;
    return NPZ2100_OK;
}

/* =========================================================================
 * npz2100_datalog_read_entries
 * ======================================================================= */

npz2100_err_t npz2100_datalog_read_entries(const npz2100_hal_t          *hal,
                                            npz2100_config_t             *cfg,
                                            const npz2100_datalog_info_t *info,
                                            npz2100_datalog_entry_t      *entries,
                                            size_t                        max_entries,
                                            size_t                       *count)
{
    if (hal == NULL || cfg == NULL || info == NULL ||
        entries == NULL || count == NULL || max_entries == 0u) {
        return NPZ2100_ERR_ARG;
    }

    *count = 0u;

    /* Stack-allocate a raw buffer for the entire log region.
     * Maximum log region size: 0xFF - (LOG_START_ADDR + 5) + 1 bytes.
     * In the worst case (LOG_START_ADDR=0x00) that is 251 bytes. */
    uint8_t raw[251u];
    size_t  raw_len = 0u;

    npz2100_err_t err = npz2100_datalog_read_raw(hal, cfg, info,
                                                  raw, sizeof(raw),
                                                  &raw_len);
    if (err != NPZ2100_OK) {
        return err;
    }

    if (raw_len == 0u) {
        return NPZ2100_OK;  /* empty log */
    }

    /* Walk the raw buffer and parse entries.
     * When a partial/misaligned entry is detected (parse returns ERR_ARG),
     * advance by 1 byte and retry - this handles partial overwrites at the
     * rotation boundary when mixed-size peripheral entries are in use.      */
    size_t pos    = 0u;
    size_t parsed = 0u;

    while (pos < raw_len && parsed < max_entries) {
        /* Skip 0xFF padding bytes that may precede entry_start or follow the
         * last written entry (nPZ2100 initialises the log region with 0xFF). */
        if (raw[pos] == 0xFFu) {
            pos++;
            continue;
        }

        /* Validate header before attempting a full parse. */
        if (!header_valid(raw[pos])) {
            pos++;  /* misaligned - advance and retry */
            continue;
        }

        npz2100_datalog_entry_t e;
        npz2100_err_t pe = npz2100_datalog_parse_entry(raw + pos,
                                                        raw_len - pos,
                                                        cfg, &e);
        if (pe != NPZ2100_OK) {
            /* Not enough bytes for a complete entry - stop. */
            break;
        }

        entries[parsed] = e;
        parsed++;
        pos += (size_t)e.entry_size;
    }

    *count = parsed;
    return NPZ2100_OK;
}

/* =========================================================================
 * npz2100_datalog_clear
 * ======================================================================= */

npz2100_err_t npz2100_datalog_clear(const npz2100_hal_t          *hal,
                                     npz2100_config_t             *cfg,
                                     const npz2100_datalog_info_t *info)
{
    if (hal == NULL || cfg == NULL || info == NULL) {
        return NPZ2100_ERR_ARG;
    }

    /* Fill the entire log region (start_addr to 0xFF) with 0xFF. */
    uint8_t erase_buf[256u];
    size_t  erase_len = (size_t)(NPZ2100_LOG_SRAM_MAX - info->start_addr + 1u);
    memset(erase_buf, 0xFFu, erase_len);

    npz2100_err_t err = npz2100_sram_write_ll(hal, cfg,
                                               info->start_addr,
                                               erase_buf, erase_len);
    if (err != NPZ2100_OK) {
        return err;
    }

    /* Reset LOGCADDR to point at the first entry slot (start_addr + 5). */
    uint8_t reset_addr = info->start_addr + NPZ2100_LOG_ENTRY_OFFSET;
    err = npz2100_reg_write(hal, NPZ2100_REG_LOGCADDR, reset_addr);

    return err;
}
