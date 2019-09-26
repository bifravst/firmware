#
# Copyright (c) 2019 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#
zephyr_library()
zephyr_library_sources(
	src/bifravst_cloud.c
	src/bifravst_cloud_codec.c
)
zephyr_include_directories(./include)