/*
 * Copyright (c) Nanopower Semiconductor AS
 * SPDX-License-Identifier: Apache-2.0
 *
 * drivers/npz2100/npz2100_zephyr.c
 * ----------------------------------
 * Zephyr device-model wrapper for the nPZ2100 power-saving IC.
 * Target: nRF5x / NCS 3.3.4.
 *
 * Power architecture
 * ------------------
 * The nPZ2100 controls the nRF5x host power switch (SW_HP).  When the
 * nPZ2100 enters idle mode it cuts power to the nRF5x completely.
 * There is no always-on interrupt line between the two chips.
 *
 * Every nRF5x boot is a fresh start caused by the nPZ2100 re-enabling
 * SW_HP.  The Zephyr device init (npz2100_init) runs at POST_KERNEL, before
 * main().  It sets up the HAL, probes the device, and seeds the shadow with
 * defaults.  It does NOT read back registers or apply a regmap — those are
 * application responsibilities that depend on the wake reason.
 *
 * Required application boot sequence (every boot, before application logic):
 *
 *   npz2100_boot_status(dev, &reason);   // read STA1-3, kick watchdog
 *   npz2100_readback(dev);               // sync shadow from device
 *   npz2100_apply_regmap(dev, map, len); // write only changed registers
 *   ... handle reason, optionally modify shadow ...
 *   npz2100_shadow_flush(dev);           // push runtime changes
 *   npz2100_enter_idle(dev);             // nPZ2100 cuts nRF5x power
 *
 * Portability boundary
 * --------------------
 * This is the ONLY source file that includes Zephyr headers.
 * npz2100.c and npz2100_mid.c are strictly platform-agnostic.
 *
 * Name collision resolution
 * -------------------------
 * the Zephyr public API to _ll-suffixed names before any header is included.
 */

#define DT_DRV_COMPAT nanopower_npz2100

/* Alias mid-level names before including any npz2100 header. */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <errno.h>

/* Platform-agnostic headers — no Zephyr dependencies inside these. */
#include "npz2100_mid.h"
#include "drivers/npz2100.h"

LOG_MODULE_REGISTER(npz2100, CONFIG_NPZ2100_LOG_LEVEL);

/* =========================================================================
 * Error code translation
 * ======================================================================= */

static int err_to_zephyr(npz2100_err_t e)
{
	switch (e) {
	case NPZ2100_OK:          return 0;
	case NPZ2100_ERR_IO:      return -EIO;
	case NPZ2100_ERR_ARG:     return -EINVAL;
	case NPZ2100_ERR_DEV:     return -ENODEV;
	case NPZ2100_ERR_TIMEOUT: return -ETIMEDOUT;
	case NPZ2100_ERR_STATE:   return -EPERM;
	default:                   return -EIO;
	}
}

/* =========================================================================
 * Per-device data  (mutable, one instance per DT node)
 * ======================================================================= */

struct npz2100_data {
	/** Platform-agnostic HAL descriptor — populated at init, immutable after. */
	npz2100_hal_t    hal;

	/** Shadow of the nPZ2100 register state.
	 *  Seeded with reset defaults at init.
	 *  Updated by readback(), apply_regmap(), and shadow_flush(). */
	npz2100_config_t shadow;

	/** Serialises all bus accesses across application threads. */
	struct k_mutex   lock;
};

/* =========================================================================
 * Per-device config  (immutable, built from DT at compile time)
 * ======================================================================= */

struct npz2100_config {
	/** I²C bus descriptor built from the devicetree node. */
	struct i2c_dt_spec i2c;
};

/* =========================================================================
 * Zephyr I²C HAL callbacks
 *
 * These two functions are the only coupling point between the Zephyr I²C
 * subsystem and the platform-agnostic npz2100 core.  They are registered
 * in npz2100_hal_t.write / .read and called by npz2100.c / npz2100_mid.c.
 *
 * ctx carries a const pointer to the i2c_dt_spec from npz2100_config.
 * The spec is immutable after init so no locking is needed here.
 * ======================================================================= */

/**
 * @brief HAL write — single I²C transaction: [reg_addr, data...].
 *
 * buf[0] is the register address prepended by the core driver.
 * buf[1..len-1] is the data payload.
 * Issues one START/STOP for the whole buffer regardless of length.
 */
