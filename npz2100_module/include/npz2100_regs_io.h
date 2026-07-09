/**
 * @file npz2100_regs_io.h
 * @brief Register addresses and bitfield macros — I/O Configuration block.
 *
 * Covers: IOCFG1 (0x05), IOCFG2 (0x06), IOCFG3 (0x07),
 *         IOCFG4 (0x08), IOCFG5 (0x09).
 *
 * These registers control:
 *  - Power switch modes (host and peripheral)
 *  - Interrupt pin pull-ups and direction
 *  - I²C pull-up control
 *  - SPI auto-disable (HiZ when idle — saves power)
 *  - Gate boost for low VBAT operation
 *  - Drive strength and slew rate
 *
 * @version 0.7
 * @date    2026-05-06
 */

#ifndef NPZ2100_REGS_IO_H
#define NPZ2100_REGS_IO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * IOCFG1  —  0x05  (R/W)
 * Host power switch and peripheral standby switch states.
 * ======================================================================= */
#define NPZ2100_REG_IOCFG1                (0x05u)

/**
 * @defgroup iocfg1_pswa Peripheral standby power switch state (PSWA_S[1-4])
 * Controls whether the peripheral power switch is ON or OFF during Standby.
 * Bit N-1 corresponds to peripheral N.
 * @{
 */
#define NPZ2100_IOCFG1_PSWA_S1_MSK        (0x01u)
#define NPZ2100_IOCFG1_PSWA_S1_POS        (0u)
#define NPZ2100_IOCFG1_PSWA_S2_MSK        (0x02u)
#define NPZ2100_IOCFG1_PSWA_S2_POS        (1u)
#define NPZ2100_IOCFG1_PSWA_S3_MSK        (0x04u)
#define NPZ2100_IOCFG1_PSWA_S3_POS        (2u)
#define NPZ2100_IOCFG1_PSWA_S4_MSK        (0x08u)
#define NPZ2100_IOCFG1_PSWA_S4_POS        (3u)
/** Set standby state for peripheral n (1–4): val=0 disabled, val=1 enabled. */
#define NPZ2100_IOCFG1_PSWA_Sn(n, val)   (uint8_t)(((val) & 0x01u) << ((n) - 1u))
/** @} */

/**
 * @defgroup iocfg1_pswmod_h Host power switch mode (PSWMOD_H)
 * [5:4] Two-bit field. See datasheet Table 21.
 * @{
 */
#define NPZ2100_IOCFG1_PSWMOD_H_MSK       (0x30u)
#define NPZ2100_IOCFG1_PSWMOD_H_POS       (4u)
#define NPZ2100_IOCFG1_PSWMOD_H(v)        (uint8_t)(((v) & 0x03u) << NPZ2100_IOCFG1_PSWMOD_H_POS)
#define NPZ2100_IOCFG1_PSWMOD_H_GET(r)    (((r) & NPZ2100_IOCFG1_PSWMOD_H_MSK) >> NPZ2100_IOCFG1_PSWMOD_H_POS)

/** Host PSW modes. */
#define NPZ2100_PSWMOD_H_POWER_SW         (0x00u)  /**< Power switch (VBAT or open). */
#define NPZ2100_PSWMOD_H_LOGIC_HIGH       (0x02u)  /**< Logic high when host ON.     */
#define NPZ2100_PSWMOD_H_LOGIC_LOW        (0x03u)  /**< Logic low when host ON.      */
/** @} */

/** [6] PSW_EN_VN: enable automatic gate boost for reduced Rds(on).
 *  WARNING: increases VBAT current by up to 5 µA when any PSW is on. */
#define NPZ2100_IOCFG1_PSW_EN_VN_MSK      (0x40u)
#define NPZ2100_IOCFG1_PSW_EN_VN_POS      (6u)
#define NPZ2100_IOCFG1_PSW_EN_VN(v)       (uint8_t)(((v) & 0x01u) << NPZ2100_IOCFG1_PSW_EN_VN_POS)

/** [7] PSW_VN_ON: read-only, 1 if gate boost is currently active. */
#define NPZ2100_IOCFG1_PSW_VN_ON_MSK      (0x80u)
#define NPZ2100_IOCFG1_PSW_VN_ON_GET(r)   (((r) & NPZ2100_IOCFG1_PSW_VN_ON_MSK) >> 7u)

