/*
 * Copyright (c) 2020-2021, Nordic Semiconductor ASA | nordicsemi.no
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <zephyr.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <drivers/sensor.h>
#include <drivers/gps.h>
#include <modem/lte_lc.h>
#include <modem/modem_info.h>
#include <net/socket.h>
#include <net/cloud.h>
#include <device.h>
#include <power/reboot.h>
#include <dfu/mcuboot.h>
#include <date_time.h>
#include <dk_buttons_and_leds.h>
#include <math.h>

/* Application specific modules. */
#include "ext_sensors.h"
#include "watchdog.h"
#include "cloud_codec.h"
#include "ui.h"

#include <logging/log.h>
#include <logging/log_ctrl.h>
LOG_MODULE_REGISTER(cat_tracker, CONFIG_CAT_TRACKER_LOG_LEVEL);

/* Application specific AWS topics. */
#if !defined(CONFIG_USE_CUSTOM_MQTT_CLIENT_ID)
#define AWS_CLOUD_CLIENT_ID_LEN 15
#else
#define AWS_CLOUD_CLIENT_ID_LEN (sizeof(CONFIG_MQTT_CLIENT_ID) - 1)
#endif
#define AWS "$aws/things/"
#define AWS_LEN (sizeof(AWS) - 1)
#define CFG_TOPIC AWS "%s/shadow/get/accepted/desired/cfg"
#define CFG_TOPIC_LEN (AWS_LEN + AWS_CLOUD_CLIENT_ID_LEN + 32)
#define BATCH_TOPIC "%s/batch"
#define BATCH_TOPIC_LEN (AWS_CLOUD_CLIENT_ID_LEN + 6)
#define MESSAGES_TOPIC "%s/messages"
#define MESSAGES_TOPIC_LEN (AWS_CLOUD_CLIENT_ID_LEN + 9)

/* Maximum GPS interval value. Dummy value, will not be used. Starting
 * and stopping of GPS is done by the application.
 */
#define GPS_INTERVAL_MAX 1800

/* Default device configuration values. */
#define ACTIVE_TIMEOUT_SECONDS 60
#define PASSIVE_TIMEOUT_SECONDS 60
#define MOVEMENT_TIMEOUT_SECONDS 3600
#define ACCELEROMETER_THRESHOLD 100
#define GPS_TIMEOUT_SECONDS 60
#define DEVICE_MODE true

/* Time between cloud re-connection attempts. */
#define CLOUD_RECONNECTION_INTERVAL 30

/* Timeout in seconds in which the application will wait for an initial event
 * from the date time library.
 */
#define DATE_TIME_TIMEOUT_S 15

enum app_endpoint_type { CLOUD_EP_TOPIC_MESSAGES = CLOUD_EP_PRIV_START };

/* Circular buffers. All data sent to cloud are stored in cicular buffers.
 * Upon a LTE connection loss the device will keep sampling/storing data in
 * the buffers, and empty the buffers in batches upon a reconnect.
 */
static struct cloud_data_gps gps_buf[CONFIG_GPS_BUFFER_MAX];
static struct cloud_data_sensors sensors_buf[CONFIG_SENSOR_BUFFER_MAX];
static struct cloud_data_modem modem_buf[CONFIG_MODEM_BUFFER_MAX];
static struct cloud_data_ui ui_buf[CONFIG_UI_BUFFER_MAX];
static struct cloud_data_accelerometer accel_buf[CONFIG_ACCEL_BUFFER_MAX];
static struct cloud_data_battery bat_buf[CONFIG_BAT_BUFFER_MAX];

/** Head of circular buffers. */
static int head_gps_buf;
static int head_sensor_buf;
static int head_modem_buf;
static int head_ui_buf;
static int head_accel_buf;
static int head_bat_buf;

/* Default device configuration. */
static struct cloud_data_cfg cfg = { .gpst = GPS_TIMEOUT_SECONDS,
				     .act = DEVICE_MODE,
				     .actw = ACTIVE_TIMEOUT_SECONDS,
				     .pasw = PASSIVE_TIMEOUT_SECONDS,
				     .movt = MOVEMENT_TIMEOUT_SECONDS,
				     .acct = ACCELEROMETER_THRESHOLD };

static struct cloud_endpoint sub_ep_topics_sub[1];
static struct cloud_endpoint pub_ep_topics_sub[2];

static char client_id_buf[AWS_CLOUD_CLIENT_ID_LEN + 1];
static char batch_topic[BATCH_TOPIC_LEN + 1];
static char cfg_topic[CFG_TOPIC_LEN + 1];
static char messages_topic[MESSAGES_TOPIC_LEN + 1];

static struct modem_param_info modem_param;
static struct cloud_backend *cloud_backend;

static bool gps_fix;
static bool cloud_connected;

static struct k_delayed_work device_config_get_work;
static struct k_delayed_work device_config_send_work;
static struct k_delayed_work data_send_work;
static struct k_delayed_work buffered_data_send_work;
static struct k_delayed_work ui_send_work;
static struct k_delayed_work leds_set_work;
static struct k_delayed_work mov_timeout_work;
static struct k_delayed_work sample_data_work;
static struct k_delayed_work cloud_connect_work;

/* Value that always holds the latest RSRP value. */
static uint16_t rsrp_value_latest;

/* Depend on this semaphore when in passive mode. Release only if the movement
 * of the subject breaks the set accelerometer threshold value. When the
 * sempahore is released the application performs its normal publish cycle.
 */
static K_SEM_DEFINE(accel_trig_sem, 0, 1);
/* Give this semaphore when the GPS either obtains a fix or times out. */
static K_SEM_DEFINE(gps_timeout_sem, 0, 1);
/* Give this semaphore when the device has a successful LTE connection. */
static K_SEM_DEFINE(lte_conn_sem, 0, 1);
/* Give this semaphore when the date time library has tried to obtain time. */
static K_SEM_DEFINE(date_time_sem, 0, 1);

