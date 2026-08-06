# nPZ2100 — nRF Connect SDK Driver Package

**Target:** nRF Host Chip · **SDK:** NCS 3.0.2 · **IDE:** VS Code + nRF Connect extension

---

## What is in this package

```
npz2100_ncs/
├── README.md                    ← This file
├── npz2100_module/              ← Zephyr driver module (add once, reference forever)
│   ├── zephyr/module.yml        ← West module declaration
│   ├── CMakeLists.txt
│   ├── Kconfig
│   ├── drivers/npz2100/
│   │   ├── npz2100.c            ← Platform-agnostic HAL primitives
│   │   ├── npz2100_mid.c        ← Mid-level API (regmap, shadow, typed helpers)
│   │   ├── npz2100_zephyr.c     ← Zephyr device-model wrapper
│   │   ├── CMakeLists.txt
│   │   └── Kconfig
│   ├── dts/bindings/misc/
│   │   └── nanopower,npz2100.yaml
│   └── include/
│       ├── drivers/npz2100.h    ← Public Zephyr API (include this in your app)
│       ├── npz2100_hal.h
│       ├── npz2100_mid.h
│       ├── npz2100_regs_system.h
│       ├── npz2100_regs_io.h
│       ├── npz2100_regs_periph.h
│       └── npz2100_regs_adc_log.h
│
└── npz2100_sample/              ← Reference application (open this in VS Code)
    ├── CMakeLists.txt
    ├── prj.conf
    ├── boards/
    │   └── <your_board>.overlay ← Rename/replace for your target board
    └── src/
        └── main.c
```

---

## Prerequisites

| Tool | Version | Download |
|------|---------|----------|
| VS Code | Any recent | https://code.visualstudio.com |
| nRF Connect for VS Code extension pack | Latest | VS Code Extensions Marketplace |
| nRF Connect SDK | **3.0.2** | Installed via nRF Connect for VS Code |
| nRF Host Chip DK or custom board | Any NCS-supported nRF Host Chip | https://www.nordicsemi.com |

Install the nRF Connect SDK through the **nRF Connect for VS Code** extension:
`View → Extensions → nRF Connect for VS Code → Install SDK 3.0.2`

---

## Quick start — 5 steps

### Step 1 — Extract the zip

Extract this zip to a location of your choice. The result must be:

```
<your-path>/npz2100_ncs/
├── npz2100_module/
└── npz2100_sample/
```

Keep both folders as siblings — the sample's `CMakeLists.txt` references the module via a relative path (`../npz2100_module`).

---

### Step 2 — Open the sample in VS Code

```
File → Open Folder → select npz2100_ncs/npz2100_sample/
```

VS Code will detect the nRF Connect application structure automatically.

---

### Step 3 — Add a build configuration

In the **nRF Connect** sidebar panel:

1. Click **+ Add Build Configuration**
2. Board: select your target (e.g. `nrf52833dk/nrf52833`, `nrf5340dk/nrf5340/cpuapp`, `nrf9160dk/nrf9160`)
3. SDK: `3.0.2`
4. Leave all other settings at default
5. Click **Build Configuration**

The `CMakeLists.txt` in `npz2100_sample/` registers `npz2100_module/` automatically via `ZEPHYR_EXTRA_MODULES` — no additional setup is required.

> **Board overlay:** The sample includes a reference overlay for the nRF52833 DK.
> Rename or replace it with an overlay matching your target board.
> See **Adapting the sample** below for instructions.

---

### Step 4 — Connect your hardware

Wire the nPZ2100 to your nRF Host Chip:

| nPZ2100 pin | nRF Host Chip pin | Notes |
|-------------|---------------|-------|
| SDA | I²C SDA pin of your nRF Host Chip | Check your board's default I²C0 SDA |
| SCL | I²C SCL pin of your nRF Host Chip | Check your board's default I²C0 SCL |
| VBAT | VDD (3.0 V) | Device VDD or external supply |
| VSS | GND | |
| SW_HP | nRF Host Chip VDD | **Power control line — see below** |

