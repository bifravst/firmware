/*
 * Copyright (c) 2018 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <zephyr.h>
#include <stdio.h>
#include <uart.h>
#include <string.h>
#include <logging/log.h>
#include <misc/reboot.h>
#include <mqtt_behaviour.h>
#include <modem_data.h>
#include <device.h>
#include <sensor.h>
#include <gps_controller.h>
#include <mqtt_codec.h>
#include <leds.h>

#define NORMAL_OPERATION false
#define SYNCRONIZATION true
#define NO_GPS_FIX false
#define GPS_FIX true

static bool active;

K_SEM_DEFINE(gps_timeout_sem, 0, 1);
K_SEM_DEFINE(accel_trig_sem, 0, 1);

struct k_poll_event events[2] = {
	K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
					K_POLL_MODE_NOTIFY_ONLY,
					&gps_timeout_sem, 0),
	K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
					K_POLL_MODE_NOTIFY_ONLY,
					&accel_trig_sem, 0)
};

static void publish_cloud()
{
	int err;
	err = publish_data(NORMAL_OPERATION);
	if (err != 0) {
		printk("Error publishing data: %d", err);
	}
}

static void cloud_sync()
{
	int err;
	err = publish_data(SYNCRONIZATION);
	if (err != 0) {
		printk("Error syncronizing with brokerr: %d", err);
	}
}

static void gps_found_work_fn(struct k_work *work)
{
	set_gps_found(true);
}

static void gps_not_found_work_fn(struct k_work *work)
{
	set_gps_found(false);
}

static void gps_start_work_fn(struct k_work *work)
{
	gps_control_start();
}

static void gps_stop_work_fn(struct k_work *work)
{
	gps_control_stop();
}

static void get_modem_info_work_fn(struct k_work *work)
{
	attach_battery_data(request_battery_status());
}

static void gps_control_handler(struct device *dev, struct gps_trigger *trigger)
{
	static struct gps_data gps_data;

	switch (trigger->type) {
	case GPS_TRIG_FIX:
		printk("gps control handler triggered!\n");
		gps_control_on_trigger();
		gps_sample_fetch(dev);
		gps_channel_get(dev, GPS_CHAN_PVT, &gps_data);
		attach_gps_data(gps_data);
		k_sem_give(events[0].sem);
		break;

	default:
		break;
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
		lte_connecting_led_start();
		err = lte_lc_init_and_connect();
		__ASSERT(err == 0, "LTE link could not be established.");
		printk("LTE Link Connected!\n");
		lte_connecting_led_stop();
	}
	lte_lc_psm_req(true);
}

static void cloud_publish(bool gps_fix, bool action)
{
	int err;

	set_gps_found(gps_fix);

	err = publish_data(action);
	if (err != 0) {
		printk("Error publishing data: %d", err);
	}
}

#if defined(CONFIG_ADXL362)
static void adxl362_trigger_handler(struct device *dev,
				    struct sensor_trigger *trig)
{
	static struct sensor_value accel[3];

	switch (trig->type) {
	case SENSOR_TRIG_THRESHOLD:

		if (sensor_sample_fetch(dev) < 0) {
			printk("Sample fetch error\n");
			return;
		}

		sensor_channel_get(dev, SENSOR_CHAN_ACCEL_X, &accel[0]);
		sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Y, &accel[1]);
		sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Z, &accel[2]);

		if ((abs(sensor_value_to_double(&accel[0])) >
		     check_accel_thres()) ||
		    (abs(sensor_value_to_double(&accel[1])) >
		     check_accel_thres()) ||
		    (abs(sensor_value_to_double(&accel[2])) >
		     check_accel_thres())) {
			attach_accel_data(sensor_value_to_double(&accel[0]),
					  sensor_value_to_double(&accel[1]),
					  sensor_value_to_double(&accel[2]));

			printf("x: %.1f, y: %.1f, z: %.1f (m/s^2)\n",
			       sensor_value_to_double(&accel[0]),
			       sensor_value_to_double(&accel[1]),
			       sensor_value_to_double(&accel[2]));
			k_sem_give(events[1].sem);
		}

		break;
	default:
		printk("Unknown trigger\n");
	}
}
#endif

#if defined(CONFIG_ENABLE_NRF9160_GPS)
static void gps_control_handler(struct device *dev, struct gps_trigger *trigger)
{
	static struct gps_data gps_data;

	switch (trigger->type) {
	case GPS_TRIG_FIX:
		printk("gps control handler triggered!\n");
		gps_control_on_trigger();
		gps_sample_fetch(dev);
		gps_channel_get(dev, GPS_CHAN_PVT, &gps_data);
		attach_gps_data(gps_data);
		k_sem_give(events[0].sem);
		break;

	default:
		break;
	}
}
#endif

static void button_handler(u32_t button_states, u32_t has_changed)
{
	u8_t btn_num;

	while (has_changed) {
		btn_num = 0;

		for (u8_t i = 0; i < 32; i++) {
			if (has_changed & BIT(i)) {
				btn_num = i + 1;
				break;
			}
		}

		has_changed &= ~(1UL << (btn_num - 1));

		if (has_changed == 0 || has_changed == 1) {
			gps_control_stop(K_NO_WAIT);
			set_gps_found(false);
			k_sem_give(events[0].sem);
		}
	}
}

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

void main(void)
{
	printk("The cat tracker has started\n");
	adxl362_init();

	err = dk_buttons_init(button_handler);
	if (err != 0) {
		printk("dk buttons not initialized correctly: %d\n", err);
	}

	cloud_configuration_init();
	lte_connect();

#if defined(CONFIG_ENABLE_NRF9160_GPS)
	gps_control_init(gps_control_handler);
#endif

	k_sleep(5000);

	cloud_publish(NO_GPS_FIX, SYNCRONIZATION);

check_mode:
	attach_battery_data(request_battery_status());
	if (check_mode()) {
		active = true;
		goto active;
	} else {
		active = false;
		goto passive;
	}

active:
	printk("ACTIVE MODE\n");
	goto gps_search;

passive:
	printk("PASSIVE MODE\n");
#if defined(CONFIG_ADXL362)
	k_poll(events, 2, K_FOREVER);
	if (events[1].state == K_POLL_STATE_SEM_AVAILABLE) {
		k_sem_take(events[1].sem, 0);
	}
#endif
	goto gps_search;

gps_search:
#if defined(CONFIG_ENABLE_NRF9160_GPS)
	gps_control_start();
	k_poll(events, 1, K_SECONDS(check_gps_timeout()));
	if (events[0].state == K_POLL_STATE_SEM_AVAILABLE) {
		k_sem_take(events[0].sem, 0);
		set_gps_found(true);
		publish_cloud();
	} else {
		gps_control_stop(K_NO_WAIT);
		set_gps_found(false);
		publish_cloud();
	}
	events[1].state = K_POLL_STATE_NOT_READY;
	events[0].state = K_POLL_STATE_NOT_READY;
	k_sleep(K_SECONDS(check_active_wait(active)));
#else
	set_gps_found(false);
	publish_cloud();
	k_sleep(K_SECONDS(check_active_wait(active)));
#endif

	goto check_mode;
}
