/**
 * @file npz2100_regs_periph.h
 * @brief Register addresses and bitfield macros — Per-Peripheral Configuration block.
 *
 * Covers: P_BANK (0x1F), CFGP (0x20), IOP (0x21), MODP (0x22),
 *         PERP (0x23–0x24), NCMDP (0x25), ADDRP (0x26), RREGP (0x27),
 *         THROVP (0x28–0x29), THRUNP (0x2A–0x2B), TWTP (0x2C),
 *         TCFGP (0x2D), VALP (0x2E–0x2F).
 *
 * Usage pattern
 * -------------
 * The registers at 0x20–0x2F are a "banked" window onto the configuration of
 * one peripheral at a time.  Before accessing any of these registers:
 *
 *   1. Write the desired peripheral index (0–5) to P_BANK (0x1F).
 *   2. Read or write the configuration registers.
 *
 * The driver function npz2100_periph_select() handles step 1.
 *
 * @version 0.7
 * @date    2026-05-06
 */

#ifndef NPZ2100_REGS_PERIPH_H
#define NPZ2100_REGS_PERIPH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * P_BANK  —  0x1F  (R/W)
 * Selects which peripheral's configuration is visible at 0x20–0x2F.
 * ======================================================================= */
#define NPZ2100_REG_P_BANK                (0x1Fu)

#define NPZ2100_P_BANK_MSK                (0x07u)
#define NPZ2100_P_BANK_POS                (0u)
#define NPZ2100_P_BANK(n)                 (uint8_t)((n) & NPZ2100_P_BANK_MSK)  /**< n = 0–5 */

/** Maximum valid peripheral bank index. */
#define NPZ2100_P_BANK_MAX                (5u)

/* =========================================================================
 * CFGP  —  0x20  (R/W)
 * Peripheral power mode, polling mode, and logging configuration.
 * ======================================================================= */
#define NPZ2100_REG_CFGP                  (0x20u)

/** [1:0] PWMOD: peripheral power mode. */
#define NPZ2100_CFGP_PWMOD_MSK            (0x03u)
#define NPZ2100_CFGP_PWMOD_POS            (0u)
#define NPZ2100_CFGP_PWMOD(v)             (uint8_t)(((v) & 0x03u) << NPZ2100_CFGP_PWMOD_POS)
#define NPZ2100_CFGP_PWMOD_GET(r)         (((r) & NPZ2100_CFGP_PWMOD_MSK) >> NPZ2100_CFGP_PWMOD_POS)

/** Power mode values (datasheet Table 40). */
#define NPZ2100_PWMOD_DISABLED            (0x00u)  /**< Peripheral fully disabled.       */
#define NPZ2100_PWMOD_PERIODIC            (0x01u)  /**< Periodic power-on.               */
#define NPZ2100_PWMOD_ALWAYS_ON           (0x03u)  /**< Always powered.                  */

/** [3:2] TMOD: peripheral polling mode. */
#define NPZ2100_CFGP_TMOD_MSK             (0x0Cu)
#define NPZ2100_CFGP_TMOD_POS             (2u)
#define NPZ2100_CFGP_TMOD(v)              (uint8_t)(((v) & 0x03u) << NPZ2100_CFGP_TMOD_POS)
#define NPZ2100_CFGP_TMOD_GET(r)          (((r) & NPZ2100_CFGP_TMOD_MSK) >> NPZ2100_CFGP_TMOD_POS)

/** Polling mode values (datasheet Table 41). */
#define NPZ2100_TMOD_INIT_READ_CMP        (0x00u)  /**< Init, read, compare threshold.              */
#define NPZ2100_TMOD_INIT_WAIT_INT        (0x01u)  /**< Init, wait for INT, read, compare.          */
#define NPZ2100_TMOD_INIT_WAIT_ONLY       (0x02u)  /**< Init and wait for INT (no read).            */
#define NPZ2100_TMOD_ASYNC_INT            (0x03u)  /**< Wait for asynchronous interrupt only.       */

/** [4] PLOG: enable logging of read values. */
#define NPZ2100_CFGP_PLOG_MSK             (0x10u)
#define NPZ2100_CFGP_PLOG_POS             (4u)
#define NPZ2100_CFGP_PLOG(v)              (uint8_t)(((v) & 0x01u) << NPZ2100_CFGP_PLOG_POS)

/** [5] PLOGTS: include timestamp in log entries. */
#define NPZ2100_CFGP_PLOGTS_MSK           (0x20u)
#define NPZ2100_CFGP_PLOGTS_POS           (5u)
#define NPZ2100_CFGP_PLOGTS(v)            (uint8_t)(((v) & 0x01u) << NPZ2100_CFGP_PLOGTS_POS)

