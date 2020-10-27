/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <zephyr.h>
#include <event_manager.h>
#include <settings/settings.h>

#include "cloud_codec.h"

#define MODULE data_manager

#include "events/app_mgr_event.h"
#include "events/cloud_mgr_event.h"
#include "events/data_mgr_event.h"
#include "events/gps_mgr_event.h"
#include "events/modem_mgr_event.h"
#include "events/sensor_mgr_event.h"
#include "events/ui_mgr_event.h"
#include "events/util_mgr_event.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_CAT_TRACKER_LOG_LEVEL);

extern atomic_t manager_count;

#define DEVICE_SETTINGS_KEY			"data_manager"
#define DEVICE_SETTINGS_CONFIG_KEY		"config"

/* Default device configuration values. */
#define DEFAULT_ACTIVE_TIMEOUT_SECONDS		120
#define DEFAULT_PASSIVE_TIMEOUT_SECONDS		120
#define DEFAULT_MOVEMENT_TIMEOUT_SECONDS	3600
#define DEFAULT_ACCELEROMETER_THRESHOLD		100
#define DEFAULT_GPS_TIMEOUT_SECONDS		60
#define DEFAULT_DEVICE_MODE			true

struct data_msg_data {
	union {
		struct modem_mgr_event modem;
		struct cloud_mgr_event cloud;
		struct gps_mgr_event gps;
		struct ui_mgr_event ui;
		struct sensor_mgr_event sensor;
		struct data_mgr_event data;
		struct app_mgr_event app;
		struct util_mgr_event util;
	} manager;
};

/* Ringbuffers. All data received by the data manager are stored in ringbuffers.
 * Upon a LTE connection loss the device will keep sampling/storing data in
 * the buffers, and empty the buffers in batches upon a reconnect.
 */
static struct cloud_data_gps gps_buf[CONFIG_GPS_BUFFER_MAX];
static struct cloud_data_sensors sensors_buf[CONFIG_SENSOR_BUFFER_MAX];
static struct cloud_data_modem modem_buf[CONFIG_MODEM_BUFFER_MAX];
static struct cloud_data_ui ui_buf[CONFIG_UI_BUFFER_MAX];
static struct cloud_data_accelerometer accel_buf[CONFIG_ACCEL_BUFFER_MAX];
static struct cloud_data_battery bat_buf[CONFIG_BAT_BUFFER_MAX];

/* Head of ringbuffers. */
static int head_gps_buf;
static int head_sensor_buf;
static int head_modem_buf;
static int head_ui_buf;
static int head_accel_buf;
static int head_bat_buf;

/* Default device configuration. */
static struct cloud_data_cfg cfg = {
	.gpst = DEFAULT_GPS_TIMEOUT_SECONDS,
	.act  = DEFAULT_DEVICE_MODE,
	.actw = DEFAULT_ACTIVE_TIMEOUT_SECONDS,
	.pasw = DEFAULT_PASSIVE_TIMEOUT_SECONDS,
	.movt = DEFAULT_MOVEMENT_TIMEOUT_SECONDS,
	.acct = DEFAULT_ACCELEROMETER_THRESHOLD
};

/* Data manager super states. */
enum data_manager_state_type {
	/* In this state the data manager does not alter its data because
	 * it is shared with other managers.
	 */
	DATA_MGR_STATE_NOT_SHARING_DATA,
	/* This state is triggered if the data manager shares data. In this
	 * state the data manager can not alter the data that is shared.
	 */
	DATA_MGR_STATE_SHARING_DATA
} data_state;

/* Data manager sub states. */
enum data_manager_sub_state_type {
	DATA_MGR_SUB_STATE_DISCONNECTED,
	DATA_MGR_SUB_STATE_CONNECTED
} data_sub_state;

/* Data manager sub-sub states. */
enum data_manager_sub_sub_state_type {
	DATA_MGR_SUB_SUB_STATE_TIME_NOT_OBTAINED,
	DATA_MGR_SUB_SUB_STATE_TIME_OBTAINED
} data_sub_sub_state;

static struct k_delayed_work data_send_work;

/* List used to keep track of responses from other managers with data that is
 * requested to be sampled/published.
 */
static struct app_mgr_event_data data_types_list[APP_DATA_NUMBER_OF_TYPES_MAX];

/* Total number of data types requested for a particular sample/publish
 * cycle.
 */