static npz2100_err_t zephyr_i2c_write(uint8_t        i2c_addr,
				       const uint8_t *buf,
				       size_t         len,
				       void          *ctx)
{
	ARG_UNUSED(i2c_addr); /* Encoded in the i2c_dt_spec. */

	const struct i2c_dt_spec *spec = ctx;
	int ret = i2c_write_dt(spec, buf, len);

	if (ret != 0) {
		LOG_ERR("I2C write reg=0x%02X len=%zu err=%d", buf[0], len, ret);
		return NPZ2100_ERR_IO;
	}
	return NPZ2100_OK;
}

/**
 * @brief HAL read — write register pointer, then read N bytes.
 *
 * Issues: START addr+W, reg, rSTART addr+R, buf[0..len-1], STOP.
 */
static npz2100_err_t zephyr_i2c_read(uint8_t  i2c_addr,
				      uint8_t  reg,
				      uint8_t *buf,
				      size_t   len,
				      void    *ctx)
{
	ARG_UNUSED(i2c_addr);

	const struct i2c_dt_spec *spec = ctx;
	int ret = i2c_write_read_dt(spec, &reg, 1u, buf, len);

	if (ret != 0) {
		LOG_ERR("I2C read reg=0x%02X len=%zu err=%d", reg, len, ret);
		return NPZ2100_ERR_IO;
	}
	return NPZ2100_OK;
}

/* =========================================================================
 * Device initialisation
 * ======================================================================= */

/**
 * @brief nPZ2100 driver init — runs at POST_KERNEL, before main().
 *
 * Responsibilities:
 *   1. Verify the I²C bus is ready.
 *   2. Populate the HAL with Zephyr I²C callbacks and the bus spec pointer.
 *   3. Seed the shadow with power-on reset defaults (no I²C transaction).
 *   4. Probe the device (ID register = 0x74).
 *
 * What init does NOT do:
 *   - Does NOT read back device registers (application calls npz2100_readback).
 *   - Does NOT apply a regmap (application calls npz2100_apply_regmap).
 *   - Does NOT read STA1–STA3 (application calls npz2100_boot_status first).
 *
 * Rationale: the wake reason must be read before any other operation modifies
 * the status registers or the device state.  Deferring readback/apply to the
 * application lets it read the wake reason first and make decisions before
 * spending I²C time on a full register sync.
 */
static int npz2100_init(const struct device *dev)
{
	const struct npz2100_config *cfg  = dev->config;
	struct npz2100_data         *data = dev->data;
	int ret;

	/* 1. Verify I²C bus. */
	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR("I2C bus %s not ready", cfg->i2c.bus->name);
		return -ENODEV;
	}

	/* 2. Populate HAL.
	 *    ctx points to cfg->i2c — immutable, no ownership concern. */
	data->hal.write    = zephyr_i2c_write;
	data->hal.read     = zephyr_i2c_read;
	data->hal.i2c_addr = cfg->i2c.addr;
	data->hal.ctx      = (void *)&cfg->i2c;

	/* 3. Mutex + shadow defaults (no I²C). */
	k_mutex_init(&data->lock);

	ret = err_to_zephyr(npz2100_config_init_defaults(&data->shadow));
	if (ret != 0) {
		LOG_ERR("Shadow init failed: %d", ret);
		return ret;
	}

	/* 4. Probe — confirm ID register = 0x74. */
	ret = err_to_zephyr(npz2100_probe_ll(&data->hal));
	if (ret != 0) {
		LOG_ERR("nPZ2100 not found on %s @ 0x%02X (err %d)",
			cfg->i2c.bus->name, cfg->i2c.addr, ret);
		return ret;
	}

	LOG_INF("nPZ2100 ready on %s @ 0x%02X",
		cfg->i2c.bus->name, cfg->i2c.addr);

	return 0;
}

/* =========================================================================
 * Public API implementations
 * ======================================================================= */

