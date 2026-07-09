/**
 * @file npz2100_mid.h
 * @brief nPZ2100 mid-level API — register map application, shadow diffing,
 *        and typed configuration helpers.
 *
 * Layering
 * --------
 *
 *   ┌─────────────────────────────────────────────┐
 *   │            Application code                 │
 *   ├─────────────────────────────────────────────┤
 *   │   Mid-level API   (this file + npz2100_mid.c)│
 *   │   • flat byte-stream regmap parsing         │
 *   │   • npz2100_config_t   — flat shadow struct  │
 *   │   • map apply / read-back / diff             │
 *   │   • typed config helpers (system, IO, periph,│
 *   │     ADC, logging, counter)                   │
 *   ├─────────────────────────────────────────────┤
 *   │   Low-level API   (npz2100_hal.h / .c)      │
 *   │   • reg_write / reg_read / burst_* / rmw    │
 *   ├─────────────────────────────────────────────┤
 *   │   HAL callbacks   (user-supplied)            │
 *   └─────────────────────────────────────────────┘
 *
 * Register-map format
 * --------------------
 * The external configuration tool produces a flat `const uint8_t[]` byte
 * stream — NOT a struct array. It is a concatenation of variable-length
 * segments, each shaped like this:
 *
 * @code
 *   [length] [start_addr] [data_0] [data_1] ... [data_(length-2)]
 * @endcode
 *
 * - `length`     — total bytes in this segment, INCLUDING start_addr.
 *                  The data payload is therefore `length - 1` bytes.
 * - `start_addr` — first register address; data bytes are written to
 *                  start_addr, start_addr+1, start_addr+2, ... in order.
 *
 * Segments are simply concatenated back-to-back with no separator or
 * terminator. The caller supplies the total buffer length (typically via
 * `sizeof()` on a compile-time array) and the parser consumes segments
 * until that length is exhausted.
 *
 * Example (matches Nanopower's reference tool output):
 * @code
 *   const uint8_t regmap[] = {
 *       9, 0x05, 0x00, 0x00, 0xFF, 0x00, 0x1D, 0x00, 0x00, 0xFF, 0xFF,
 *       15, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
 *           0x00, 0x00, 0x00, 0x00, 0x00,
 *       // ... more segments ...
 *   };
 *   npz2100_map_apply(&hal, &shadow, regmap, sizeof(regmap));
 * @endcode
 *
 * P_BANK (0x1F) is just another addressable register within this stream —
 * it requires no special handling by the caller. When a segment writes to
 * 0x1F, that becomes the active bank for any subsequent banked register
 * addresses (0x20–0x2D) in that same or a later segment, exactly as it
 * would on real hardware.
 *
 * Shadow diffing
 * --------------
 * The driver maintains an npz2100_config_t: a flat struct with one uint8_t
 * field per writable register, acting as a shadow of the device state.
 *
 * When npz2100_map_apply() is called:
 *   1. The byte stream is parsed segment by segment.
 *   2. Each resulting (addr, value) pair is compared against the shadow.
 *   3. Only registers whose value differs are written to the device — a
 *      single I²C transaction per changed register.
 *   4. The shadow is updated to match.
 *
 * This minimises I²C bus activity, which matters on the nPZ2100's 100 kHz bus.
 *
 * Typed config helpers
 * --------------------
 * Functions such as npz2100_sys_set() and npz2100_periph_set() write into
 * the shadow struct using named fields and bitfield macros from the
 * low-level headers. Call npz2100_map_apply() afterwards to push changes to
 * the device, or npz2100_shadow_write_reg() for an immediate single-register
 * write that also updates the shadow.
 *
 * @version 0.7
 * @date    2026-05-06
 * @author  Nanopower Semiconductor AS
 */

#ifndef NPZ2100_MID_H
#define NPZ2100_MID_H

#include "npz2100_hal.h"
#include "npz2100_regs_system.h"
#include "npz2100_regs_io.h"
#include "npz2100_regs_periph.h"
#include "npz2100_regs_adc_log.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Register map — byte-stream format produced by the configuration tool
 * ======================================================================= */

