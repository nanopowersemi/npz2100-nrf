/**
 * @file npz2100_regs_adc_log.h
 * @brief Register addresses and bitfield macros — ADC, Logging, Event Counter,
 *        and SRAM Bank blocks.
 *
 * Covers:
 *  - ADCCFG  (0x40), THROVA[1-3] (0x41,0x43,0x45), THRUNA[1-3] (0x42,0x44,0x46),
 *    VAL_ADC[1-3] (0x47–0x49)
 *  - LOGCFG  (0x50), LOGSADDR (0x51), LOGCADDR (0x52)
 *  - CNTVAL  (0x53–0x56), CNTCFG (0x57), CNTTRIG (0x58–0x5B)
 *  - SRAM_BANK (0x7F)
 *  - SRAM window (0x80–0xFF)
 *
 * @version 0.7
 * @date    2026-05-06
 */

#ifndef NPZ2100_REGS_ADC_LOG_H
#define NPZ2100_REGS_ADC_LOG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * ADCCFG  —  0x40  (R/W)
 * Enable ADC channels, select sampling clock, and sync with peripherals.
 * ======================================================================= */
#define NPZ2100_REG_ADCCFG                (0x40u)

/** [0] ADC1_EN: enable ADC channel 1 (ADC1 pin). */
#define NPZ2100_ADCCFG_ADC1_EN_MSK        (0x01u)
#define NPZ2100_ADCCFG_ADC1_EN_POS        (0u)
#define NPZ2100_ADCCFG_ADC1_EN(v)         (uint8_t)(((v) & 0x01u) << NPZ2100_ADCCFG_ADC1_EN_POS)

/** [1] ADC2_EN: enable ADC channel 2 (ADC2 pin). */
#define NPZ2100_ADCCFG_ADC2_EN_MSK        (0x02u)
#define NPZ2100_ADCCFG_ADC2_EN_POS        (1u)
#define NPZ2100_ADCCFG_ADC2_EN(v)         (uint8_t)(((v) & 0x01u) << NPZ2100_ADCCFG_ADC2_EN_POS)

/** [2] ADC3_EN: enable ADC channel 3 (internal battery reference). */
#define NPZ2100_ADCCFG_ADC3_EN_MSK        (0x04u)
#define NPZ2100_ADCCFG_ADC3_EN_POS        (2u)
#define NPZ2100_ADCCFG_ADC3_EN(v)         (uint8_t)(((v) & 0x01u) << NPZ2100_ADCCFG_ADC3_EN_POS)

/**
 * @defgroup adccfg_clk ADC sampling clock selection
 * [5:4] ADC_CLK_SEL — see datasheet Table 59.
 * NOTE: Values other than 0b00 automatically turn on the XO.
 * @{
 */
#define NPZ2100_ADCCFG_ADC_CLK_SEL_MSK    (0x30u)
#define NPZ2100_ADCCFG_ADC_CLK_SEL_POS    (4u)
#define NPZ2100_ADCCFG_ADC_CLK_SEL(v)     (uint8_t)(((v) & 0x03u) << NPZ2100_ADCCFG_ADC_CLK_SEL_POS)
#define NPZ2100_ADCCFG_ADC_CLK_SEL_GET(r) (((r) & NPZ2100_ADCCFG_ADC_CLK_SEL_MSK) >> NPZ2100_ADCCFG_ADC_CLK_SEL_POS)

#define NPZ2100_ADC_CLK_SYSCLK            (0x00u)  /**< System clock (default).          */
#define NPZ2100_ADC_CLK_XO_64HZ           (0x01u)  /**< XO ÷ 512  = 64 Hz.              */
#define NPZ2100_ADC_CLK_XO_256HZ          (0x02u)  /**< XO ÷ 128  = 256 Hz.             */
#define NPZ2100_ADC_CLK_XO_1024HZ         (0x03u)  /**< XO ÷ 32   = 1024 Hz.            */
/** @} */

/** [6] ADC1_PSYNC: sync ADC ch.1 sampling with peripheral 1 polling cycle. */
#define NPZ2100_ADCCFG_ADC1_PSYNC_MSK     (0x40u)
#define NPZ2100_ADCCFG_ADC1_PSYNC_POS     (6u)
#define NPZ2100_ADCCFG_ADC1_PSYNC(v)      (uint8_t)(((v) & 0x01u) << NPZ2100_ADCCFG_ADC1_PSYNC_POS)

