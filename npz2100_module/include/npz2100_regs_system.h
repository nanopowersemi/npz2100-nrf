/**
 * @file npz2100_regs_system.h
 * @brief Register addresses and bitfield macros — System / Global block.
 *
 * Covers: IDLE_RST, ID, STA1–STA3, SYSCFG1–SYSCFG2, TOUT, GCT,
 *         GCTALM, WDOG, GTC_CFG, PA_CFG.
 *
 * Naming convention
 * -----------------
 *  NPZ2100_REG_<NAME>          Register address (uint8_t).
 *  NPZ2100_<REG>_<FIELD>_MSK  Bitmask for the field inside the register.
 *  NPZ2100_<REG>_<FIELD>_POS  LSB position of the field (for shifting).
 *  NPZ2100_<REG>_<FIELD>(v)   Encode value v into the field position.
 *  NPZ2100_<REG>_<FIELD>_GET(r) Extract field value from raw register byte r.
 *
 * @version 0.7
 * @date    2026-05-06
 */

#ifndef NPZ2100_REGS_SYSTEM_H
#define NPZ2100_REGS_SYSTEM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * IDLE_RST  —  0x00  (R/W)
 * Put device in idle mode or trigger a reset.
 * Always reads as 0x00.
 * ======================================================================= */
#define NPZ2100_REG_IDLE_RST              (0x00u)

/** Write 0xFF to enter idle mode. */
#define NPZ2100_IDLE_RST_ENTER_IDLE       (0xFFu)
/** Write 0xA5 to perform a soft reset. */
#define NPZ2100_IDLE_RST_SOFT_RESET       (0xA5u)
/** Write 0x5A to perform a full system reset. */
#define NPZ2100_IDLE_RST_SYS_RESET        (0x5Au)

/* =========================================================================
 * ID  —  0x01  (R)
 * Device identification.  Always reads 0x74.
 * ======================================================================= */
#define NPZ2100_REG_ID                    (0x01u)
#define NPZ2100_ID_EXPECTED               (0x74u)

/* =========================================================================
 * STA1  —  0x02  (R)
 * Wake-up / reset status flags.
 * ======================================================================= */
#define NPZ2100_REG_STA1                  (0x02u)

/** [1:0] System reset source. */
#define NPZ2100_STA1_RST_SRC_MSK          (0x03u)
#define NPZ2100_STA1_RST_SRC_POS          (0u)
#define NPZ2100_STA1_RST_SRC_GET(r)       (((r) & NPZ2100_STA1_RST_SRC_MSK) >> NPZ2100_STA1_RST_SRC_POS)

/** RST_SRC decode values (see datasheet Table 16). */
#define NPZ2100_RST_SRC_POR               (0x00u)  /**< Power-on reset.      */
#define NPZ2100_RST_SRC_EXT               (0x01u)  /**< External RSTN pin.   */
#define NPZ2100_RST_SRC_I2C               (0x02u)  /**< I²C system reset.    */
#define NPZ2100_RST_SRC_BOR               (0x03u)  /**< Brown-out reset.     */

/** [3:2] Soft reset source. */
#define NPZ2100_STA1_SRST_SRC_MSK         (0x0Cu)
#define NPZ2100_STA1_SRST_SRC_POS         (2u)
#define NPZ2100_STA1_SRST_SRC_GET(r)      (((r) & NPZ2100_STA1_SRST_SRC_MSK) >> NPZ2100_STA1_SRST_SRC_POS)

/** SRST_SRC decode values (see datasheet Table 17). */
#define NPZ2100_SRST_SRC_POR              (0x00u)  /**< Power-on reset.        */
#define NPZ2100_SRST_SRC_I2C              (0x01u)  /**< I²C soft reset.        */
#define NPZ2100_SRST_SRC_WDOG             (0x02u)  /**< Watchdog reset.        */

/** [4] ADC channel 1 triggered. */
#define NPZ2100_STA1_FADC1_MSK            (0x10u)
#define NPZ2100_STA1_FADC1_POS            (4u)
#define NPZ2100_STA1_FADC1_GET(r)         (((r) & NPZ2100_STA1_FADC1_MSK) >> NPZ2100_STA1_FADC1_POS)

/** [5] ADC channel 2 triggered. */
#define NPZ2100_STA1_FADC2_MSK            (0x20u)
#define NPZ2100_STA1_FADC2_POS            (5u)
#define NPZ2100_STA1_FADC2_GET(r)         (((r) & NPZ2100_STA1_FADC2_MSK) >> NPZ2100_STA1_FADC2_POS)

