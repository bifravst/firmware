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
#include <batstat.h>

#define PUBLISH_INTERVAL	10000
#define PAYLOAD_LENGTH		20

static char gps_dummy_string[PAYLOAD_LENGTH] = "\0";

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

	//lte_lc_psm_req(true);

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

static void insert_gps_data(char *gps_dummy_string) {
	strcat(gps_dummy_string, ",63.42173,10.43415");
}

void main(void)
{
	//int err;
	printk("The phoenix tracker has started\n");
	leds_init();

	// err = gps_init();
	// if (err != 0) {
	// 	printk("The GPS initialized successfully\n");
	// }

	modem_configure();

	while (1) {
		
		request_battery_status(gps_dummy_string);
		insert_gps_data(gps_dummy_string);

		// err = get_gps_data(gps_dummy_string);
		// if (err) {
		// 	printk("failed getting gps data\n");
		// }

		publish_gps_data(gps_dummy_string, sizeof(gps_dummy_string));

		//printk("printed gps string: %s\n", gps_dummy_string);

		memset(gps_dummy_string,0,strlen(gps_dummy_string)); //reset string sequence

		led_notification();
		k_sleep(PUBLISH_INTERVAL);
	}
}