/** [7:6] PLOGF: logging frequency. */
#define NPZ2100_CFGP_PLOGF_MSK            (0xC0u)
#define NPZ2100_CFGP_PLOGF_POS            (6u)
#define NPZ2100_CFGP_PLOGF(v)             (uint8_t)(((v) & 0x03u) << NPZ2100_CFGP_PLOGF_POS)
#define NPZ2100_CFGP_PLOGF_GET(r)         (((r) & NPZ2100_CFGP_PLOGF_MSK) >> NPZ2100_CFGP_PLOGF_POS)

/** Logging frequency values (datasheet Table 42). */
#define NPZ2100_PLOGF_EVERY_READ          (0x00u)  /**< Every sample.                */
#define NPZ2100_PLOGF_EVERY_2             (0x01u)  /**< Every 2 samples.             */
#define NPZ2100_PLOGF_EVERY_4             (0x02u)  /**< Every 4 samples.             */
#define NPZ2100_PLOGF_THRESHOLD_ONLY      (0x03u)  /**< Only on threshold crossing.  */

/* =========================================================================
 * IOP  —  0x21  (R/W)
 * Pin assignment for this peripheral (PSW, INT, CSN) and PA polling rate.
 * ======================================================================= */
#define NPZ2100_REG_IOP                   (0x21u)

/** [1:0] SELPSW: which SW_LP pin drives this peripheral. */
#define NPZ2100_IOP_SELPSW_MSK            (0x03u)
#define NPZ2100_IOP_SELPSW_POS            (0u)
#define NPZ2100_IOP_SELPSW(v)             (uint8_t)(((v) & 0x03u) << NPZ2100_IOP_SELPSW_POS)
#define NPZ2100_IOP_SELPSW_GET(r)         (((r) & NPZ2100_IOP_SELPSW_MSK) >> NPZ2100_IOP_SELPSW_POS)

/** [3:2] SELINT: which INT pin is assigned to this peripheral. */
#define NPZ2100_IOP_SELINT_MSK            (0x0Cu)
#define NPZ2100_IOP_SELINT_POS            (2u)
#define NPZ2100_IOP_SELINT(v)             (uint8_t)(((v) & 0x03u) << NPZ2100_IOP_SELINT_POS)
#define NPZ2100_IOP_SELINT_GET(r)         (((r) & NPZ2100_IOP_SELINT_MSK) >> NPZ2100_IOP_SELINT_POS)

/** [5:4] SELCSN: which CSN pin is assigned to this peripheral. */
#define NPZ2100_IOP_SELCSN_MSK            (0x30u)
#define NPZ2100_IOP_SELCSN_POS            (4u)
#define NPZ2100_IOP_SELCSN(v)             (uint8_t)(((v) & 0x03u) << NPZ2100_IOP_SELCSN_POS)
#define NPZ2100_IOP_SELCSN_GET(r)         (((r) & NPZ2100_IOP_SELCSN_MSK) >> NPZ2100_IOP_SELCSN_POS)

/** Pin assignment values (datasheet Table 44): 0=LP1/INT1/CSN1 … 3=LP4/INT4/CSN4 */
#define NPZ2100_IO_PIN_1                  (0x00u)
#define NPZ2100_IO_PIN_2                  (0x01u)
#define NPZ2100_IO_PIN_3                  (0x02u)
#define NPZ2100_IO_PIN_4                  (0x03u)

/** [7:6] PAMOD: polling period multiplier when power-aware mode is active. */
#define NPZ2100_IOP_PAMOD_MSK             (0xC0u)
#define NPZ2100_IOP_PAMOD_POS             (6u)
#define NPZ2100_IOP_PAMOD(v)              (uint8_t)(((v) & 0x03u) << NPZ2100_IOP_PAMOD_POS)
#define NPZ2100_IOP_PAMOD_GET(r)          (((r) & NPZ2100_IOP_PAMOD_MSK) >> NPZ2100_IOP_PAMOD_POS)

/** PAMOD values (datasheet Table 45). */
#define NPZ2100_PAMOD_NO_CHANGE           (0x00u)
#define NPZ2100_PAMOD_2X                  (0x01u)
#define NPZ2100_PAMOD_4X                  (0x02u)
#define NPZ2100_PAMOD_DISABLED            (0x03u)

/* =========================================================================
 * MODP  —  0x22  (R/W)
 * SPI mode, data type, threshold logic, and I²C options.
 * ======================================================================= */
#define NPZ2100_REG_MODP                  (0x22u)

/** [0] CMOD: threshold trigger inversion.
 *  0=trigger on outside range, 1=trigger on inside range. */
