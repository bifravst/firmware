/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <zephyr.h>
#include <drivers/gps.h>
#include <stdio.h>
#include <date_time.h>
#include <event_manager.h>
#include <drivers/gps.h>

#include "cloud_codec.h"

#define MODULE gps_manager

#include "events/app_mgr_event.h"
#include "events/gps_mgr_event.h"
#include "events/data_mgr_event.h"
#include "events/util_mgr_event.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_CAT_TRACKER_LOG_LEVEL);

extern atomic_t manager_count;

struct gps_msg_data {
	union {
		struct app_mgr_event app;
		struct data_mgr_event data;
		struct util_mgr_event util;
		struct gps_mgr_event gps;
	} manager;
};

/* GPS manager super states. */
enum gps_manager_state_type {
	GPS_MGR_STATE_INIT,
	GPS_MGR_STATE_RUNNING
} gps_state;

enum gps_manager_sub_state_type {
	GPS_MGR_SUB_STATE_IDLE,
	GPS_MGR_SUB_STATE_SEARCH
} gps_sub_state;

K_MSGQ_DEFINE(msgq_gps, sizeof(struct gps_msg_data), 10, 4);

static void notify_gps_manager(struct gps_msg_data *data)
{
	while (k_msgq_put(&msgq_gps, data, K_NO_WAIT) != 0) {
		/* message queue is full: purge old data & try again */
		k_msgq_purge(&msgq_gps);
		LOG_ERR("GPS manager message queue full, queue purged");
	}
}

/* Maximum GPS interval value. Dummy value, will not be used. Starting
 * and stopping of GPS is done by the application.
 */
#define GPS_INTERVAL_MAX 1800

/* GPS device. Used to identify the GPS driver in the sensor API. */
static const struct device *gps_dev;

/* nRF9160 GPS driver configuration. */
static struct gps_config gps_cfg = {
	.nav_mode = GPS_NAV_MODE_PERIODIC,
	.power_mode = GPS_POWER_MODE_DISABLED,
	.interval = GPS_INTERVAL_MAX
};

static void signal_error(int err)
{
	struct gps_mgr_event *gps_mgr_event = new_gps_mgr_event();

	gps_mgr_event->type = GPS_MGR_EVT_ERROR;
	gps_mgr_event->data.err = err;

	EVENT_SUBMIT(gps_mgr_event);
}

static void signal_gps_activity(bool active)
{
	struct gps_mgr_event *gps_mgr_event = new_gps_mgr_event();

	gps_mgr_event->type = active ? GPS_MGR_EVT_ACTIVE :
				       GPS_MGR_EVT_INACTIVE;

	EVENT_SUBMIT(gps_mgr_event);
}

static void gps_manager_data_send(struct gps_pvt *gps_data)
{
	struct gps_mgr_event *gps_mgr_event = new_gps_mgr_event();

	gps_mgr_event->data.gps.longi = gps_data->longitude;
	gps_mgr_event->data.gps.lat = gps_data->latitude;
	gps_mgr_event->data.gps.alt = gps_data->altitude;
	gps_mgr_event->data.gps.acc = gps_data->accuracy;
	gps_mgr_event->data.gps.spd = gps_data->speed;
	gps_mgr_event->data.gps.hdg = gps_data->heading;
	gps_mgr_event->data.gps.gps_ts = k_uptime_get();
	gps_mgr_event->data.gps.queued = true;
	gps_mgr_event->type = GPS_MGR_EVT_DATA_READY;

	EVENT_SUBMIT(gps_mgr_event);
}

static void gps_manager_gps_timeout(void)
{
	struct gps_mgr_event *gps_mgr_event = new_gps_mgr_event();

	gps_mgr_event->type = GPS_MGR_EVT_TIMEOUT;

	EVENT_SUBMIT(gps_mgr_event);
}

static void gps_manager_search_start(void)
{
	int err;

	err = gps_start(gps_dev, &gps_cfg);
	if (err) {
		LOG_WRN("Failed to start GPS, error: %d", err);
		return;
	}

	signal_gps_activity(true);
}

static void gps_manager_search_stop(void)
{
	int err;

	err = gps_stop(gps_dev);
	if (err) {
		LOG_WRN("Failed to stop GPS, error: %d", err);
		return;
	}

	signal_gps_activity(false);
}

