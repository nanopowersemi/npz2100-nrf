# Copyright (c) Nanopower Semiconductor AS
# SPDX-License-Identifier: Apache-2.0
#
# CMakeLists.txt  (drivers/npz2100)
# -----------------------------------
# Compiles the nPZ2100 driver sources when CONFIG_NPZ2100=y.
#
# Source layout:
#
#   npz2100_zephyr.c   - Zephyr device-model wrapper:
#                        Implements Zephyr init/device callbacks.
#                        Bridges Zephyr I2C API to npz2100 HAL callbacks.
#
#   npz2100.c          - Platform-agnostic HAL primitives:
#                        npz2100_reg_write / _read / _burst_* / _rmw.
#
#   npz2100_lib.c      - Platform-agnostic lib API:
#                        Byte-stream regmap parser, shadow diffing,
#                        typed config helpers, SRAM access.
#
#   npz2100_datalog.c  - Platform-agnostic data-log layer:
#                        SRAM log extraction, entry parsing,
#                        rotation handling, log clear.
#
# The platform-agnostic files must NOT include any Zephyr headers.
# All Zephyr coupling is in npz2100_zephyr.c only.

zephyr_library_named(npz2100)

# Map Kconfig shadow switch to the C preprocessor symbol consumed by
# npz2100_lib.c and npz2100_zephyr.c.
zephyr_compile_definitions(
    NPZ2100_SHADOW_ENABLE=$<IF:$<BOOL:${CONFIG_NPZ2100_SHADOW}>,1,0>
)

# Platform-agnostic core sources (no Zephyr dependencies).
zephyr_library_sources(
    npz2100.c
    npz2100_lib.c
    npz2100_datalog.c
)

# Zephyr device-model wrapper (all Zephyr API calls live here).
zephyr_library_sources(npz2100_zephyr.c)

# Expose the driver public API header to all consumers.
zephyr_library_include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
