/*
 * Copyright (c) Nanopower Semiconductor AS
 * SPDX-License-Identifier: Apache-2.0
 *
 * sample/src/main.c
 * ------------------
 * Reference application for the nPZ2100 driver on nRF52833 / NCS 3.0.2.
 *
 * Power architecture
 * ------------------
 * The nPZ2100 controls the nRF52833 power supply via SW_HP.  When idle
 * the nPZ2100 cuts power to the nRF52833 completely — there is no always-on
 * interrupt line between the two chips.
 *
 * Every boot of this application was caused by the nPZ2100 re-enabling SW_HP
 * in response to a configured trigger.  The first task on every boot is
 * therefore to ask the nPZ2100 why it woke the system up.
 *
 * Required boot sequence (executed unconditionally on every boot):
 *
 *   1. npz2100_boot_status() — read STA1–STA3, kick watchdog, decode reason.
 *   2. npz2100_readback()    — sync shadow from nPZ2100 (retains config
 *                              across nRF52833 power cycles).
 *   3. npz2100_apply_regmap() — write only registers that differ from shadow.
 *   4. Application logic     — handle the wake reason.
 *   5. npz2100_shadow_flush() — push any runtime config changes to device.
 *   6. npz2100_enter_idle()  — hand control back; nRF52833 power cut.
 *
 * The application never reaches the end of main() in normal operation.
 * npz2100_enter_idle() is the final instruction — after it the nPZ2100
 * cuts nRF52833 power and the MCU stops executing.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/logging/log.h>

#include <drivers/npz2100.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

/* -------------------------------------------------------------------------
 * Device handle — obtained from the DT node labelled "npz2100".
 * Defined in boards/nrf52833dk_nrf52833.overlay.
 * ---------------------------------------------------------------------- */
static const struct device *const npz2100 =
	DEVICE_DT_GET(DT_NODELABEL(npz2100));

/* -------------------------------------------------------------------------
 * Tool-generated register map.
 *
 * Replace this placeholder with the actual output from the Nanopower
 * configuration tool.  The byte-stream format is:
 *   [length] [start_addr] [data_0] ... [data_(length-2)]
 * where length = 1 (start_addr) + N (data bytes).
 *
 * The diff inside npz2100_apply_regmap() ensures only registers that
 * changed since the last boot are actually written to the nPZ2100.
 * On a warm boot where nothing changed, zero I²C transactions are issued.
 * ---------------------------------------------------------------------- */
static const uint8_t regmap[] = {
	/* ---- Global configuration (IOCFG1 @ 0x05 through TOUT_H @ 0x0D) -- */
	10, 0x05,
	0x00,       /* IOCFG1  */
	0x00,       /* IOCFG2  */
	0xFF,       /* IOCFG3: all INT pull-ups enabled at ~100 kΩ            */
	0x00,       /* IOCFG4  */
	0x1D,       /* IOCFG5: SPI_AUTO=1, I2C pull-ups auto, PSW_SR=1        */
	0x03,       /* SYSCFG1: peripheral 1 and 2 as wake-up sources          */
	0x04,       /* SYSCFG2: ADC3 (battery) as wake-up source               */
	0xFF,       /* TOUT_L:  maximum periodic time-out (LSB)                */
	0xFF,       /* TOUT_H:  maximum periodic time-out (MSB)                */

	/* ---- Peripheral 1: I²C temperature sensor @ 0x48 ----------------- */
	15, 0x1F,
	0x00,       /* P_BANK:  slot 0                                         */
	0x01,       /* CFGP1:   periodic power-on, poll+read+compare           */
	0x00,       /* IOP1:    SW_LP1, INT1, CSN1                             */
	0x00,       /* MODP1:   16-bit unsigned, default threshold trigger     */
	0x00, 0x01, /* PERP1:   polling period = 256 system clocks             */
	0x01,       /* NCMDP1:  1 initialisation command in SRAM               */
	0x48,       /* ADDRP1:  sensor I²C address                             */
	0x00,       /* RREGP1:  read from register 0x00                        */
	0x20, 0x1C, /* THROVP1: over-threshold = 0x1C20 (~+28 °C for TMP117)  */
	0x00, 0x00, /* THRUNP1: under-threshold = 0 (disabled)                 */
	0x08,       /* TWTP1:   post-init wait before read                     */

	/* ---- Peripheral 2–6: disabled (default state) --------------------- */
	15, 0x1F,
	0x01, 0x00, 0x15, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,

	15, 0x1F,
	0x02, 0x00, 0x2A, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,

	15, 0x1F,
	0x03, 0x00, 0x3F, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,

	15, 0x1F,
	0x04, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,

	15, 0x1F,
	0x05, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00,
};

