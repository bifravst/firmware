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

#define PUBLISH_INTERVAL	60	//1min
#define TRACKER_ID			"CT3001"
#define GPS_SEARCH_TIMEOUT	180 //3min
#define SLEEP_ACCEL_THRES	300 //5min

static char mqtt_assembly_line_d[100] = "";

static struct gps_data gps_data;

K_SEM_DEFINE(gps_timing_sem, 0, 1);
K_SEM_DEFINE(idle_user_sem, 1, 1);

struct k_poll_event events[2] = {
	K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
					K_POLL_MODE_NOTIFY_ONLY, &gps_timing_sem, 0),
	K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
					K_POLL_MODE_NOTIFY_ONLY, &idle_user_sem, 0)
};

static struct k_work request_battery_status_work;
static struct k_work publish_gps_data_work;
static struct k_work delete_assembly_data_work;

static void request_battery_status_work_fn(struct k_work *work)
{
	request_battery_status(mqtt_assembly_line_d);
}

static void publish_gps_data_work_fn(struct k_work *work)
{
	publish_gps_data(mqtt_assembly_line_d, sizeof(mqtt_assembly_line_d));
}

static void delete_assembly_data_work_fn(struct k_work *work)
{
	delete_publish_data(mqtt_assembly_line_d);
	concat_structure(mqtt_assembly_line_d, TRACKER_ID);
}

static void work_init() {
	k_work_init(&request_battery_status_work, request_battery_status_work_fn);
	k_work_init(&publish_gps_data_work, publish_gps_data_work_fn);
	k_work_init(&delete_assembly_data_work, delete_assembly_data_work_fn);
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

#if defined(CONFIG_ADXL362)
static void adxl362_trigger_handler(struct device *dev, struct sensor_trigger *trig)
{
	switch (trig->type) {
	case SENSOR_TRIG_THRESHOLD:
		printk("The cat has moved, grant publish access \n");

		k_sem_give(events[1].sem);

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
#endif

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
	#if defined(CONFIG_ADXL362)
	adxl362_init();
	#endif
	concat_structure(mqtt_assembly_line_d, TRACKER_ID);
	gps_control_init(gps_control_handler);

	while (1) {
		#if defined(CONFIG_ADXL362)
		k_poll(events, 2, K_FOREVER);
		if (events[1].state == K_POLL_STATE_SEM_AVAILABLE) {
			k_sem_take(events[1].sem, 0);
		#endif
			k_work_submit(&request_battery_status_work);
			gps_control_start(0);
			k_poll(events, 1, K_SECONDS(GPS_SEARCH_TIMEOUT));
			if (events[0].state == K_POLL_STATE_SEM_AVAILABLE) {
				k_sem_take(events[0].sem, 0);
				k_work_submit(&publish_gps_data_work);
				k_work_submit(&delete_assembly_data_work);
			} else {
				gps_control_stop(0);
				k_work_submit(&delete_assembly_data_work);
				printk("GPS data could not be found within %d seconds, deleting assembly string\n", GPS_SEARCH_TIMEOUT);
			}
			events[0].state = K_POLL_STATE_NOT_READY;
			k_sleep(K_SECONDS(PUBLISH_INTERVAL));
			printk("finished a publish cycle\n");
		#if defined(CONFIG_ADXL362)
		}
		
		events[1].state = K_POLL_STATE_NOT_READY;
		#endif
	}	
}