**Required bypass capacitors** (place as close to the nPZ2100 as possible):

| Capacitor | Value | Pin |
|-----------|-------|-----|
| C1 | 100 nF C0G | VBAT |
| C2 | 10 nF C0G | VDD1V2 |
| C3 | 10 nF C0G | VDDD |

**SW_HP → nRF Host Chip VDD:**
The nPZ2100's SW_HP pin controls the nRF Host Chip power supply. When the nPZ2100
enters idle mode it de-asserts SW_HP, cutting power to the nRF Host Chip completely.
SW_HP should feed the nRF Host Chip VDD rail through an appropriate P-channel MOSFET
or power switch IC rated for the chip's current requirements.

> **For initial evaluation on a DK**, you can leave SW_HP disconnected and
> power the nRF Host Chip from the DK's USB. The driver will still initialise,
> communicate with the nPZ2100, and `npz2100_enter_idle()` will write the
> idle command — but the nRF Host Chip will remain powered since SW_HP is not
> connected to its supply. This is sufficient to test I²C communication,
> register configuration, and wake-reason decoding.

---

### Step 5 — Build, flash, and observe

In the nRF Connect panel:

1. Click **Build** (or `Ctrl+Shift+B`)
2. Click **Flash** once the build completes
3. Open **RTT Viewer** or the **Serial Terminal** to see output

Expected first-boot output:

```
[00:00:00.008] <inf> npz2100: nPZ2100 ready on i2c@40003000 @ 0x6f
[00:00:00.012] <inf> app: --- nPZ2100 sample boot ---
[00:00:00.015] <inf> npz2100: boot_status: STA1=0x00 STA2=0x00 STA3=0x00
[00:00:00.016] <inf> npz2100:   reset_src: Power-on reset (0x00)
[00:00:00.031] <inf> npz2100: apply_regmap: 18 register(s) written
[00:00:00.034] <inf> app: Returning to idle — nRF Host Chip power will be cut
[00:00:00.036] <inf> npz2100: Entering idle — nRF Host Chip power will be cut
```

If you see `nPZ2100 not found`, check your SDA/SCL wiring and verify the
I²C address matches the `reg = <0x6f>` in the overlay.

---

## Adapting the sample for your application

### Create a board overlay for your target

The sample ships with a reference overlay for the nRF52833 DK. To target a
different board, create a new file in `boards/` named after your board target:

```
boards/
└── <board_target>.overlay
```

where `<board_target>` matches the board string used in the build configuration
(e.g. `nrf5340dk_nrf5340_cpuapp`, `nrf9160dk_nrf9160`, `your_custom_board`).

Minimal overlay content — adjust the I²C controller (`&i2c0`, `&i2c1`, etc.)
and I²C address to match your hardware:

```dts
&i2c0 {
    status = "okay";
    clock-frequency = <I2C_BITRATE_STANDARD>;   /* 100 kHz */

    npz2100: npz2100@6f {
        compatible = "nanopower,npz2100";
        reg = <0x6f>;
        label = "NPZ2100";
    };
};
```

### Set I²C pins for your board

If your board's I²C pins are not already assigned in its board definition,
add a `pinctrl` block to your overlay:

```dts
&pinctrl {
    i2c0_default: i2c0_default {
        group1 {
            psels = <NRF_PSEL(TWIM_SDA, 0, YOUR_SDA_PIN)>,
                    <NRF_PSEL(TWIM_SCL, 0, YOUR_SCL_PIN)>;
        };
    };
    i2c0_sleep: i2c0_sleep {
        group1 {
            psels = <NRF_PSEL(TWIM_SDA, 0, YOUR_SDA_PIN)>,
                    <NRF_PSEL(TWIM_SCL, 0, YOUR_SCL_PIN)>;
            low-power-enable;
        };
    };
};

&i2c0 {
    pinctrl-0 = <&i2c0_default>;
    pinctrl-1 = <&i2c0_sleep>;
    pinctrl-names = "default", "sleep";
};
```

### Replace the register map