int npz2100_boot_status(const struct device   *dev,
			 npz2100_wake_reason_t *reason)
{
	struct npz2100_data *data = dev->data;
	uint8_t sta1, sta2, sta3;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	/* Single burst read: STA1 (0x02), STA2 (0x03), STA3 (0x04).
	 * Reading STA1 and STA2 resets the nPZ2100 watchdog timer. */
	ret = err_to_zephyr(
		npz2100_status_read(&data->hal, &sta1, &sta2, &sta3));

	k_mutex_unlock(&data->lock);

	if (ret != 0) {
		LOG_ERR("boot_status: STA read failed: %d", ret);
		return ret;
	}

	LOG_INF("boot_status: STA1=0x%02X STA2=0x%02X STA3=0x%02X",
		sta1, sta2, sta3);

	if (reason == NULL) {
		return 0; /* Caller only needed the watchdog kick. */
	}

	/* ---- Decode into wake reason struct ---- */

	/* Reset sources from STA1. */
	reason->rst_src  = NPZ2100_STA1_RST_SRC_GET(sta1);
	reason->srst_src = NPZ2100_STA1_SRST_SRC_GET(sta1);

	/* ADC and timing flags from STA1. */
	reason->adc1    = (bool)(sta1 & NPZ2100_STA1_FADC1_MSK);
	reason->adc2    = (bool)(sta1 & NPZ2100_STA1_FADC2_MSK);
	reason->adc3    = (bool)(sta1 & NPZ2100_STA1_FADC3_MSK);
	reason->timeout = (bool)(sta1 & NPZ2100_STA1_FTOUT_MSK);

	/* Peripheral trigger, alarm, log_full flags from STA2. */
	for (int i = 0; i < 6; i++) {
		reason->periph[i] = (bool)(sta2 & BIT(i));
	}
	reason->alarm    = (bool)(sta2 & NPZ2100_STA2_FALM_MSK);
	reason->log_full = (bool)(sta2 & NPZ2100_STA2_FLOG_MSK);

	/* Counter, PA active, NAK flags from STA3. */
	reason->counter   = (bool)(sta3 & NPZ2100_STA3_FCNT_MSK);
	reason->pa_active = (bool)(sta3 & NPZ2100_STA3_FPA_MSK);
	for (int i = 0; i < 6; i++) {
		reason->nak[i] = (bool)(sta3 & BIT(i));
	}

	/* Log all status fields — identical output to STM32 port. */

	/* Reset source — always log so every boot shows why it started. */
	static const char * const rst_names[] = {
		"Power-on reset", "External NRST pin",
		"I2C command",    "Brown-out reset"
	};
	LOG_INF("  reset_src: %s (0x%02X)",
		rst_names[reason->rst_src & 0x03u], reason->rst_src);
	if (reason->srst_src != 0u) {
		static const char * const srst_names[] = {
			"Power-on reset", "I2C soft reset",
			"Watchdog reset",  "Invalid"
		};
		LOG_INF("  soft_reset_src: %s (0x%02X)",
			srst_names[reason->srst_src & 0x03u], reason->srst_src);
	}

	/* Peripheral threshold triggers. */
	for (int i = 0; i < 6; i++) {
		if (reason->periph[i]) {
			LOG_INF("  Wake: peripheral %d triggered", i + 1);
		}
	}

	/* ADC, timing, event flags. */
	if (reason->adc1)      { LOG_INF("  Wake: ADC1 threshold"); }
	if (reason->adc2)      { LOG_INF("  Wake: ADC2 threshold"); }
	if (reason->adc3)      { LOG_INF("  Wake: ADC3 (battery) threshold"); }
	if (reason->timeout)   { LOG_INF("  Wake: periodic time-out"); }
	if (reason->alarm)     { LOG_INF("  Wake: time counter alarm"); }
	if (reason->counter)   { LOG_INF("  Wake: event counter"); }
	if (reason->log_full)  { LOG_INF("  Wake: SRAM log area full"); }
	if (reason->pa_active) { LOG_INF("  Wake: power-aware mode active"); }

	/* NAK flags — peripheral did not respond during autonomous polling. */
	for (int i = 0; i < 6; i++) {
		if (reason->nak[i]) {
			LOG_INF("  NAK:  peripheral %d did not acknowledge I2C", i + 1);
		}
	}

	return 0;
}

/* ---- readback --------------------------------------------------------- */

int npz2100_readback(const struct device *dev)
{
	struct npz2100_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = err_to_zephyr(
		npz2100_map_readback(&data->hal, &data->shadow));
	k_mutex_unlock(&data->lock);

	if (ret == 0) {
		LOG_DBG("readback: shadow synced from device");
	} else {
		LOG_ERR("readback failed: %d", ret);
	}
	return ret;
}

/* ---- apply_regmap ----------------------------------------------------- */