static int affirmed_data_types;

/* Counter of data types received from other managers. When this number
 * matches the affirmed_data_type variable all requested data has been
 * received by the data manager.
 */
static int data_cnt;

K_MSGQ_DEFINE(msgq_data, sizeof(struct data_msg_data), 10, 4);

static void notify_data_manager(struct data_msg_data *data)
{
	while (k_msgq_put(&msgq_data, data, K_NO_WAIT) != 0) {
		/* message queue is full: purge old data & try again */
		k_msgq_purge(&msgq_data);
		LOG_ERR("Data manager message queue full, queue purged");
	}
}

static int config_settings_handler(const char *key, size_t len,
				   settings_read_cb read_cb, void *cb_arg)
{
	int err;

	if (strcmp(key, DEVICE_SETTINGS_CONFIG_KEY) == 0) {
		err = read_cb(cb_arg, &cfg, sizeof(cfg));
		if (err < 0) {
			LOG_ERR("Failed to load configuration, error: %d", err);
			return err;
		}
	}

	LOG_DBG("Device configuration loaded from flash");

	return 0;
}

static int data_manager_save_config(const void *buf, size_t buf_len)
{
	int err;

	err = settings_save_one(DEVICE_SETTINGS_KEY "/"
				DEVICE_SETTINGS_CONFIG_KEY,
				buf, buf_len);
	if (err) {
		LOG_WRN("settings_save_one, error: %d", err);
		return err;
	}

	LOG_DBG("Device configuration stored to flash");

	return 0;
}

static int data_manager_setup(void)
{
	int err;

	struct settings_handler settings_cfg = {
		.name = DEVICE_SETTINGS_KEY,
		.h_set = config_settings_handler
	};

	err = settings_subsys_init();
	if (err) {
		LOG_ERR("settings_subsys_init, error: %d", err);
		return err;
	}

	err = settings_register(&settings_cfg);
	if (err) {
		LOG_ERR("settings_register, error: %d", err);
		return err;
	}

	err = settings_load_subtree(DEVICE_SETTINGS_KEY);
	if (err) {
		LOG_ERR("settings_load_subtree, error: %d", err);
		return err;
	}

	return 0;
}

static void signal_error(int err)
{
	struct data_mgr_event *data_mgr_event = new_data_mgr_event();

	data_mgr_event->type = DATA_MGR_EVT_ERROR;
	data_mgr_event->data.err = err;

	EVENT_SUBMIT(data_mgr_event);
}

/* This function shares the pointer to and length of the ringbuffers. When this
 * occurs the data manager switches to a state where it can not alter the
 * ringbuffers. When the cloud manager acknowledges when it is finished with the
 * data it sends an events that it is done using the data. The data manager then
 * switches back to a state where it can manipulate the ringbuffers again.
 */
static void data_send(void)
{
	struct data_mgr_event *data_mgr_event = new_data_mgr_event();

	data_mgr_event->type = DATA_MGR_EVT_DATA_SEND;
	data_mgr_event->data.buffer.bat = bat_buf;
	data_mgr_event->data.buffer.bat_count = ARRAY_SIZE(bat_buf);
	data_mgr_event->data.buffer.accel = accel_buf;
	data_mgr_event->data.buffer.accel_count = ARRAY_SIZE(accel_buf);
	data_mgr_event->data.buffer.gps = gps_buf;
	data_mgr_event->data.buffer.gps_count = ARRAY_SIZE(gps_buf);
	data_mgr_event->data.buffer.modem = modem_buf;
	data_mgr_event->data.buffer.modem_count = ARRAY_SIZE(modem_buf);
	data_mgr_event->data.buffer.ui = ui_buf;
	data_mgr_event->data.buffer.ui_count = ARRAY_SIZE(ui_buf);
	data_mgr_event->data.buffer.sensors = sensors_buf;
	data_mgr_event->data.buffer.sensors_count = ARRAY_SIZE(sensors_buf);

	/* Send the head of the ringbuffers signifying where the newest data
	 * is stored.
	 */
	data_mgr_event->data.buffer.head_gps = head_gps_buf;
	data_mgr_event->data.buffer.head_bat = head_bat_buf;
	data_mgr_event->data.buffer.head_modem = head_modem_buf;
	data_mgr_event->data.buffer.head_ui = head_ui_buf;
	data_mgr_event->data.buffer.head_accel = head_accel_buf;
	data_mgr_event->data.buffer.head_sensor = head_sensor_buf;

	EVENT_SUBMIT(data_mgr_event);
}