The `npz2100_regmap[]` array in `src/main.c` is a placeholder. Replace it
with the output of the **Nanopower configuration tool**.

The format is a flat byte stream of segments:
```
[length] [start_addr] [data_0] ... [data_(length-2)]
```
where `length = 1 (start_addr) + N (data bytes)`.

### Handle wake reasons

Add cases to `main.c` in the wake-reason handler section:

```c
if (reason.periph[0]) { /* peripheral 1 threshold crossed */ }
if (reason.periph[1]) { /* peripheral 2 threshold crossed */ }
if (reason.adc3)      { /* battery voltage threshold      */ }
if (reason.alarm)     { /* time counter alarm              */ }
if (reason.timeout)   { /* periodic time-out               */ }
```

### Modify configuration at runtime

Use the typed helpers and flush before re-entering idle:

```c
npz2100_config_t *shadow = npz2100_get_shadow(npz2100);

/* Example: slow down peripheral 1 polling when battery is low */
npz2100_periph_cfg_t pcfg = { .period = 4096, /* other fields */ };
npz2100_periph_set(shadow, 0, &pcfg);

npz2100_shadow_flush(npz2100);
npz2100_enter_idle(npz2100);
```

---

## Adding the module to your own application

### Option A — ZEPHYR_EXTRA_MODULES (no west.yml change)

Copy `npz2100_module/` next to your application and add to your
`CMakeLists.txt` before `find_package(Zephyr ...)`:

```cmake
list(APPEND ZEPHYR_EXTRA_MODULES
    ${CMAKE_CURRENT_SOURCE_DIR}/../npz2100_module
)
```

### Option B — west.yml (production, recommended)

Add the module as a West project and run `west update`:

```yaml
projects:
  - name: npz2100
    url: https://github.com/nanopowersemi/npz2100-zephyr  # your repo URL
    revision: v1.0.0
    path: modules/npz2100
```

Then remove the `ZEPHYR_EXTRA_MODULES` line from `CMakeLists.txt`.

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| `nPZ2100 not found on i2c@... @ 0x6f` | Wrong I²C address or wiring | Check SDA/SCL, verify address with a logic analyser |
| `I2C bus ... not ready` | I²C controller not enabled in DT | Add `status = "okay"` to your I²C node in the overlay |
| Build error: `unknown vendor prefix 'nanopower'` | DT binding not found | Verify `npz2100_module/` path in `CMakeLists.txt` |
| Build error: `DT_HAS_NANOPOWER_NPZ2100_ENABLED` undefined | DT node missing or wrong compatible | Check overlay `compatible = "nanopower,npz2100"` |
| `apply_regmap: malformed byte stream` | Wrong `length` in regmap | Check segment framing: `length = 1 (start_addr) + N (data bytes)` |
| nRF Host Chip keeps rebooting immediately | SW_HP wired incorrectly | Verify SW_HP polarity and MOSFET circuit |
| Overlay not picked up | Wrong overlay filename | Filename must match board target string exactly |

---

## Key API reference

```c
#include <drivers/npz2100.h>

/* Boot sequence — call in this order on every boot */
int npz2100_boot_status(dev, &reason);   // read wake reason + kick watchdog
int npz2100_readback(dev);               // sync shadow from device
int npz2100_apply_regmap(dev, map, len); // write only changed registers

/* Runtime config */
npz2100_config_t *npz2100_get_shadow(dev);   // get shadow for typed helpers
int npz2100_shadow_flush(dev);               // push shadow to device

/* SRAM (sensor init commands) */
int npz2100_sram_write(dev, addr, data, len);
int npz2100_sram_read(dev, addr, data, len);

/* Control */
int npz2100_enter_idle(dev);   // does not return — nPZ2100 cuts power
int npz2100_soft_reset(dev);
int npz2100_probe(dev);        // verify ID register = 0x74
```

All functions return `0` on success or a standard Zephyr `errno` on failure.

---

## Contact

Nanopower Semiconductor AS  
www.nanopowersemi.com  
info@nanopowersemi.com