/** [7] ADC2_PSYNC: sync ADC ch.2 sampling with peripheral 2 polling cycle. */
#define NPZ2100_ADCCFG_ADC2_PSYNC_MSK     (0x80u)
#define NPZ2100_ADCCFG_ADC2_PSYNC_POS     (7u)
#define NPZ2100_ADCCFG_ADC2_PSYNC(v)      (uint8_t)(((v) & 0x01u) << NPZ2100_ADCCFG_ADC2_PSYNC_POS)

/* =========================================================================
 * THROVA[1-3]  — Over-threshold for ADC channels 1–3  (R/W, 8-bit unsigned)
 * THRUNA[1-3]  — Under-threshold for ADC channels 1–3 (R/W, 8-bit unsigned)
 * VAL_ADC[1-3] — Last ADC sample value               (R,   8-bit unsigned)
 * ======================================================================= */
#define NPZ2100_REG_THROVA1               (0x41u)
#define NPZ2100_REG_THRUNA1               (0x42u)
#define NPZ2100_REG_THROVA2               (0x43u)
#define NPZ2100_REG_THRUNA2               (0x44u)
#define NPZ2100_REG_THROVA3               (0x45u)
#define NPZ2100_REG_THRUNA3               (0x46u)
#define NPZ2100_REG_VAL_ADC1              (0x47u)
#define NPZ2100_REG_VAL_ADC2              (0x48u)
#define NPZ2100_REG_VAL_ADC3              (0x49u)

/** Helper macros — index n = 1, 2, or 3. */
#define NPZ2100_REG_THROVAn(n)            (uint8_t)(NPZ2100_REG_THROVA1 + (((n) - 1u) * 2u))
#define NPZ2100_REG_THRUNAn(n)            (uint8_t)(NPZ2100_REG_THRUNA1 + (((n) - 1u) * 2u))
#define NPZ2100_REG_VAL_ADCn(n)           (uint8_t)(NPZ2100_REG_VAL_ADC1 + ((n) - 1u))

/* =========================================================================
 * LOGCFG  —  0x50  (R/W)
 * Logging enable and rotation control.
 * ======================================================================= */
#define NPZ2100_REG_LOGCFG                (0x50u)

/** [0] LOG_EN: enable logging. */
#define NPZ2100_LOGCFG_LOG_EN_MSK         (0x01u)
#define NPZ2100_LOGCFG_LOG_EN_POS         (0u)
#define NPZ2100_LOGCFG_LOG_EN(v)          (uint8_t)(((v) & 0x01u) << NPZ2100_LOGCFG_LOG_EN_POS)

/** [1] LOG_ROT: enable ring-buffer rotation when SRAM log area is full. */
#define NPZ2100_LOGCFG_LOG_ROT_MSK        (0x02u)
#define NPZ2100_LOGCFG_LOG_ROT_POS        (1u)
#define NPZ2100_LOGCFG_LOG_ROT(v)         (uint8_t)(((v) & 0x01u) << NPZ2100_LOGCFG_LOG_ROT_POS)

/** [7] LOG_IS_ROT: read-only, set when rotation has occurred. */
#define NPZ2100_LOGCFG_LOG_IS_ROT_MSK     (0x80u)
#define NPZ2100_LOGCFG_LOG_IS_ROT_GET(r)  (((r) & NPZ2100_LOGCFG_LOG_IS_ROT_MSK) >> 7u)

/* =========================================================================
 * LOGSADDR  —  0x51  (R/W)
 * Start address of the logging area in SRAM (0x00–0xFF).
 * ======================================================================= */
#define NPZ2100_REG_LOGSADDR              (0x51u)

/* =========================================================================
 * LOGCADDR  —  0x52  (R)
 * Address of the last written log entry in SRAM (0x00–0xFF).
 * ======================================================================= */
#define NPZ2100_REG_LOGCADDR              (0x52u)

/* =========================================================================
 * CNTVAL  —  0x53–0x56  (R/W)
 * 32-bit event counter value.
 * ======================================================================= */