static void data_ui_send(void)
{
	struct data_mgr_event *data_mgr_event = new_data_mgr_event();

	/* UI data is sent as a separate copy. */
	data_mgr_event->type = DATA_MGR_EVT_UI_DATA_SEND;
	data_mgr_event->data.ui = ui_buf[head_ui_buf];

	/* Since a copy of data is sent we must unqueue the head of the
	 * UI buffer.
	 */

	ui_buf[head_ui_buf].queued = false;

	EVENT_SUBMIT(data_mgr_event);
}

static void config_signal_managers(void)
{
	struct data_mgr_event *data_mgr_event = new_data_mgr_event();

	data_mgr_event->type = DATA_MGR_EVT_CONFIG_READY;
	data_mgr_event->data.cfg = cfg;

	EVENT_SUBMIT(data_mgr_event);
}

static void config_send(void)
{
	struct data_mgr_event *data_mgr_event = new_data_mgr_event();

	data_mgr_event->type = DATA_MGR_EVT_CONFIG_SEND;
	data_mgr_event->data.cfg = cfg;

	/* Send configuration to cloud manager. */
	EVENT_SUBMIT(data_mgr_event);
}

static void config_init(void)
{
	struct data_mgr_event *data_mgr_event = new_data_mgr_event();

	data_mgr_event->type = DATA_MGR_EVT_CONFIG_INIT;
	data_mgr_event->data.cfg = cfg;

	/* Send initial config to other managers. */
	EVENT_SUBMIT(data_mgr_event);
}

static bool event_handler(const struct event_header *eh)
{
	if (is_modem_mgr_event(eh)) {
		struct modem_mgr_event *event = cast_modem_mgr_event(eh);
		struct data_msg_data data_msg = {
			.manager.modem = *event
		};

		notify_data_manager(&data_msg);
	}

	if (is_cloud_mgr_event(eh)) {
		struct cloud_mgr_event *event = cast_cloud_mgr_event(eh);
		struct data_msg_data data_msg = {
			.manager.cloud = *event
		};

		notify_data_manager(&data_msg);
	}

	if (is_gps_mgr_event(eh)) {
		struct gps_mgr_event *event = cast_gps_mgr_event(eh);
		struct data_msg_data data_msg = {
			.manager.gps = *event
		};

		notify_data_manager(&data_msg);
	}

	if (is_sensor_mgr_event(eh)) {
		struct sensor_mgr_event *event = cast_sensor_mgr_event(eh);
		struct data_msg_data data_msg = {
			.manager.sensor = *event
		};

		notify_data_manager(&data_msg);
	}

	if (is_ui_mgr_event(eh)) {
		struct ui_mgr_event *event = cast_ui_mgr_event(eh);
		struct data_msg_data data_msg = {
			.manager.ui = *event
		};

		notify_data_manager(&data_msg);
	}

	if (is_app_mgr_event(eh)) {
		struct app_mgr_event *event = cast_app_mgr_event(eh);
		struct data_msg_data data_msg = {
			.manager.app = *event
		};

		notify_data_manager(&data_msg);
	}

	if (is_data_mgr_event(eh)) {
		struct data_mgr_event *event = cast_data_mgr_event(eh);
		struct data_msg_data data_msg = {
			.manager.data = *event
		};

		notify_data_manager(&data_msg);
	}

	if (is_util_mgr_event(eh)) {
		struct util_mgr_event *event = cast_util_mgr_event(eh);
		struct data_msg_data data_msg = {
			.manager.util = *event
		};

		notify_data_manager(&data_msg);
	}

	return false;
}

static void clear_data_list(void)
{
	for (int i = 0; i < APP_DATA_NUMBER_OF_TYPES_MAX; i++) {
		data_types_list[i].buf = "\0";
	}
	affirmed_data_types = 0;
	data_cnt = 0;
}

static void data_send_work_fn(struct k_work *work)
{

	struct app_mgr_event *app_mgr_event = new_app_mgr_event();

	app_mgr_event->type = APP_MGR_EVT_DATA_SEND;

	EVENT_SUBMIT(app_mgr_event);

	clear_data_list();
	k_delayed_work_cancel(&data_send_work);
}

