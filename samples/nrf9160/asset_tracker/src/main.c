/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <dk_buttons_and_leds.h>
#include <zephyr.h>
#include <stdio.h>
#include <uart.h>
#include <string.h>
#include <logging/log.h>
#include <misc/reboot.h>
#include <mqtt_func.h>
#include <gps_func.h>

static char gps_dummy_string[] = "$GPGGA,181908.00,3404.7041778,N,07044.3966270,W,4,13,1.00,495.144,M,29.200,M,0.10,0000*40";
static size_t gps_data_len = sizeof(gps_dummy_string);
static u8_t *ptr_gps_head_stream = gps_dummy_string;

#if defined(CONFIG_LTE_LINK_CONTROL)
static void modem_configure(void)
{
	if (IS_ENABLED(CONFIG_LTE_AUTO_INIT_AND_CONNECT)) {
		/* Do nothing, modem is already turned on
			* and connected.
			*/
	} else {
		int err;

		printk("LTE Link Connecting ...\n");
		err = lte_lc_init_and_connect();
		__ASSERT(err == 0, "LTE link could not be established.");
		printk("LTE Link Connected!\n");
	}

	//remember to set spm and the desired settings in config file

}
#endif

#if defined(CONFIG_DK_LIBRARY)
static void leds_init(void)
{
	int err;

	err = dk_leds_init();
	if (err) {
		printk("Could not initialize leds, err code: %d\n", err);
	}

	err = dk_set_leds_state(0x00, DK_ALL_LEDS_MSK);
	if (err) {
		printk("Could not set leds state, err code: %d\n", err);
	}
}

static void led_notification(void)
{
	dk_set_led_on(DK_BTN1);
	k_sleep(100);
	dk_set_led_off(DK_BTN1);
}
#endif



void main(void)
{
	printk("The phoenix tracker has started\n");
	leds_init();

	//int err;
	// err = gps_init();
	// if (!err) {
	// 	printk("gps could not be initialized"); //	if (init_app() != 0) { return -1; } //should prolly reboot in case
	// }

	modem_configure();

	while (1) {
		publish_gps_data(ptr_gps_head_stream, gps_data_len);
		led_notification();
		k_sleep(10000);
	}
}