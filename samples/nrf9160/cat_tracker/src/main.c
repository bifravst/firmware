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

#define SYNC true
#define NORM false

static struct gps_data gps_data;

static struct sensor_value accel[3];

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

// static struct k_work request_battery_status_work;
static struct k_work publish_data_work;
static struct k_work sync_broker_work;
static struct k_work gps_start_work;
static struct k_work gps_stop_work;
static struct k_work gps_found_work;
static struct k_work gps_not_found_work;

// static void request_battery_status_work_fn(struct k_work *work)
// {
// 	attach_battery_data(request_battery_status());
// }

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
	gps_control_start(0);
}

static void gps_stop_work_fn(struct k_work *work)
{
	gps_control_stop(0);
}

static void publish_data_work_fn(struct k_work *work)
{
	int err;

	attach_battery_data(request_battery_status());

	err = publish_data(NORM);
	if (err != 0) {
		printk("Error publishing data: %d", err);
	}

	if (check_config_change()) {
		err = publish_data(NORM);
		if (err != 0) {
			printk("Sync Error: %d", err);
		}
	}
}

static void sync_broker_work_fn(struct k_work *work)
{
	int err;
	err = publish_data(SYNC);
	if (err != 0) {
		printk("Sync Error: %d", err);
	}

	if (check_config_change()) {
		err = publish_data(NORM);
		if (err != 0) {
			printk("Sync Error: %d", err);
		}
	}
}

static void work_init()
{
	k_work_init(&publish_data_work, publish_data_work_fn);
	k_work_init(&sync_broker_work, sync_broker_work_fn);
	k_work_init(&gps_start_work, gps_start_work_fn);
	k_work_init(&gps_stop_work, gps_stop_work_fn);
	k_work_init(&gps_found_work, gps_found_work_fn);
	k_work_init(&gps_not_found_work, gps_not_found_work_fn);
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
static void adxl362_trigger_handler(struct device *dev,
				    struct sensor_trigger *trig)
{
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
		gps_stop(0);
		gps_sample_fetch(dev);
		gps_channel_get(dev, GPS_CHAN_PVT, &gps_data);
		attach_gps_data(gps_data);
		k_sem_give(events[0].sem);
		break;

	default:
		break;
	}
}

void my_work_handler(struct k_work *work)
{
	k_sem_give(events[1].sem);
}

K_WORK_DEFINE(my_work, my_work_handler);

void my_timer_handler(struct k_timer *dummy)
{
	k_work_submit(&my_work);
}

K_TIMER_DEFINE(my_timer, my_timer_handler, NULL);

void main(void)
{
	printk("The cat tracker has started\n");
	work_init();
	provision_certificates();
	lte_connect();
	adxl362_init();
	// set_client_id_imei(request_init_modem_data());
	gps_control_init(gps_control_handler);
	k_work_submit(&sync_broker_work);

	// k_timer_start(&my_timer, K_SECONDS(check_mov_timeout()),
	// 	      K_SECONDS(check_mov_timeout()));

	while (1) {
		if (check_mode()) {
			printk("ACTIVE MODE\n");
			k_work_submit(&gps_start_work);
			k_poll(events, 1, K_SECONDS(check_gps_timeout()));
			if (events[0].state == K_POLL_STATE_SEM_AVAILABLE) {
				k_sem_take(events[0].sem, 0);
				k_work_submit(&gps_found_work);
				k_work_submit(&publish_data_work);
			} else {
				printk("GPS data could not be found\n");
				k_work_submit(&gps_stop_work);
				k_work_submit(&gps_not_found_work);
				k_work_submit(&publish_data_work);
			}
			events[0].state = K_POLL_STATE_NOT_READY;
			k_sleep(K_SECONDS(check_active_wait(true)));
		} else {
			printk("PASSIVE MODE\n");
			k_poll(events, 2, K_FOREVER);
			if (events[1].state == K_POLL_STATE_SEM_AVAILABLE) {
				k_sem_take(events[1].sem, 0);
				k_work_submit(&gps_start_work);
				k_poll(events, 1,
				       K_SECONDS(check_gps_timeout()));
				if (events[0].state ==
				    K_POLL_STATE_SEM_AVAILABLE) {
					k_sem_take(events[0].sem, 0);
					k_work_submit(&gps_found_work);
					k_work_submit(&publish_data_work);
				} else {
					printk("GPS data could not be found\n");
					k_work_submit(&gps_stop_work);
					k_work_submit(&gps_not_found_work);
					k_work_submit(&publish_data_work);
				}
				events[0].state = K_POLL_STATE_NOT_READY;
				k_sleep(K_SECONDS(check_active_wait(false)));
			}
			events[1].state = K_POLL_STATE_NOT_READY;
		}
	}
}