static void data_status_set(const char *data_type)
{
	if (data_type == NULL) {
		return;
	}

	for (int i = 0; i < affirmed_data_types; i++) {

		if (data_types_list[i].buf == NULL) {
			continue;
		}

		if (strcmp(data_types_list[i].buf, data_type) == 0) {
			data_cnt++;
		}
	}

	if (data_cnt == affirmed_data_types) {
		data_send_work_fn(NULL);
	}
}

static void data_list_set(struct app_mgr_event_data *data_list, size_t count)
{
	if (count > APP_DATA_NUMBER_OF_TYPES_MAX) {
		LOG_ERR("Invalid data type list length");
		return;
	}

	if (data_list == NULL) {
		LOG_ERR("List empty!");
		return;
	}

	clear_data_list();

	for (int i = 0; i < count; i++) {
		data_types_list[i].buf = data_list[i].buf;
	}

	affirmed_data_types = count;
}

static void on_state_disconnected(struct data_msg_data *data_msg)
{
	if (is_cloud_mgr_event(&data_msg->manager.cloud.header) &&
	    data_msg->manager.cloud.type == CLOUD_MGR_EVT_CONNECTED) {
		data_sub_state = DATA_MGR_SUB_STATE_CONNECTED;
	}
}

static void on_state_connected(struct data_msg_data *data_msg)
{
	if (is_cloud_mgr_event(&data_msg->manager.cloud.header) &&
	    data_msg->manager.cloud.type == CLOUD_MGR_EVT_DISCONNECTED) {
		data_sub_state = DATA_MGR_SUB_STATE_DISCONNECTED;
	}

	/* Config is not timestamped and does not to be dependent on the
	 * MODEM_MGR_SUB_SUB_STATE_DATE_TIME_OBTAINED state.
	 */
	if (is_app_mgr_event(&data_msg->manager.app.header) &&
	    data_msg->manager.app.type == APP_MGR_EVT_CONFIG_SEND) {
		config_send();
	}

	if (is_cloud_mgr_event(&data_msg->manager.cloud.header) &&
	    data_msg->manager.cloud.type == CLOUD_MGR_EVT_CONFIG_RECEIVED) {

		int err;

		struct cloud_data_cfg new = {
			.act = data_msg->manager.cloud.data.config.act,
			.actw = data_msg->manager.cloud.data.config.actw,
			.pasw = data_msg->manager.cloud.data.config.pasw,
			.movt = data_msg->manager.cloud.data.config.movt,
			.gpst = data_msg->manager.cloud.data.config.gpst,
			.acct = data_msg->manager.cloud.data.config.acct,

		};

		/* Only change values that are not 0 and have changed.
		 * Since 0 is a valid value for the mode configuration we dont
		 * include the 0 check. In general I think we should have
		 * minimum allowed values so that extremely low configurations
		 * dont suffocate the application.
		 */
		if (cfg.act != new.act) {
			cfg.act = new.act;

			if (cfg.act) {
				LOG_WRN("New Device mode: Active");
			} else {
				LOG_WRN("New Device mode: Passive");
			}
		}

		if (cfg.actw != new.actw && new.actw != 0) {
			cfg.actw = new.actw;
			LOG_WRN("New Active timeout: %d", cfg.actw);
		}

		if (cfg.pasw != new.pasw && new.pasw != 0) {
			cfg.pasw = new.pasw;
			LOG_WRN("New Movement resolution: %d", cfg.pasw);
		}

		if (cfg.movt != new.movt && new.movt != 0) {
			cfg.movt = new.movt;
			LOG_WRN("New Movement timeout: %d", cfg.movt);
		}

		if (cfg.acct != new.acct && new.acct != 0) {
			cfg.acct = new.acct;
			LOG_WRN("New Movement threshold: %d", cfg.acct);
		}

		if (cfg.gpst != new.gpst && new.gpst != 0) {
			cfg.gpst = new.gpst;
			LOG_WRN("New GPS timeout: %d", cfg.gpst);
		}

		err = data_manager_save_config(&cfg, sizeof(cfg));
		if (err) {
			LOG_WRN("Configuration not stored, error: %d", err);
		}

		config_signal_managers();
	}
}

