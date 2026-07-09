/*
 * Copyright (c) Nanopower Semiconductor AS
 * SPDX-License-Identifier: Apache-2.0
 *
 * include/drivers/npz2100.h
 * --------------------------
 * Public Zephyr API for the nPZ2100 power-saving IC driver.
 * Target: nRF52833 / NCS 3.0.2.
 *
 * Power architecture
 * ------------------
 * The nPZ2100 controls the nRF52833 power supply via its SW_HP host switch.
 * When the nPZ2100 enters idle mode it de-asserts SW_HP, cutting power to the
 * nRF52833 completely.  There is no always-on interrupt line.
 *
 * Every nRF52833 boot is caused by the nPZ2100 re-asserting SW_HP in response
 * to a configured trigger (sensor threshold, ADC limit, time alarm, etc.).
 *
 * Required boot sequence (call from main() before any application logic):
 *
 *   // 1. Read wake reason — must be first I²C operation.
 *   npz2100_wake_reason_t reason;
 *   npz2100_boot_status(dev, &reason);
 *
 *   // 2. Sync shadow from device (nPZ2100 retains config across host power cycles).
 *   npz2100_readback(dev);
 *
 *   // 3. Apply desired config — only changed registers are written.
 *   npz2100_apply_regmap(dev, regmap, sizeof(regmap));
 *
 *   // 4. Handle wake reason (application logic).
 *   handle_wake(&reason);
 *
 *   // 5. Optionally update config for next cycle.
 *   npz2100_shadow_flush(dev);
 *
 *   // 6. Hand control back — nPZ2100 will cut nRF52833 power.
 *   npz2100_enter_idle(dev);
 *
 * Thread safety
 * -------------
 * All API functions acquire a per-device k_mutex.  Safe to call from
 * multiple threads; must NOT be called from ISR context.
 */

#ifndef DRIVERS_NPZ2100_H_
#define DRIVERS_NPZ2100_H_

#include <zephyr/device.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Pull in register macros, mid-level types, and typed config helpers so
 * application code can use NPZ2100_REG_*, npz2100_config_t, npz2100_sys_set(),
 * npz2100_periph_set(), etc. without additional includes. */
#include "npz2100_mid.h"

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * Wake-up reason struct
 *
 * Returned by npz2100_boot_status().  Populated from STA1, STA2, STA3.
 * Reading STA1/STA2 also resets the nPZ2100 watchdog timer.
 * ======================================================================= */

/**
 * @brief Decoded nPZ2100 wake-up reason, read at the start of every boot.
 *
 * Multiple flags may be set simultaneously (e.g. both a peripheral trigger
 * and a time-out can fire in the same wake cycle).
 */
typedef struct {
	/* ---- System reset source (STA1 RST_SRC) ---- */
	uint8_t  rst_src;        /**< System reset source: NPZ2100_RST_SRC_* */
	uint8_t  srst_src;       /**< Soft reset source: NPZ2100_SRST_SRC_*  */

	/* ---- Peripheral trigger flags (STA2 bits 5:0) ---- */
	bool     periph[6];      /**< periph[0..5]: peripheral 1–6 triggered  */

	/* ---- ADC trigger flags (STA1) ---- */
	bool     adc1;           /**< ADC channel 1 threshold crossed         */
	bool     adc2;           /**< ADC channel 2 threshold crossed         */
	bool     adc3;           /**< ADC channel 3 (battery) threshold crossed*/

	/* ---- Timing and system flags ---- */
	bool     timeout;        /**< Time-out elapsed (no other source first) */
	bool     alarm;          /**< Global time counter alarm fired          */
	bool     log_full;       /**< SRAM logging area is full               */
	bool     counter;        /**< Event counter reached trigger value      */
	bool     pa_active;      /**< Power-aware mode is currently active    */

	/* ---- NAK flags (STA3 bits 5:0) ---- */
	bool     nak[6];         /**< nak[0..5]: peripheral 1–6 NAK'd I²C    */
} npz2100_wake_reason_t;

/* =========================================================================
 * Boot-time API — call these at the top of main() on every boot
 * ======================================================================= */

/**
 * @brief Read wake-up reason from STA1–STA3.  CALL FIRST ON EVERY BOOT.
 *
 * This must be the first nPZ2100 I²C operation after the nRF52833 boots.
 * Reading STA1/STA2 simultaneously resets the nPZ2100 watchdog timer.
 *
 * The three status registers are read in a single burst (3 bytes, one I²C
 * transaction) to minimise bus time at the start of the wake cycle.
 *
 * @param dev     Pointer to the nPZ2100 device (DEVICE_DT_GET).
 * @param reason  Output struct populated with decoded wake-up flags.
 *                Pass NULL if only the watchdog-kick side effect is needed.
 * @return 0 on success, -EIO on I²C error.
 */
int npz2100_boot_status(const struct device      *dev,
                         npz2100_wake_reason_t    *reason);

/**
 * @brief Sync the driver shadow from the device's current register state.
 *
 * The nPZ2100 retains its register configuration while the nRF52833 is
 * powered off.  Call this after npz2100_boot_status() to populate the
 * driver shadow before diffing against the desired regmap.
 *
 * Uses burst reads for consecutive register blocks to minimise I²C traffic.
 *
 * @param dev  Pointer to the nPZ2100 device.
 * @return 0 on success, -EIO on I²C error.
 */
int npz2100_readback(const struct device *dev);

