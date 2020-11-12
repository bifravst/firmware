#
# Copyright (c) 2019-2020 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#

zephyr_include_directories(.)
target_sources_ifdef(CONFIG_AWS_IOT app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/aws_iot_codec.c)
target_sources_ifdef(CONFIG_AZURE_IOT_HUB app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/azure_iot_hub_codec.c)
target_sources_ifdef(CONFIG_NRF_CLOUD app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/nrf_cloud_codec.c)
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/cloud_codec_ringbuffer.c)
