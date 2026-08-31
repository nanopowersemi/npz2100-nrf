# Copyright (c) Nanopower Semiconductor AS
# SPDX-License-Identifier: Apache-2.0
#
# CMakeLists.txt  (drivers/npz2100)
# -----------------------------------
# Compiles the nPZ2100 driver sources when CONFIG_NPZ2100=y.
#
# Source layout:
#
#   npz2100_zephyr.c   — Zephyr device-model wrapper (this driver):
#                         • Implements the Zephyr init/device callbacks.
#                         • Bridges Zephyr I²C API ↔ npz2100 HAL callbacks.
#
#   npz2100.c          — Platform-agnostic low-level primitives (HAL):
#                         • npz2100_reg_write / _read / _burst_* / _rmw.
#
#   npz2100_mid.c      — Platform-agnostic mid-level API:
#                         • Byte-stream regmap parser, shadow diffing,
#                           typed config helpers, SRAM access.
#
# The platform-agnostic files are copied from the npz2100 C driver project.
# They must NOT include any Zephyr headers — all Zephyr coupling is in
# npz2100_zephyr.c only, preserving portability of the core driver.

zephyr_library_named(npz2100)
# Map Kconfig shadow switch to the C preprocessor symbol consumed by
# npz2100_mid.c and npz2100_zephyr.c.
zephyr_compile_definitions(
    NPZ2100_SHADOW_ENABLE=$<IF:$<BOOL:${CONFIG_NPZ2100_SHADOW}>,1,0>
)


# Platform-agnostic core sources (no Zephyr dependencies).
zephyr_library_sources(
    npz2100.c
    npz2100_mid.c
)

# Zephyr device-model wrapper (all Zephyr API calls live here).
zephyr_library_sources(npz2100_zephyr.c)

# Expose the driver's public API header to all consumers.
zephyr_library_include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