/* =========================================================================
 * Configuration API
 * ======================================================================= */

/**
 * @brief Apply a tool-generated byte-stream register map to the device.
 *
 * Parses the flat `[length][start_addr][data...]` stream and writes only
 * registers that differ from the shadow — the nPZ2100 retains its config
 * across nRF52833 power cycles, so unchanged registers generate zero I²C
 * transactions on subsequent boots.
 *
 * @param dev      Pointer to the nPZ2100 device.
 * @param map      Byte-stream register map from the Nanopower config tool.
 * @param map_len  Total length in bytes (use sizeof() for compile-time arrays).
 * @return 0 on success, -EINVAL on malformed stream, -EIO on I²C error.
 */
int npz2100_apply_regmap(const struct device *dev,
                          const uint8_t       *map,
                          size_t               map_len);

/**
 * @brief Return a pointer to the driver's internal configuration shadow.
 *
 * Use this with the typed mid-level helpers to build a modified configuration
 * for the next idle cycle:
 *
 * @code
 *   npz2100_config_t *s = npz2100_get_shadow(dev);
 *   npz2100_periph_cfg_t pcfg = { .period = 1024, ... };
 *   npz2100_periph_set(s, 0, &pcfg);
 *   npz2100_shadow_flush(dev);   // push to device
 * @endcode
 *
 * @param dev  Pointer to the nPZ2100 device.
 * @return Pointer to the npz2100_config_t shadow, or NULL if dev is invalid.
 */
npz2100_config_t *npz2100_get_shadow(const struct device *dev);

/**
 * @brief Push the current shadow to the device, writing only changed registers.
 *
 * Use after calling typed helpers (npz2100_sys_set, npz2100_periph_set, etc.)
 * on the shadow returned by npz2100_get_shadow().
 *
 * @param dev  Pointer to the nPZ2100 device.
 * @return 0 on success, -EIO on I²C error.
 */
int npz2100_shadow_flush(const struct device *dev);

/* =========================================================================
 * SRAM access
 * ======================================================================= */

/**
 * @brief Write a byte block into the nPZ2100's 256-byte SRAM.
 *
 * The SRAM stores sensor initialisation sequences that the nPZ2100 sends
 * autonomously during polling while the nRF52833 is powered off.
 * Handles the 128-byte bank boundary transparently.
 *
 * @param dev        Pointer to the nPZ2100 device.
 * @param sram_addr  SRAM byte offset (0x00–0xFF).
 * @param data       Source data.
 * @param len        Number of bytes to write.
 * @return 0 on success, -EINVAL on out-of-bounds, -EIO on I²C error.
 */
int npz2100_sram_write(const struct device *dev,
                        uint8_t              sram_addr,
                        const uint8_t       *data,
                        size_t               len);

/**
 * @brief Read a byte block from the nPZ2100's 256-byte SRAM.
 *
 * @param dev        Pointer to the nPZ2100 device.
 * @param sram_addr  SRAM byte offset (0x00–0xFF).
 * @param data       Destination buffer.
 * @param len        Number of bytes to read.
 * @return 0 on success, -EINVAL on out-of-bounds, -EIO on I²C error.
 */
int npz2100_sram_read(const struct device *dev,
                       uint8_t              sram_addr,
                       uint8_t             *data,
                       size_t               len);

/* =========================================================================
 * Control
 * ======================================================================= */

/**
 * @brief Verify the nPZ2100 is present and responding.
 *
 * Reads ID register (0x01) and confirms it equals 0x74.
 *
 * @param dev  Pointer to the nPZ2100 device.
 * @return 0 on success, -EIO on bus error, -ENODEV on ID mismatch.
 */
int npz2100_probe(const struct device *dev);

/**
 * @brief Hand control to the nPZ2100.  nRF52833 will lose power.
 *
 * Writes 0xFF to IDLE_RST.  The nPZ2100 will de-assert SW_HP, cutting
 * power to the nRF52833.  This function does not return in normal operation.
 *
 * The nRF52833 will be powered on again only when the nPZ2100 detects a
 * configured wake-up trigger (sensor threshold, alarm, ADC limit, etc.).
 *
 * @param dev  Pointer to the nPZ2100 device.
 * @return -EIO on I²C error (if the command cannot be sent).
 *         Does not return on success — power is cut immediately.
 */
int npz2100_enter_idle(const struct device *dev);

/**
 * @brief Issue a soft reset to the nPZ2100 (SRAM and config preserved).
 *
 * The shadow is marked stale; call npz2100_readback() before the next
 * npz2100_apply_regmap() or npz2100_shadow_flush().
 *
 * @param dev  Pointer to the nPZ2100 device.
 * @return 0 on success, -EIO on I²C error.
 */
int npz2100_soft_reset(const struct device *dev);

#ifdef __cplusplus
}
#endif

/**
 * @brief Read the last sampled value for a peripheral slot.
 *
 * Selects the correct P_BANK, reads VALP_L/VALP_H, and reconstructs
 * the value according to the DTYPE setting in the shadow.
 *
 * @param dev    Pointer to the nPZ2100 device.
 * @param slot   Peripheral index 0-5.
 * @param value  Output: raw 16-bit value (8-bit types are zero-extended).
 * @return 0 on success, -EINVAL on bad slot, -EIO on I2C error.
 */
int npz2100_periph_read_value(const struct device *dev,
                               uint8_t              slot,
                               uint16_t            *value);

#endif /* DRIVERS_NPZ2100_H_ */
