#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#

cmake_minimum_required(VERSION 3.8.2)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cat_tracker)

# NORDIC SDK APP START
target_sources(app PRIVATE src/main.c)
# NORDIC SDK APP END

# Include source directory
zephyr_include_directories(src)

# Application directories
add_subdirectory(src/gps_controller)
add_subdirectory(src/ui)
add_subdirectory(src/cloud_codec)
add_subdirectory(src/ext_sensors)
add_subdirectory_ifdef(CONFIG_WATCHDOG src/watchdog)
