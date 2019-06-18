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

#define PUBLISH_INTERVAL 30000

static char gps_dummy_string[] = "$GPGGA,181908.00,3404.7041778,N,07044.3966270,W,4,13,1.00,495.144,M,29.200,M,0.10,0000*40";
static size_t gps_data_len = sizeof(gps_dummy_string);
static u8_t *ptr_gps_head_stream = gps_dummy_string;

// static struct mqtt_data_type {
// 	nrf_gnss_data_frame_t	gps_fix;
// 	int						battery_percentage;
// };

// static struct mqtt_data_type mqtt_gps_bat_data;

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
	int err;

	printk("The phoenix tracker has started\n");
	leds_init();
	modem_configure();
	
	err = gps_init();
	if (err != 0) {
		printk("gps could not be initialized\n");
	}

	while (1) {

		//get_gps_data();

		//request_battery_status();

		publish_gps_data(ptr_gps_head_stream, gps_data_len);

		led_notification();
		k_sleep(PUBLISH_INTERVAL);
	}
}