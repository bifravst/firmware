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
#include <mqtt_behaviour.h>
#include <modem_stats.h>
#include <accelerometer.h>
#include <device.h>
#include <sensor.h>
#include <gps_controller.h>
#include <string_manipulation.h>

#define PUBLISH_INTERVAL	60
#define TRACKER_ID			"CT3001"
#define GPS_SEARCH_TIMEOUT	360
#define SLEEP_ACCEL_THRES	30

static bool grant = true;

static char mqtt_assembly_line_d[100] = "";

static struct gps_data gps_data;

K_SEM_DEFINE(my_sem, 0, 1);
K_SEM_DEFINE(my_sem2, 0, 1);

struct k_poll_event events[2] = {
	K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
					K_POLL_MODE_NOTIFY_ONLY, &my_sem, 0),
	K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
					K_POLL_MODE_NOTIFY_ONLY, &my_sem2, 0)
};

static struct k_work request_battery_status_work;
static struct k_work publish_gps_data_work;
static struct k_work delete_publish_data_work;

static void request_battery_status_work_fn(struct k_work *work)
{
	request_battery_status(mqtt_assembly_line_d);
}

static void publish_gps_data_work_fn(struct k_work *work)
{
	publish_gps_data(mqtt_assembly_line_d, sizeof(mqtt_assembly_line_d));
}

static void delete_publish_data_work_fn(struct k_work *work)
{
	delete_publish_data(mqtt_assembly_line_d);
	concat_structure(mqtt_assembly_line_d, TRACKER_ID);
}

static void work_init() {
	k_work_init(&request_battery_status_work, request_battery_status_work_fn);
	k_work_init(&publish_gps_data_work, publish_gps_data_work_fn);
	k_work_init(&delete_publish_data_work, delete_publish_data_work_fn);
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
	lte_lc_psm_req(true);
}

/* experimental */
static void my_work_handler(struct k_work *work) {
	grant = false;
	printk("The cat has been idle for quite some time, go to sleep\n");
}

K_WORK_DEFINE(my_work, my_work_handler);

static void my_timer_handler(struct k_timer *dummy) {
	k_work_submit(&my_work);
}

K_TIMER_DEFINE(my_timer, my_timer_handler, NULL);
/* experimental */

static void adxl362_trigger_handler(struct device *dev, struct sensor_trigger *trig)
{
	switch (trig->type) {
	case SENSOR_TRIG_THRESHOLD:
		printk("The cat has moved, grant publish access \n");

		grant = true;

		k_timer_start(&my_timer, K_SECONDS(SLEEP_ACCEL_THRES),
			      K_SECONDS(SLEEP_ACCEL_THRES));

		break;
	default:
		printk("Unknown trigger\n");
	}
}

static void adxl362_init(void)
{
	struct device *dev = device_get_binding(DT_INST_0_ADI_ADXL362_LABEL);
	if (dev == NULL) {
		printk("Device get binding device\n");
		return;
	}

	if (IS_ENABLED(CONFIG_ADXL362_TRIGGER)) {
		struct sensor_trigger trig = { .chan = SENSOR_CHAN_ACCEL_XYZ };

		trig.type = SENSOR_TRIG_THRESHOLD;
		if (sensor_trigger_set(dev, &trig, adxl362_trigger_handler)) {
			printk("Trigger set error\n");
			return;
		}
	}
}

static void gps_control_handler(struct device *dev, struct gps_trigger *trigger) {

	switch(trigger->type) {
		case GPS_TRIG_FIX:
			printk("gps control handler triggered!\n");
			gps_control_on_trigger();
			gps_control_stop(0);
			gps_sample_fetch(dev);
			gps_channel_get(dev, GPS_CHAN_NMEA, &gps_data);
			concat_structure(mqtt_assembly_line_d, gps_data.nmea.buf);
			k_sem_give(events[0].sem);
		break;

		default:
		break;
	}
}

void main(void)
{

	printk("The cat tracker has started\n");
	work_init();
	lte_connect();
	adxl362_init();
	concat_structure(mqtt_assembly_line_d, TRACKER_ID); //bad name
	gps_control_init(gps_control_handler);

	k_timer_start(&my_timer, K_SECONDS(SLEEP_ACCEL_THRES),
		      K_SECONDS(SLEEP_ACCEL_THRES));

	while (1) {
		if (grant) {
			k_work_submit(&request_battery_status_work);
			gps_control_start(0);
			k_poll(events, 1, K_SECONDS(GPS_SEARCH_TIMEOUT));
			if (events[0].state == K_POLL_STATE_SEM_AVAILABLE) {
				k_sem_take(events[0].sem, 0);
				k_work_submit(&publish_gps_data_work);
				k_work_submit(&delete_publish_data_work);
			} else {
				gps_control_stop(0);
				k_work_submit(&delete_publish_data_work);
				printk("GPS data could not be found, deleting assembly string\n");
			}
			events[0].state = K_POLL_STATE_NOT_READY;
			k_sleep(K_SECONDS(PUBLISH_INTERVAL));
		}
	}
}