static void on_sub_state_connected_date_time_obt(struct data_msg_data *data_msg)
{
	if (is_app_mgr_event(&data_msg->manager.app.header) &&
	    data_msg->manager.app.type == APP_MGR_EVT_UI_DATA_SEND) {
		data_ui_send();
	}
}

static void on_sub_state_date_time_obtained(struct data_msg_data *data_msg)
{
	if (is_app_mgr_event(&data_msg->manager.app.header)) {
		switch (data_msg->manager.app.type) {
		case APP_MGR_EVT_DATA_SEND:
			data_send();
			break;
		case APP_MGR_EVT_UI_DATA_SEND:
			data_ui_send();
			break;
		default:
			break;
		}
	}
}

static void on_sub_state_date_time_not_obtained(struct data_msg_data *data_msg)
{
	if (is_modem_mgr_event(&data_msg->manager.modem.header) &&
	    data_msg->manager.modem.type == MODEM_MGR_EVT_DATE_TIME_OBTAINED) {
		data_sub_sub_state = DATA_MGR_SUB_SUB_STATE_TIME_OBTAINED;
	}
}

static void on_state_sharing_data(struct data_msg_data *data_msg)
{
	if (is_cloud_mgr_event(&data_msg->manager.cloud.header) &&
	    data_msg->manager.cloud.type == CLOUD_MGR_EVT_SHARED_DATA_DONE) {
		data_state = DATA_MGR_STATE_NOT_SHARING_DATA;
	}
}

static void on_state_not_sharing_data(struct data_msg_data *data_msg)
{
	if (is_data_mgr_event(&data_msg->manager.data.header) &&
	    data_msg->manager.data.type == DATA_MGR_EVT_DATA_SEND) {
		data_state = DATA_MGR_STATE_SHARING_DATA;
	}

	if (is_app_mgr_event(&data_msg->manager.app.header) &&
		data_msg->manager.app.type == APP_MGR_EVT_DATA_GET) {
		data_list_set(data_msg->manager.app.data_list,
			      data_msg->manager.app.count);

		k_delayed_work_submit(&data_send_work,
				      K_SECONDS(data_msg->manager.app.timeout));
	}

	if (is_modem_mgr_event(&data_msg->manager.modem.header)) {

		switch (data_msg->manager.modem.type) {
		case MODEM_MGR_EVT_MODEM_DATA_READY:
			cloud_codec_populate_modem_buffer(
					modem_buf,
					&data_msg->manager.modem.data.modem,
					&head_modem_buf);

			data_status_set(APP_DATA_MODEM);
			break;
		case MODEM_MGR_EVT_BATTERY_DATA_READY:
			cloud_codec_populate_bat_buffer(
					bat_buf,
					&data_msg->manager.modem.data.bat,
					&head_bat_buf);

			data_status_set(APP_DATA_BATTERY);
			break;
		default:
			break;
		}
	}

	if (is_sensor_mgr_event(&data_msg->manager.sensor.header)) {

		switch (data_msg->manager.sensor.type) {
		case SENSOR_MGR_EVT_ENVIRONMENTAL_DATA_READY:
			cloud_codec_populate_sensor_buffer(
				sensors_buf,
				&data_msg->manager.sensor.data.sensors,
				&head_sensor_buf);

			data_status_set(APP_DATA_ENVIRONMENTALS);
			break;
		case SENSOR_MGR_EVT_MOVEMENT_DATA_READY:
			cloud_codec_populate_accel_buffer(
					accel_buf,
					&data_msg->manager.sensor.data.accel,
					&head_accel_buf);
			break;
		default:
			break;
		}
	}

	if (is_ui_mgr_event(&data_msg->manager.ui.header)) {

		switch (data_msg->manager.ui.type) {
		case UI_MGR_EVT_BUTTON_DATA_READY:
			cloud_codec_populate_ui_buffer(
						ui_buf,
						&data_msg->manager.ui.data.ui,
						&head_ui_buf);

			struct app_mgr_event *app_mgr_event =
					new_app_mgr_event();

			app_mgr_event->type = APP_MGR_EVT_UI_DATA_SEND;
			EVENT_SUBMIT(app_mgr_event);
			break;
		default:
			break;
		}
	}

	if (is_gps_mgr_event(&data_msg->manager.gps.header)) {

		switch (data_msg->manager.gps.type) {
		case GPS_MGR_EVT_DATA_READY:
			cloud_codec_populate_gps_buffer(
						gps_buf,
						&data_msg->manager.gps.data.gps,
						&head_gps_buf);

			data_status_set(APP_DATA_GPS);
			break;
		case GPS_MGR_EVT_TIMEOUT:
			data_status_set(APP_DATA_GPS);
			break;
		default:
			break;
		}
	}
}