#define NPZ2100_MODP_CMOD_MSK             (0x01u)
#define NPZ2100_MODP_CMOD_POS             (0u)
#define NPZ2100_MODP_CMOD(v)              (uint8_t)(((v) & 0x01u) << NPZ2100_MODP_CMOD_POS)

/** [2:1] DTYPE: data type of peripheral value. */
#define NPZ2100_MODP_DTYPE_MSK            (0x06u)
#define NPZ2100_MODP_DTYPE_POS            (1u)
#define NPZ2100_MODP_DTYPE(v)             (uint8_t)(((v) & 0x03u) << NPZ2100_MODP_DTYPE_POS)
#define NPZ2100_MODP_DTYPE_GET(r)         (((r) & NPZ2100_MODP_DTYPE_MSK) >> NPZ2100_MODP_DTYPE_POS)

/** Data type values (datasheet Table 47). */
#define NPZ2100_DTYPE_UINT16              (0x00u)  /**< 16-bit unsigned.  */
#define NPZ2100_DTYPE_INT16               (0x01u)  /**< 16-bit signed.    */
#define NPZ2100_DTYPE_UINT8               (0x02u)  /**< 8-bit unsigned.   */

/** [3] SEQRW: multi-byte sequential read/write addressing. */
#define NPZ2100_MODP_SEQRW_MSK            (0x08u)
#define NPZ2100_MODP_SEQRW_POS            (3u)
#define NPZ2100_MODP_SEQRW(v)             (uint8_t)(((v) & 0x01u) << NPZ2100_MODP_SEQRW_POS)

/** [4] WUNAK: wake up if peripheral I²C NAKs. */
#define NPZ2100_MODP_WUNAK_MSK            (0x10u)
#define NPZ2100_MODP_WUNAK_POS            (4u)
#define NPZ2100_MODP_WUNAK(v)             (uint8_t)(((v) & 0x01u) << NPZ2100_MODP_WUNAK_POS)

/** [5] SWPRREG: swap high/low bytes after read. */
#define NPZ2100_MODP_SWPRREG_MSK          (0x20u)
#define NPZ2100_MODP_SWPRREG_POS          (5u)
#define NPZ2100_MODP_SWPRREG(v)           (uint8_t)(((v) & 0x01u) << NPZ2100_MODP_SWPRREG_POS)

/** [7:6] SPIMOD: SPI clock polarity and phase (requires SPIEN in TCFGP). */
#define NPZ2100_MODP_SPIMOD_MSK           (0xC0u)
#define NPZ2100_MODP_SPIMOD_POS           (6u)
#define NPZ2100_MODP_SPIMOD(v)            (uint8_t)(((v) & 0x03u) << NPZ2100_MODP_SPIMOD_POS)
#define NPZ2100_MODP_SPIMOD_GET(r)        (((r) & NPZ2100_MODP_SPIMOD_MSK) >> NPZ2100_MODP_SPIMOD_POS)

/** SPI mode values (datasheet Table 48). */
#define NPZ2100_SPIMOD_0                  (0x00u)  /**< CPOL=0, CPHA=0. */
#define NPZ2100_SPIMOD_1                  (0x01u)  /**< CPOL=0, CPHA=1. */
#define NPZ2100_SPIMOD_2                  (0x02u)  /**< CPOL=1, CPHA=0. */
#define NPZ2100_SPIMOD_3                  (0x03u)  /**< CPOL=1, CPHA=1. */

/* =========================================================================
 * PERP  —  0x23–0x24  (R/W)
 * 16-bit polling period in system clock periods.
 * WARNING: value 0 is invalid and causes undefined behaviour.
 * ======================================================================= */
#define NPZ2100_REG_PERP_L                (0x23u)
#define NPZ2100_REG_PERP_H                (0x24u)

/* =========================================================================
 * NCMDP  —  0x25  (R/W)
 * Number of initialisation commands to send from SRAM.
 * I²C: number of (address, value) pairs.  SPI: number of bytes.
 * ======================================================================= */
#define NPZ2100_REG_NCMDP                 (0x25u)

/* =========================================================================
 * ADDRP  —  0x26  (R/W)
 * I²C mode: 7-bit I²C address of the peripheral.
 * SPI mode: number of bytes to send from SRAM for the data read phase.
 * ======================================================================= */
#define NPZ2100_REG_ADDRP                 (0x26u)

/* =========================================================================
 * RREGP  —  0x27  (R/W)
 * I²C mode: register address of lower 8 bits of the value to read.
 * For 16-bit types, the next register (RREGP+1) gives the upper byte.
 * ======================================================================= */
#define NPZ2100_REG_RREGP                 (0x27u)

/* =========================================================================
 * THROVP  —  0x28–0x29  (R/W)
 * 16-bit over-threshold value.  Match DTYPE setting (MODP).
 * ======================================================================= */