/** [6] ADC channel 3 (internal ref) triggered. */
#define NPZ2100_STA1_FADC3_MSK            (0x40u)
#define NPZ2100_STA1_FADC3_POS            (6u)
#define NPZ2100_STA1_FADC3_GET(r)         (((r) & NPZ2100_STA1_FADC3_MSK) >> NPZ2100_STA1_FADC3_POS)

/** [7] Wake-up was a global time-out (no other source triggered first). */
#define NPZ2100_STA1_FTOUT_MSK            (0x80u)
#define NPZ2100_STA1_FTOUT_POS            (7u)
#define NPZ2100_STA1_FTOUT_GET(r)         (((r) & NPZ2100_STA1_FTOUT_MSK) >> NPZ2100_STA1_FTOUT_POS)

/* =========================================================================
 * STA2  —  0x03  (R)
 * Peripheral and alarm flags.
 * NOTE: Reading STA1 or STA2 resets the watchdog timer (datasheet §2.2.16).
 * ======================================================================= */
#define NPZ2100_REG_STA2                  (0x03u)

/** [0] Peripheral 1 triggered. */
#define NPZ2100_STA2_FP1_MSK              (0x01u)
#define NPZ2100_STA2_FP1_POS              (0u)
/** [1] Peripheral 2 triggered. */
#define NPZ2100_STA2_FP2_MSK              (0x02u)
#define NPZ2100_STA2_FP2_POS              (1u)
/** [2] Peripheral 3 triggered. */
#define NPZ2100_STA2_FP3_MSK              (0x04u)
#define NPZ2100_STA2_FP3_POS              (2u)
/** [3] Peripheral 4 triggered. */
#define NPZ2100_STA2_FP4_MSK              (0x08u)
#define NPZ2100_STA2_FP4_POS              (3u)
/** [4] Peripheral 5 triggered. */
#define NPZ2100_STA2_FP5_MSK              (0x10u)
#define NPZ2100_STA2_FP5_POS              (4u)
/** [5] Peripheral 6 triggered. */
#define NPZ2100_STA2_FP6_MSK              (0x20u)
#define NPZ2100_STA2_FP6_POS              (5u)
/** Helper: extract flag for peripheral n (1–6). */
#define NPZ2100_STA2_FPn_GET(r, n)        (((r) >> ((n) - 1u)) & 0x01u)

/** [6] Global counter alarm triggered. */
#define NPZ2100_STA2_FALM_MSK             (0x40u)
#define NPZ2100_STA2_FALM_POS             (6u)
#define NPZ2100_STA2_FALM_GET(r)          (((r) & NPZ2100_STA2_FALM_MSK) >> NPZ2100_STA2_FALM_POS)

/** [7] Logging memory full triggered wake-up. */
#define NPZ2100_STA2_FLOG_MSK             (0x80u)
#define NPZ2100_STA2_FLOG_POS             (7u)
#define NPZ2100_STA2_FLOG_GET(r)          (((r) & NPZ2100_STA2_FLOG_MSK) >> NPZ2100_STA2_FLOG_POS)

/* =========================================================================
 * STA3  —  0x04  (R)
 * NAK, event counter, and power-aware flags.
 * ======================================================================= */
#define NPZ2100_REG_STA3                  (0x04u)

/** [5:0] Peripheral N did not acknowledge I²C address. */
#define NPZ2100_STA3_FNAKPn_GET(r, n)    (((r) >> ((n) - 1u)) & 0x01u)

/** [6] Event counter triggered. */
#define NPZ2100_STA3_FCNT_MSK             (0x40u)
#define NPZ2100_STA3_FCNT_POS             (6u)
#define NPZ2100_STA3_FCNT_GET(r)          (((r) & NPZ2100_STA3_FCNT_MSK) >> NPZ2100_STA3_FCNT_POS)

/** [7] Power-aware mode is enabled and currently active. */
#define NPZ2100_STA3_FPA_MSK              (0x80u)
#define NPZ2100_STA3_FPA_POS              (7u)
#define NPZ2100_STA3_FPA_GET(r)           (((r) & NPZ2100_STA3_FPA_MSK) >> NPZ2100_STA3_FPA_POS)

/* =========================================================================
 * SYSCFG1  —  0x0A  (R/W)
 * Wake-up source enable per peripheral.
 * ======================================================================= */
