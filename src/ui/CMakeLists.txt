#
# Copyright (c) 2019-2020 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#

zephyr_include_directories(.)
target_sources_ifdef(CONFIG_USE_UI_MODULE app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui.c)
target_sources_ifdef(CONFIG_UI_LED_USE_PWM app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/led_pwm.c)
