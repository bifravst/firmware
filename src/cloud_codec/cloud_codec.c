#include <cloud_codec.h>
#include <stdbool.h>
#include <string.h>
#include <zephyr.h>
#include <zephyr/types.h>
#include <modem/modem_info.h>
#include <stdio.h>
#include <stdlib.h>
#include "cJSON.h"
#include "cJSON_os.h"
#include <net/cloud.h>
#include <date_time.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(cloud_codec, CONFIG_CAT_TRACKER_LOG_LEVEL);

static bool change_gpst = true;
static bool change_active = true;
static bool change_active_wait = true;
static bool change_passive_wait = true;
static bool change_movt = true;
static bool change_acc_thres = true;

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

int cloud_codec_decode_response(char *input, struct cloud_data_cfg *data)
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

	printk("Decoded message: %s\n", string);

	group_obj = json_object_decode(root_obj, "cfg");
	if (group_obj != NULL) {
		goto get_data;
	}

	group_obj = json_object_decode(root_obj, "state");
	if (group_obj == NULL) {
		goto exit;
	}

	subgroup_obj = json_object_decode(group_obj, "cfg");
	if (subgroup_obj == NULL) {
		goto exit;
	}

get_data:

	gpst = cJSON_GetObjectItem(subgroup_obj, "gpst");
	active = cJSON_GetObjectItem(subgroup_obj, "act");
	active_wait = cJSON_GetObjectItem(subgroup_obj, "actwt");
	passive_wait = cJSON_GetObjectItem(subgroup_obj, "mvres");
	movt = cJSON_GetObjectItem(subgroup_obj, "mvt");
	acc_thres = cJSON_GetObjectItem(subgroup_obj, "acct");

	if (gpst != NULL && data->gpst != gpst->valueint) {
		data->gpst = gpst->valueint;
		LOG_INF("SETTING GPST TO: %d", gpst->valueint);
		change_gpst = true;
	}

	if (active != NULL && data->act != active->valueint) {
		data->act = active->valueint;
		LOG_INF("SETTING ACTIVE TO: %d", active->valueint);
		change_active = true;
	}

	if (active_wait != NULL && data->actw != active_wait->valueint) {
		data->actw = active_wait->valueint;
		LOG_INF("SETTING ACTIVE WAIT TO: %d", active_wait->valueint);
		change_active_wait = true;
	}

	if (passive_wait != NULL && data->pasw != passive_wait->valueint) {
		data->pasw = passive_wait->valueint;
		LOG_INF("SETTING PASSIVE_WAIT TO: %d", passive_wait->valueint);
		change_passive_wait = true;
	}

	if (movt != NULL && data->movt != movt->valueint) {
		data->movt = movt->valueint;
		LOG_INF("SETTING MOVEMENT TIMEOUT TO: %d", movt->valueint);
		change_movt = true;
	}

	if (acc_thres != NULL && data->acct != acc_thres->valueint) {
		data->acct = acc_thres->valueint;
		LOG_INF("SETTING ACCEL THRESHOLD TIMEOUT TO: %d",
			acc_thres->valueint);
		change_acc_thres = true;
	}
exit:
	cJSON_Delete(root_obj);
	return 0;
}

static int cloud_codec_static_modem_data_add(cJSON *parent,
					     struct cloud_data_modem *data)
{
	int err = 0;
	char nw_mode[50];

	static const char lte_string[] = "LTE-M";
	static const char nbiot_string[] = "NB-IoT";
	static const char gps_string[] = " GPS";

