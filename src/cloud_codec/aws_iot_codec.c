/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <cloud_codec.h>
#include <stdbool.h>
#include <string.h>
#include <zephyr.h>
#include <zephyr/types.h>
#include <stdio.h>
#include <stdlib.h>
#include "cJSON.h"
#include "cJSON_os.h"
#include <date_time.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(cloud_codec, CONFIG_CAT_TRACKER_LOG_LEVEL);

#define MODEM_CURRENT_BAND	"band"
#define MODEM_NETWORK_MODE	"nw"
#define MODEM_ICCID		"iccid"
#define MODEM_FIRMWARE_VERSION	"modV"
#define MODEM_BOARD		"brdV"
#define MODEM_APP_VERSION	"appV"
#define MODEM_RSRP		"rsrp"
#define MODEM_AREA_CODE		"area"
#define MODEM_MCCMNC		"mccmnc"
#define MODEM_CELL_ID		"cell"
#define MODEM_IP_ADRESS		"ip"

#define CONFIG_DEVICE_MODE	"act"
#define CONFIG_ACTIVE_TIMEOUT	"actwt"
#define CONFIG_MOVEMENT_TIMEOUT "mvt"
#define CONFIG_MOVEMENT_RES	"mvres"
#define CONFIG_GPS_TIMEOUT	"gpst"
#define CONFIG_MOVEMENT_THRES	"acct"

#define OBJECT_CONFIG		"cfg"
#define OBJECT_REPORTED		"reported"
#define OBJECT_STATE		"state"
#define OBJECT_VALUE		"v"
#define OBJECT_TIMESTAMP	"ts"

#define DATA_MODEM_DYNAMIC	"roam"
#define DATA_MODEM_STATIC	"dev"
#define DATA_BATTERY		"bat"
#define DATA_TEMPERATURE	"temp"
#define DATA_HUMID		"hum"
#define DATA_ENVIRONMENTALS	"env"
#define DATA_BUTTON		"btn"

#define DATA_MOVEMENT		"acc"
#define DATA_MOVEMENT_X		"x"
#define DATA_MOVEMENT_Y		"y"
#define DATA_MOVEMENT_Z		"z"

#define DATA_GPS		"gps"
#define DATA_GPS_LONGITUDE	"lng"
#define DATA_GPS_LATITUDE	"lat"
#define DATA_GPS_ALTITUDE	"alt"
#define DATA_GPS_SPEED		"spd"
#define DATA_GPS_HEADING	"hdg"

static int json_add_obj(cJSON *parent, const char *str, cJSON *item)
{
	cJSON_AddItemToObject(parent, str, item);

	return 0;
}

static int json_add_obj_array(cJSON *parent, cJSON *item)
{
	cJSON_AddItemToArray(parent, item);

	return 0;
}

static int json_add_number(cJSON *parent, const char *str, double item)
{
	cJSON *json_num;

	json_num = cJSON_CreateNumber(item);
	if (json_num == NULL) {
		return -ENOMEM;
	}

	return json_add_obj(parent, str, json_num);
}

static int json_add_bool(cJSON *parent, const char *str, int item)
{
	cJSON *json_bool;

	json_bool = cJSON_CreateBool(item);
	if (json_bool == NULL) {
		return -ENOMEM;
	}

	return json_add_obj(parent, str, json_bool);
}

static cJSON *json_object_decode(cJSON *obj, const char *str)
{
	return obj ? cJSON_GetObjectItem(obj, str) : NULL;
}

static int json_add_str(cJSON *parent, const char *str, const char *item)
{
	cJSON *json_str;

	json_str = cJSON_CreateString(item);
	if (json_str == NULL) {
		return -ENOMEM;
	}

	return json_add_obj(parent, str, json_str);
}

void cloud_codec_init(void)
{
	cJSON_Init();
}

