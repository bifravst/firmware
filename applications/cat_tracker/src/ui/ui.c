/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include "ui.h"
#include "led_pwm.h"

static enum ui_led_pattern current_led_state;

void ui_led_set_pattern(enum ui_led_pattern state)
{
#if defined (CONFIG_LED_USAGE)
	current_led_state = state;
	ui_led_set_effect(state);
#endif
}

enum ui_led_pattern ui_led_get_pattern(void)
{
	return current_led_state;
}

int ui_led_set_color(u8_t red, u8_t green, u8_t blue)
{
	return ui_led_set_rgb(red, green, blue);
}

int ui_init()
{

	int err = 0;

	err = ui_leds_init();
	if (err) {
		printk("Error when initializing PWM controlled LEDs\n");
		return err;
	}

	return err;
}

void ui_stop_leds()
{
#if defined(CONFIG_LED_USAGE)
	ui_leds_stop();
#endif
}