static void state_agnostic_manager_events(struct data_msg_data *data_msg)
{
	if (is_util_mgr_event(&data_msg->manager.util.header) &&
	    data_msg->manager.util.type == UTIL_MGR_EVT_SHUTDOWN_REQUEST) {

		struct data_mgr_event *data_mgr_event = new_data_mgr_event();

		data_mgr_event->type = DATA_MGR_EVT_SHUTDOWN_READY;
		EVENT_SUBMIT(data_mgr_event);
	}
}

static void data_manager(void)
{
	int err;
	struct data_msg_data data_msg;

	atomic_inc(&manager_count);

	k_delayed_work_init(&data_send_work, data_send_work_fn);

	err = data_manager_setup();
	if (err) {
		LOG_ERR("data_manager_setup, error: %d", err);
		signal_error(err);
	}

	/* Start by sending the default configuration to the application. */
	config_init();

	while (true) {
		k_msgq_get(&msgq_data, &data_msg, K_FOREVER);

		switch (data_state) {
		case DATA_MGR_STATE_NOT_SHARING_DATA:
			switch (data_sub_state) {
			case DATA_MGR_SUB_STATE_DISCONNECTED:
				on_state_disconnected(&data_msg);
				break;
			case DATA_MGR_SUB_STATE_CONNECTED:
				switch (data_sub_sub_state) {
				case DATA_MGR_SUB_SUB_STATE_TIME_OBTAINED:
					on_sub_state_date_time_obtained(
								&data_msg);
					break;
				case DATA_MGR_SUB_SUB_STATE_TIME_NOT_OBTAINED:
					on_sub_state_date_time_not_obtained(
								&data_msg);
					break;
				default:
					LOG_WRN("Unknown sub-sub state.");
					break;
				}
				on_state_connected(&data_msg);
				break;
			default:
				LOG_WRN("Unknown sub state.");
				break;
			}
			on_state_not_sharing_data(&data_msg);
			break;
		case DATA_MGR_STATE_SHARING_DATA:
			switch (data_sub_state) {
			case DATA_MGR_SUB_STATE_DISCONNECTED:
				on_state_disconnected(&data_msg);
				break;
			case DATA_MGR_SUB_STATE_CONNECTED:
				switch (data_sub_sub_state) {
				case DATA_MGR_SUB_SUB_STATE_TIME_OBTAINED:
					on_sub_state_connected_date_time_obt(
								&data_msg);
					break;
				case DATA_MGR_SUB_SUB_STATE_TIME_NOT_OBTAINED:
					on_sub_state_date_time_not_obtained(
								&data_msg);
					break;
				default:
					LOG_WRN("Unknown sub-sub state.");
					break;
				}
				on_state_connected(&data_msg);
				break;
			default:
				LOG_WRN("Unknown sub state.");
				break;
			}
			on_state_sharing_data(&data_msg);
			break;
		default:
			LOG_WRN("Unknwon data manager state");
			break;
		}
		state_agnostic_manager_events(&data_msg);
	}
}

K_THREAD_DEFINE(data_manager_thread, CONFIG_DATA_MGR_THREAD_STACK_SIZE,
		data_manager, NULL, NULL, NULL,
		K_HIGHEST_APPLICATION_THREAD_PRIO, 0, -1);

EVENT_LISTENER(MODULE, event_handler);
EVENT_SUBSCRIBE(MODULE, app_mgr_event);
EVENT_SUBSCRIBE(MODULE, util_mgr_event);
EVENT_SUBSCRIBE(MODULE, data_mgr_event);
EVENT_SUBSCRIBE_EARLY(MODULE, modem_mgr_event);
EVENT_SUBSCRIBE_EARLY(MODULE, cloud_mgr_event);
EVENT_SUBSCRIBE_EARLY(MODULE, gps_mgr_event);
EVENT_SUBSCRIBE_EARLY(MODULE, ui_mgr_event);
EVENT_SUBSCRIBE_EARLY(MODULE, sensor_mgr_event);
