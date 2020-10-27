/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <zephyr.h>
#include <stdio.h>
#include <drivers/sensor.h>
#include <event_manager.h>

#include "ext_sensors.h"

#define MODULE sensor_manager

#include "events/app_mgr_event.h"
#include "events/data_mgr_event.h"
#include "events/sensor_mgr_event.h"
#include "events/util_mgr_event.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(sensor_manager, CONFIG_CAT_TRACKER_LOG_LEVEL);

extern atomic_t manager_count;

struct sensor_msg_data {
	union {
		struct app_mgr_event app;
		struct data_mgr_event data;
		struct util_mgr_event util;
	} manager;
};

/* Sensor manager super states. */
enum sensor_manager_state {
	SENSOR_MGR_STATE_INIT,
	SENSOR_MGR_STATE_RUNNING
} sensor_state;

K_MSGQ_DEFINE(msgq_sensor, sizeof(struct sensor_msg_data), 10, 4);

static void notify_sensor_manager(struct sensor_msg_data *data)
{
	while (k_msgq_put(&msgq_sensor, data, K_NO_WAIT) != 0) {
		/* message queue is full: purge old data & try again */
		k_msgq_purge(&msgq_sensor);
		LOG_ERR("Sensor manager message queue full, queue purged");
	}
}

static void signal_error(int err)
{
	struct sensor_mgr_event *sensor_mgr_event = new_sensor_mgr_event();

	sensor_mgr_event->type = SENSOR_MGR_EVT_ERROR;
	sensor_mgr_event->data.err = err;

	EVENT_SUBMIT(sensor_mgr_event);
}

#if defined(CONFIG_EXTERNAL_SENSORS)
static void sensor_manager_accelerometer_data_send(
			const struct ext_sensor_evt *const acc_data)
{
	static int buf_entry_try_again_timeout;

	/* Only populate accelerometer buffer if a configurable amount of time
	 * has passed since the last accelerometer buffer entry was filled.
	 */
	if (k_uptime_get() - buf_entry_try_again_timeout >
	    1000 * CONFIG_TIME_BETWEEN_ACCELEROMETER_BUFFER_STORE_SEC) {

		struct sensor_mgr_event *sensor_mgr_event =
				new_sensor_mgr_event();

		sensor_mgr_event->data.accel.values[0] =
				acc_data->value_array[0];
		sensor_mgr_event->data.accel.values[1] =
				acc_data->value_array[1];
		sensor_mgr_event->data.accel.values[2] =
				acc_data->value_array[2];
		sensor_mgr_event->data.accel.ts = k_uptime_get();
		sensor_mgr_event->data.accel.queued = true;
		sensor_mgr_event->type = SENSOR_MGR_EVT_MOVEMENT_DATA_READY;

		EVENT_SUBMIT(sensor_mgr_event);

		buf_entry_try_again_timeout = k_uptime_get();
	}
}

static void ext_sensor_handler(const struct ext_sensor_evt *const evt)
{
	switch (evt->type) {
	case EXT_SENSOR_EVT_ACCELEROMETER_TRIGGER:
		sensor_manager_accelerometer_data_send(evt);
		break;
	default:
		break;
	}
}
#endif

static int sensor_manager_data_get(void)
{
	struct sensor_mgr_event *sensor_mgr_event = new_sensor_mgr_event();
#if defined(CONFIG_EXTERNAL_SENSORS)
	int err;

	sensor_mgr_event->data.sensors.env_ts = k_uptime_get();
	sensor_mgr_event->data.sensors.queued = true;
	sensor_mgr_event->type = SENSOR_MGR_EVT_ENVIRONMENTAL_DATA_READY;

	/* Request data from external sensors. */
	err = ext_sensors_temperature_get(&sensor_mgr_event->data.sensors.temp);
	if (err) {
		LOG_ERR("temperature_get, error: %d", err);
		return err;
	}

	err = ext_sensors_humidity_get(&sensor_mgr_event->data.sensors.hum);
	if (err) {
		LOG_ERR("temperature_get, error: %d", err);
		return err;
	}
#else

	/* This event must be sent even though environmental sensors are not
	 * available on the nRF9160DK. This is because the data manager expects
	 * responses from the different managers within a certain amounf of time
	 * after the APP_EVT_DATA_GET event has been emitted.
	 */
	LOG_DBG("No external sensors, submitting dummy sensor data");

	/* Set this entry to false signifying that the event carries no data.
	 * This makes sure that the entry is not stored in the circular buffer.
	 */
	sensor_mgr_event->data.sensors.queued = false;
	sensor_mgr_event->type = SENSOR_MGR_EVT_ENVIRONMENTAL_DATA_READY;
#endif
	EVENT_SUBMIT(sensor_mgr_event);

	return 0;
}