int npz2100_apply_regmap(const struct device *dev,
			  const uint8_t       *map,
			  size_t               map_len)
{
	struct npz2100_data *data = dev->data;
	int ret;

	if (map == NULL || map_len == 0u) {
		return -EINVAL;
	}

	/* Validate stream framing before touching the bus. */
	if (npz2100_map_validate(map, map_len) != NPZ2100_OK) {
		LOG_ERR("apply_regmap: malformed byte stream");
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	uint8_t ndiff = npz2100_map_diff_count(&data->shadow, map, map_len);

	LOG_DBG("apply_regmap: %u register(s) differ from shadow", ndiff);

	if (ndiff == 0u) {
		/* Device already matches — skip all I²C writes. */
		k_mutex_unlock(&data->lock);
		LOG_INF("apply_regmap: device in sync, nothing written");
		return 0;
	}

	ret = err_to_zephyr(
		npz2100_map_apply(&data->hal, &data->shadow, map, map_len));

	k_mutex_unlock(&data->lock);

	if (ret == 0) {
		LOG_INF("apply_regmap: %u register(s) written", ndiff);
	} else {
		LOG_ERR("apply_regmap failed: %d", ret);
	}
	return ret;
}

/* ---- get_shadow ------------------------------------------------------- */

npz2100_config_t *npz2100_get_shadow(const struct device *dev)
{
	if (dev == NULL) {
		return NULL;
	}
	/* No lock — caller must coordinate externally when modifying fields.
	 * Use npz2100_shadow_flush() to push changes under the driver lock. */
	return &((struct npz2100_data *)dev->data)->shadow;
}

/* ---- shadow_flush ----------------------------------------------------- */

int npz2100_shadow_flush(const struct device *dev)
{
	struct npz2100_data *data = dev->data;
	int ret = 0;

	/*
	 * Walk every writable non-banked register and every peripheral bank,
	 * writing via shadow_write_reg() which diffs before each write.
	 * Consecutive runs of dirty registers could be burst-written for
	 * extra efficiency, but the diff check already skips unchanged ones
	 * so the worst case is one I²C transaction per changed register.
	 */
	struct { uint8_t addr; uint8_t *field; } regs[] = {
		{ NPZ2100_REG_IOCFG1,    &data->shadow.iocfg1     },
		{ NPZ2100_REG_IOCFG2,    &data->shadow.iocfg2     },
		{ NPZ2100_REG_IOCFG3,    &data->shadow.iocfg3     },
		{ NPZ2100_REG_IOCFG4,    &data->shadow.iocfg4     },
		{ NPZ2100_REG_IOCFG5,    &data->shadow.iocfg5     },
		{ NPZ2100_REG_SYSCFG1,   &data->shadow.syscfg1    },
		{ NPZ2100_REG_SYSCFG2,   &data->shadow.syscfg2    },
		{ NPZ2100_REG_TOUT_L,    &data->shadow.tout_l     },
		{ NPZ2100_REG_TOUT_H,    &data->shadow.tout_h     },
		{ NPZ2100_REG_GCT_MS,    &data->shadow.gct_ms     },
		{ NPZ2100_REG_GCT_0,     &data->shadow.gct_0      },
		{ NPZ2100_REG_GCT_1,     &data->shadow.gct_1      },
		{ NPZ2100_REG_GCT_2,     &data->shadow.gct_2      },
		{ NPZ2100_REG_GCT_3,     &data->shadow.gct_3      },
		{ NPZ2100_REG_GCT_ALM_0, &data->shadow.gct_alm_0  },
		{ NPZ2100_REG_GCT_ALM_1, &data->shadow.gct_alm_1  },
		{ NPZ2100_REG_GCT_ALM_2, &data->shadow.gct_alm_2  },
		{ NPZ2100_REG_GCT_ALM_3, &data->shadow.gct_alm_3  },
		{ NPZ2100_REG_WDOG_L,    &data->shadow.wdog_l     },
		{ NPZ2100_REG_WDOG_H,    &data->shadow.wdog_h     },
		{ NPZ2100_REG_GTC_CFG,   &data->shadow.gtc_cfg    },
		{ NPZ2100_REG_PA_CFG,    &data->shadow.pa_cfg     },
		{ NPZ2100_REG_ADCCFG,    &data->shadow.adccfg     },
		{ NPZ2100_REG_THROVA1,   &data->shadow.throva1    },
		{ NPZ2100_REG_THRUNA1,   &data->shadow.thruna1    },
		{ NPZ2100_REG_THROVA2,   &data->shadow.throva2    },
		{ NPZ2100_REG_THRUNA2,   &data->shadow.thruna2    },
		{ NPZ2100_REG_THROVA3,   &data->shadow.throva3    },
		{ NPZ2100_REG_THRUNA3,   &data->shadow.thruna3    },
		{ NPZ2100_REG_LOGCFG,    &data->shadow.logcfg     },
		{ NPZ2100_REG_LOGSADDR,  &data->shadow.logsaddr   },
		{ NPZ2100_REG_CNTCFG,    &data->shadow.cntcfg     },
		{ NPZ2100_REG_CNT_TRIG_0,&data->shadow.cnt_trig_0 },
		{ NPZ2100_REG_CNT_TRIG_1,&data->shadow.cnt_trig_1 },
		{ NPZ2100_REG_CNT_TRIG_2,&data->shadow.cnt_trig_2 },
		{ NPZ2100_REG_CNT_TRIG_3,&data->shadow.cnt_trig_3 },
		{ NPZ2100_REG_SRAM_BANK, &data->shadow.sram_bank  },
	};

	k_mutex_lock(&data->lock, K_FOREVER);

	for (size_t i = 0u; i < ARRAY_SIZE(regs) && ret == 0; i++) {
		ret = err_to_zephyr(
			npz2100_shadow_write_reg(&data->hal, &data->shadow,
						 regs[i].addr,
						 *regs[i].field));
	}

	/* Peripheral banks 0–5. */
	for (uint8_t slot = 0u;
	     slot <= NPZ2100_P_BANK_MAX && ret == 0;
	     slot++) {
		ret = err_to_zephyr(
			npz2100_periph_apply(&data->hal,
					     &data->shadow, slot));
	}

	k_mutex_unlock(&data->lock);

	if (ret != 0) {
		LOG_ERR("shadow_flush failed: %d", ret);
	}
	return ret;
}

/* ---- SRAM ------------------------------------------------------------- */

int npz2100_sram_write(const struct device *dev,
		        uint8_t              sram_addr,
		        const uint8_t       *buf,
		        size_t               len)
{
	struct npz2100_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = err_to_zephyr(
		npz2100_sram_write_ll(&data->hal, &data->shadow,
				      sram_addr, buf, len));
	k_mutex_unlock(&data->lock);

	return ret;
}

int npz2100_sram_read(const struct device *dev,
		       uint8_t              sram_addr,
		       uint8_t             *buf,
		       size_t               len)
{
	struct npz2100_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = err_to_zephyr(
		npz2100_sram_read_ll(&data->hal, &data->shadow,
				     sram_addr, buf, len));
	k_mutex_unlock(&data->lock);

	return ret;
}

/* ---- probe / enter_idle / soft_reset ---------------------------------- */

int npz2100_probe(const struct device *dev)
{
	struct npz2100_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = err_to_zephyr(npz2100_probe_ll(&data->hal));
	k_mutex_unlock(&data->lock);

	return ret;
}

int npz2100_enter_idle(const struct device *dev)
{
	struct npz2100_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	LOG_INF("Entering idle — nRF5x power will be cut");
	ret = err_to_zephyr(npz2100_enter_idle_ll(&data->hal));
	k_mutex_unlock(&data->lock);

	/*
	 * If the I²C write succeeded, the nPZ2100 will cut power to the
	 * nRF5x within microseconds.  Execution will not reach here
	 * in normal operation.
	 *
	 * If the write failed (ret != 0), return the error so the caller
	 * can handle it (e.g. retry or reset).
	 */
	return ret;
}

int npz2100_soft_reset(const struct device *dev)
{
	struct npz2100_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = err_to_zephyr(npz2100_soft_reset_ll(&data->hal));
	k_mutex_unlock(&data->lock);

	if (ret == 0) {
		LOG_INF("nPZ2100 soft reset issued");
	}
	return ret;
}

/* =========================================================================
 * Device instantiation
 *
 * DEVICE_DT_INST_DEFINE expands once per nanapower,npz2100 DT node with
 * status = "okay", producing independent config/data/device objects.
 * Multiple nPZ2100 ICs are supported with no code changes.
 * ======================================================================= */

#define NPZ2100_DEFINE(inst)                                              \
	static const struct npz2100_config npz2100_cfg_##inst = {        \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                       \
	};                                                                \
	static struct npz2100_data npz2100_data_##inst;                   \
	DEVICE_DT_INST_DEFINE(inst,                                       \
			      npz2100_init,                               \
			      NULL,          /* pm_device: none */        \
			      &npz2100_data_##inst,                       \
			      &npz2100_cfg_##inst,                        \
			      POST_KERNEL,                                \
			      CONFIG_NPZ2100_INIT_PRIORITY,               \
			      NULL);         /* no device API vtable */


int npz2100_periph_read_value(const struct device *dev,
                               uint8_t              slot,
                               uint16_t            *value)
{
	struct npz2100_data *data = dev->data;
	int ret;

	if (slot > NPZ2100_P_BANK_MAX || value == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = err_to_zephyr(
		npz2100_periph_read_value_ll(&data->hal, &data->shadow,
					     slot, value));
	k_mutex_unlock(&data->lock);

	return ret;
}

DT_INST_FOREACH_STATUS_OKAY(NPZ2100_DEFINE)
