#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#

cmake_minimum_required(VERSION 3.8.2)

include($ENV{ZEPHYR_BASE}/cmake/app/boilerplate.cmake NO_POLICY_SCOPE)
project(asset_tracker)

# Include application event headers
zephyr_library_include_directories(src/events)

# NORDIC SDK APP START
target_sources(app PRIVATE src/main.c)

target_sources(app PRIVATE
		src/events/measurement_event.c
)

target_sources(app PRIVATE
		src/modules/sensor_simulated.c
)


# NORDIC SDK APP END