/* -------------------------------------------------------------------------
 * Sensor initialisation commands stored in nPZ2100 SRAM.
 *
 * The nPZ2100 sends these to the sensor autonomously during polling while
 * the nRF52833 is powered off.  Each pair is (register_addr, value).
 * ---------------------------------------------------------------------- */
static const uint8_t sensor_init_cmds[] = {
	0x01, 0x00,   /* TMP117: config register, continuous conversion mode */
};

/* -------------------------------------------------------------------------
 * handle_peripheral_wake
 *
 * Called when a peripheral threshold crossing woke the system.
 * Reads the last sampled value from the nPZ2100 and processes it.
 * ---------------------------------------------------------------------- */
static void handle_peripheral_wake(int slot)
{
	uint16_t raw_value;
	int ret;

	ret = npz2100_periph_read_value(npz2100, (uint8_t)slot, &raw_value);
	if (ret != 0) {
		LOG_ERR("periph_read_value slot %d failed: %d", slot, ret);
		return;
	}

	/*
	 * TMP117 raw value: signed 16-bit, 1 LSB = 0.0078125 °C.
	 * Convert to integer millidegrees for logging without float.
	 */
	int32_t temp_mdeg = (int32_t)(int16_t)raw_value * 78;  /* ×0.0078125 °C × 10000 */

	LOG_INF("Peripheral %d: raw=0x%04X (~%d.%03d °C)",
		slot + 1, raw_value,
		temp_mdeg / 10000, (temp_mdeg % 10000) / 10);
}

/* -------------------------------------------------------------------------
 * handle_battery_low
 *
 * Called when the ADC3 (battery voltage) threshold was crossed.
 * Adjusts polling period to reduce power consumption.
 * ---------------------------------------------------------------------- */
static void handle_battery_low(void)
{
	LOG_WRN("Battery low — increasing polling period to conserve energy");

	/*
	 * Use the typed helper to modify peripheral 1's polling period
	 * in the shadow, then flush to device before re-entering idle.
	 * The change persists across nRF52833 power cycles because the
	 * nPZ2100 retains its register values while idle.
	 */
	npz2100_config_t *shadow = npz2100_get_shadow(npz2100);

	/* Double the polling period: read current value and double it. */
	npz2100_periph_shadow_t *p = &shadow->periph[0];
	uint16_t current_period = (uint16_t)p->perp_l |
				   ((uint16_t)p->perp_h << 8u);
	uint16_t new_period = current_period * 2u;

	if (new_period == 0u) {
		new_period = 2u; /* Clamp to minimum safe value. */
	}

	p->perp_l = (uint8_t)(new_period & 0xFFu);
	p->perp_h = (uint8_t)(new_period >> 8u);

	LOG_INF("Peripheral 1 period: %u → %u system clocks",
		current_period, new_period);

	/* npz2100_shadow_flush() is called in main() after all handlers. */
}

/* -------------------------------------------------------------------------
 * main
 *
 * Executes once per nRF52833 boot (= once per nPZ2100 wake cycle).
 * Does not contain a loop — the final call to npz2100_enter_idle() causes
 * the nPZ2100 to cut power to the nRF52833.
 * ---------------------------------------------------------------------- */