static void gps_time_set(struct gps_pvt *gps_data)
{
	/* Change datetime.year and datetime.month to accommodate the
	 * correct input format.
	 */
	struct tm gps_time = {
		.tm_year = gps_data->datetime.year - 1900,
		.tm_mon = gps_data->datetime.month - 1,
		.tm_mday = gps_data->datetime.day,
		.tm_hour = gps_data->datetime.hour,
		.tm_min = gps_data->datetime.minute,
		.tm_sec = gps_data->datetime.seconds,
	};

	date_time_set(&gps_time);
}

static void gps_trigger_handler(const struct device *dev, struct gps_event *evt)
{
	switch (evt->type) {
	case GPS_EVT_SEARCH_STARTED:
		LOG_DBG("GPS_EVT_SEARCH_STARTED");
		break;
	case GPS_EVT_SEARCH_STOPPED:
		LOG_DBG("GPS_EVT_SEARCH_STOPPED");
		break;
	case GPS_EVT_SEARCH_TIMEOUT:
		LOG_DBG("GPS_EVT_SEARCH_TIMEOUT");
		gps_manager_gps_timeout();
		gps_manager_search_stop();
		break;
	case GPS_EVT_PVT:
		/* Don't spam logs */
		break;
	case GPS_EVT_PVT_FIX:
		LOG_DBG("GPS_EVT_PVT_FIX");
		gps_time_set(&evt->pvt);
		gps_manager_data_send(&evt->pvt);
		gps_manager_search_stop();
		break;
	case GPS_EVT_NMEA:
		/* Don't spam logs */
		break;
	case GPS_EVT_NMEA_FIX:
		LOG_DBG("Position fix with NMEA data");
		break;
	case GPS_EVT_OPERATION_BLOCKED:
		LOG_DBG("GPS_EVT_OPERATION_BLOCKED");
		break;
	case GPS_EVT_OPERATION_UNBLOCKED:
		LOG_DBG("GPS_EVT_OPERATION_UNBLOCKED");
		break;
	case GPS_EVT_AGPS_DATA_NEEDED:
		LOG_DBG("GPS_EVT_AGPS_DATA_NEEDED");
		struct gps_mgr_event *gps_mgr_event = new_gps_mgr_event();

		gps_mgr_event->data.agps_request = evt->agps_request;
		gps_mgr_event->type = GPS_MGR_EVT_AGPS_NEEDED;
		EVENT_SUBMIT(gps_mgr_event);
		break;
	case GPS_EVT_ERROR:
		LOG_DBG("GPS_EVT_ERROR\n");
		break;
	default:
		break;
	}
}

static int gps_manager_setup(void)
{
	int err;

	gps_dev = device_get_binding(CONFIG_GPS_DEV_NAME);
	if (gps_dev == NULL) {
		LOG_ERR("Could not get %s device",
			log_strdup(CONFIG_GPS_DEV_NAME));
		return -ENODEV;
	}

	err = gps_init(gps_dev, gps_trigger_handler);
	if (err) {
		LOG_ERR("Could not initialize GPS, error: %d", err);
		return err;
	}

	return 0;
}

static bool event_handler(const struct event_header *eh)
{
	if (is_app_mgr_event(eh)) {
		struct app_mgr_event *event = cast_app_mgr_event(eh);
		struct gps_msg_data gps_msg = {
			.manager.app = *event
		};

		notify_gps_manager(&gps_msg);
	}

	if (is_data_mgr_event(eh)) {
		struct data_mgr_event *event = cast_data_mgr_event(eh);
		struct gps_msg_data gps_msg = {
			.manager.data = *event
		};

		notify_gps_manager(&gps_msg);
	}

	if (is_util_mgr_event(eh)) {
		struct util_mgr_event *event = cast_util_mgr_event(eh);
		struct gps_msg_data gps_msg = {
			.manager.util = *event
		};

		notify_gps_manager(&gps_msg);
	}

	if (is_gps_mgr_event(eh)) {
		struct gps_mgr_event *event = cast_gps_mgr_event(eh);
		struct gps_msg_data gps_msg = {
			.manager.gps = *event
		};

		notify_gps_manager(&gps_msg);
	}

	return false;
}

static bool gps_data_requested(struct app_mgr_event_data *data_list,
			       size_t count)
{
	for (int i = 0; i < count; i++) {
		if (strcmp(data_list[i].buf, APP_DATA_GPS) == 0) {
			return true;
		}
	}

	return false;
}

