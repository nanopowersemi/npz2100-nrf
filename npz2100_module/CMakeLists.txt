# Copyright (c) Nanopower Semiconductor AS
# SPDX-License-Identifier: Apache-2.0
#
# CMakeLists.txt  (module root)
# ------------------------------
# Entry point for the Zephyr build system when this repository is used as a
# West module under NCS 3.0.2.
#
# Zephyr's module infrastructure calls add_subdirectory() on the path declared
# in zephyr/module.yml cmake: field (this file's directory).  We forward into
# the driver subdirectory; the driver CMakeLists.txt handles conditional
# compilation based on CONFIG_NPZ2100.
#
# The include/ directory is added here so that <drivers/npz2100.h> is
# resolvable from application and driver code without any extra configuration.

cmake_minimum_required(VERSION 3.20)

# Expose include/ to all targets in the workspace.
zephyr_include_directories(include)

# Delegate driver compilation to the driver subdirectory.
add_subdirectory(drivers/npz2100)