	if (!data->queued) {
		LOG_INF("Head of modem buffer not indexing a queued entry");
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

	err = json_add_number(static_m_v, "band", data->bnd);
	err += json_add_str(static_m_v, "nw", nw_mode);
	err += json_add_str(static_m_v, "iccid", data->iccid);
	err += json_add_str(static_m_v, "modV", data->fw);
	err += json_add_str(static_m_v, "brdV", data->brdv);
	err += json_add_str(static_m_v, "appV", data->appv);

	err += json_add_obj(static_m, "v", static_m_v);
	err += json_add_number(static_m, "ts", data->mod_ts_static);
	err += json_add_obj(parent, "dev", static_m);

exit:
	return err;
}

static int cloud_codec_dynamic_modem_data_add(cJSON *parent,
					      struct cloud_data_modem *data,
					      bool buffered_entry)
{
	int err = 0;
	long mccmnc;

	if (!data->queued) {
		LOG_INF("Head of modem buffer not indexing a queued entry");
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

	if (buffered_entry) {
		err = json_add_number(dynamic_m_v, "rsrp", data->rsrp);
		err += json_add_number(dynamic_m_v, "area", data->area);
		err += json_add_number(dynamic_m_v, "mccmnc", mccmnc);
		err += json_add_number(dynamic_m_v, "cell", data->cell);
		err += json_add_str(dynamic_m_v, "ip", data->ip);

		err += json_add_obj(dynamic_m, "v", dynamic_m_v);
		err += json_add_number(dynamic_m, "ts", data->mod_ts);
		err += json_add_obj_array(parent, dynamic_m);
	} else {
		err = json_add_number(dynamic_m_v, "rsrp", data->rsrp);
		err += json_add_number(dynamic_m_v, "area", data->area);
		err += json_add_number(dynamic_m_v, "mccmnc", mccmnc);
		err += json_add_number(dynamic_m_v, "cell", data->cell);
		err += json_add_str(dynamic_m_v, "ip", data->ip);

		err += json_add_obj(dynamic_m, "v", dynamic_m_v);
		err += json_add_number(dynamic_m, "ts", data->mod_ts);
		err += json_add_obj(parent, "roam", dynamic_m);
	}

	data->queued = false;

exit:
	return err;
}

static int cloud_codec_sensor_data_add(cJSON *parent,
				       struct cloud_data_sensors *data,
				       bool buffered_entry)
{
	int err = 0;

	if (!data->queued) {
		LOG_INF("Head of sensor buffer not indexing a queued entry");
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

	if (buffered_entry) {
		err = json_add_number(sensor_val_obj, "temp", data->temp);
		err += json_add_number(sensor_val_obj, "hum", data->hum);
		err += json_add_obj(sensor_obj, "v", sensor_val_obj);
		err += json_add_number(sensor_obj, "ts", data->env_ts);
		err += json_add_obj_array(parent, sensor_obj);
	} else {
		err = json_add_number(sensor_val_obj, "temp", data->temp);
		err += json_add_number(sensor_val_obj, "hum", data->hum);
		err += json_add_obj(sensor_obj, "v", sensor_val_obj);
		err += json_add_number(sensor_obj, "ts", data->env_ts);
		err += json_add_obj(parent, "env", sensor_obj);
	}

	data->queued = false;

exit:
	return err;
}

static int cloud_codec_gps_data_add(cJSON *parent, struct cloud_data_gps *data,
				    bool buffered_entry)
{
	int err = 0;

	if (!data->queued) {
		LOG_INF("Head of gps buffer not indexing a queued entry");
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

	if (buffered_entry) {
		err = json_add_number(gps_val_obj, "lng", data->longi);
		err += json_add_number(gps_val_obj, "lat", data->lat);
		err += json_add_number(gps_val_obj, "acc", data->acc);
		err += json_add_number(gps_val_obj, "alt", data->alt);
		err += json_add_number(gps_val_obj, "spd", data->spd);
		err += json_add_number(gps_val_obj, "hdg", data->hdg);
		err += json_add_obj(gps_obj, "v", gps_val_obj);
		err += json_add_number(gps_obj, "ts", data->gps_ts);
		err += json_add_obj_array(parent, gps_obj);
	} else {
		err = json_add_number(gps_val_obj, "lng", data->longi);
		err += json_add_number(gps_val_obj, "lat", data->lat);
		err += json_add_number(gps_val_obj, "acc", data->acc);
		err += json_add_number(gps_val_obj, "alt", data->alt);
		err += json_add_number(gps_val_obj, "spd", data->spd);
		err += json_add_number(gps_val_obj, "hdg", data->hdg);

		err += json_add_obj(gps_obj, "v", gps_val_obj);
		err += json_add_number(gps_obj, "ts", data->gps_ts);
		err += json_add_obj(parent, "gps", gps_obj);
	}

	data->queued = false;

exit:
	return err;
}

static int cloud_codec_accel_data_add(cJSON *parent,
				      struct cloud_data_accelerometer *data,
				      bool buffered_entry)
{
	int err = 0;

	if (!data->queued) {
		LOG_INF("Head of accel buffer not indexing a queued entry");
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

	err = json_add_number(acc_v_obj, "x", data->values[0]);
	err += json_add_number(acc_v_obj, "y", data->values[1]);
	err += json_add_number(acc_v_obj, "z", data->values[2]);

	if (buffered_entry) {
		err += json_add_obj(acc_obj, "v", acc_v_obj);
		err += json_add_number(acc_obj, "ts", data->ts);
		err += json_add_obj_array(parent, acc_obj);
	} else {
		err += json_add_obj(acc_obj, "v", acc_v_obj);
		err += json_add_number(acc_obj, "ts", data->ts);
		err += json_add_obj(parent, "acc", acc_obj);
	}

	data->queued = false;

exit:
	return err;
}

static int cloud_codec_ui_data_add(cJSON *parent, struct cloud_data_ui *data,
				   bool buffered_entry)
{
	int err = 0;

	if (!data->queued) {
		LOG_INF("Head of UI buffer not indexing a queued entry");
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

	if (buffered_entry) {
		err = json_add_number(btn_obj, "v", data->btn);
		err += json_add_number(btn_obj, "ts", data->btn_ts);
		err += json_add_obj_array(parent, btn_obj);
	} else {
		err = json_add_number(btn_obj, "v", data->btn);
		err += json_add_number(btn_obj, "ts", data->btn_ts);
		err += json_add_obj(parent, "btn", btn_obj);
	}

	data->queued = false;

exit:
	return err;
}

static int cloud_codec_bat_data_add(cJSON *parent,
				    struct cloud_data_battery *data,
				    bool buffered_entry)
{
	int err = 0;

	if (!data->queued) {
		LOG_INF("Head of battery buffer not indexing a queued entry");
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

	if (buffered_entry) {
		err = json_add_number(bat_obj, "v", data->bat);
		err += json_add_number(bat_obj, "ts", data->bat_ts);
		err += json_add_obj_array(parent, bat_obj);
	} else {
		err = json_add_number(bat_obj, "v", data->bat);
		err += json_add_number(bat_obj, "ts", data->bat_ts);
		err += json_add_obj(parent, "bat", bat_obj);
	}

	data->queued = false;

exit:
	return err;
}

int cloud_codec_encode_cfg_data(struct cloud_msg *output,
				struct cloud_data_cfg *data)
{
	char *buffer;
	int err = 0;
	int change_cnt = 0;

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

	if (change_gpst) {
		err += json_add_number(cfg_obj, "gpst", data->gpst);
		change_cnt++;
	}

	if (change_active) {
		err += json_add_bool(cfg_obj, "act", data->act);
		change_cnt++;
	}

	if (change_active_wait) {
		err += json_add_number(cfg_obj, "actwt", data->actw);
		change_cnt++;
	}

	if (change_passive_wait) {
		err += json_add_number(cfg_obj, "mvres", data->pasw);
		change_cnt++;
	}

	if (change_movt) {
		err += json_add_number(cfg_obj, "mvt", data->movt);
		change_cnt++;
	}

	if (change_acc_thres) {
		err += json_add_number(cfg_obj, "acct", data->acct);
		change_cnt++;
	}

	if (change_cnt == 0) {
		cJSON_Delete(root_obj);
		cJSON_Delete(state_obj);
		cJSON_Delete(rep_obj);
		cJSON_Delete(cfg_obj);
		return -EAGAIN;
	}

	err += json_add_obj(rep_obj, "cfg", cfg_obj);
	err += json_add_obj(state_obj, "reported", rep_obj);
	err += json_add_obj(root_obj, "state", state_obj);

	if (err) {
		goto exit;
	}

	buffer = cJSON_Print(root_obj);

	printk("Encoded message: %s\n", buffer);

	output->buf = buffer;
	output->len = strlen(buffer);

	change_gpst = false;
	change_active = false;
	change_active_wait = false;
	change_passive_wait = false;
	change_movt = false;
	change_acc_thres = false;

exit:
	cJSON_Delete(root_obj);
	return err;
}

int cloud_codec_encode_data(struct cloud_msg *output,
			    struct cloud_data_gps *gps_buf,
			    struct cloud_data_sensors *sensor_buf,
			    struct cloud_data_modem *modem_buf,
			    struct cloud_data_ui *ui_buf,
			    struct cloud_data_accelerometer *accel_buf,
			    struct cloud_data_battery *bat_buf,
			    enum cloud_data_encode_schema encode_schema)
{
	int err = 0;
	char *buffer;

	cJSON *root_obj = cJSON_CreateObject();
	cJSON *state_obj = cJSON_CreateObject();
	cJSON *rep_obj = cJSON_CreateObject();

	if (root_obj == NULL || state_obj == NULL || rep_obj == NULL) {
		cJSON_Delete(root_obj);
		cJSON_Delete(state_obj);
		cJSON_Delete(rep_obj);
		return -ENOMEM;
	}

	switch (encode_schema) {
	case CLOUD_DATA_ENCODE_MSTAT_MDYN_SENS_BAT:
		err = cloud_codec_bat_data_add(rep_obj, bat_buf, false);
		err += cloud_codec_static_modem_data_add(rep_obj, modem_buf);
		err += cloud_codec_dynamic_modem_data_add(rep_obj, modem_buf,
							  false);
		err += cloud_codec_sensor_data_add(rep_obj, sensor_buf, false);
		err += json_add_obj(state_obj, "reported", rep_obj);
		err += json_add_obj(root_obj, "state", state_obj);
		break;
	case CLOUD_DATA_ENCODE_MSTAT_MDYN_SENS_BAT_GPS:
		err = cloud_codec_bat_data_add(rep_obj, bat_buf, false);
		err += cloud_codec_static_modem_data_add(rep_obj, modem_buf);
		err += cloud_codec_dynamic_modem_data_add(rep_obj, modem_buf,
							  false);
		err += cloud_codec_sensor_data_add(rep_obj, sensor_buf, false);
		err += cloud_codec_gps_data_add(rep_obj, gps_buf, false);
		err += json_add_obj(state_obj, "reported", rep_obj);
		err += json_add_obj(root_obj, "state", state_obj);
		break;
	case CLOUD_DATA_ENCODE_MSTAT_MDYN_SENS_BAT_GPS_ACCEL:
		err = cloud_codec_bat_data_add(rep_obj, bat_buf, false);
		err += cloud_codec_static_modem_data_add(rep_obj, modem_buf);
		err += cloud_codec_dynamic_modem_data_add(rep_obj, modem_buf,
							  false);
		err += cloud_codec_sensor_data_add(rep_obj, sensor_buf, false);
		err += cloud_codec_gps_data_add(rep_obj, gps_buf, false);
		err += cloud_codec_accel_data_add(rep_obj, accel_buf, false);
		err += json_add_obj(state_obj, "reported", rep_obj);
		err += json_add_obj(root_obj, "state", state_obj);
		break;
	case CLOUD_DATA_ENCODE_MSTAT_MDYN_SENS_BAT_ACCEL:
		err = cloud_codec_bat_data_add(rep_obj, bat_buf, false);
		err += cloud_codec_static_modem_data_add(rep_obj, modem_buf);
		err += cloud_codec_dynamic_modem_data_add(rep_obj, modem_buf,
							  false);
		err += cloud_codec_sensor_data_add(rep_obj, sensor_buf, false);
		err += cloud_codec_accel_data_add(rep_obj, accel_buf, false);
		err += json_add_obj(state_obj, "reported", rep_obj);
		err += json_add_obj(root_obj, "state", state_obj);
		break;
	case CLOUD_DATA_ENCODE_MDYN_SENS_BAT:
		err = cloud_codec_bat_data_add(rep_obj, bat_buf, false);
		err += cloud_codec_dynamic_modem_data_add(rep_obj, modem_buf,
							  false);
		err += cloud_codec_sensor_data_add(rep_obj, sensor_buf, false);
		err += json_add_obj(state_obj, "reported", rep_obj);
		err += json_add_obj(root_obj, "state", state_obj);
		break;
	case CLOUD_DATA_ENCODE_MDYN_SENS_BAT_GPS:
		err = cloud_codec_bat_data_add(rep_obj, bat_buf, false);
		err += cloud_codec_dynamic_modem_data_add(rep_obj, modem_buf,
							  false);
		err += cloud_codec_sensor_data_add(rep_obj, sensor_buf, false);
		err += cloud_codec_gps_data_add(rep_obj, gps_buf, false);
		err += json_add_obj(state_obj, "reported", rep_obj);
		err += json_add_obj(root_obj, "state", state_obj);
		break;
	case CLOUD_DATA_ENCODE_MDYN_SENS_BAT_GPS_ACCEL:
		err = cloud_codec_bat_data_add(rep_obj, bat_buf, false);
		err += cloud_codec_dynamic_modem_data_add(rep_obj, modem_buf,
							  false);
		err += cloud_codec_sensor_data_add(rep_obj, sensor_buf, false);
		err += cloud_codec_gps_data_add(rep_obj, gps_buf, false);
		err += cloud_codec_accel_data_add(rep_obj, accel_buf, false);
		err += json_add_obj(state_obj, "reported", rep_obj);
		err += json_add_obj(root_obj, "state", state_obj);
		break;
	case CLOUD_DATA_ENCODE_MDYN_SENS_BAT_ACCEL:
		err = cloud_codec_bat_data_add(rep_obj, bat_buf, false);
		err += cloud_codec_dynamic_modem_data_add(rep_obj, modem_buf,
							  false);
		err += cloud_codec_sensor_data_add(rep_obj, sensor_buf, false);
		err += cloud_codec_accel_data_add(rep_obj, accel_buf, false);
		err += json_add_obj(state_obj, "reported", rep_obj);
		err += json_add_obj(root_obj, "state", state_obj);
		break;
	case CLOUD_DATA_ENCODE_UI:
		err = cloud_codec_ui_data_add(root_obj, ui_buf, false);
		cJSON_Delete(state_obj);
		cJSON_Delete(rep_obj);
		break;
	default:
		LOG_ERR("Unknown encoding schema");
		break;
	}

	if (err) {
		goto exit;
	}

	buffer = cJSON_Print(root_obj);

	printk("Encoded message: %s\n", buffer);

	output->buf = buffer;
	output->len = strlen(buffer);

exit:
	cJSON_Delete(root_obj);

	return err;
}

int cloud_codec_encode_gps_buffer(struct cloud_msg *output,
				  struct cloud_data_gps *data)
{
	int err = 0;
	int encoded_counter = 0;
	char *buffer;

	cJSON *root_obj = cJSON_CreateObject();
	cJSON *gps_obj = cJSON_CreateArray();

	if (root_obj == NULL || gps_obj == NULL) {
		cJSON_Delete(root_obj);
		cJSON_Delete(gps_obj);
		return -ENOMEM;
	}

	for (int i = 0; i < CONFIG_GPS_BUFFER_MAX; i++) {
		if (data[i].queued &&
		    (encoded_counter < CONFIG_ENCODED_BUFFER_ENTRIES_MAX)) {
			err += cloud_codec_gps_data_add(gps_obj, &data[i],
							true);
			encoded_counter++;
		}
	}

	err += json_add_obj(root_obj, "gps", gps_obj);

	if (err) {
		goto exit;
	}

	buffer = cJSON_Print(root_obj);

	printk("Encoded message: %s\n", buffer);

	output->buf = buffer;
	output->len = strlen(buffer);

exit:

	cJSON_Delete(root_obj);

	return 0;
}

int cloud_codec_encode_modem_buffer(struct cloud_msg *output,
				    struct cloud_data_modem *data)
{
	int err = 0;
	int encoded_counter = 0;
	char *buffer;

	cJSON *root_obj = cJSON_CreateObject();
	cJSON *modem_obj = cJSON_CreateArray();

	if (root_obj == NULL || modem_obj == NULL) {
		cJSON_Delete(root_obj);
		cJSON_Delete(modem_obj);
		return -ENOMEM;
	}

	for (int i = 0; i < CONFIG_MODEM_BUFFER_MAX; i++) {
		if (data[i].queued &&
		    (encoded_counter < CONFIG_ENCODED_BUFFER_ENTRIES_MAX)) {
			err += cloud_codec_dynamic_modem_data_add(
				modem_obj, &data[i], true);
			encoded_counter++;
		}
	}

	err += json_add_obj(root_obj, "roam", modem_obj);

	if (err) {
		goto exit;
	}

	buffer = cJSON_Print(root_obj);

	printk("Encoded message: %s\n", buffer);

	output->buf = buffer;
	output->len = strlen(buffer);

exit:

	cJSON_Delete(root_obj);

	return 0;
}

int cloud_codec_encode_sensor_buffer(struct cloud_msg *output,
				     struct cloud_data_sensors *data)
{
	int err = 0;
	int encoded_counter = 0;
	char *buffer;

	cJSON *root_obj = cJSON_CreateObject();
	cJSON *sensor_obj = cJSON_CreateArray();

	if (root_obj == NULL || sensor_obj == NULL) {
		cJSON_Delete(root_obj);
		cJSON_Delete(sensor_obj);
		return -ENOMEM;
	}

	for (int i = 0; i < CONFIG_SENSOR_BUFFER_MAX; i++) {
		if (data[i].queued &&
		    (encoded_counter < CONFIG_ENCODED_BUFFER_ENTRIES_MAX)) {
			err += cloud_codec_sensor_data_add(sensor_obj, &data[i],
							   true);
			encoded_counter++;
		}
	}

	err += json_add_obj(root_obj, "env", sensor_obj);

	if (err) {
		goto exit;
	}

	buffer = cJSON_Print(root_obj);

	printk("Encoded message: %s\n", buffer);

	output->buf = buffer;
	output->len = strlen(buffer);

exit:

	cJSON_Delete(root_obj);

	return 0;
}

int cloud_codec_encode_ui_buffer(struct cloud_msg *output,
				 struct cloud_data_ui *data)
{
	int err = 0;
	int encoded_counter = 0;
	char *buffer;

	cJSON *root_obj = cJSON_CreateObject();
	cJSON *ui_obj = cJSON_CreateArray();

	if (root_obj == NULL || ui_obj == NULL) {
		cJSON_Delete(root_obj);
		cJSON_Delete(ui_obj);
		return -ENOMEM;
	}

	for (int i = 0; i < CONFIG_UI_BUFFER_MAX; i++) {
		if (data[i].queued &&
		    (encoded_counter < CONFIG_ENCODED_BUFFER_ENTRIES_MAX)) {
			err += cloud_codec_ui_data_add(ui_obj, &data[i], true);
			encoded_counter++;
		}
	}

	err += json_add_obj(root_obj, "btn", ui_obj);

	if (err) {
		goto exit;
	}

	buffer = cJSON_Print(root_obj);

	printk("Encoded message: %s\n", buffer);

	output->buf = buffer;
	output->len = strlen(buffer);

exit:

	cJSON_Delete(root_obj);

	return 0;
}

int cloud_codec_encode_accel_buffer(struct cloud_msg *output,
				    struct cloud_data_accelerometer *data)
{
	int err = 0;
	int encoded_counter = 0;
	char *buffer;

	cJSON *root_obj = cJSON_CreateObject();
	cJSON *acc_obj = cJSON_CreateArray();

	if (root_obj == NULL || acc_obj == NULL) {
		cJSON_Delete(root_obj);
		cJSON_Delete(acc_obj);
		return -ENOMEM;
	}

	for (int i = 0; i < CONFIG_ACCEL_BUFFER_MAX; i++) {
		if (data[i].queued &&
		    (encoded_counter < CONFIG_ENCODED_BUFFER_ENTRIES_MAX)) {
			err += cloud_codec_accel_data_add(acc_obj, &data[i],
							  true);
			encoded_counter++;
		}
	}

	err += json_add_obj(root_obj, "acc", acc_obj);

	if (err) {
		goto exit;
	}

	buffer = cJSON_Print(root_obj);

	printk("Encoded message: %s\n", buffer);

	output->buf = buffer;
	output->len = strlen(buffer);

exit:

	cJSON_Delete(root_obj);

	return 0;
}

int cloud_codec_encode_bat_buffer(struct cloud_msg *output,
				  struct cloud_data_battery *data)
{
	int err = 0;
	int encoded_counter = 0;
	char *buffer;

	cJSON *root_obj = cJSON_CreateObject();
	cJSON *bat_obj = cJSON_CreateArray();

	if (root_obj == NULL || bat_obj) {
		cJSON_Delete(root_obj);
		cJSON_Delete(bat_obj);
		return -ENOMEM;
	}

	for (int i = 0; i < CONFIG_BAT_BUFFER_MAX; i++) {
		if (data[i].queued &&
		    (encoded_counter < CONFIG_ENCODED_BUFFER_ENTRIES_MAX)) {
			err += cloud_codec_bat_data_add(root_obj, &data[i],
							true);
			encoded_counter++;
		}
	}

	err += json_add_obj(root_obj, "bat", bat_obj);

	if (err) {
		goto exit;
	}

	buffer = cJSON_Print(root_obj);

	printk("Encoded message: %s\n", buffer);

	output->buf = buffer;
	output->len = strlen(buffer);

exit:

	cJSON_Delete(root_obj);

	return 0;
}