static int sensor_manager_setup(void)
{
#if defined(CONFIG_EXTERNAL_SENSORS)
	int err;

	err = ext_sensors_init(ext_sensor_handler);
	if (err) {
		LOG_ERR("ext_sensors_init, error: %d", err);
		return err;
	}
#endif
	return 0;
}

static bool event_handler(const struct event_header *eh)
{
	if (is_app_mgr_event(eh)) {
		struct app_mgr_event *event = cast_app_mgr_event(eh);
		struct sensor_msg_data sensor_msg = {
			.manager.app = *event
		};

		notify_sensor_manager(&sensor_msg);
	}

	if (is_data_mgr_event(eh)) {
		struct data_mgr_event *event = cast_data_mgr_event(eh);
		struct sensor_msg_data sensor_msg = {
			.manager.data = *event
		};

		notify_sensor_manager(&sensor_msg);
	}

	if (is_util_mgr_event(eh)) {
		struct util_mgr_event *event = cast_util_mgr_event(eh);
		struct sensor_msg_data sensor_msg = {
			.manager.util = *event
		};

		notify_sensor_manager(&sensor_msg);
	}

	return false;
}

static bool sensor_data_requested(struct app_mgr_event_data *data_list,
				 size_t count)
{
	for (int i = 0; i < count; i++) {
		if (strcmp(data_list[i].buf, APP_DATA_ENVIRONMENTALS) == 0) {
			return true;
		}
	}

	return false;
}

static void on_state_init(struct sensor_msg_data *sensor_msg)
{
	if (is_data_mgr_event(&sensor_msg->manager.data.header) &&
	    sensor_msg->manager.data.type == DATA_MGR_EVT_CONFIG_INIT) {

		int movement_threshold = sensor_msg->manager.data.data.cfg.acct;

		ext_sensors_mov_thres_set(movement_threshold);
		sensor_state = SENSOR_MGR_STATE_RUNNING;
	}
}

static void on_state_running(struct sensor_msg_data *sensor_msg)
{
	if (is_data_mgr_event(&sensor_msg->manager.data.header) &&
	    sensor_msg->manager.data.type == DATA_MGR_EVT_CONFIG_READY) {

		int movement_threshold = sensor_msg->manager.data.data.cfg.acct;

		ext_sensors_mov_thres_set(movement_threshold);
	}

	if (is_app_mgr_event(&sensor_msg->manager.app.header) &&
	    sensor_msg->manager.app.type == APP_MGR_EVT_DATA_GET) {
		if (!sensor_data_requested(
			sensor_msg->manager.app.data_list,
			sensor_msg->manager.app.count)) {
			return;
		}

		int err;

		err = sensor_manager_data_get();
		if (err) {
			LOG_ERR("sensor_manager_data_get, error: %d", err);
			signal_error(err);
		}
	}
}

static void state_agnostic_manager_events(struct sensor_msg_data *sensor_msg)
{
	if (is_util_mgr_event(&sensor_msg->manager.util.header) &&
	    sensor_msg->manager.util.type == UTIL_MGR_EVT_SHUTDOWN_REQUEST) {

		struct sensor_mgr_event *sensor_evt = new_sensor_mgr_event();

		sensor_evt->type = SENSOR_MGR_EVT_SHUTDOWN_READY;
		EVENT_SUBMIT(sensor_evt);
	}
}

static void sensor_manager(void)
{
	int err;

	struct sensor_msg_data sensor_msg;

	atomic_inc(&manager_count);

	err = sensor_manager_setup();
	if (err) {
		LOG_ERR("sensor_manager_setup, error: %d", err);
		signal_error(err);
	}

	while (true) {
		k_msgq_get(&msgq_sensor, &sensor_msg, K_FOREVER);

		switch (sensor_state) {
		case SENSOR_MGR_STATE_INIT:
			on_state_init(&sensor_msg);
			break;
		case SENSOR_MGR_STATE_RUNNING:
			on_state_running(&sensor_msg);
			break;
		default:
			LOG_WRN("Unknown sensor manager state.");
			break;
		}
		state_agnostic_manager_events(&sensor_msg);
	}
}

K_THREAD_DEFINE(sensor_manager_thread, CONFIG_SENSOR_MGR_THREAD_STACK_SIZE,
		sensor_manager, NULL, NULL, NULL,
		K_HIGHEST_APPLICATION_THREAD_PRIO, 0, -1);

EVENT_LISTENER(MODULE, event_handler);
EVENT_SUBSCRIBE(MODULE, app_mgr_event);
EVENT_SUBSCRIBE(MODULE, data_mgr_event);
EVENT_SUBSCRIBE(MODULE, util_mgr_event);
