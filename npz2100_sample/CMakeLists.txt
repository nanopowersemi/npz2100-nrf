# Copyright (c) Nanopower Semiconductor AS
# SPDX-License-Identifier: Apache-2.0
#
# npz2100_sample/CMakeLists.txt
# ------------------------------
# CMake entry point for the nPZ2100 sample application.
# Target: nRF52833 / NCS 3.0.2 / VS Code nRF Connect extension.
#
# The nPZ2100 Zephyr module (npz2100_module/) is referenced via
# ZEPHYR_EXTRA_MODULES so no west.yml changes are required for evaluation.
#
# When promoting to production, add the module to your workspace west.yml
# instead (see npz2100_module/zephyr/module.yml for the manifest snippet).
#
# Building from the VS Code nRF Connect extension:
#   1. Open the npz2100_sample/ folder as the application.
#   2. Add build configuration → board: nrf52833dk/nrf52833.
#   3. The CMake cache entry below adds the module automatically.
#
# Building from the terminal:
#   west build -b nrf52833dk/nrf52833 npz2100_sample/ \
#       -- -DZEPHYR_EXTRA_MODULES=<absolute-path>/npz2100_module

cmake_minimum_required(VERSION 3.20)

# ---- Register the nPZ2100 module ----------------------------------------
#
# ZEPHYR_EXTRA_MODULES tells the Zephyr build system to treat the module
# directory as a West module, loading its zephyr/module.yml automatically.
# This adds the driver, DT bindings, and include paths without modifying
# west.yml or the NCS installation.
#
# The path below is relative to this CMakeLists.txt.  It assumes the
# delivered zip is extracted so that npz2100_module/ and npz2100_sample/
# are siblings — the expected layout:
#
#   npz2100_ncs/
#   ├── npz2100_module/      ← Zephyr module (driver, headers, bindings)
#   └── npz2100_sample/      ← This application (open this in VS Code)
#
# If you move npz2100_module/ elsewhere, update the path below to match.
list(APPEND ZEPHYR_EXTRA_MODULES
    ${CMAKE_CURRENT_SOURCE_DIR}/../npz2100_module
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(npz2100_sample VERSION 1.0.0)

target_sources(app PRIVATE src/main.c)