/**
 * @brief Maximum data bytes in a single regmap segment.
 *
 * Bounds the size of internal stack buffers used while parsing. The longest
 * segment in practice covers one full peripheral bank (14 bytes: P_BANK +
 * CFGP..TCFGP) but this is sized generously for SRAM-init segments too.
 */
#define NPZ2100_REGMAP_MAX_SEGMENT_DATA  (64u)

/**
 * @brief Result of parsing one regmap segment — used internally and by
 *        npz2100_map_foreach() callbacks.
 */
typedef struct {
    uint8_t start_addr;  /**< First register address in this segment. */
    uint8_t data_len;     /**< Number of data bytes (length - 1).       */
    const uint8_t *data; /**< Pointer into the original buffer.        */
} npz2100_regmap_segment_t;

/* =========================================================================
 * Shadow / config struct — flat mirror of every writable register
 * ======================================================================= */

/**
 * @brief Peripheral configuration shadow (banked registers 0x20–0x2D).
 *
 * One instance per peripheral slot (indices 0–5).
 * Read-only VALP (0x2E–0x2F) is not mirrored here.
 */
typedef struct {
    uint8_t cfgp;     /**< 0x20 CFGP  — power mode, polling mode, logging. */
    uint8_t iop;      /**< 0x21 IOP   — pin assignment.                     */
    uint8_t modp;     /**< 0x22 MODP  — SPI mode, data type.                */
    uint8_t perp_l;   /**< 0x23 PERP_L — polling period LSB.                */
    uint8_t perp_h;   /**< 0x24 PERP_H — polling period MSB.                */
    uint8_t ncmdp;    /**< 0x25 NCMDP — number of init commands.             */
    uint8_t addrp;    /**< 0x26 ADDRP — I²C address / SPI byte count.       */
    uint8_t rregp;    /**< 0x27 RREGP — read register address.               */
    uint8_t throvp_l; /**< 0x28 THROVP_L — over-threshold LSB.              */
    uint8_t throvp_h; /**< 0x29 THROVP_H — over-threshold MSB.              */
    uint8_t thrunp_l; /**< 0x2A THRUNP_L — under-threshold LSB.             */
    uint8_t thrunp_h; /**< 0x2B THRUNP_H — under-threshold MSB.             */
    uint8_t twtp;     /**< 0x2C TWTP  — wait time.                          */
    uint8_t tcfgp;    /**< 0x2D TCFGP — timing/protocol options.            */
} npz2100_periph_shadow_t;

/**
 * @brief Flat shadow of all writable nPZ2100 registers.
 *
 * Mirrors the device state after the last successful apply or read-back.
 * Application code modifies fields here via typed helpers, then pushes
 * changes with npz2100_map_apply() or npz2100_shadow_write_reg().
 *
 * Field names match the register names in the datasheet exactly.
 */