int main(void)
{
	int ret;
	npz2100_wake_reason_t reason;

	LOG_INF("--- nPZ2100 sample boot ---");

	/* ------------------------------------------------------------------ */
	/* 0. Verify the Zephyr device is ready (I²C bus OK, probe passed).   */
	/* ------------------------------------------------------------------ */
	if (!device_is_ready(npz2100)) {
		/*
		 * If the nPZ2100 is not reachable the system cannot re-enter
		 * idle safely.  Loop here so the watchdog fires and the
		 * nPZ2100 power-cycles the nRF52833.
		 */
		LOG_ERR("nPZ2100 device not ready — waiting for watchdog");
		while (true) {
			k_msleep(1000);
		}
	}

	/* ------------------------------------------------------------------ */
	/* 1. Read wake-up reason.  MUST be the first nPZ2100 operation.      */
	/*    Also kicks the nPZ2100 watchdog (STA1/STA2 read resets timer).  */
	/* ------------------------------------------------------------------ */
	ret = npz2100_boot_status(npz2100, &reason);
	if (ret != 0) {
		LOG_ERR("boot_status failed: %d — cannot determine wake reason", ret);
		/* Fall through: still try to re-enter idle. */
	}

	/* ------------------------------------------------------------------ */
	/* 2. Sync shadow from device.                                         */
	/*    The nPZ2100 retains all registers while the nRF52833 is off.    */
	/*    Reading back before applying the regmap ensures the diff is      */
	/*    computed against the device's actual current state.              */
	/* ------------------------------------------------------------------ */
	ret = npz2100_readback(npz2100);
	if (ret != 0) {
		LOG_ERR("readback failed: %d", ret);
	}

	/* ------------------------------------------------------------------ */
	/* 3. Apply desired register map.                                      */
	/*    Only registers that differ from the shadow are written.          */
	/*    On a typical warm boot this is zero I²C transactions.           */
	/* ------------------------------------------------------------------ */
	ret = npz2100_apply_regmap(npz2100, regmap, sizeof(regmap));
	if (ret != 0) {
		LOG_ERR("apply_regmap failed: %d", ret);
	}

	/* ------------------------------------------------------------------ */
	/* 4. Write sensor initialisation commands to SRAM (first boot only). */
	/*    After the first boot the nPZ2100 retains SRAM, so the diff      */
	/*    in apply_regmap already skipped SRAM_BANK if unchanged — but    */
	/*    SRAM content itself must be written explicitly if needed.        */
	/*                                                                     */
	/*    A production application would check the reset source to decide  */
	/*    whether SRAM needs re-initialising (only on cold/POR boot).      */
	/* ------------------------------------------------------------------ */
	if (reason.rst_src == NPZ2100_RST_SRC_POR) {
		LOG_INF("Power-on reset detected — writing SRAM init commands");
		ret = npz2100_sram_write(npz2100, 0x00u,
					 sensor_init_cmds,
					 sizeof(sensor_init_cmds));
		if (ret != 0) {
			LOG_ERR("SRAM write failed: %d", ret);
		}
	}

	/* ------------------------------------------------------------------ */
	/* 5. Handle wake reason — application logic.                          */
	/* ------------------------------------------------------------------ */
	bool config_changed = false;

	for (int i = 0; i < 6; i++) {
		if (reason.periph[i]) {
			handle_peripheral_wake(i);
		}
	}

	if (reason.adc3) {
		handle_battery_low();
		config_changed = true;
	}

	if (reason.timeout) {
		/* Periodic time-out: no action needed beyond re-entering idle. */
		LOG_INF("Periodic time-out wake — nothing to process");
	}

	if (reason.alarm) {
		/* Global counter alarm — application-specific handler here. */
		LOG_INF("Time counter alarm fired");
	}

	/* ------------------------------------------------------------------ */
	/* 6. Push runtime config changes to device (if any handler modified   */
	/*    the shadow via npz2100_get_shadow() + typed helpers).            */
	/* ------------------------------------------------------------------ */
	if (config_changed) {
		ret = npz2100_shadow_flush(npz2100);
		if (ret != 0) {
			LOG_ERR("shadow_flush failed: %d", ret);
		}
	}

	/* ------------------------------------------------------------------ */
	/* 7. Re-enter idle.  nPZ2100 will cut nRF52833 power.                */
	/*    This is the last instruction that executes.                      */
	/* ------------------------------------------------------------------ */
	LOG_INF("Returning to idle — nRF52833 power will be cut");

	ret = npz2100_enter_idle(npz2100);

	/*
	 * Reaching here means the I²C write to IDLE_RST failed.
	 * The nPZ2100 watchdog will power-cycle the system after its timeout.
	 */
	LOG_ERR("enter_idle failed: %d — waiting for watchdog", ret);
	while (true) {
		k_msleep(1000);
	}

	return 0; /* unreachable */
}