/* GPS device. Used to identify the GPS driver in the sensor API. */
static const struct device *gps_dev;

/* nRF9160 GPS driver configuration. */
static struct gps_config gps_cfg = { .nav_mode = GPS_NAV_MODE_PERIODIC,
				     .power_mode = GPS_POWER_MODE_DISABLED,
				     .interval = GPS_INTERVAL_MAX,
				     .timeout = GPS_TIMEOUT_SECONDS };

static void error_handler(int err_code)
{
	LOG_ERR("err_handler, error code: %d", err_code);
	ui_led_set_pattern(UI_LED_ERROR_SYSTEM_FAULT);

#if !defined(CONFIG_DEBUG) && defined(CONFIG_REBOOT)
	LOG_PANIC();
	sys_reboot(0);
#else
	while (true) {
		k_cpu_idle();
	}
#endif
}

void bsd_recoverable_error_handler(uint32_t err)
{
	error_handler((int)err);
}

void k_sys_fatal_error_handler(unsigned int reason, const z_arch_esf_t *esf)
{
	ARG_UNUSED(esf);

	LOG_PANIC();
	LOG_ERR("k_sys_fatal_error_handler, error: %d", reason);
	error_handler(reason);
	CODE_UNREACHABLE;
}

static int device_mode_check(void)
{
	/* Return either active passive timeout depending on the
	 * device mode.
	 */
	if (!cfg.act) {
		return cfg.pasw;
	}

	return cfg.actw;
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

static void leds_set(void)
{
	if (!k_sem_count_get(&gps_timeout_sem)) {
		if (!cfg.act) {
			ui_led_set_pattern(UI_LED_PASSIVE_MODE);
		} else {
			ui_led_set_pattern(UI_LED_ACTIVE_MODE);
		}
	} else {
		ui_led_set_pattern(UI_LED_GPS_SEARCHING);
	}
}

static void battery_buffer_populate(void)
{
	/* Go to start of buffer if end is reached. */
	head_bat_buf += 1;
	if (head_bat_buf == CONFIG_BAT_BUFFER_MAX) {
		head_bat_buf = 0;
	}

	bat_buf[head_bat_buf].bat = modem_param.device.battery.value;
	bat_buf[head_bat_buf].bat_ts = k_uptime_get();
	bat_buf[head_bat_buf].queued = true;

	LOG_DBG("Entry: %d of %d in battery buffer filled", head_bat_buf,
		CONFIG_BAT_BUFFER_MAX - 1);
}

static void gps_buffer_populate(struct gps_pvt *gps_data)
{
	/* Go to start of buffer if end is reached. */
	head_gps_buf += 1;
	if (head_gps_buf == CONFIG_GPS_BUFFER_MAX) {
		head_gps_buf = 0;
	}

	gps_buf[head_gps_buf].longi = gps_data->longitude;
	gps_buf[head_gps_buf].lat = gps_data->latitude;
	gps_buf[head_gps_buf].alt = gps_data->altitude;
	gps_buf[head_gps_buf].acc = gps_data->accuracy;
	gps_buf[head_gps_buf].spd = gps_data->speed;
	gps_buf[head_gps_buf].hdg = gps_data->heading;
	gps_buf[head_gps_buf].gps_ts = k_uptime_get();
	gps_buf[head_gps_buf].queued = true;

	LOG_DBG("Entry: %d of %d in GPS buffer filled", head_gps_buf,
		CONFIG_GPS_BUFFER_MAX - 1);
}

void acc_array_swap(struct cloud_data_accelerometer *xp,
		    struct cloud_data_accelerometer *yp)
{
	struct cloud_data_accelerometer temp = *xp;
	*xp = *yp;
	*yp = temp;
}

#if defined(CONFIG_EXTERNAL_SENSORS)
static void
accelerometer_buffer_populate(const struct ext_sensor_evt *const acc_data)
{
	static int buf_entry_try_again_timeout;
	int j, k, n;
	int i = 0;
	double temp = 0;
	double temp_ = 0;
	int64_t newest_time = 0;

	/** Only populate accelerometer buffer if a configurable amount of time
	 *  has passed since the last accelerometer buffer entry was filled.
	 *
	 *  If the circular buffer is filled always keep the highest
         *  values in the circular buffer.
	 */
	if (k_uptime_get() - buf_entry_try_again_timeout >
	    1000 * CONFIG_TIME_BETWEEN_ACCELEROMETER_BUFFER_STORE_SEC) {
		/** Populate the next available unqueued entry. */
		for (k = 0; k < ARRAY_SIZE(accel_buf); k++) {
			if (!accel_buf[k].queued) {
				head_accel_buf = k;
				goto populate_buffer;
			}
		}

		/** Sort list after highest values using bubble sort.
		 */
		for (j = 0; j < ARRAY_SIZE(accel_buf) - i - 1; j++) {
			for (n = 0; n < 3; n++) {
				if (temp < abs(accel_buf[j].values[n])) {
					temp = abs(accel_buf[j].values[n]);
				}

				if (temp_ < abs(accel_buf[j + 1].values[n])) {
					temp_ = abs(accel_buf[j + 1].values[n]);
				}
			}

			if (temp > temp_) {
				acc_array_swap(&accel_buf[j],
					       &accel_buf[j + 1]);
			}
		}

		temp = 0;
		temp_ = 0;

		/** Find highest value in new accelerometer entry. */
		for (n = 0; n < 3; n++) {
			if (temp < abs(acc_data->value_array[n])) {
				temp = abs(acc_data->value_array[n]);
			}
		}

		/** Replace old accelerometer entry with the new entry if the
		 *  highest value in new value is greater than the old.
		 */
		for (int k = 0; k < ARRAY_SIZE(accel_buf); k++) {
			for (n = 0; n < 3; n++) {
				if (temp_ < abs(accel_buf[k].values[n])) {
					temp_ = abs(accel_buf[k].values[n]);
				}

				if (temp > temp_) {
					head_accel_buf = k;
				}
			}
		}

		// clang-format off
populate_buffer:
		// clang-format on

		accel_buf[head_accel_buf].values[0] = acc_data->value_array[0];
		accel_buf[head_accel_buf].values[1] = acc_data->value_array[1];
		accel_buf[head_accel_buf].values[2] = acc_data->value_array[2];
		accel_buf[head_accel_buf].ts = k_uptime_get();
		accel_buf[head_accel_buf].queued = true;

		LOG_DBG("Entry: %d of %d in accelerometer buffer filled",
			head_accel_buf, CONFIG_ACCEL_BUFFER_MAX - 1);

		buf_entry_try_again_timeout = k_uptime_get();

		/** Always point head of buffer to the newest sampled value. */
		for (i = 0; i < ARRAY_SIZE(accel_buf); i++) {
			if (newest_time < accel_buf[i].ts &&
			    accel_buf[i].queued) {
				newest_time = accel_buf[i].ts;
				head_accel_buf = i;
			}
		}
	}
}
#endif

/* Produce a warning if modem firmware version is unexpected. */
static void check_modem_fw_version(void)
{
	static bool modem_fw_version_checked;

	if (modem_fw_version_checked) {
		return;
	}
	if (strcmp(modem_param.device.modem_fw.value_string,
		   CONFIG_EXPECTED_MODEM_FIRMWARE_VERSION) != 0) {
		LOG_WRN("Unsupported modem firmware version: %s",
			log_strdup(modem_param.device.modem_fw.value_string));
		LOG_WRN("Expected firmware version: %s",
			CONFIG_EXPECTED_MODEM_FIRMWARE_VERSION);
		LOG_WRN("You can change the expected version through the");
		LOG_WRN("EXPECTED_MODEM_FIRMWARE_VERSION setting.");
		LOG_WRN("Please upgrade: http://bit.ly/nrf9160-mfw-upgrade");
	} else {
		LOG_INF("Board is running expected modem firmware version: %s",
			log_strdup(modem_param.device.modem_fw.value_string));
	}
	modem_fw_version_checked = true;
}

static int modem_buffer_populate(void)
{
	int err;

	/* Request data from modem. */
	err = modem_info_params_get(&modem_param);
	if (err) {
		LOG_ERR("modem_info_params_get, error: %d", err);
		return err;
	}

	check_modem_fw_version();

	/* Go to start of buffer if end is reached. */
	head_modem_buf += 1;
	if (head_modem_buf == CONFIG_MODEM_BUFFER_MAX) {
		head_modem_buf = 0;
	}

	modem_buf[head_modem_buf].rsrp = rsrp_value_latest;
	modem_buf[head_modem_buf].ip =
		modem_param.network.ip_address.value_string;
	modem_buf[head_modem_buf].cell = modem_param.network.cellid_dec;
	modem_buf[head_modem_buf].mccmnc =
		modem_param.network.current_operator.value_string;
	modem_buf[head_modem_buf].area = modem_param.network.area_code.value;
	modem_buf[head_modem_buf].appv = CONFIG_CAT_TRACKER_APP_VERSION;
	modem_buf[head_modem_buf].brdv = modem_param.device.board;
	modem_buf[head_modem_buf].fw = modem_param.device.modem_fw.value_string;
	modem_buf[head_modem_buf].iccid = modem_param.sim.iccid.value_string;
	modem_buf[head_modem_buf].nw_lte_m = modem_param.network.lte_mode.value;
	modem_buf[head_modem_buf].nw_nb_iot =
		modem_param.network.nbiot_mode.value;
	modem_buf[head_modem_buf].nw_gps = modem_param.network.gps_mode.value;
	modem_buf[head_modem_buf].bnd = modem_param.network.current_band.value;
	modem_buf[head_modem_buf].mod_ts = k_uptime_get();
	modem_buf[head_modem_buf].mod_ts_static = k_uptime_get();
	modem_buf[head_modem_buf].queued = true;

	LOG_DBG("Entry: %d of %d in modem buffer filled", head_modem_buf,
		CONFIG_MODEM_BUFFER_MAX - 1);

	return 0;
}

#if defined(CONFIG_EXTERNAL_SENSORS)
static int sensors_buffer_populate(void)
{
	int err;

	/* Go to start of buffer if end is reached. */
	head_sensor_buf += 1;
	if (head_sensor_buf == CONFIG_SENSOR_BUFFER_MAX) {
		head_sensor_buf = 0;
	}

	/* Request data from external sensors. */
	err = ext_sensors_temperature_get(&sensors_buf[head_sensor_buf].temp);
	if (err) {
		LOG_ERR("temperature_get, error: %d", err);
		return err;
	}

	err = ext_sensors_humidity_get(&sensors_buf[head_sensor_buf].hum);
	if (err) {
		LOG_ERR("temperature_get, error: %d", err);
		return err;
	}

	sensors_buf[head_sensor_buf].env_ts = k_uptime_get();
	sensors_buf[head_sensor_buf].queued = true;

	LOG_DBG("Entry: %d of %d in sensor buffer filled", head_sensor_buf,
		CONFIG_SENSOR_BUFFER_MAX - 1);

	return 0;
}
#endif

static void ui_buffer_populate(int btn_number)
{
	/* Go to start of buffer if end is reached. */
	head_ui_buf += 1;
	if (head_ui_buf == CONFIG_UI_BUFFER_MAX) {
		head_ui_buf = 0;
	}

	ui_buf[head_ui_buf].btn = 1;
	ui_buf[head_ui_buf].btn_ts = k_uptime_get();
	ui_buf[head_ui_buf].queued = true;

	LOG_DBG("Entry: %d of %d in UI buffer filled", head_ui_buf,
		CONFIG_UI_BUFFER_MAX - 1);
}

static void lte_evt_handler(const struct lte_lc_evt *const evt)
{
	switch (evt->type) {
	case LTE_LC_EVT_NW_REG_STATUS:
		if ((evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
		    (evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING)) {
			break;
		}

		LOG_INF("Network registration status: %s",
			evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ?
				"Connected to home network" :
				"Connected to roaming network");

		k_sem_give(&lte_conn_sem);
		break;
	case LTE_LC_EVT_PSM_UPDATE:
		LOG_DBG("PSM parameter update: TAU: %d, Active time: %d",
			evt->psm_cfg.tau, evt->psm_cfg.active_time);
		break;
	case LTE_LC_EVT_EDRX_UPDATE: {
		char log_buf[60];
		ssize_t len;

		len = snprintf(log_buf, sizeof(log_buf),
			       "eDRX parameter update: eDRX: %f, PTW: %f",
			       evt->edrx_cfg.edrx, evt->edrx_cfg.ptw);
		if (len > 0) {
			LOG_DBG("%s", log_strdup(log_buf));
		}
		break;
	}
	case LTE_LC_EVT_RRC_UPDATE:
		LOG_DBG("RRC mode: %s",
			evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ?
				"Connected" :
				"Idle");
		break;
	case LTE_LC_EVT_CELL_UPDATE:
		LOG_DBG("LTE cell changed: Cell ID: %d, Tracking area: %d",
			evt->cell.id, evt->cell.tac);
		break;
	default:
		break;
	}
}

#if defined(CONFIG_EXTERNAL_SENSORS)
static void ext_sensors_evt_handler(const struct ext_sensor_evt *const evt)
{
	switch (evt->type) {
	case EXT_SENSOR_EVT_ACCELEROMETER_TRIGGER:
		if (!cfg.act) {
			accelerometer_buffer_populate(evt);
			k_sem_give(&accel_trig_sem);
		}
		break;
	default:
		break;
	}
}
#endif

static int modem_configure(void)
{
	int err;

	LOG_INF("Configuring the modem...");

	err = lte_lc_init_and_connect_async(lte_evt_handler);
	if (err) {
		LOG_ERR("lte_lc_init_connect_async, error: %d", err);
	}

	return err;
}

static void device_config_get(void)
{
	int err;

	struct cloud_msg msg = { .qos = CLOUD_QOS_AT_MOST_ONCE,
				 .endpoint.type = CLOUD_EP_TOPIC_STATE,
				 .buf = "",
				 .len = 0 };

	err = cloud_send(cloud_backend, &msg);
	if (err) {
		LOG_ERR("Cloud send failed, err: %d", err);
	}
}

static void ui_send(void)
{
	int err;
	struct cloud_codec_data codec;

	ui_led_set_pattern(UI_CLOUD_PUBLISHING);

	err = cloud_codec_encode_ui_data(&codec, &ui_buf[head_ui_buf]);
	if (err) {
		LOG_ERR("cloud_codec_encode_ui_data, error: %d", err);
		return;
	}

	struct cloud_msg msg = { .qos = CLOUD_QOS_AT_MOST_ONCE,
				 .endpoint = pub_ep_topics_sub[1],
				 .buf = codec.buf,
				 .len = codec.len };

	err = cloud_send(cloud_backend, &msg);
	cloud_codec_release_data(&codec);
	if (err) {
		LOG_ERR("Cloud send failed, err: %d", err);
	}
}

static void device_config_send(void)
{
	int err;
	struct cloud_codec_data codec;

	err = cloud_codec_encode_cfg_data(&codec, &cfg);
	if (err == -EAGAIN) {
		LOG_DBG("No change in device configuration");
		return;
	} else if (err) {
		LOG_ERR("Device configuration not encoded, error: %d", err);
		return;
	}

	struct cloud_msg msg = { .qos = CLOUD_QOS_AT_MOST_ONCE,
				 .endpoint.type = CLOUD_EP_TOPIC_MSG,
				 .buf = codec.buf,
				 .len = codec.len };

	err = cloud_send(cloud_backend, &msg);
	cloud_codec_release_data(&codec);
	if (err) {
		LOG_ERR("Cloud send failed, err: %d", err);
	}
}

static void data_send(void)
{
	int err;
	struct cloud_codec_data codec;

	err = cloud_codec_encode_data(
		&codec, &gps_buf[head_gps_buf], &sensors_buf[head_sensor_buf],
		&modem_buf[head_modem_buf], &ui_buf[head_ui_buf],
		&accel_buf[head_accel_buf], &bat_buf[head_bat_buf]);
	if (err) {
		LOG_ERR("Error enconding message %d", err);
		return;
	}

	struct cloud_msg msg = { .qos = CLOUD_QOS_AT_MOST_ONCE,
				 .endpoint.type = CLOUD_EP_TOPIC_MSG,
				 .buf = codec.buf,
				 .len = codec.len };

	err = cloud_send(cloud_backend, &msg);
	cloud_codec_release_data(&codec);
	if (err) {
		LOG_ERR("Cloud send failed, err: %d", err);
		return;
	}
}

static void buffered_data_send(void)
{
	int err;
	bool queued_entries = false;
	struct cloud_codec_data codec;

	struct cloud_msg msg = {
		.qos = CLOUD_QOS_AT_MOST_ONCE,
		.endpoint = pub_ep_topics_sub[0],
	};

check_gps_buffer:

	/* Check if it exists queued entries in the gps buffer. */
	for (int i = 0; i < CONFIG_GPS_BUFFER_MAX; i++) {
		if (gps_buf[i].queued) {
			queued_entries = true;
			break;
		} else {
			queued_entries = false;
		}
	}

	if (queued_entries) {
		/* Encode and send queued entries in batches. */
		err = cloud_codec_encode_gps_buffer(&codec, gps_buf);
		if (err) {
			LOG_ERR("Error encoding GPS buffer: %d", err);
			return;
		}

		msg.buf = codec.buf;
		msg.len = codec.len;

		err = cloud_send(cloud_backend, &msg);
		cloud_codec_release_data(&codec);
		if (err) {
			LOG_ERR("Cloud send failed, err: %d", err);
			return;
		}

		goto check_gps_buffer;
	}

check_sensors_buffer:

	/* Check if it exists queued entries in the gps buffer. */
	for (int i = 0; i < CONFIG_SENSOR_BUFFER_MAX; i++) {
		if (sensors_buf[i].queued) {
			queued_entries = true;
			break;
		} else {
			queued_entries = false;
		}
	}

	if (queued_entries) {
		/* Encode and send queued entries in batches. */
		err = cloud_codec_encode_sensor_buffer(&codec, sensors_buf);
		if (err) {
			LOG_ERR("Error encoding sensors buffer: %d", err);
			return;
		}

		msg.buf = codec.buf;
		msg.len = codec.len;

		err = cloud_send(cloud_backend, &msg);
		cloud_codec_release_data(&codec);
		if (err) {
			LOG_ERR("Cloud send failed, err: %d", err);
			return;
		}

		goto check_sensors_buffer;
	}

check_modem_buffer:

	/* Check if it exists queued entries in the gps buffer. */
	for (int i = 0; i < CONFIG_MODEM_BUFFER_MAX; i++) {
		if (modem_buf[i].queued) {
			queued_entries = true;
			break;
		} else {
			queued_entries = false;
		}
	}

	if (queued_entries) {
		/* Encode and send queued entries in batches. */
		err = cloud_codec_encode_modem_buffer(&codec, modem_buf);
		if (err) {
			LOG_ERR("Error encoding modem buffer: %d", err);
			return;
		}

		msg.buf = codec.buf;
		msg.len = codec.len;

		err = cloud_send(cloud_backend, &msg);
		cloud_codec_release_data(&codec);
		if (err) {
			LOG_ERR("Cloud send failed, err: %d", err);
			return;
		}

		goto check_modem_buffer;
	}

check_ui_buffer:

	/* Check if it exists queued entries in the gps buffer. */
	for (int i = 0; i < CONFIG_UI_BUFFER_MAX; i++) {
		if (ui_buf[i].queued) {
			queued_entries = true;
			break;
		} else {
			queued_entries = false;
		}
	}

	if (queued_entries) {
		/* Encode and send queued entries in batches. */
		err = cloud_codec_encode_ui_buffer(&codec, ui_buf);
		if (err) {
			LOG_ERR("Error encoding modem buffer: %d", err);
			return;
		}

		msg.buf = codec.buf;
		msg.len = codec.len;

		err = cloud_send(cloud_backend, &msg);
		cloud_codec_release_data(&codec);
		if (err) {
			LOG_ERR("Cloud send failed, err: %d", err);
			return;
		}

		goto check_ui_buffer;
	}

check_accel_buffer:

	/* Check if it exists queued entries in the gps buffer. */
	for (int i = 0; i < CONFIG_ACCEL_BUFFER_MAX; i++) {
		if (accel_buf[i].queued) {
			queued_entries = true;
			break;
		} else {
			queued_entries = false;
		}
	}

	/** Only publish buffered accelerometer data if in
	 * passive device mode.
	 */
	if (queued_entries && !cfg.act) {
		/* Encode and send queued entries in batches. */
		err = cloud_codec_encode_accel_buffer(&codec, accel_buf);
		if (err) {
			LOG_ERR("Error encoding accelerometer buffer: %d", err);
			return;
		}

		msg.buf = codec.buf;
		msg.len = codec.len;

		err = cloud_send(cloud_backend, &msg);
		cloud_codec_release_data(&codec);
		if (err) {
			LOG_ERR("Cloud send failed, err: %d", err);
			return;
		}

		goto check_accel_buffer;
	}

check_battery_buffer:

	/* Check if it exists queued entries in the gps buffer. */
	for (int i = 0; i < CONFIG_BAT_BUFFER_MAX; i++) {
		if (bat_buf[i].queued) {
			queued_entries = true;
			break;
		} else {
			queued_entries = false;
		}
	}

	if (queued_entries) {
		/* Encode and send queued entries in batches. */
		err = cloud_codec_encode_bat_buffer(&codec, bat_buf);
		if (err) {
			LOG_ERR("Error encoding accelerometer buffer: %d", err);
			return;
		}

		msg.buf = codec.buf;
		msg.len = codec.len;

		err = cloud_send(cloud_backend, &msg);
		cloud_codec_release_data(&codec);
		if (err) {
			LOG_ERR("Cloud send failed, err: %d", err);
			return;
		}

		goto check_battery_buffer;
	}
}

static void config_get(void)
{
	ui_led_set_pattern(UI_CLOUD_PUBLISHING);

	/** Sample data from modem and environmental sensor before
	 *  cloud publication.
	 */
	k_delayed_work_submit(&sample_data_work, K_NO_WAIT);

	if (cloud_connected) {
		k_delayed_work_submit(&device_config_get_work, K_NO_WAIT);
		k_delayed_work_submit(&device_config_send_work, K_NO_WAIT);
		k_delayed_work_submit(&data_send_work, K_NO_WAIT);
		k_delayed_work_submit(&buffered_data_send_work, K_NO_WAIT);
	} else {
		LOG_INF("Not connected to cloud!");
	}
}

static void data_publish(void)
{
	ui_led_set_pattern(UI_CLOUD_PUBLISHING);

	/** Sample data from modem and environmental sensor before
	 *  cloud publication.
	 */
	k_delayed_work_submit(&sample_data_work, K_NO_WAIT);

	if (cloud_connected) {
		k_delayed_work_submit(&data_send_work, K_NO_WAIT);
		k_delayed_work_submit(&buffered_data_send_work, K_NO_WAIT);
	} else {
		LOG_INF("Not connected to cloud!");
	}
}

static void cloud_connect_work_fn(struct k_work *work)
{
	int err;

	err = cloud_connect(cloud_backend);
	if (err) {
		LOG_ERR("cloud_connect failed: %d", err);
	}

	LOG_DBG("Re-connection to cloud in progress");
	LOG_DBG("New reconnection attempt in %d seconds",
		CLOUD_RECONNECTION_INTERVAL);

	/* Try to reconnect to cloud every 30 seconds. */
	k_delayed_work_submit(&cloud_connect_work,
			      K_SECONDS(CLOUD_RECONNECTION_INTERVAL));
}

static void sample_data_work_fn(struct k_work *work)
{
	int err;

	err = modem_buffer_populate();
	if (err) {
		LOG_ERR("modem_buffer_populate, error: %d", err);
		return;
	}

	battery_buffer_populate();

#if defined(CONFIG_EXTERNAL_SENSORS)
	err = sensors_buffer_populate();
	if (err) {
		LOG_ERR("sensors_buffer_populate, error: %d", err);
		return;
	}
#endif
}

static void leds_set_work_fn(struct k_work *work)
{
	leds_set();
}

static void device_config_get_work_fn(struct k_work *work)
{
	device_config_get();
}

static void device_config_send_work_fn(struct k_work *work)
{
	device_config_send();
}

static void data_send_work_fn(struct k_work *work)
{
	data_send();
}

static void buffered_data_send_work_fn(struct k_work *work)
{
	buffered_data_send();
}

static void ui_send_work_fn(struct k_work *work)
{
	ui_send();
}

static void mov_timeout_work_fn(struct k_work *work)
{
	if (!cfg.act) {
		LOG_INF("Movement timeout triggered");
		k_sem_give(&accel_trig_sem);
	}

	k_delayed_work_submit(&mov_timeout_work, K_SECONDS(cfg.movt));
}

static void work_init(void)
{
	k_delayed_work_init(&device_config_get_work, device_config_get_work_fn);
	k_delayed_work_init(&data_send_work, data_send_work_fn);
	k_delayed_work_init(&device_config_send_work,
			    device_config_send_work_fn);
	k_delayed_work_init(&buffered_data_send_work,
			    buffered_data_send_work_fn);
	k_delayed_work_init(&leds_set_work, leds_set_work_fn);
	k_delayed_work_init(&mov_timeout_work, mov_timeout_work_fn);
	k_delayed_work_init(&ui_send_work, ui_send_work_fn);
	k_delayed_work_init(&sample_data_work, sample_data_work_fn);
	k_delayed_work_init(&cloud_connect_work, cloud_connect_work_fn);
}

static void gps_trigger_handler(const struct device *dev, struct gps_event *evt)
{
	switch (evt->type) {
	case GPS_EVT_SEARCH_STARTED:
		LOG_INF("GPS_EVT_SEARCH_STARTED");
		break;
	case GPS_EVT_SEARCH_STOPPED:
		LOG_INF("GPS_EVT_SEARCH_STOPPED");
		break;
	case GPS_EVT_SEARCH_TIMEOUT:
		LOG_INF("GPS_EVT_SEARCH_TIMEOUT");
		k_sem_give(&gps_timeout_sem);
		break;
	case GPS_EVT_PVT:
		/* Don't spam logs */
		break;
	case GPS_EVT_PVT_FIX:
		LOG_INF("GPS_EVT_PVT_FIX");
		gps_time_set(&evt->pvt);
		gps_buffer_populate(&evt->pvt);
		gps_fix = true;
		k_sem_give(&gps_timeout_sem);
		break;
	case GPS_EVT_NMEA:
		/* Don't spam logs */
		break;
	case GPS_EVT_NMEA_FIX:
		LOG_INF("Position fix with NMEA data");
		break;
	case GPS_EVT_OPERATION_BLOCKED:
		LOG_INF("GPS_EVT_OPERATION_BLOCKED");
		break;
	case GPS_EVT_OPERATION_UNBLOCKED:
		LOG_INF("GPS_EVT_OPERATION_UNBLOCKED");
		ui_led_set_pattern(UI_LED_GPS_SEARCHING);
		break;
	case GPS_EVT_AGPS_DATA_NEEDED:
		LOG_INF("GPS_EVT_AGPS_DATA_NEEDED");
		break;
	case GPS_EVT_ERROR:
		LOG_INF("GPS_EVT_ERROR\n");
		break;
	default:
		break;
	}
}

void cloud_event_handler(const struct cloud_backend *const backend,
			 const struct cloud_event *const evt, void *user_data)
{
	ARG_UNUSED(user_data);

	int err;
	static int mov_timeout_prev;

	switch (evt->type) {
	case CLOUD_EVT_CONNECTING:
		LOG_INF("CLOUD_EVT_CONNECTING");
		break;
	case CLOUD_EVT_CONNECTED:
		LOG_INF("CLOUD_EVT_CONNECTED");
		cloud_connected = true;
		config_get();
		boot_write_img_confirmed();
		k_delayed_work_cancel(&cloud_connect_work);
		break;
	case CLOUD_EVT_READY:
		LOG_INF("CLOUD_EVT_READY");
		err = lte_lc_psm_req(true);
		if (err) {
			LOG_ERR("PSM request failed, error: %d", err);
		} else {
			LOG_INF("PSM enabled");
		}
		break;
	case CLOUD_EVT_DISCONNECTED:
		LOG_INF("CLOUD_EVT_DISCONNECTED");
		cloud_connected = false;
		k_delayed_work_submit(&cloud_connect_work, K_NO_WAIT);
		break;
	case CLOUD_EVT_ERROR:
		LOG_ERR("CLOUD_EVT_ERROR");
		break;
	case CLOUD_EVT_FOTA_START:
		LOG_INF("CLOUD_EVT_FOTA_START");
		break;
	case CLOUD_EVT_FOTA_ERASE_PENDING:
		LOG_INF("CLOUD_EVT_FOTA_ERASE_PENDING");
		break;
	case CLOUD_EVT_FOTA_ERASE_DONE:
		LOG_INF("CLOUD_EVT_FOTA_ERASE_DONE");
		break;
	case CLOUD_EVT_FOTA_DONE:
		LOG_INF("CLOUD_EVT_FOTA_DONE");
		cloud_disconnect(cloud_backend);
		sys_reboot(0);
		break;
	case CLOUD_EVT_DATA_SENT:
		LOG_DBG("CLOUD_EVT_DATA_SENT");
		break;
	case CLOUD_EVT_DATA_RECEIVED:
		LOG_DBG("CLOUD_EVT_DATA_RECEIVED");
		err = cloud_codec_decode_response(evt->data.msg.buf, &cfg);
		if (err) {
			LOG_ERR("Could not decode response %d", err);
		}
		/* Set new accelerometer threshold and GPS timeout. */
		gps_cfg.timeout = cfg.gpst;
		ext_sensors_mov_thres_set(cfg.acct);
		k_delayed_work_submit(&device_config_send_work, K_NO_WAIT);

		/* Start movement timer which triggers every movement timeout.
		 * Makes sure the device publishes every once and a while even
		 * though the device is in passive mode and movement is not
		 * detected. Schedule new timeout only if the movement timeout
		 * has changed.
		 */

		if (cfg.movt != mov_timeout_prev) {
			LOG_INF("Schedueling movement timeout in %d seconds",
				cfg.movt);
			k_delayed_work_submit(&mov_timeout_work,
					      K_SECONDS(cfg.movt));
			mov_timeout_prev = cfg.movt;
		}

		break;
	case CLOUD_EVT_PAIR_REQUEST:
		LOG_DBG("CLOUD_EVT_PAIR_REQUEST");
		break;
	case CLOUD_EVT_PAIR_DONE:
		LOG_DBG("CLOUD_EVT_PAIR_DONE");
		break;
	default:
		LOG_ERR("Unknown cloud event type: %d", evt->type);
		break;
	}
}

static void modem_rsrp_handler(char rsrp_value)
{
	/* RSRP raw values that represent actual signal strength are
	 * 0 through 97 (per "nRF91 AT Commands" v1.1).
	 */

	if (rsrp_value > 97) {
		return;
	}

	/* Set temporary variable to hold RSRP value. RSRP callbacks and other
	 * data from the modem info module are retrieved separately.
	 * This temporarily saves the latest value which are sent to cloud upon
	 * a cloud publication.
	 */

	rsrp_value_latest = rsrp_value;

	LOG_DBG("Incoming RSRP status message, RSRP value is %d",
		rsrp_value_latest);
}

static int modem_data_init(void)
{
	int err;

	err = modem_info_init();
	if (err) {
		LOG_INF("modem_info_init, error: %d", err);
		return err;
	}

	err = modem_info_params_init(&modem_param);
	if (err) {
		LOG_INF("modem_info_params_init, error: %d", err);
		return err;
	}

	err = modem_info_rsrp_register(modem_rsrp_handler);
	if (err) {
		LOG_INF("modem_info_rsrp_register, error: %d", err);
		return err;
	}

	return 0;
}

static void button_handler(uint32_t button_states, uint32_t has_changed)
{
	static int try_again_timeout;

	/* Publication of data due to button presses limited
	 * to 1 push every 2 seconds to avoid spamming the cloud socket.
	 */
	if ((has_changed & button_states & DK_BTN1_MSK) &&
	    k_uptime_get() - try_again_timeout > 2 * 1000) {
		LOG_INF("Cloud publication by button 1 triggered, ");
		LOG_INF("2 seconds to next allowed cloud publication ");
		LOG_INF("triggered by button 1");

		ui_buffer_populate(1);

		if (cloud_connected) {
			k_delayed_work_submit(&ui_send_work, K_NO_WAIT);
			k_delayed_work_submit(&leds_set_work, K_SECONDS(3));
		} else {
			LOG_INF("Not connected to cloud!");
		}

		try_again_timeout = k_uptime_get();
	}

#if defined(CONFIG_BOARD_NRF9160_PCA10090NS)
	/* Fake motion. The nRF9160 DK does not have an accelerometer by
	 * default. Reset accelerometer data.
	 */
	if (has_changed & button_states & DK_BTN2_MSK) {
		k_sem_give(&accel_trig_sem);
	}
#endif
}

static int populate_app_endpoint_topics()
{
	int err;

	err = snprintf(batch_topic, sizeof(batch_topic), BATCH_TOPIC,
		       client_id_buf);
	if (err != BATCH_TOPIC_LEN) {
		return -ENOMEM;
	}

	pub_ep_topics_sub[0].str = batch_topic;
	pub_ep_topics_sub[0].len = BATCH_TOPIC_LEN;
	pub_ep_topics_sub[0].type = CLOUD_EP_TOPIC_BATCH;

	err = snprintf(messages_topic, sizeof(messages_topic), MESSAGES_TOPIC,
		       client_id_buf);
	if (err != MESSAGES_TOPIC_LEN) {
		return -ENOMEM;
	}

	pub_ep_topics_sub[1].str = messages_topic;
	pub_ep_topics_sub[1].len = MESSAGES_TOPIC_LEN;
	pub_ep_topics_sub[1].type = CLOUD_EP_TOPIC_MESSAGES;

	err = snprintf(cfg_topic, sizeof(cfg_topic), CFG_TOPIC, client_id_buf);
	if (err != CFG_TOPIC_LEN) {
		return -ENOMEM;
	}

	sub_ep_topics_sub[0].str = cfg_topic;
	sub_ep_topics_sub[0].len = CFG_TOPIC_LEN;
	sub_ep_topics_sub[0].type = CLOUD_EP_TOPIC_CONFIG;

	err = cloud_ep_subscriptions_add(cloud_backend, sub_ep_topics_sub,
					 ARRAY_SIZE(sub_ep_topics_sub));
	if (err) {
		LOG_INF("cloud_ep_subscriptions_add, error: %d", err);
		error_handler(err);
	}

	return 0;
}

static int cloud_setup(void)
{
	int err;

	cloud_backend = cloud_get_binding(CONFIG_CLOUD_BACKEND);
	__ASSERT(cloud_backend != NULL, "%s cloud backend not found",
		 CONFIG_CLOUD_BACKEND);

#if !defined(CONFIG_USE_CUSTOM_MQTT_CLIENT_ID)
	/* Fetch IMEI from modem data and set IMEI as cloud connection ID **/
	err = modem_info_string_get(MODEM_INFO_IMEI, client_id_buf,
				    sizeof(client_id_buf));
	if (err != AWS_CLOUD_CLIENT_ID_LEN) {
		LOG_ERR("modem_info_string_get, error: %d", err);
		return err;
	}
#else
	snprintf(client_id_buf, sizeof(client_id_buf), "%s",
		 CONFIG_MQTT_CLIENT_ID);
#endif
	cloud_backend->config->id = client_id_buf;
	cloud_backend->config->id_len = sizeof(client_id_buf);

	err = cloud_init(cloud_backend, cloud_event_handler);
	if (err) {
		LOG_ERR("cloud_init, error: %d", err);
		return err;
	}

	/* Populate cloud specific endpoint topics */
	err = populate_app_endpoint_topics();
	if (err) {
		LOG_ERR("populate_app_endpoint_topics, error: %d", err);
		return err;
	}

	return err;
}

static void date_time_event_handler(const struct date_time_evt *evt)
{
	switch (evt->type) {
	case DATE_TIME_OBTAINED_MODEM:
		LOG_DBG("DATE_TIME_OBTAINED_MODEM");
		break;
	case DATE_TIME_OBTAINED_NTP:
		LOG_DBG("DATE_TIME_OBTAINED_NTP");
		break;
	case DATE_TIME_OBTAINED_EXT:
		LOG_DBG("DATE_TIME_OBTAINED_EXT");
		break;
	case DATE_TIME_NOT_OBTAINED:
		LOG_WRN("DATE_TIME_NOT_OBTAINED");
		break;
	default:
		break;
	}

	/* Do not depend on obtained time, continue upon any event from the
	 * date time library.
	 */
	k_sem_give(&date_time_sem);
}

static int gps_setup(void)
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

void main(void)
{
	int err;

#if defined(CONFIG_WATCHDOG)
	err = watchdog_init_and_start();
	if (err) {
		LOG_INF("watchdog_init_and_start, error: %d", err);
		error_handler(err);
	}
#endif

	work_init();

#if defined(CONFIG_EXTERNAL_SENSORS)
	err = ext_sensors_init(ext_sensors_evt_handler);
	if (err) {
		LOG_INF("ext_sensors_init, error: %d", err);
		error_handler(err);
	}
#endif
	err = dk_buttons_init(button_handler);
	if (err) {
		LOG_INF("dk_buttons_init, error: %d", err);
		error_handler(err);
	}

	err = modem_data_init();
	if (err) {
		LOG_INF("modem_data_init, error: %d", err);
		error_handler(err);
	}

	err = cloud_setup();
	if (err) {
		LOG_INF("cloud_setup, error %d", err);
		error_handler(err);
	}

	LOG_INF("********************************************");
	LOG_INF(" The cat tracker has started");
	LOG_INF(" Version:     %s", log_strdup(CONFIG_CAT_TRACKER_APP_VERSION));
	LOG_INF(" Client ID:   %s", log_strdup(client_id_buf));
	LOG_INF(" Endpoint:    %s",
		log_strdup(CONFIG_AWS_IOT_BROKER_HOST_NAME));
	LOG_INF("********************************************");

	err = ui_init();
	if (err) {
		LOG_INF("ui_init, error: %d", err);
		error_handler(err);
	}

	err = gps_setup();
	if (err) {
		LOG_INF("gps_setup, error: %d", err);
		error_handler(err);
	}

	err = modem_configure();
	if (err) {
		LOG_INF("modem_configure, error: %d", err);
		error_handler(err);
	}

	k_sem_take(&lte_conn_sem, K_FOREVER);

	date_time_update_async(date_time_event_handler);

	err = k_sem_take(&date_time_sem, K_SECONDS(DATE_TIME_TIMEOUT_S));
	if (err) {
		LOG_WRN("Date time, no callback event within %d seconds",
			DATE_TIME_TIMEOUT_S);
	}

	k_delayed_work_submit(&cloud_connect_work, K_NO_WAIT);

	while (true) {
		/*Check current device mode*/
		if (!cfg.act) {
			LOG_INF("Device in PASSIVE mode");
			k_delayed_work_submit(&leds_set_work, K_NO_WAIT);
			if (!k_sem_take(&accel_trig_sem, K_FOREVER)) {
				LOG_INF("The cat is moving!");
				LOG_INF("Or it's lazy and this is just the movement timeout!");
			}
		} else {
			LOG_INF("Device in ACTIVE mode");
		}

		/** Start GPS search, disable GPS if gpst is set to 0. */
		if (cfg.gpst > 0) {
			gps_start(gps_dev, &gps_cfg);
			if (err) {
				LOG_ERR("Failed to enable GPS, error: %d", err);
				error_handler(err);
			}

			/*Wait for GPS search timeout*/
			k_sem_take(&gps_timeout_sem, K_FOREVER);
		}

		err = gps_stop(gps_dev);
		if (err) {
			LOG_ERR("Failed to stop GPS, error: %d", err);
			error_handler(err);
		}

		/*Send update to cloud. */
		data_publish();

		/* Set device mode led behaviour */
		k_delayed_work_submit(&leds_set_work, K_SECONDS(15));

		/*Sleep*/
		LOG_INF("Going to sleep for: %d seconds", device_mode_check());
		k_sleep(K_SECONDS(device_mode_check()));
	}
}