typedef struct {
    /* --- System / global (non-banked) ---------------------------------- */
    uint8_t idle_rst;   /**< 0x00 IDLE_RST  */
    uint8_t p_bank;     /**< 0x1F P_BANK — tracks the currently active bank on
                              the device (0–5). Updated whenever 0x1F is
                              written, including via npz2100_map_apply(). */
    uint8_t iocfg1;     /**< 0x05 IOCFG1    */
    uint8_t iocfg2;     /**< 0x06 IOCFG2    */
    uint8_t iocfg3;     /**< 0x07 IOCFG3    */
    uint8_t iocfg4;     /**< 0x08 IOCFG4    */
    uint8_t iocfg5;     /**< 0x09 IOCFG5    */
    uint8_t syscfg1;    /**< 0x0A SYSCFG1   */
    uint8_t syscfg2;    /**< 0x0B SYSCFG2   */
    uint8_t tout_l;     /**< 0x0C TOUT_L    */
    uint8_t tout_h;     /**< 0x0D TOUT_H    */
    uint8_t gct_ms;     /**< 0x10 GCT_MS    */
    uint8_t gct_0;      /**< 0x11 GCT_0     */
    uint8_t gct_1;      /**< 0x12 GCT_1     */
    uint8_t gct_2;      /**< 0x13 GCT_2     */
    uint8_t gct_3;      /**< 0x14 GCT_3     */
    uint8_t gct_alm_0;  /**< 0x15 GCT_ALM_0 */
    uint8_t gct_alm_1;  /**< 0x16 GCT_ALM_1 */
    uint8_t gct_alm_2;  /**< 0x17 GCT_ALM_2 */
    uint8_t gct_alm_3;  /**< 0x18 GCT_ALM_3 */
    uint8_t wdog_l;     /**< 0x19 WDOG_L    */
    uint8_t wdog_h;     /**< 0x1A WDOG_H    */
    uint8_t gtc_cfg;    /**< 0x1B GTC_CFG   */
    uint8_t pa_cfg;     /**< 0x1C PA_CFG    */

    /* --- Per-peripheral banked registers (6 slots) --------------------- */
    npz2100_periph_shadow_t periph[6]; /**< Indexed 0–5, matches P_BANK value. */

    /* --- ADC ----------------------------------------------------------- */
    uint8_t adccfg;     /**< 0x40 ADCCFG    */
    uint8_t throva1;    /**< 0x41 THROVA1   */
    uint8_t thruna1;    /**< 0x42 THRUNA1   */
    uint8_t throva2;    /**< 0x43 THROVA2   */
    uint8_t thruna2;    /**< 0x44 THRUNA2   */
    uint8_t throva3;    /**< 0x45 THROVA3   */
    uint8_t thruna3;    /**< 0x46 THRUNA3   */

    /* --- Logging ------------------------------------------------------- */
    uint8_t logcfg;     /**< 0x50 LOGCFG    */
    uint8_t logsaddr;   /**< 0x51 LOGSADDR  */

    /* --- Event counter ------------------------------------------------- */
    uint8_t cnt_val_0;  /**< 0x53 CNT_VAL_0 */
    uint8_t cnt_val_1;  /**< 0x54 CNT_VAL_1 */
    uint8_t cnt_val_2;  /**< 0x55 CNT_VAL_2 */
    uint8_t cnt_val_3;  /**< 0x56 CNT_VAL_3 */
    uint8_t cntcfg;     /**< 0x57 CNTCFG    */
    uint8_t cnt_trig_0; /**< 0x58 CNT_TRIG_0 */
    uint8_t cnt_trig_1; /**< 0x59 CNT_TRIG_1 */
    uint8_t cnt_trig_2; /**< 0x5A CNT_TRIG_2 */
    uint8_t cnt_trig_3; /**< 0x5B CNT_TRIG_3 */

    /* --- SRAM bank select ---------------------------------------------- */
    uint8_t sram_bank;  /**< 0x7F SRAM_BANK */
} npz2100_config_t;

/* =========================================================================
 * Typed parameter structs for config helpers
 * ======================================================================= */

/**
 * @brief System-level wake-up and timing configuration.
 */
typedef struct {
    uint16_t tout_value;    /**< Raw TOUT value (≥ NPZ2100_TOUT_MIN_SAFE).       */
    bool     tout_ext;      /**< true = 2-second increments; false = clock ticks. */
    uint8_t  wup_periph_mask; /**< Bitmask of peripherals as wake-up sources (bits 0–5 = P1–P6). */
    bool     wup_any;       /**< true = wake on any trigger; false = wait for all. */
    bool     wup_adc1;      /**< Enable ADC ch.1 as wake-up source.              */
    bool     wup_adc2;      /**< Enable ADC ch.2 as wake-up source.              */
    bool     wup_adc3;      /**< Enable ADC ch.3 (battery) as wake-up source.    */
    bool     clk_xo;        /**< true = use crystal XO; false = low-power RC.    */
} npz2100_sys_cfg_t;

/**
 * @brief Global time counter configuration.
 */
typedef struct {
    uint32_t gct_seconds;   /**< Current time to set (seconds).                  */
    uint8_t  gct_ms;        /**< Fractional 1/16 s ticks (0–15).                 */
    uint32_t alarm_seconds; /**< Alarm time in seconds.                           */
    bool     alarm_enable;  /**< Enable the alarm.                                */
    uint16_t wdog_value;    /**< Watchdog timeout in 2-second units (0 = off).   */
    bool     wdog_enable;   /**< Enable the watchdog.                             */
} npz2100_timer_cfg_t;

