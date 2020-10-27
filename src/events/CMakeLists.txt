#
# Copyright (c) 2020 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#

target_sources(app PRIVATE
	       ${CMAKE_CURRENT_SOURCE_DIR}/app_mgr_event.c
	       ${CMAKE_CURRENT_SOURCE_DIR}/cloud_mgr_event.c
	       ${CMAKE_CURRENT_SOURCE_DIR}/gps_mgr_event.c
	       ${CMAKE_CURRENT_SOURCE_DIR}/modem_mgr_event.c
	       ${CMAKE_CURRENT_SOURCE_DIR}/data_mgr_event.c
	       ${CMAKE_CURRENT_SOURCE_DIR}/sensor_mgr_event.c
	       ${CMAKE_CURRENT_SOURCE_DIR}/ui_mgr_event.c
	       ${CMAKE_CURRENT_SOURCE_DIR}/util_mgr_event.c
	       ${CMAKE_CURRENT_SOURCE_DIR}/output_mgr_event.c
)
