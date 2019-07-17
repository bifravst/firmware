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
#include <device.h>
#include <sensor.h>
#include <gps_controller.h>

static struct gps_data gps_data;

K_SEM_DEFINE(gps_timing_sem, 0, 1);
K_SEM_DEFINE(idle_user_sem, 1, 1);

struct k_poll_event events[2] = {
	K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
					K_POLL_MODE_NOTIFY_ONLY,
					&gps_timing_sem, 0),
	K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
					K_POLL_MODE_NOTIFY_ONLY, &idle_user_sem,
					0)
};

static struct k_work request_battery_status_work;
static struct k_work publish_gps_data_work;
static struct k_work sync_broker_work;

static void request_battery_status_work_fn(struct k_work *work)
{
	int battery_percentage;

	battery_percentage = request_battery_status();
	insert_battery_data(battery_percentage);
}

static void publish_gps_data_work_fn(struct k_work *work)
{
	int err;
	err = publish_gps_data();
	if (err != 0) {
		printk("Error publishing data: %d", err);
	}
}

static void sync_broker_work_fn(struct k_work *work)
{
	int err;
	err = sync_broker();
	if (err != 0) {
		printk("Sync Error: %d", err);
	}
}

static void work_init()
{
	k_work_init(&request_battery_status_work,
		    request_battery_status_work_fn);
	k_work_init(&publish_gps_data_work, publish_gps_data_work_fn);
	k_work_init(&sync_broker_work, sync_broker_work_fn);
}

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

static void led_notification_publish_data(void)
{
	dk_set_led_on(DK_BTN1);
	k_sleep(1000);
	dk_set_led_off(DK_BTN1);
}

static void led_notification_lte_connected(void)
{
	dk_set_led_on(DK_BTN2);
}
#endif

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
	led_notification_lte_connected();
}

#if defined(CONFIG_ADXL362)
static void adxl362_trigger_handler(struct device *dev,
				    struct sensor_trigger *trig)
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
#endif

static void adxl362_init(void)
{
#if defined(CONFIG_ADXL362)
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
#endif
}

static void gps_control_handler(struct device *dev, struct gps_trigger *trigger)
{
	switch (trigger->type) {
	case GPS_TRIG_FIX:
		printk("gps control handler triggered!\n");
		gps_control_on_trigger();
		gps_control_stop(0);
		gps_sample_fetch(dev);
		gps_channel_get(dev, GPS_CHAN_PVT, &gps_data);
		insert_gps_data(gps_data.pvt.longitude, gps_data.pvt.latitude,
				gps_data.pvt.datetime);
		k_sem_give(events[0].sem);
		break;

	default:
		break;
	}
}

void main(void)
{
	printk("The cat tracker has started\n");
	leds_init();
	work_init();
	provision_certificates();
	lte_connect();
	adxl362_init();
	gps_control_init(gps_control_handler);

	//k_work_submit(&sync_broker_work); //this does not work properly at the moment

	while (1) {
		if (check_mode()) {
			printk("We are in active mode\n");
			k_work_submit(&request_battery_status_work);
			gps_control_start(0);
			k_poll(events, 1, K_SECONDS(check_gps_timeout()));
			if (events[0].state == K_POLL_STATE_SEM_AVAILABLE) {
				k_sem_take(events[0].sem, 0);
				k_work_submit(&publish_gps_data_work);
				led_notification_publish_data();
			} else {
				gps_control_stop(0);
				printk("GPS data could not be found within %d seconds\n",
				       GPS_SEARCH_TIMEOUT);
			}
			events[0].state = K_POLL_STATE_NOT_READY;
			k_sleep(K_SECONDS(check_publish_interval()));
		} else {
			printk("We are in passive mode\n");
			k_poll(events, 2, K_FOREVER);
			if (events[1].state == K_POLL_STATE_SEM_AVAILABLE) {
				k_sem_take(events[1].sem, 0);
				k_work_submit(&request_battery_status_work);
				gps_control_start(0);
				k_poll(events, 1,
				       K_SECONDS(check_gps_timeout()));
				if (events[0].state ==
				    K_POLL_STATE_SEM_AVAILABLE) {
					k_sem_take(events[0].sem, 0);
					k_work_submit(&publish_gps_data_work);
					led_notification_publish_data();
				} else {
					gps_control_stop(0);
					printk("GPS data could not be found within %d seconds\n",
					       GPS_SEARCH_TIMEOUT);
				}
				events[0].state = K_POLL_STATE_NOT_READY;
				k_sleep(K_SECONDS(check_publish_interval()));
			}
			events[1].state = K_POLL_STATE_NOT_READY;
		}
	}
}