/* =========================================================================
 * IOCFG2  —  0x06  (R/W)
 * Peripheral power switch modes (SW_LP[1-4]).
 * Each peripheral occupies a 2-bit field; see datasheet Table 23.
 * ======================================================================= */
#define NPZ2100_REG_IOCFG2                (0x06u)

/** PSWMOD_S[1-4] field position: peripheral n occupies bits [2n-1 : 2n-2]. */
#define NPZ2100_IOCFG2_PSWMOD_Sn_MSK(n)  (uint8_t)(0x03u << (((n) - 1u) * 2u))
#define NPZ2100_IOCFG2_PSWMOD_Sn_POS(n)  (uint8_t)(((n) - 1u) * 2u)
#define NPZ2100_IOCFG2_PSWMOD_Sn(n, v)   (uint8_t)(((v) & 0x03u) << NPZ2100_IOCFG2_PSWMOD_Sn_POS(n))
#define NPZ2100_IOCFG2_PSWMOD_Sn_GET(r,n) (((r) & NPZ2100_IOCFG2_PSWMOD_Sn_MSK(n)) >> NPZ2100_IOCFG2_PSWMOD_Sn_POS(n))

/** Peripheral PSW modes (datasheet Table 23). */
#define NPZ2100_PSWMOD_S_POWER_RISE       (0x00u)  /**< Power switch with rise detection. */
#define NPZ2100_PSWMOD_S_POWER            (0x01u)  /**< Plain power switch.               */
#define NPZ2100_PSWMOD_S_LOGIC_HIGH       (0x02u)  /**< Logic high when peripheral ON.    */
#define NPZ2100_PSWMOD_S_LOGIC_LOW        (0x03u)  /**< Logic low when peripheral ON.     */

/* =========================================================================
 * IOCFG3  —  0x07  (R/W)
 * Pull-up enable and strength on INT[1-4] pins.
 * Reset state: all pull-ups enabled at ≈100 kΩ.
 * ======================================================================= */
#define NPZ2100_REG_IOCFG3                (0x07u)

/** [3:0] PU_INT[1-4]: enable internal pull-up on INT pin n (bit n-1). */
#define NPZ2100_IOCFG3_PU_INT_MSK         (0x0Fu)
#define NPZ2100_IOCFG3_PU_INTn(n, v)      (uint8_t)(((v) & 0x01u) << ((n) - 1u))
#define NPZ2100_IOCFG3_PU_INTn_GET(r, n)  (((r) >> ((n) - 1u)) & 0x01u)

/** [7:4] PU_S_INT[1-4]: pull-up strength on INT pin n (bit n+3).
 *  0 = ≈100 kΩ, 1 = ≈50 kΩ. */
#define NPZ2100_IOCFG3_PU_S_INT_MSK       (0xF0u)
#define NPZ2100_IOCFG3_PU_S_INTn(n, v)    (uint8_t)(((v) & 0x01u) << ((n) + 3u))
#define NPZ2100_IOCFG3_PU_S_INTn_GET(r,n) (((r) >> ((n) + 3u)) & 0x01u)

/* =========================================================================
 * IOCFG4  —  0x08  (R/W)
 * Interrupt pin direction / mode (INTMOD_I[1-4]).
 * Each interrupt occupies a 2-bit field; see datasheet Table 26.
 * ======================================================================= */
#define NPZ2100_REG_IOCFG4                (0x08u)

/** INTMOD_I[1-4]: 2-bit field for interrupt n. */
#define NPZ2100_IOCFG4_INTMOD_MSK(n)      (uint8_t)(0x03u << (((n) - 1u) * 2u))
#define NPZ2100_IOCFG4_INTMOD_POS(n)      (uint8_t)(((n) - 1u) * 2u)
#define NPZ2100_IOCFG4_INTMOD(n, v)       (uint8_t)(((v) & 0x03u) << NPZ2100_IOCFG4_INTMOD_POS(n))
#define NPZ2100_IOCFG4_INTMOD_GET(r, n)   (((r) & NPZ2100_IOCFG4_INTMOD_MSK(n)) >> NPZ2100_IOCFG4_INTMOD_POS(n))