#define NPZ2100_REG_SYSCFG1               (0x0Au)

/** [5:0] Enable peripheral N as wake-up source (bit N-1). */
#define NPZ2100_SYSCFG1_WUP_Pn_MSK(n)    (uint8_t)(1u << ((n) - 1u))

/** [6] Wake-up mode: 0 = any trigger, 1 = all triggers must fire. */
#define NPZ2100_SYSCFG1_WUPMOD_MSK        (0x40u)
#define NPZ2100_SYSCFG1_WUPMOD_POS        (6u)
#define NPZ2100_SYSCFG1_WUPMOD(v)         (uint8_t)(((v) & 0x01u) << NPZ2100_SYSCFG1_WUPMOD_POS)
#define NPZ2100_SYSCFG1_WUPMOD_GET(r)     (((r) & NPZ2100_SYSCFG1_WUPMOD_MSK) >> NPZ2100_SYSCFG1_WUPMOD_POS)

/* =========================================================================
 * SYSCFG2  —  0x0B  (R/W)
 * ADC wake-up enables, clock source selection, TOUT extension.
 * ======================================================================= */
#define NPZ2100_REG_SYSCFG2               (0x0Bu)

/** [0] ADC channel 1 as wake-up source. */
#define NPZ2100_SYSCFG2_WUP_ADC1_MSK      (0x01u)
#define NPZ2100_SYSCFG2_WUP_ADC1_POS      (0u)
/** [1] ADC channel 2 as wake-up source. */
#define NPZ2100_SYSCFG2_WUP_ADC2_MSK      (0x02u)
#define NPZ2100_SYSCFG2_WUP_ADC2_POS      (1u)
/** [2] ADC channel 3 (battery) as wake-up source. */
#define NPZ2100_SYSCFG2_WUP_ADC3_MSK      (0x04u)
#define NPZ2100_SYSCFG2_WUP_ADC3_POS      (2u)

/** [4] TOUT_EXT: extend time-out unit to 2-second increments. */
#define NPZ2100_SYSCFG2_TOUT_EXT_MSK      (0x10u)
#define NPZ2100_SYSCFG2_TOUT_EXT_POS      (4u)
#define NPZ2100_SYSCFG2_TOUT_EXT(v)       (uint8_t)(((v) & 0x01u) << NPZ2100_SYSCFG2_TOUT_EXT_POS)

/** [6] SCLK_SEL: 0 = low-power oscillator, 1 = crystal XO. */
#define NPZ2100_SYSCFG2_SCLK_SEL_MSK      (0x40u)
#define NPZ2100_SYSCFG2_SCLK_SEL_POS      (6u)
#define NPZ2100_SYSCFG2_SCLK_SEL(v)       (uint8_t)(((v) & 0x01u) << NPZ2100_SYSCFG2_SCLK_SEL_POS)

/** [7] SCLK_SEL_STATUS: read-only active clock. */
#define NPZ2100_SYSCFG2_SCLK_STATUS_MSK   (0x80u)
#define NPZ2100_SYSCFG2_SCLK_STATUS_POS   (7u)
#define NPZ2100_SYSCFG2_SCLK_STATUS_GET(r) (((r) & NPZ2100_SYSCFG2_SCLK_STATUS_MSK) >> NPZ2100_SYSCFG2_SCLK_STATUS_POS)

/* =========================================================================
 * TOUT  —  0x0C–0x0D  (R/W)
 * 16-bit time-out before host wake-up.
 * Unit: system clock periods (TOUT_EXT=0) or 2-second steps (TOUT_EXT=1).
 * WARNING: values below 0x0003 cause undefined behaviour.
 * ======================================================================= */
#define NPZ2100_REG_TOUT_L                (0x0Cu)
#define NPZ2100_REG_TOUT_H                (0x0Du)
#define NPZ2100_TOUT_MIN_SAFE             (0x0003u)  /**< Minimum safe TOUT value. */

/* =========================================================================
 * GCT  —  0x10–0x14  (R/W)
 * Global time counter (32-bit seconds + 4-bit 1/16 s fractional).
 * A write to GCT_3 (0x14) atomically latches all five bytes.
 * ======================================================================= */
#define NPZ2100_REG_GCT_MS                (0x10u)  /**< [3:0] 1/16 s ticks. */
#define NPZ2100_REG_GCT_0                 (0x11u)  /**< Seconds byte 0 (LSB). */
#define NPZ2100_REG_GCT_1                 (0x12u)
#define NPZ2100_REG_GCT_2                 (0x13u)
#define NPZ2100_REG_GCT_3                 (0x14u)  /**< Seconds byte 3 (MSB) — write triggers latch. */