#define NPZ2100_REG_CNT_VAL_0             (0x53u)  /**< LSB */
#define NPZ2100_REG_CNT_VAL_1             (0x54u)
#define NPZ2100_REG_CNT_VAL_2             (0x55u)
#define NPZ2100_REG_CNT_VAL_3             (0x56u)  /**< MSB */

/* =========================================================================
 * CNTCFG  —  0x57  (R/W)
 * Event counter enable and source selection.
 * Writing CNT_EN resets the counter value.
 * ======================================================================= */
#define NPZ2100_REG_CNTCFG                (0x57u)

/** [0] CNT_EN: enable event counter. Writing this field also resets counter. */
#define NPZ2100_CNTCFG_CNT_EN_MSK         (0x01u)
#define NPZ2100_CNTCFG_CNT_EN_POS         (0u)
#define NPZ2100_CNTCFG_CNT_EN(v)          (uint8_t)(((v) & 0x01u) << NPZ2100_CNTCFG_CNT_EN_POS)

/** [3:1] CNT_SRC: event counter input source. */
#define NPZ2100_CNTCFG_CNT_SRC_MSK        (0x0Eu)
#define NPZ2100_CNTCFG_CNT_SRC_POS        (1u)
#define NPZ2100_CNTCFG_CNT_SRC(v)         (uint8_t)(((v) & 0x07u) << NPZ2100_CNTCFG_CNT_SRC_POS)
#define NPZ2100_CNTCFG_CNT_SRC_GET(r)     (((r) & NPZ2100_CNTCFG_CNT_SRC_MSK) >> NPZ2100_CNTCFG_CNT_SRC_POS)

/** Counter source values (datasheet Table 68). */
#define NPZ2100_CNT_SRC_INT1              (0x00u)
#define NPZ2100_CNT_SRC_INT2              (0x01u)
#define NPZ2100_CNT_SRC_INT3              (0x02u)
#define NPZ2100_CNT_SRC_INT4              (0x03u)
#define NPZ2100_CNT_SRC_SW_LP1            (0x04u)
#define NPZ2100_CNT_SRC_SW_LP2            (0x05u)
#define NPZ2100_CNT_SRC_SW_LP3            (0x06u)
#define NPZ2100_CNT_SRC_SW_LP4            (0x07u)

/* =========================================================================
 * CNTTRIG  —  0x58–0x5B  (R/W)
 * 32-bit event counter trigger value — fires when CNTVAL == CNTTRIG.
 * ======================================================================= */
#define NPZ2100_REG_CNT_TRIG_0            (0x58u)  /**< LSB */
#define NPZ2100_REG_CNT_TRIG_1            (0x59u)
#define NPZ2100_REG_CNT_TRIG_2            (0x5Au)
#define NPZ2100_REG_CNT_TRIG_3            (0x5Bu)  /**< MSB */

/* =========================================================================
 * SRAM_BANK  —  0x7F  (R/W)
 * Controls which 128-byte half of SRAM is visible through the I²C window
 * at addresses 0x80–0xFF.
 *   SRAM_BANK=0 → I²C 0x80–0xFF maps to SRAM 0x00–0x7F
 *   SRAM_BANK=1 → I²C 0x80–0xFF maps to SRAM 0x80–0xFF
 * ======================================================================= */
#define NPZ2100_REG_SRAM_BANK             (0x7Fu)

#define NPZ2100_SRAM_BANK_MSK             (0x01u)
#define NPZ2100_SRAM_BANK_POS             (0u)
#define NPZ2100_SRAM_BANK(v)              (uint8_t)((v) & NPZ2100_SRAM_BANK_MSK)

/** I²C base address of the SRAM window. */
#define NPZ2100_SRAM_WINDOW_BASE          (0x80u)
/** Size of one SRAM bank window (bytes). */
#define NPZ2100_SRAM_WINDOW_SIZE          (0x80u)
/** Total SRAM size (bytes). */
#define NPZ2100_SRAM_TOTAL_SIZE           (0x100u)

#ifdef __cplusplus
}
#endif

#endif /* NPZ2100_REGS_ADC_LOG_H */
