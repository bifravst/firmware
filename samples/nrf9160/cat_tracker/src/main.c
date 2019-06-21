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
#define PAYLOAD_LENGTH		20
#define NUMBER_OF_PACKAGES	1

//K_SEM_DEFINE(sem, 0, 1);
// K_SEM_DEFINE(sem2, 0, 1);

static char gps_dummy_string[PAYLOAD_LENGTH] = "\0";

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

static void send_data_to_broker() {		
	//request_battery_status(gps_dummy_string);
	insert_gps_data(gps_dummy_string);

	// err = get_gps_data(gps_dummy_string);
	// if (err) {
	// 	printk("failed getting gps data\n");
	// }

	publish_gps_data(gps_dummy_string, sizeof(gps_dummy_string));

	memset(gps_dummy_string,0,strlen(gps_dummy_string)); //reset string sequence

	led_notification();
}

#if defined(CONFIG_ADXL362)
static void trigger_handler(struct device *dev, struct sensor_trigger *trig)
{
	switch (trig->type) {
	case SENSOR_TRIG_DATA_READY:
		if (sensor_sample_fetch(dev) < 0) {
			printf("Sample fetch error\n");
			return;
		}
		//k_sem_give(&sem);
		break;
	case SENSOR_TRIG_THRESHOLD:
		printk("The cat has awoken, send cat data to the broker\n");
		
		send_data_to_broker();

		break;
	default:
		printk("Unknown trigger\n");
	}	
}

void accel_enable(void)
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

		// trig.type = SENSOR_TRIG_DATA_READY;
		// if (sensor_trigger_set(dev, &trig, trigger_handler)) {
		// 	printk("Trigger set error\n");
		// }
	}
}
#endif

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

void main(void)
{
	//int err;
	printk("The cat tracker has started\n");
	leds_init();
	modem_configure();
	accel_enable(); //needs to be implemented correctly

	// err = gps_init();
	// if (err != 0) {
	// 	printk("The GPS initialized successfully\n");
	// }

	for (;;) {
		k_cpu_idle();
	}
}