/**
 * @brief Power-aware mode configuration.
 */
typedef struct {
    bool    enable;   /**< Enable power-aware mode.                            */
    bool    no_wup;   /**< Disable all wake-ups when PA mode is active.        */
    uint8_t src;      /**< Source pin/channel — use NPZ2100_PA_SRC_* constants. */
} npz2100_pa_cfg_t;

/**
 * @brief Single peripheral configuration.
 *
 * Covers all fields in the banked register window (0x20–0x2D).
 */
typedef struct {
    /* Power and polling */
    uint8_t  pwmod;         /**< Power mode — use NPZ2100_PWMOD_* constants.    */
    uint8_t  tmod;          /**< Polling mode — use NPZ2100_TMOD_* constants.   */
    uint16_t period;        /**< Polling period in system clock periods (≥ 1).  */

    /* Pin assignment */
    uint8_t  psw_pin;       /**< Power switch pin — NPZ2100_IO_PIN_*.           */
    uint8_t  int_pin;       /**< Interrupt pin — NPZ2100_IO_PIN_*.              */
    uint8_t  csn_pin;       /**< SPI chip-select pin — NPZ2100_IO_PIN_*.        */

    /* Communication protocol */
    bool     use_spi;       /**< true = SPI, false = I²C.                       */
    uint8_t  spi_mode;      /**< SPI mode — NPZ2100_SPIMOD_*.                   */
    uint8_t  i2c_addr;      /**< I²C address (7-bit). Ignored when use_spi=true.*/
    uint8_t  read_reg;      /**< I²C register to read from.                     */
    bool     i2c_read_only; /**< Skip write phase, issue read directly.         */
    uint8_t  i2c_retries;   /**< Number of retries on NAK (0 = abort on NAK).  */

    /* Data */
    uint8_t  dtype;         /**< Data type — NPZ2100_DTYPE_*.                   */
    bool     swap_bytes;    /**< Swap high/low bytes after read.                */
    bool     seq_rw;        /**< Multi-byte sequential addressing.              */
    bool     inv_cmp;       /**< Invert threshold comparison (inside = trigger).*/
    bool     wunak;         /**< Wake up if peripheral NAKs.                    */
    uint16_t threshold_ov;  /**< Over-threshold value.                          */
    uint16_t threshold_un;  /**< Under-threshold value.                         */

    /* Timing */
    uint8_t  twt;           /**< Wait time value (units depend on tcfg flags).  */
    bool     twt_en;        /**< Enable post-init wait.                         */
    bool     twt_ext;       /**< Extend post-init wait unit to 4096 clocks.     */
    bool     tinit_en;      /**< Enable pre-init wait.                          */
    bool     tinit_ext;     /**< Extend pre-init wait unit to 4096 clocks.      */

    /* Logging */
    bool     log_en;        /**< Enable logging for this peripheral.            */
    bool     log_ts;        /**< Include timestamp in log entries.              */
    uint8_t  log_freq;      /**< Logging frequency — NPZ2100_PLOGF_*.          */

    /* SRAM init commands */
    uint8_t  ncmd;          /**< Number of init commands in SRAM.               */

    /* Power-aware polling */
    uint8_t  pamod;         /**< PA polling multiplier — NPZ2100_PAMOD_*.      */
} npz2100_periph_cfg_t;

/**
 * @brief ADC channel configuration.
 */
typedef struct {
    bool    en_ch1;         /**< Enable ADC channel 1 (ADC1 pin).              */
    bool    en_ch2;         /**< Enable ADC channel 2 (ADC2 pin).              */
    bool    en_ch3;         /**< Enable ADC channel 3 (battery reference).     */
    uint8_t clk_sel;        /**< Sampling clock — NPZ2100_ADC_CLK_*.           */
    bool    psync_ch1;      /**< Sync ch.1 with peripheral 1 polling.          */
    bool    psync_ch2;      /**< Sync ch.2 with peripheral 2 polling.          */
    uint8_t throva[3];      /**< Over-threshold for channels 1–3.              */
    uint8_t thruna[3];      /**< Under-threshold for channels 1–3.             */
} npz2100_adc_cfg_t;

