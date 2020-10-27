#
# Copyright (c) 2020 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#

zephyr_include_directories(.)
target_sources_ifdef(CONFIG_CLOUD_MANAGER app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cloud_manager.c)
target_sources_ifdef(CONFIG_GPS_MANAGER app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gps_manager.c)
target_sources_ifdef(CONFIG_MODEM_MANAGER app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/modem_manager.c)
target_sources_ifdef(CONFIG_UI_MANAGER app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui_manager.c)
target_sources_ifdef(CONFIG_SENSOR_MANAGER app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sensor_manager.c)
target_sources_ifdef(CONFIG_DATA_MANAGER app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/data_manager.c)
target_sources_ifdef(CONFIG_UTIL_MANAGER app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/util_manager.c)
target_sources_ifdef(CONFIG_OUTPUT_MANAGER app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/output_manager.c)