#define NPZ2100_REG_THROVP_L              (0x28u)
#define NPZ2100_REG_THROVP_H              (0x29u)

/* =========================================================================
 * THRUNP  —  0x2A–0x2B  (R/W)
 * 16-bit under-threshold value.  Match DTYPE setting (MODP).
 * ======================================================================= */
#define NPZ2100_REG_THRUNP_L              (0x2Au)
#define NPZ2100_REG_THRUNP_H              (0x2Bu)

/* =========================================================================
 * TWTP  —  0x2C  (R/W)
 * Wait time before/after init.  Unit depends on TCFGP TWT_EXT / TINIT_EXT:
 *   EXT=0 → TWTP × 256 periods of internal 400 kHz clock
 *   EXT=1 → TWTP × 4096 periods of internal 400 kHz clock
 * ======================================================================= */
#define NPZ2100_REG_TWTP                  (0x2Cu)

/* =========================================================================
 * TCFGP  —  0x2D  (R/W)
 * Timing and communication protocol options for this peripheral.
 * ======================================================================= */
#define NPZ2100_REG_TCFGP                 (0x2Du)

/** [0] TWT_EN: enable post-initialisation wait (TWTP). */
#define NPZ2100_TCFGP_TWT_EN_MSK          (0x01u)
#define NPZ2100_TCFGP_TWT_EN_POS          (0u)
#define NPZ2100_TCFGP_TWT_EN(v)           (uint8_t)(((v) & 0x01u) << NPZ2100_TCFGP_TWT_EN_POS)

/** [1] TWT_EXT: extend post-init wait unit to 4096 clock periods. */
#define NPZ2100_TCFGP_TWT_EXT_MSK         (0x02u)
#define NPZ2100_TCFGP_TWT_EXT_POS         (1u)
#define NPZ2100_TCFGP_TWT_EXT(v)          (uint8_t)(((v) & 0x01u) << NPZ2100_TCFGP_TWT_EXT_POS)

/** [2] TINIT_EN: enable pre-initialisation wait (TWTP). */
#define NPZ2100_TCFGP_TINIT_EN_MSK        (0x04u)
#define NPZ2100_TCFGP_TINIT_EN_POS        (2u)
#define NPZ2100_TCFGP_TINIT_EN(v)         (uint8_t)(((v) & 0x01u) << NPZ2100_TCFGP_TINIT_EN_POS)

/** [3] TINIT_EXT: extend pre-init wait unit to 4096 clock periods. */
#define NPZ2100_TCFGP_TINIT_EXT_MSK       (0x08u)
#define NPZ2100_TCFGP_TINIT_EXT_POS       (3u)
#define NPZ2100_TCFGP_TINIT_EXT(v)        (uint8_t)(((v) & 0x01u) << NPZ2100_TCFGP_TINIT_EXT_POS)

/** [4] I2CRET: number of I²C retries on NAK (0=no retries, abort on NAK). */
#define NPZ2100_TCFGP_I2CRET_MSK          (0x10u)
#define NPZ2100_TCFGP_I2CRET_POS          (4u)
#define NPZ2100_TCFGP_I2CRET(v)           (uint8_t)(((v) & 0x01u) << NPZ2100_TCFGP_I2CRET_POS)

/** [5] I2CRO: read-only mode — skip write of RREGP, issue read directly. */
#define NPZ2100_TCFGP_I2CRO_MSK           (0x20u)
#define NPZ2100_TCFGP_I2CRO_POS           (5u)
#define NPZ2100_TCFGP_I2CRO(v)            (uint8_t)(((v) & 0x01u) << NPZ2100_TCFGP_I2CRO_POS)

/** [7] SPIEN: select SPI (1) or I²C (0) for this peripheral. */
#define NPZ2100_TCFGP_SPIEN_MSK           (0x80u)
#define NPZ2100_TCFGP_SPIEN_POS           (7u)
#define NPZ2100_TCFGP_SPIEN(v)            (uint8_t)(((v) & 0x01u) << NPZ2100_TCFGP_SPIEN_POS)
#define NPZ2100_TCFGP_SPIEN_GET(r)        (((r) & NPZ2100_TCFGP_SPIEN_MSK) >> NPZ2100_TCFGP_SPIEN_POS)

/* =========================================================================
 * VALP  —  0x2E–0x2F  (R)
 * Last read value from peripheral (8 or 16-bit, per DTYPE).
 * ======================================================================= */
#define NPZ2100_REG_VALP_L                (0x2Eu)
#define NPZ2100_REG_VALP_H                (0x2Fu)

#ifdef __cplusplus
}
#endif

#endif /* NPZ2100_REGS_PERIPH_H */