/**
 * @brief SRAM logging configuration.
 */
typedef struct {
    bool    enable;         /**< Enable logging.                               */
    bool    rotate;         /**< Enable ring-buffer rotation.                  */
    uint8_t start_addr;     /**< Start address in SRAM (0x00–0xFF).           */
} npz2100_log_cfg_t;

/**
 * @brief Event counter configuration.
 */
typedef struct {
    bool     enable;        /**< Enable counter (also resets the count).       */
    uint8_t  src;           /**< Counter source — NPZ2100_CNT_SRC_*.          */
    uint32_t trigger;       /**< Trigger value — fires when count == trigger.  */
} npz2100_counter_cfg_t;

/* =========================================================================
 * Shadow initialisation
 * ======================================================================= */

/**
 * @brief Initialise the shadow struct to the nPZ2100 power-on reset defaults.
 *
 * Call this once before using any other mid-level function.
 * After calling this, the shadow reflects what the device looks like
 * immediately after reset — no I²C transaction is issued.
 *
 * @param[out] cfg  Pointer to the config shadow to initialise.
 * @return NPZ2100_OK or NPZ2100_ERR_ARG if cfg is NULL.
 */
npz2100_err_t npz2100_config_init_defaults(npz2100_config_t *cfg);

/* =========================================================================
 * Register-map operations
 * ======================================================================= */

/**
 * @brief Apply a tool-generated register map to the device, writing only
 *        registers that differ from the current shadow.
 *
 * Parses @p map as a concatenation of segments shaped
 * `[length][start_addr][data...]` (see file-level documentation above).
 * For each resulting register address / value pair:
 *   - Compare value against the corresponding shadow field.
 *   - If different: write to device, update shadow.
 *   - If identical: skip (zero I²C transactions for that register).
 *
 * P_BANK (0x1F) requires no special handling — it is written like any
 * other register in the stream, and subsequent banked addresses (0x20–0x2D)
 * within the same or a later segment resolve against whatever bank was
 * last written, exactly as on real hardware.
 *
 * @param[in]     hal       Pointer to initialised HAL descriptor.
 * @param[in,out] cfg       Shadow struct — updated for every register written.
 * @param[in]     map       Byte-stream register map from the configuration tool.
 * @param[in]     map_len   Total length of @p map in bytes (use sizeof() for
 *                          compile-time arrays).
 * @return NPZ2100_OK, or the first error encountered.
 *         NPZ2100_ERR_ARG if the stream is malformed (a segment's length
 *         field would read past the end of the buffer).
 */
npz2100_err_t npz2100_map_apply(const npz2100_hal_t *hal,
                                 npz2100_config_t    *cfg,
                                 const uint8_t       *map,
                                 size_t               map_len);

/**
 * @brief Read all device registers back and update the shadow.
 *
 * Useful after reset or power-cycle to resync the shadow with actual
 * device state before the first npz2100_map_apply() call.
 *
 * Read-only registers (STA1–STA3, ID, VALP, VAL_ADC*, LOGCADDR) are read
 * but not stored in the shadow (they have no shadow field).
 *
 * @param[in]  hal  Pointer to initialised HAL descriptor.
 * @param[out] cfg  Shadow struct to populate.
 * @return NPZ2100_OK, or an I²C error.
 */
npz2100_err_t npz2100_map_readback(const npz2100_hal_t *hal,
                                    npz2100_config_t    *cfg);

/**
 * @brief Compute the number of registers that differ between @p map and @p cfg.
 *
 * Non-destructive — does not write to the device or modify the shadow.
 * Parses the same byte-stream format as npz2100_map_apply().
 * Useful for logging or asserting before a critical apply.
 *
 * @param[in] cfg      Current shadow.
 * @param[in] map      Byte-stream register map to compare against.
 * @param[in] map_len  Total length of @p map in bytes.
 * @return Number of differing registers (0 if fully in sync).
 */
uint8_t npz2100_map_diff_count(const npz2100_config_t *cfg,
                                const uint8_t          *map,
                                size_t                  map_len);

