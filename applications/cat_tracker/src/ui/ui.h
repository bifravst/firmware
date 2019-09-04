/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

/**@file
 *
 * @brief   User interface module.
 *
 * Module that handles user interaction through button, RGB LED and buzzer.
 */

#ifndef UI_H__
#define UI_H__

#include <zephyr.h>

#include "led_effect.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_LED_1			1
#define UI_LED_2			2
#define UI_LED_3			3
#define UI_LED_4			4

#define UI_LED_ON(x)			(x)
#define UI_LED_BLINK(x)			((x) << 8)
#define UI_LED_GET_ON(x)		((x) & 0xFF)
#define UI_LED_GET_BLINK(x)		(((x) >> 8) & 0xFF)

#define UI_LED_ON_PERIOD_NORMAL		500
#define UI_LED_OFF_PERIOD_NORMAL	3500
#define UI_LED_ON_PERIOD_ERROR		500
#define UI_LED_OFF_PERIOD_ERROR		500
#define UI_LED_ON_PERIOD_SHORT		350
#define UI_LED_OFF_PERIOD_LONG		7000

#define UI_LED_MAX			50

#define UI_LED_COLOR_OFF		LED_COLOR(0, 0, 0)
#define UI_LED_COLOR_RED		LED_COLOR(UI_LED_MAX, 0, 0)
#define UI_LED_COLOR_GREEN		LED_COLOR(0, UI_LED_MAX, 0)
#define UI_LED_COLOR_BLUE		LED_COLOR(0, 0, UI_LED_MAX)
#define UI_LED_COLOR_WHITE		LED_COLOR(UI_LED_MAX, UI_LED_MAX,      \
						  UI_LED_MAX)
#define UI_LED_COLOR_YELLOW		LED_COLOR(UI_LED_MAX, UI_LED_MAX, 0)
#define UI_LED_COLOR_CYAN		LED_COLOR(0, UI_LED_MAX, UI_LED_MAX)
#define UI_LED_COLOR_PURPLE		LED_COLOR(UI_LED_MAX, 0, UI_LED_MAX)

#define UI_LTE_DISCONNECTED_COLOR	UI_LED_COLOR_OFF
#define UI_LTE_CONNECTING_COLOR		UI_LED_COLOR_YELLOW
#define UI_LTE_CONNECTED_COLOR		UI_LED_COLOR_CYAN
#define UI_CLOUD_CONNECTING_COLOR	UI_LED_COLOR_CYAN
#define UI_CLOUD_CONNECTED_COLOR	UI_LED_COLOR_BLUE
#define UI_CLOUD_PAIRING_COLOR		UI_LED_COLOR_YELLOW
#define UI_CLOUD_PUBLISHING_COLOR	UI_LED_COLOR_GREEN
#define UI_ACCEL_CALIBRATING_COLOR	UI_LED_COLOR_PURPLE
#define UI_LED_ERROR_CLOUD_COLOR	UI_LED_COLOR_RED
#define UI_LED_ERROR_BSD_REC_COLOR	UI_LED_COLOR_RED
#define UI_LED_ERROR_BSD_IRREC_COLOR	UI_LED_COLOR_RED
#define UI_LED_ERROR_LTE_LC_COLOR	UI_LED_COLOR_RED
#define UI_LED_ERROR_UNKNOWN_COLOR	UI_LED_COLOR_WHITE
#define UI_LED_GPS_SEARCHING_COLOR	UI_LED_COLOR_PURPLE
#define UI_LED_ACTIVE_MODE_COLOR	UI_LED_COLOR_CYAN
#define UI_LED_PASSIVE_MODE_COLOR	UI_LED_COLOR_BLUE

/**@brief UI LED state pattern definitions. */
enum ui_led_pattern {
	UI_LTE_DISCONNECTED,
	UI_LTE_CONNECTING,
	UI_LTE_CONNECTED,
	UI_CLOUD_CONNECTING,
	UI_CLOUD_CONNECTED,
	UI_CLOUD_PAIRING,
	UI_CLOUD_PUBLISHING,
	UI_ACCEL_CALIBRATING,
	UI_LED_ERROR_CLOUD,
	UI_LED_ERROR_BSD_REC,
	UI_LED_ERROR_BSD_IRREC,
	UI_LED_ERROR_LTE_LC,
	UI_LED_ERROR_UNKNOWN,
	UI_LED_GPS_SEARCHING,
	UI_LED_ACTIVE_MODE,
	UI_LED_PASSIVE_MODE,
};

/**
 * @brief Initializes the user interface module.
 *
 * @param cb UI callback handler. Can be NULL to disable callbacks.
 *
 * @return 0 on success or negative error value on failure.
 */
int ui_init();

/**
 * @brief Sets the LED pattern.
 *
 * @param pattern LED pattern.
 */
void ui_led_set_pattern(enum ui_led_pattern pattern);

/**
 * @brief Gets the LED pattern.
 *
 * @return Current LED pattern.
 */
enum ui_led_pattern ui_led_get_pattern(void);

/**
 * @brief Sets the LED RGB color.
 *
 * @param red Red, in range 0 - 255.
 * @param green Green, in range 0 - 255.
 * @param blue Blue, in range 0 - 255.
 *
 * @return 0 on success or negative error value on failure.
 */
int ui_led_set_color(u8_t red, u8_t green, u8_t blue);

/**
 * @brief Stops leds
 */
void ui_stop_leds();

#ifdef __cplusplus
}
#endif

#endif /* UI_H__ */