/** Interrupt pin modes (datasheet Table 26). */
#define NPZ2100_INTMOD_IN_ACTIVE_HIGH     (0x00u)  /**< Input, active-high.            */
#define NPZ2100_INTMOD_IN_ACTIVE_LOW      (0x01u)  /**< Input, active-low.             */
#define NPZ2100_INTMOD_OUT_ACTIVE_HIGH    (0x02u)  /**< Trigger output, active-high.   */
#define NPZ2100_INTMOD_OUT_ACTIVE_LOW     (0x03u)  /**< Trigger output, active-low.    */

/* =========================================================================
 * IOCFG5  —  0x09  (R/W)
 * Miscellaneous I/O options.  Reset state: SPI_AUTO=1, I2C_PUP_EN=1,
 * I2C_PUP_AUTO=1, PSW_SR=1.
 * ======================================================================= */
#define NPZ2100_REG_IOCFG5                (0x09u)

/** [0] PSW_SR: power switch output slew rate. 0=slow, 1=fast. */
#define NPZ2100_IOCFG5_PSW_SR_MSK         (0x01u)
#define NPZ2100_IOCFG5_PSW_SR_POS         (0u)
#define NPZ2100_IOCFG5_PSW_SR(v)          (uint8_t)(((v) & 0x01u) << NPZ2100_IOCFG5_PSW_SR_POS)

/** [1] IO_STR: I/O drive strength. 0=normal, 1=high. */
#define NPZ2100_IOCFG5_IO_STR_MSK         (0x02u)
#define NPZ2100_IOCFG5_IO_STR_POS         (1u)
#define NPZ2100_IOCFG5_IO_STR(v)          (uint8_t)(((v) & 0x01u) << NPZ2100_IOCFG5_IO_STR_POS)

/** [2] I2C_PUP_EN: enable internal I²C pull-ups (≈2 kΩ). */
#define NPZ2100_IOCFG5_I2C_PUP_EN_MSK     (0x04u)
#define NPZ2100_IOCFG5_I2C_PUP_EN_POS     (2u)
#define NPZ2100_IOCFG5_I2C_PUP_EN(v)      (uint8_t)(((v) & 0x01u) << NPZ2100_IOCFG5_I2C_PUP_EN_POS)

/** [3] I2C_PUP_AUTO: auto-disable I²C pull-ups when bus idle.
 *  Requires I2C_PUP_EN=1.  Saves leakage current between transactions. */
#define NPZ2100_IOCFG5_I2C_PUP_AUTO_MSK   (0x08u)
#define NPZ2100_IOCFG5_I2C_PUP_AUTO_POS   (3u)
#define NPZ2100_IOCFG5_I2C_PUP_AUTO(v)    (uint8_t)(((v) & 0x01u) << NPZ2100_IOCFG5_I2C_PUP_AUTO_POS)

/** [4] SPI_AUTO: put SPI outputs in HiZ when interface not in use.
 *  Recommended ON — prevents SPI pins from driving a disabled peripheral. */
#define NPZ2100_IOCFG5_SPI_AUTO_MSK       (0x10u)
#define NPZ2100_IOCFG5_SPI_AUTO_POS       (4u)
#define NPZ2100_IOCFG5_SPI_AUTO(v)        (uint8_t)(((v) & 0x01u) << NPZ2100_IOCFG5_SPI_AUTO_POS)

/** [5] XO_SEL_CAP: enable internal XO load capacitors. */
#define NPZ2100_IOCFG5_XO_SEL_CAP_MSK     (0x20u)
#define NPZ2100_IOCFG5_XO_SEL_CAP_POS     (5u)
#define NPZ2100_IOCFG5_XO_SEL_CAP(v)      (uint8_t)(((v) & 0x01u) << NPZ2100_IOCFG5_XO_SEL_CAP_POS)

/** [6] INTOUTMOD: INT output logic when multiple peripherals share a pin.
 *  0=AND, 1=OR. */
#define NPZ2100_IOCFG5_INTOUTMOD_MSK       (0x40u)
#define NPZ2100_IOCFG5_INTOUTMOD_POS       (6u)
#define NPZ2100_IOCFG5_INTOUTMOD(v)        (uint8_t)(((v) & 0x01u) << NPZ2100_IOCFG5_INTOUTMOD_POS)

#ifdef __cplusplus
}
#endif

#endif /* NPZ2100_REGS_IO_H */