/**
 * @brief Validate a regmap byte stream without applying it.
 *
 * Walks every segment and checks that each segment's length field does not
 * run past the end of the buffer. Does not touch the device or shadow.
 * Useful for sanity-checking tool output before calling npz2100_map_apply().
 *
 * @param[in] map      Byte-stream register map to validate.
 * @param[in] map_len  Total length of @p map in bytes.
 * @return NPZ2100_OK if well-formed, NPZ2100_ERR_ARG if malformed.
 */
npz2100_err_t npz2100_map_validate(const uint8_t *map, size_t map_len);

/**
 * @brief Write a single register immediately and update the shadow.
 *
 * Convenience function for one-off register updates without going through
 * a full map apply cycle.  Issues one I²C transaction.
 *
 * For banked peripheral registers (0x20–0x2D), use the typed peripheral
 * helpers instead — they select the correct P_BANK automatically.
 *
 * @param[in]     hal    Pointer to initialised HAL descriptor.
 * @param[in,out] cfg    Shadow to update.
 * @param[in]     addr   Register address.
 * @param[in]     value  Value to write.
 * @return NPZ2100_OK or an error code.
 */
npz2100_err_t npz2100_shadow_write_reg(const npz2100_hal_t *hal,
                                        npz2100_config_t    *cfg,
                                        uint8_t              addr,
                                        uint8_t              value);

/* =========================================================================
 * Typed configuration helpers
 * All helpers write into the shadow only.
 * Call npz2100_map_apply() afterwards to push changes to the device,
 * or npz2100_shadow_write_reg() for an immediate single-register write.
 * ======================================================================= */

/**
 * @brief Apply system-level wake-up and timing parameters to the shadow.
 *
 * Encodes SYSCFG1, SYSCFG2, TOUT_L, TOUT_H.
 *
 * @param[in,out] cfg  Config shadow.
 * @param[in]     sys  System configuration parameters.
 * @return NPZ2100_OK or NPZ2100_ERR_ARG (e.g. tout_value < NPZ2100_TOUT_MIN_SAFE).
 */
npz2100_err_t npz2100_sys_set(npz2100_config_t      *cfg,
                               const npz2100_sys_cfg_t *sys);

/**
 * @brief Apply global timer, alarm, and watchdog parameters to the shadow.
 *
 * Encodes GCT_MS, GCT_[0-3], GCT_ALM_[0-3], WDOG_L/H, GTC_CFG.
 *
 * @param[in,out] cfg    Config shadow.
 * @param[in]     timer  Timer configuration parameters.
 * @return NPZ2100_OK or NPZ2100_ERR_ARG.
 */
npz2100_err_t npz2100_timer_set(npz2100_config_t        *cfg,
                                 const npz2100_timer_cfg_t *timer);

/**
 * @brief Apply power-aware mode parameters to the shadow.
 *
 * Encodes PA_CFG.
 *
 * @param[in,out] cfg  Config shadow.
 * @param[in]     pa   Power-aware configuration.
 * @return NPZ2100_OK or NPZ2100_ERR_ARG (invalid src).
 */
npz2100_err_t npz2100_pa_set(npz2100_config_t     *cfg,
                               const npz2100_pa_cfg_t *pa);

/**
 * @brief Apply full I/O configuration to the shadow.
 *
 * Encodes IOCFG1–IOCFG5 directly from raw bytes.
 * For bitfield-level updates prefer npz2100_shadow_write_reg() with RMW.
 *
 * @param[in,out] cfg    Config shadow.
 * @param[in]     iocfg1 Raw IOCFG1 value.
 * @param[in]     iocfg2 Raw IOCFG2 value.
 * @param[in]     iocfg3 Raw IOCFG3 value.
 * @param[in]     iocfg4 Raw IOCFG4 value.
 * @param[in]     iocfg5 Raw IOCFG5 value.
 * @return NPZ2100_OK or NPZ2100_ERR_ARG.
 */
npz2100_err_t npz2100_io_set(npz2100_config_t *cfg,
                               uint8_t           iocfg1,
                               uint8_t           iocfg2,
                               uint8_t           iocfg3,
                               uint8_t           iocfg4,
                               uint8_t           iocfg5);

