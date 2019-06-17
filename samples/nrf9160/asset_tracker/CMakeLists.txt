#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#

cmake_minimum_required(VERSION 3.8.2)

include($ENV{ZEPHYR_BASE}/cmake/app/boilerplate.cmake NO_POLICY_SCOPE)
project(asset_tracker)

# NORDIC SDK APP START
target_sources(app PRIVATE src/main.c)

target_sources(app PRIVATE src/dependencies/gps_func.c)

target_sources(app PRIVATE src/dependencies/mqtt_func.c)

target_sources(app PRIVATE src/dependencies/accelerometer.c)

# NORDIC SDK APP END

include_directories(src/dependencies)