#define NPZ2100_GCT_MS_MSK                (0x0Fu)  /**< Only lower 4 bits valid. */

/* =========================================================================
 * GCTALM  —  0x15–0x18  (R/W)
 * Global counter alarm (32-bit value in seconds).
 * ======================================================================= */
#define NPZ2100_REG_GCT_ALM_0             (0x15u)
#define NPZ2100_REG_GCT_ALM_1             (0x16u)
#define NPZ2100_REG_GCT_ALM_2             (0x17u)
#define NPZ2100_REG_GCT_ALM_3             (0x18u)

/* =========================================================================
 * WDOG  —  0x19–0x1A  (R/W)
 * 16-bit watchdog timer in units of 2 seconds.
 * Timer is reset every time STA1 or STA2 is read.
 * ======================================================================= */
#define NPZ2100_REG_WDOG_L                (0x19u)
#define NPZ2100_REG_WDOG_H                (0x1Au)

/* =========================================================================
 * GTC_CFG  —  0x1B  (R/W)
 * Enable global time counter alarm and watchdog.
 * ======================================================================= */
#define NPZ2100_REG_GTC_CFG               (0x1Bu)

/** [0] GTC_AEN: enable global counter alarm. */
#define NPZ2100_GTC_CFG_GTC_AEN_MSK       (0x01u)
#define NPZ2100_GTC_CFG_GTC_AEN_POS       (0u)
#define NPZ2100_GTC_CFG_GTC_AEN(v)        (uint8_t)(((v) & 0x01u) << NPZ2100_GTC_CFG_GTC_AEN_POS)

/** [1] WDOGEN: enable watchdog. */
#define NPZ2100_GTC_CFG_WDOGEN_MSK        (0x02u)
#define NPZ2100_GTC_CFG_WDOGEN_POS        (1u)
#define NPZ2100_GTC_CFG_WDOGEN(v)         (uint8_t)(((v) & 0x01u) << NPZ2100_GTC_CFG_WDOGEN_POS)

/* =========================================================================
 * PA_CFG  —  0x1C  (R/W)
 * Power-aware mode configuration.
 * ======================================================================= */
#define NPZ2100_REG_PA_CFG                (0x1Cu)

/** [0] PA_EN: enable power-aware mode. */
#define NPZ2100_PA_CFG_PA_EN_MSK          (0x01u)
#define NPZ2100_PA_CFG_PA_EN_POS          (0u)
#define NPZ2100_PA_CFG_PA_EN(v)           (uint8_t)(((v) & 0x01u) << NPZ2100_PA_CFG_PA_EN_POS)

/** [1] PA_NOWUP: disable all wake-ups when PA mode active. */
#define NPZ2100_PA_CFG_PA_NOWUP_MSK       (0x02u)
#define NPZ2100_PA_CFG_PA_NOWUP_POS       (1u)
#define NPZ2100_PA_CFG_PA_NOWUP(v)        (uint8_t)(((v) & 0x01u) << NPZ2100_PA_CFG_PA_NOWUP_POS)

/** [4:2] PA_SRC: activation source for PA mode. */
#define NPZ2100_PA_CFG_PA_SRC_MSK         (0x1Cu)
#define NPZ2100_PA_CFG_PA_SRC_POS         (2u)
#define NPZ2100_PA_CFG_PA_SRC(v)          (uint8_t)(((v) & 0x07u) << NPZ2100_PA_CFG_PA_SRC_POS)
#define NPZ2100_PA_CFG_PA_SRC_GET(r)      (((r) & NPZ2100_PA_CFG_PA_SRC_MSK) >> NPZ2100_PA_CFG_PA_SRC_POS)

/** PA_SRC source values (datasheet Table 36). */
#define NPZ2100_PA_SRC_INT1               (0x00u)
#define NPZ2100_PA_SRC_INT2               (0x01u)
#define NPZ2100_PA_SRC_INT3               (0x02u)
#define NPZ2100_PA_SRC_INT4               (0x03u)
#define NPZ2100_PA_SRC_ADC1               (0x04u)
#define NPZ2100_PA_SRC_ADC2               (0x05u)
#define NPZ2100_PA_SRC_ADC3               (0x06u)

#ifdef __cplusplus
}
#endif

#endif /* NPZ2100_REGS_SYSTEM_H */