/**
 * @brief Apply a full peripheral configuration to the shadow.
 *
 * Encodes all banked registers (CFGP, IOP, MODP, PERP, NCMDP, ADDRP,
 * RREGP, THROVP, THRUNP, TWTP, TCFGP) for peripheral slot @p slot.
 *
 * Does NOT write P_BANK or touch the device — call npz2100_map_apply()
 * or npz2100_periph_apply() afterwards.
 *
 * @param[in,out] cfg   Config shadow.
 * @param[in]     slot  Peripheral index 0–5 (matches P_BANK value).
 * @param[in]     pcfg  Peripheral configuration parameters.
 * @return NPZ2100_OK or NPZ2100_ERR_ARG (invalid slot or period = 0).
 */
npz2100_err_t npz2100_periph_set(npz2100_config_t          *cfg,
                                   uint8_t                    slot,
                                   const npz2100_periph_cfg_t *pcfg);

/**
 * @brief Write one peripheral's shadow registers to the device immediately.
 *
 * Selects the peripheral bank, diffs against the shadow, and writes only
 * changed registers — then restores P_BANK to 0 on exit.
 *
 * @param[in]     hal   Pointer to initialised HAL descriptor.
 * @param[in,out] cfg   Config shadow.
 * @param[in]     slot  Peripheral index 0–5.
 * @return NPZ2100_OK or an error code.
 */
npz2100_err_t npz2100_periph_apply(const npz2100_hal_t *hal,
                                    npz2100_config_t    *cfg,
                                    uint8_t              slot);

/**
 * @brief Apply ADC configuration to the shadow.
 *
 * Encodes ADCCFG, THROVA[1-3], THRUNA[1-3].
 *
 * @param[in,out] cfg   Config shadow.
 * @param[in]     adc   ADC configuration.
 * @return NPZ2100_OK or NPZ2100_ERR_ARG.
 */
npz2100_err_t npz2100_adc_set(npz2100_config_t      *cfg,
                                const npz2100_adc_cfg_t *adc);

/**
 * @brief Apply logging configuration to the shadow.
 *
 * Encodes LOGCFG and LOGSADDR.
 *
 * @param[in,out] cfg  Config shadow.
 * @param[in]     log  Logging configuration.
 * @return NPZ2100_OK or NPZ2100_ERR_ARG (start_addr > 0xFF).
 */
npz2100_err_t npz2100_log_set(npz2100_config_t      *cfg,
                                const npz2100_log_cfg_t *log);

/**
 * @brief Apply event counter configuration to the shadow.
 *
 * Encodes CNTCFG, CNTTRIG[0-3].
 *
 * @param[in,out] cfg  Config shadow.
 * @param[in]     cnt  Counter configuration.
 * @return NPZ2100_OK or NPZ2100_ERR_ARG (invalid src).
 */
npz2100_err_t npz2100_counter_set(npz2100_config_t          *cfg,
                                   const npz2100_counter_cfg_t *cnt);

/* =========================================================================
 * Device control helpers
 * ======================================================================= */

/**
 * @brief Verify the device is present and responding on the I²C bus.
 *
 * Reads the ID register and confirms it equals NPZ2100_ID_EXPECTED (0x74).
 *
 * @param[in] hal  Pointer to initialised HAL descriptor.
 * @return NPZ2100_OK if device found, NPZ2100_ERR_DEV if ID mismatch,
 *         NPZ2100_ERR_IO on bus error.
 */
npz2100_err_t npz2100_probe_ll(const npz2100_hal_t *hal);

/**
 * @brief Read all three status registers (STA1, STA2, STA3) in one burst.
 *
 * Also resets the watchdog timer (STA1/STA2 read is the watchdog kick).
 *
 * @param[in]  hal   Pointer to initialised HAL descriptor.
 * @param[out] sta1  Destination for STA1 byte (may be NULL to discard).
 * @param[out] sta2  Destination for STA2 byte (may be NULL to discard).
 * @param[out] sta3  Destination for STA3 byte (may be NULL to discard).
 * @return NPZ2100_OK or an I²C error.
 */
