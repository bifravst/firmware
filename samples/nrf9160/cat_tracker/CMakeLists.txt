#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#

cmake_minimum_required(VERSION 3.8.2)

include(../../../cmake/boilerplate.cmake NO_POLICY_SCOPE)

include($ENV{ZEPHYR_BASE}/cmake/app/boilerplate.cmake NO_POLICY_SCOPE)
project(cat_tracker)

# NORDIC SDK APP START
target_sources(app PRIVATE src/main.c)
# NORDIC SDK APP END

# Include source directory
zephyr_include_directories(src)

# Application directories
add_subdirectory(src/modem_data)
add_subdirectory(src/mqtt_behaviour)
add_subdirectory(src/gps_controller)
add_subdirectory(src/mqtt_codec)