int cloud_codec_decode_config(char *input, struct cloud_data_cfg *data)
{
	char *string = NULL;
	cJSON *root_obj = NULL;
	cJSON *group_obj = NULL;
	cJSON *subgroup_obj = NULL;
	cJSON *gpst = NULL;
	cJSON *active = NULL;
	cJSON *active_wait = NULL;
	cJSON *passive_wait = NULL;
	cJSON *movt = NULL;
	cJSON *acc_thres = NULL;

	if (input == NULL) {
		return -EINVAL;
	}

	root_obj = cJSON_Parse(input);
	if (root_obj == NULL) {
		return -ENOENT;
	}

	string = cJSON_Print(root_obj);
	if (string == NULL) {
		LOG_ERR("Failed to print message.");
		goto exit;
	}

	LOG_DBG("Decoded message: %s", log_strdup(string));

	group_obj = json_object_decode(root_obj, OBJECT_CONFIG);
	if (group_obj != NULL) {
		subgroup_obj = group_obj;
		goto get_data;
	}

	group_obj = json_object_decode(root_obj, OBJECT_STATE);
	if (group_obj == NULL) {
		goto exit;
	}

	subgroup_obj = json_object_decode(group_obj, OBJECT_CONFIG);
	if (subgroup_obj == NULL) {
		goto exit;
	}

get_data:

	gpst = cJSON_GetObjectItem(subgroup_obj, CONFIG_GPS_TIMEOUT);
	active = cJSON_GetObjectItem(subgroup_obj, CONFIG_DEVICE_MODE);
	active_wait = cJSON_GetObjectItem(subgroup_obj, CONFIG_ACTIVE_TIMEOUT);
	passive_wait = cJSON_GetObjectItem(subgroup_obj, CONFIG_MOVEMENT_RES);
	movt = cJSON_GetObjectItem(subgroup_obj, CONFIG_MOVEMENT_TIMEOUT);
	acc_thres = cJSON_GetObjectItem(subgroup_obj, CONFIG_MOVEMENT_THRES);

	if (gpst != NULL) {
		data->gpst = gpst->valueint;
	}

	if (active != NULL) {
		data->act = active->valueint;
	}

	if (active_wait != NULL) {
		data->actw = active_wait->valueint;
	}

	if (passive_wait != NULL) {
		data->pasw = passive_wait->valueint;
	}

	if (movt != NULL) {
		data->movt = movt->valueint;
	}

	if (acc_thres != NULL) {
		data->acct = acc_thres->valueint;
	}
exit:
	cJSON_Delete(root_obj);
	return 0;
}

static int static_modem_data_add(cJSON *parent, struct cloud_data_modem *data)
{
	int err = 0;
	char nw_mode[50];

	static const char lte_string[] = "LTE-M";
	static const char nbiot_string[] = "NB-IoT";
	static const char gps_string[] = " GPS";

	if (!data->queued) {
		LOG_DBG("Head of modem buffer not indexing a queued entry");
		goto exit;
	}

	err = date_time_uptime_to_unix_time_ms(&data->mod_ts_static);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	cJSON *static_m = cJSON_CreateObject();
	cJSON *static_m_v = cJSON_CreateObject();

	if (static_m == NULL || static_m_v == NULL) {
		cJSON_Delete(static_m);
		cJSON_Delete(static_m_v);
		return -ENOMEM;
	}

	if (data->nw_lte_m) {
		strcpy(nw_mode, lte_string);
	} else if (data->nw_nb_iot) {
		strcpy(nw_mode, nbiot_string);
	}

	if (data->nw_gps) {
		strcat(nw_mode, gps_string);
	}

	err += json_add_number(static_m_v, MODEM_CURRENT_BAND, data->bnd);
	err += json_add_str(static_m_v, MODEM_NETWORK_MODE, nw_mode);
	err += json_add_str(static_m_v, MODEM_ICCID, data->iccid);
	err += json_add_str(static_m_v, MODEM_FIRMWARE_VERSION, data->fw);
	err += json_add_str(static_m_v, MODEM_BOARD, data->brdv);
	err += json_add_str(static_m_v, MODEM_APP_VERSION, data->appv);

	err += json_add_obj(static_m, OBJECT_VALUE, static_m_v);
	err += json_add_number(static_m, OBJECT_TIMESTAMP, data->mod_ts_static);
	err += json_add_obj(parent, DATA_MODEM_STATIC, static_m);

	if (err) {
		return err;
	}

	data->queued = false;

exit:
	return 0;
}