npz2100_err_t npz2100_status_read(const npz2100_hal_t *hal,
                                   uint8_t             *sta1,
                                   uint8_t             *sta2,
                                   uint8_t             *sta3);

/**
 * @brief Read the last sampled value for a peripheral.
 *
 * Selects the correct P_BANK, reads VALP_L and VALP_H, and reconstructs
 * a 16-bit value (zero-extends for 8-bit dtype).
 *
 * @param[in]  hal   Pointer to initialised HAL descriptor.
 * @param[in]  cfg   Config shadow (used to determine dtype for sign extension).
 * @param[in]  slot  Peripheral index 0–5.
 * @param[out] value Pointer to receive the raw value.
 * @return NPZ2100_OK or an error code.
 */
npz2100_err_t npz2100_periph_read_value_ll(const npz2100_hal_t *hal,
                                         const npz2100_config_t *cfg,
                                         uint8_t              slot,
                                         uint16_t            *value);

/**
 * @brief Read all three ADC channel values in one burst.
 *
 * @param[in]  hal   Pointer to initialised HAL descriptor.
 * @param[out] ch1   ADC channel 1 raw value (may be NULL).
 * @param[out] ch2   ADC channel 2 raw value (may be NULL).
 * @param[out] ch3   ADC channel 3 (battery) raw value (may be NULL).
 * @return NPZ2100_OK or an I²C error.
 */
npz2100_err_t npz2100_adc_read(const npz2100_hal_t *hal,
                                uint8_t             *ch1,
                                uint8_t             *ch2,
                                uint8_t             *ch3);

/**
 * @brief Write a byte sequence into the nPZ2100 SRAM.
 *
 * Handles bank switching automatically for writes that cross the 128-byte
 * bank boundary.  Uses burst writes for efficiency.
 *
 * @param[in]     hal        Pointer to initialised HAL descriptor.
 * @param[in,out] cfg        Shadow (SRAM_BANK field is updated as needed).
 * @param[in]     sram_addr  SRAM byte offset (0x00–0xFF).
 * @param[in]     data       Source data.
 * @param[in]     len        Number of bytes to write.
 * @return NPZ2100_OK, NPZ2100_ERR_ARG (out of range), or I²C error.
 */
npz2100_err_t npz2100_sram_write_ll(const npz2100_hal_t *hal,
                                  npz2100_config_t    *cfg,
                                  uint8_t              sram_addr,
                                  const uint8_t       *data,
                                  size_t               len);

/**
 * @brief Read a byte sequence from the nPZ2100 SRAM.
 *
 * Handles bank switching automatically.
 *
 * @param[in]  hal        Pointer to initialised HAL descriptor.
 * @param[in,out] cfg     Shadow (SRAM_BANK field is updated as needed).
 * @param[in]  sram_addr  SRAM byte offset (0x00–0xFF).
 * @param[out] data       Destination buffer.
 * @param[in]  len        Number of bytes to read.
 * @return NPZ2100_OK, NPZ2100_ERR_ARG, or I²C error.
 */
npz2100_err_t npz2100_sram_read_ll(const npz2100_hal_t *hal,
                                 npz2100_config_t    *cfg,
                                 uint8_t              sram_addr,
                                 uint8_t             *data,
                                 size_t               len);

/**
 * @brief Send the idle-mode command to hand control to the nPZ2100.
 *
 * After this call the host MCU is expected to sleep or power down.
 * The shadow is not modified (IDLE_RST always reads back 0x00).
 *
 * @param[in] hal  Pointer to initialised HAL descriptor.
 * @return NPZ2100_OK or an I²C error.
 */
npz2100_err_t npz2100_enter_idle_ll(const npz2100_hal_t *hal);

/**
 * @brief Issue a soft reset command.
 *
 * SRAM contents are preserved. The shadow should be re-synced with
 * npz2100_map_readback() or re-applied with npz2100_map_apply() after reset.
 *
 * @param[in] hal  Pointer to initialised HAL descriptor.
 * @return NPZ2100_OK or an I²C error.
 */
npz2100_err_t npz2100_soft_reset_ll(const npz2100_hal_t *hal);

#ifdef __cplusplus
}
#endif

#endif /* NPZ2100_MID_H */
