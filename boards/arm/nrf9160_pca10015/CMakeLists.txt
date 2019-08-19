# Kconfig - nRF91 PCA10015 board configuration
#
# Copyright (c) 2019 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic

if(CONFIG_BOARD_NRF9160_PCA10015 AND NOT DEFINED CONFIG_MCUBOOT)
	zephyr_library()
	zephyr_library_sources(board_secure.c)
endif()

if(CONFIG_BOARD_NRF9160_PCA10015NS)
	zephyr_library()
	zephyr_library_sources(board_nonsecure.c)
endif()
