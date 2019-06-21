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
#include <device.h>
#include <sensor.h>

#define PUBLISH_INTERVAL	5000
#define PAYLOAD_LENGTH		30
#define NUMBER_OF_PACKAGES	1

static char gps_dummy_string[PAYLOAD_LENGTH] = "\0";

static void led_notification(void)
{
	dk_set_led_on(DK_BTN1);
	k_sleep(100);
	dk_set_led_off(DK_BTN1);
}

static void insert_gps_data(char *gps_dummy_string) {
	strcat(gps_dummy_string, ",63.42173,10.43415");
}

static struct k_work request_battery_status_work;
static struct k_work insert_gps_data_work;
static struct k_work get_gps_data_work;
static struct k_work publish_gps_data_work;
static struct k_work delete_publish_string_and_set_led_work;

static void request_battery_status_work_fn(struct k_work *work)
{
	request_battery_status(gps_dummy_string);
}

static void insert_gps_data_work_fn(struct k_work *work)
{
	insert_gps_data(gps_dummy_string);
}

static void get_gps_data_work_fn(struct k_work *work)
{
	get_gps_data(gps_dummy_string);
}

static void publish_gps_data_work_fn(struct k_work *work)
{
	publish_gps_data(gps_dummy_string, sizeof(gps_dummy_string));
}

static void delete_publish_string_and_set_led_work_fn(struct k_work *work)
{
	memset(gps_dummy_string,0,strlen(gps_dummy_string)); //reset string sequence
	led_notification();
}

static void work_init() {
	k_work_init(&request_battery_status_work, request_battery_status_work_fn);
	k_work_init(&insert_gps_data_work, insert_gps_data_work_fn);
	k_work_init(&get_gps_data_work, get_gps_data_work_fn);
	k_work_init(&publish_gps_data_work, publish_gps_data_work_fn);
	k_work_init(&delete_publish_string_and_set_led_work, delete_publish_string_and_set_led_work_fn);
}

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

static void trigger_handler(struct device *dev, struct sensor_trigger *trig)
{
	switch (trig->type) {
	case SENSOR_TRIG_THRESHOLD:
		printk("The cat has awoken, send cat data to the broker\n");
		
		k_work_submit(&request_battery_status_work);
		k_work_submit(&insert_gps_data_work);
		// k_work_submit(&get_gps_data_work);
		k_work_submit(&publish_gps_data_work);
		k_work_submit(&delete_publish_string_and_set_led_work);

		break;
	default:
		printk("Unknown trigger\n");
	}	
}

void adxl362_init(void)
{
	struct device *dev = device_get_binding(DT_ADI_ADXL362_0_LABEL);
	if (dev == NULL) {
		printk("Device get binding device\n");
		return;
	}

	if (IS_ENABLED(CONFIG_ADXL362_TRIGGER)) {
		struct sensor_trigger trig = { .chan = SENSOR_CHAN_ACCEL_XYZ };

		trig.type = SENSOR_TRIG_THRESHOLD;
		if (sensor_trigger_set(dev, &trig, trigger_handler)) {
			printk("Trigger set error\n");
			return;
		}
	}
}

static void lte_connect(void)
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

void main(void)
{
	//int err;
	printk("The cat tracker has started\n");
	work_init();
	leds_init();
	lte_connect();
	adxl362_init();

	// err = gps_init();
	// if (err != 0) {
	// 	printk("The GPS initialized successfully\n");
	// }

}