static int dynamic_modem_data_add(cJSON *parent, struct cloud_data_modem *data,
				  bool batch_entry)
{
	int err = 0;
	long mccmnc;

	if (!data->queued) {
		LOG_DBG("Head of modem buffer not indexing a queued entry");
		goto exit;
	}

	err = date_time_uptime_to_unix_time_ms(&data->mod_ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	cJSON *dynamic_m = cJSON_CreateObject();
	cJSON *dynamic_m_v = cJSON_CreateObject();

	if (dynamic_m == NULL || dynamic_m_v == NULL) {
		cJSON_Delete(dynamic_m);
		cJSON_Delete(dynamic_m_v);
		return -ENOMEM;
	}

	mccmnc = strtol(data->mccmnc, NULL, 10);

	err += json_add_number(dynamic_m_v, MODEM_RSRP, data->rsrp);
	err += json_add_number(dynamic_m_v, MODEM_AREA_CODE, data->area);
	err += json_add_number(dynamic_m_v, MODEM_MCCMNC, mccmnc);
	err += json_add_number(dynamic_m_v, MODEM_CELL_ID, data->cell);
	err += json_add_str(dynamic_m_v, MODEM_IP_ADRESS, data->ip);

	err += json_add_obj(dynamic_m, OBJECT_VALUE, dynamic_m_v);
	err += json_add_number(dynamic_m, OBJECT_TIMESTAMP, data->mod_ts);

	if (batch_entry) {
		err += json_add_obj_array(parent, dynamic_m);
	} else {
		err += json_add_obj(parent, DATA_MODEM_DYNAMIC, dynamic_m);
	}

	if (err) {
		return err;
	}

	data->queued = false;

exit:
	return 0;
}

static int sensor_data_add(cJSON *parent, struct cloud_data_sensors *data,
			   bool batch_entry)
{
	int err = 0;

	if (!data->queued) {
		LOG_DBG("Head of sensor buffer not indexing a queued entry");
		goto exit;
	}

	err = date_time_uptime_to_unix_time_ms(&data->env_ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	cJSON *sensor_obj = cJSON_CreateObject();
	cJSON *sensor_val_obj = cJSON_CreateObject();

	if (sensor_obj == NULL || sensor_val_obj == NULL) {
		cJSON_Delete(sensor_obj);
		cJSON_Delete(sensor_val_obj);
		return -ENOMEM;
	}

	err = json_add_number(sensor_val_obj, DATA_TEMPERATURE, data->temp);
	err += json_add_number(sensor_val_obj, DATA_HUMID, data->hum);
	err += json_add_obj(sensor_obj, OBJECT_VALUE, sensor_val_obj);
	err += json_add_number(sensor_obj, OBJECT_TIMESTAMP, data->env_ts);

	if (batch_entry) {
		err += json_add_obj_array(parent, sensor_obj);
	} else {
		err += json_add_obj(parent, DATA_ENVIRONMENTALS, sensor_obj);
	}

	if (err) {
		return err;
	}

	data->queued = false;

exit:
	return 0;
}

static int gps_data_add(cJSON *parent, struct cloud_data_gps *data,
			bool batch_entry)
{
	int err = 0;

	if (!data->queued) {
		LOG_DBG("Head of gps buffer not indexing a queued entry");
		goto exit;
	}

	err = date_time_uptime_to_unix_time_ms(&data->gps_ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	cJSON *gps_obj = cJSON_CreateObject();
	cJSON *gps_val_obj = cJSON_CreateObject();

	if (gps_obj == NULL || gps_val_obj == NULL) {
		cJSON_Delete(gps_obj);
		cJSON_Delete(gps_val_obj);
		return -ENOMEM;
	}

	err += json_add_number(gps_val_obj, DATA_GPS_LONGITUDE, data->longi);
	err += json_add_number(gps_val_obj, DATA_GPS_LATITUDE, data->lat);
	err += json_add_number(gps_val_obj, DATA_MOVEMENT, data->acc);
	err += json_add_number(gps_val_obj, DATA_GPS_ALTITUDE, data->alt);
	err += json_add_number(gps_val_obj, DATA_GPS_SPEED, data->spd);
	err += json_add_number(gps_val_obj, DATA_GPS_HEADING, data->hdg);

	err += json_add_obj(gps_obj, OBJECT_VALUE, gps_val_obj);
	err += json_add_number(gps_obj, OBJECT_TIMESTAMP, data->gps_ts);

	if (batch_entry) {
		err += json_add_obj_array(parent, gps_obj);
	} else {
		err += json_add_obj(parent, DATA_GPS, gps_obj);
	}

	if (err) {
		return err;
	}

	data->queued = false;

exit:
	return 0;
}

static int accel_data_add(cJSON *parent, struct cloud_data_accelerometer *data,
			  bool batch_entry)
{
	int err = 0;

	if (!data->queued) {
		LOG_DBG("Head of accel buffer not indexing a queued entry");
		goto exit;
	}

	err = date_time_uptime_to_unix_time_ms(&data->ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	cJSON *acc_obj = cJSON_CreateObject();
	cJSON *acc_v_obj = cJSON_CreateObject();

	if (acc_obj == NULL || acc_v_obj == NULL) {
		cJSON_Delete(acc_obj);
		cJSON_Delete(acc_v_obj);
		return -ENOMEM;
	}

	err += json_add_number(acc_v_obj, DATA_MOVEMENT_X, data->values[0]);
	err += json_add_number(acc_v_obj, DATA_MOVEMENT_Y, data->values[1]);
	err += json_add_number(acc_v_obj, DATA_MOVEMENT_Z, data->values[2]);

	err += json_add_obj(acc_obj, OBJECT_VALUE, acc_v_obj);
	err += json_add_number(acc_obj, OBJECT_TIMESTAMP, data->ts);

	if (batch_entry) {
		err += json_add_obj_array(parent, acc_obj);
	} else {
		err += json_add_obj(parent, DATA_MOVEMENT, acc_obj);
	}

	if (err) {
		return err;
	}

	data->queued = false;

exit:
	return 0;
}

static int ui_data_add(cJSON *parent, struct cloud_data_ui *data,
		       bool batch_entry)
{
	int err = 0;

	if (!data->queued) {
		LOG_DBG("Head of UI buffer not indexing a queued entry");
		goto exit;
	}

	err = date_time_uptime_to_unix_time_ms(&data->btn_ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	cJSON *btn_obj = cJSON_CreateObject();

	if (btn_obj == NULL) {
		cJSON_Delete(btn_obj);
		return -ENOMEM;
	}

	err += json_add_number(btn_obj, OBJECT_VALUE, data->btn);
	err += json_add_number(btn_obj, OBJECT_TIMESTAMP, data->btn_ts);

	if (batch_entry) {
		err += json_add_obj_array(parent, btn_obj);
	} else {
		err += json_add_obj(parent, DATA_BUTTON, btn_obj);
	}

	if (err) {
		return err;
	}

	data->queued = false;

exit:
	return 0;
}

static int bat_data_add(cJSON *parent, struct cloud_data_battery *data,
			bool batch_entry)
{
	int err = 0;

	if (!data->queued) {
		LOG_DBG("Head of battery buffer not indexing a queued entry");
		goto exit;
	}

	err = date_time_uptime_to_unix_time_ms(&data->bat_ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	cJSON *bat_obj = cJSON_CreateObject();

	if (bat_obj == NULL) {
		cJSON_Delete(bat_obj);
		return -ENOMEM;
	}

	err += json_add_number(bat_obj, OBJECT_VALUE, data->bat);
	err += json_add_number(bat_obj, OBJECT_TIMESTAMP, data->bat_ts);

	if (batch_entry) {
		err += json_add_obj_array(parent, bat_obj);
	} else {
		err += json_add_obj(parent, DATA_BATTERY, bat_obj);
	}

	if (err) {
		return err;
	}

	data->queued = false;

exit:
	return 0;
}

int cloud_codec_encode_config(struct cloud_codec_data *output,
			      struct cloud_data_cfg *data)
{
	int err = 0;
	char *buffer;

	cJSON *root_obj = cJSON_CreateObject();
	cJSON *state_obj = cJSON_CreateObject();
	cJSON *rep_obj = cJSON_CreateObject();
	cJSON *cfg_obj = cJSON_CreateObject();

	if (root_obj == NULL || state_obj == NULL || rep_obj == NULL ||
	    cfg_obj == NULL) {
		cJSON_Delete(root_obj);
		cJSON_Delete(state_obj);
		cJSON_Delete(rep_obj);
		cJSON_Delete(cfg_obj);
		return -ENOMEM;
	}

	err += json_add_bool(cfg_obj, CONFIG_DEVICE_MODE, data->act);
	err += json_add_number(cfg_obj, CONFIG_GPS_TIMEOUT, data->gpst);
	err += json_add_number(cfg_obj, CONFIG_ACTIVE_TIMEOUT, data->actw);
	err += json_add_number(cfg_obj, CONFIG_MOVEMENT_RES, data->pasw);
	err += json_add_number(cfg_obj, CONFIG_MOVEMENT_TIMEOUT, data->movt);
	err += json_add_number(cfg_obj, CONFIG_MOVEMENT_THRES, data->acct);

	err += json_add_obj(rep_obj, OBJECT_CONFIG, cfg_obj);
	err += json_add_obj(state_obj, OBJECT_REPORTED, rep_obj);
	err += json_add_obj(root_obj, OBJECT_STATE, state_obj);

	if (err) {
		goto exit;
	}

	buffer = cJSON_Print(root_obj);

	LOG_INF("Encoded message: %s\n", log_strdup(buffer));

	output->buf = buffer;
	output->len = strlen(buffer);

exit:
	cJSON_Delete(root_obj);
	return err;
}

int cloud_codec_encode_data(struct cloud_codec_data *output,
			    struct cloud_data_gps *gps_buf,
			    struct cloud_data_sensors *sensor_buf,
			    struct cloud_data_modem *modem_buf,
			    struct cloud_data_ui *ui_buf,
			    struct cloud_data_accelerometer *mov_buf,
			    struct cloud_data_battery *bat_buf)
{
	int err = 0;
	char *buffer;
	static bool initial_encode;
	bool data_encoded = false;

	cJSON *root_obj = cJSON_CreateObject();
	cJSON *state_obj = cJSON_CreateObject();
	cJSON *rep_obj = cJSON_CreateObject();

	if (root_obj == NULL || state_obj == NULL || rep_obj == NULL) {
		cJSON_Delete(root_obj);
		cJSON_Delete(state_obj);
		cJSON_Delete(rep_obj);
		return -ENOMEM;
	}

	if (bat_buf->queued) {
		err += bat_data_add(rep_obj, bat_buf, false);
		data_encoded = true;
	}

	if (modem_buf->queued) {
		/* The first time modem data is sent to cloud upon a connection
		 * we want to include static modem data.
		 */
		if (!initial_encode) {
			err += static_modem_data_add(rep_obj, modem_buf);
				initial_encode = true;
		}

		err += dynamic_modem_data_add(rep_obj, modem_buf, false);
		data_encoded = true;
	}

	if (sensor_buf->queued) {
		err += sensor_data_add(rep_obj, sensor_buf, false);
		data_encoded = true;
	}

	if (gps_buf->queued) {
		err += gps_data_add(rep_obj, gps_buf, false);
		data_encoded = true;
	}

	if (mov_buf->queued) {
		err += accel_data_add(rep_obj, mov_buf, false);
		data_encoded = true;
	}

	err += json_add_obj(state_obj, OBJECT_REPORTED, rep_obj);
	err += json_add_obj(root_obj, OBJECT_STATE, state_obj);

	/* Exit upon encoding errors or no data encoded. */
	if (err) {
		goto exit;
	}

	if (!data_encoded) {
		err = -ENODATA;
		LOG_DBG("No data to encode...");
		goto exit;
	}

	buffer = cJSON_Print(root_obj);

	LOG_INF("Encoded message: %s\n", log_strdup(buffer));

	output->buf = buffer;
	output->len = strlen(buffer);

exit:
	cJSON_Delete(root_obj);

	return err;
}

int cloud_codec_encode_ui_data(struct cloud_codec_data *output,
			       struct cloud_data_ui *ui_buf)
{
	int err = 0;
	char *buffer;

	cJSON *root_obj = cJSON_CreateObject();

	if (root_obj == NULL) {
		cJSON_Delete(root_obj);
		return -ENOMEM;
	}

	if (ui_buf->queued) {
		err += ui_data_add(root_obj, ui_buf, false);
	} else {
		goto exit;
	}

	if (err) {
		goto exit;
	}

	buffer = cJSON_Print(root_obj);

	LOG_INF("Encoded message: %s\n", log_strdup(buffer));

	output->buf = buffer;
	output->len = strlen(buffer);

exit:
	cJSON_Delete(root_obj);

	return err;
}

int cloud_codec_encode_batch_data(struct cloud_codec_data *output,
				  struct cloud_data_gps *gps_buf,
				  struct cloud_data_sensors *sensor_buf,
				  struct cloud_data_modem *modem_buf,
				  struct cloud_data_ui *ui_buf,
				  struct cloud_data_accelerometer *accel_buf,
				  struct cloud_data_battery *bat_buf,
				  size_t gps_buf_count,
				  size_t sensor_buf_count,
				  size_t modem_buf_count,
				  size_t ui_buf_count,
				  size_t accel_buf_count,
				  size_t bat_buf_count)
{
	int err = 0;
	char *buffer;
	bool data_encoded = false;

	cJSON *root_obj = cJSON_CreateObject();
	cJSON *gps_obj = cJSON_CreateArray();
	cJSON *sensor_obj = cJSON_CreateArray();
	cJSON *modem_obj = cJSON_CreateArray();
	cJSON *ui_obj = cJSON_CreateArray();
	cJSON *accel_obj = cJSON_CreateArray();
	cJSON *bat_obj = cJSON_CreateArray();

	if (root_obj == NULL || gps_obj == NULL || sensor_obj == NULL ||
	    modem_obj == NULL || ui_obj == NULL || accel_obj == NULL ||
	    bat_obj == NULL) {
		cJSON_Delete(root_obj);
		cJSON_Delete(gps_obj);
		cJSON_Delete(sensor_obj);
		cJSON_Delete(modem_obj);
		cJSON_Delete(ui_obj);
		cJSON_Delete(accel_obj);
		cJSON_Delete(bat_obj);
		return -ENOMEM;
	}

	/* GPS data */
	for (int i = 0; i < gps_buf_count; i++) {
		if (gps_buf[i].queued) {
			err += gps_data_add(gps_obj, &gps_buf[i], true);
		}
	}

	if (cJSON_GetArraySize(gps_obj) > 0) {
		err += json_add_obj(root_obj, DATA_GPS, gps_obj);
		data_encoded = true;
	} else {
		cJSON_Delete(gps_obj);
	}

	/* Environmental sensor data */
	for (int i = 0; i < sensor_buf_count; i++) {
		if (sensor_buf[i].queued) {
			err += sensor_data_add(sensor_obj,
					       &sensor_buf[i],
					       true);
		}
	}

	if (cJSON_GetArraySize(sensor_obj) > 0) {
		err += json_add_obj(root_obj, DATA_ENVIRONMENTALS, sensor_obj);
		data_encoded = true;
	} else {
		cJSON_Delete(sensor_obj);
	}

	/* UI data */
	for (int i = 0; i < ui_buf_count; i++) {
		if (ui_buf[i].queued) {
			err += ui_data_add(ui_obj, &ui_buf[i], true);
		}
	}

	if (cJSON_GetArraySize(ui_obj) > 0) {
		err += json_add_obj(root_obj, DATA_BUTTON, ui_obj);
		data_encoded = true;
	} else {
		cJSON_Delete(ui_obj);
	}

	/* Movement data */
	for (int i = 0; i < accel_buf_count; i++) {
		if (accel_buf[i].queued) {
			err += accel_data_add(accel_obj,
					      &accel_buf[i],
					      true);
		}
	}

	if (cJSON_GetArraySize(accel_obj) > 0) {
		err += json_add_obj(root_obj, DATA_MOVEMENT, accel_obj);
		data_encoded = true;
	} else {
		cJSON_Delete(accel_obj);
	}

	/* Battery data */
	for (int i = 0; i < bat_buf_count; i++) {
		if (bat_buf[i].queued) {
			err += bat_data_add(bat_obj, &bat_buf[i], true);
		}
	}

	if (cJSON_GetArraySize(bat_obj) > 0) {
		err += json_add_obj(root_obj, DATA_BATTERY, bat_obj);
		data_encoded = true;
	} else {
		cJSON_Delete(bat_obj);
	}

	/* Dynamic modem data */
	for (int i = 0; i < modem_buf_count; i++) {
		if (modem_buf[i].queued) {
			err += dynamic_modem_data_add(modem_obj,
						     &modem_buf[i],
						     true);
		}
	}

	if (cJSON_GetArraySize(modem_obj) > 0) {
		err += json_add_obj(root_obj, DATA_MODEM_DYNAMIC, modem_obj);
		data_encoded = true;
	} else {
		cJSON_Delete(modem_obj);
	}

	if (err) {
		goto exit;
	} else if (!data_encoded) {
		err = -ENODATA;
		goto exit;
	}

	buffer = cJSON_Print(root_obj);

	printk("Encoded batch message: %s\n", buffer);

	output->buf = buffer;
	output->len = strlen(buffer);

exit:

	cJSON_Delete(root_obj);

	return err;
}