static void on_state_init(struct gps_msg_data *gps_msg)
{
	if (is_data_mgr_event(&gps_msg->manager.data.header) &&
		gps_msg->manager.data.type == DATA_MGR_EVT_CONFIG_INIT) {
		gps_cfg.timeout = gps_msg->manager.data.data.cfg.gpst;
		gps_state = GPS_MGR_STATE_RUNNING;
	}
}

static void on_state_running(struct gps_msg_data *gps_msg)
{
	if (is_data_mgr_event(&gps_msg->manager.data.header) &&
		gps_msg->manager.data.type == DATA_MGR_EVT_CONFIG_READY) {
		gps_cfg.timeout = gps_msg->manager.data.data.cfg.gpst;
	}
}

static void on_state_running_gps_search(struct gps_msg_data *gps_msg)
{
	if (is_gps_mgr_event(&gps_msg->manager.gps.header) &&
		gps_msg->manager.gps.type == GPS_MGR_EVT_INACTIVE) {
		gps_sub_state = GPS_MGR_SUB_STATE_IDLE;
	}

	if (is_app_mgr_event(&gps_msg->manager.app.header) &&
		gps_msg->manager.app.type == APP_MGR_EVT_DATA_GET) {
		if (!gps_data_requested(gps_msg->manager.app.data_list,
					gps_msg->manager.app.count)) {
			return;
		}

		LOG_WRN("GPS search already active and will not be restarted");
		LOG_WRN("Try setting a sample/publication interval greater");
		LOG_WRN("than the GPS search timeout.");
	}
}

static void on_state_running_gps_idle(struct gps_msg_data *gps_msg)
{
	if (is_gps_mgr_event(&gps_msg->manager.gps.header) &&
		gps_msg->manager.gps.type == GPS_MGR_EVT_ACTIVE) {
		gps_sub_state = GPS_MGR_SUB_STATE_SEARCH;
	}

	if (is_app_mgr_event(&gps_msg->manager.app.header) &&
		gps_msg->manager.app.type == APP_MGR_EVT_DATA_GET) {
		if (!gps_data_requested(gps_msg->manager.app.data_list,
					gps_msg->manager.app.count)) {
			return;
		}

		gps_manager_search_start();
	}
}

static void state_agnostic_manager_events(struct gps_msg_data *gps_msg)
{
	if (is_util_mgr_event(&gps_msg->manager.util.header) &&
	    gps_msg->manager.util.type == UTIL_MGR_EVT_SHUTDOWN_REQUEST) {

		struct gps_mgr_event *gps_mgr_event = new_gps_mgr_event();

		gps_mgr_event->type = GPS_MGR_EVT_SHUTDOWN_READY;
		EVENT_SUBMIT(gps_mgr_event);
	}
}

static void gps_manager(void)
{
	int err;

	struct gps_msg_data gps_msg;

	atomic_inc(&manager_count);

	err = gps_manager_setup();
	if (err) {
		LOG_ERR("gps_manager_setup, error: %d", err);
		signal_error(err);
	}

	while (true) {
		k_msgq_get(&msgq_gps, &gps_msg, K_FOREVER);

		switch (gps_state) {
		case GPS_MGR_STATE_INIT:
			on_state_init(&gps_msg);
			break;
		case GPS_MGR_STATE_RUNNING:
			switch (gps_sub_state) {
			case GPS_MGR_SUB_STATE_SEARCH:
				on_state_running_gps_search(&gps_msg);
				break;
			case GPS_MGR_SUB_STATE_IDLE:
				on_state_running_gps_idle(&gps_msg);
				break;
			default:
				LOG_ERR("Unknown GPS manager sub state.");
				break;
			}

			on_state_running(&gps_msg);
			break;
		default:
			LOG_ERR("Unknown GPS manager state.");
			break;
		}
		state_agnostic_manager_events(&gps_msg);
	}
}

K_THREAD_DEFINE(gps_manager_thread, CONFIG_GPS_MGR_THREAD_STACK_SIZE,
		gps_manager, NULL, NULL, NULL,
		K_HIGHEST_APPLICATION_THREAD_PRIO, 0, -1);

EVENT_LISTENER(MODULE, event_handler);
EVENT_SUBSCRIBE(MODULE, app_mgr_event);
EVENT_SUBSCRIBE(MODULE, data_mgr_event);
EVENT_SUBSCRIBE(MODULE, util_mgr_event);
EVENT_SUBSCRIBE(MODULE, gps_mgr_event);
