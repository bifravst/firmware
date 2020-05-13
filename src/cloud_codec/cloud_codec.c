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

static bool change_gpst		= true;
static bool change_active	= true;
static bool change_active_wait	= true;
static bool change_passive_wait = true;
static bool change_mov_timeout	= true;
static bool change_acc_thres	= true;

struct twins_gps_buf {
	cJSON *gps_buf_objects;
	cJSON *gps_buf_val_objects;
};

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

int cloud_decode_response(char *input, struct cloud_data *cloud_data)
{
	char *string = NULL;
	cJSON *root_obj = NULL;
	cJSON *group_obj = NULL;
	cJSON *subgroup_obj = NULL;
	cJSON *gpst = NULL;
	cJSON *active = NULL;
	cJSON *active_wait = NULL;
	cJSON *passive_wait = NULL;
	cJSON *mov_timeout = NULL;
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
		gpst = cJSON_GetObjectItem(group_obj, "gpst");
		active = cJSON_GetObjectItem(group_obj, "act");
		active_wait = cJSON_GetObjectItem(group_obj, "actwt");
		passive_wait = cJSON_GetObjectItem(group_obj, "mvres");
		mov_timeout = cJSON_GetObjectItem(group_obj, "mvt");
		acc_thres = cJSON_GetObjectItem(group_obj, "acct");
		goto get_data;
	}

	group_obj = json_object_decode(root_obj, "state");
	if (group_obj != NULL) {
		subgroup_obj = json_object_decode(group_obj, "cfg");
		if (subgroup_obj != NULL) {
			gpst = cJSON_GetObjectItem(subgroup_obj, "gpst");
			active = cJSON_GetObjectItem(subgroup_obj, "act");
			active_wait =
				cJSON_GetObjectItem(subgroup_obj, "actwt");
			passive_wait =
				cJSON_GetObjectItem(subgroup_obj, "mvres");
			mov_timeout =
				cJSON_GetObjectItem(subgroup_obj, "mvt");
			acc_thres =
				cJSON_GetObjectItem(subgroup_obj, "acct");
		}
	} else {
		goto exit;
	}

get_data:

	if (gpst != NULL && cloud_data->gps_timeout != gpst->valueint) {
		cloud_data->gps_timeout = gpst->valueint;
		LOG_INF("SETTING GPST TO: %d", gpst->valueint);
		change_gpst = true;
	}

	if (active != NULL && cloud_data->active != active->valueint) {
		cloud_data->active = active->valueint;
		LOG_INF("SETTING ACTIVE TO: %d", active->valueint);
		change_active = true;
	}

	if (active_wait != NULL && cloud_data->active_wait != active_wait->valueint) {
		cloud_data->active_wait = active_wait->valueint;
		LOG_INF("SETTING ACTIVE WAIT TO: %d", active_wait->valueint);
		change_active_wait = true;
	}

	if (passive_wait != NULL && cloud_data->passive_wait != passive_wait->valueint) {
		cloud_data->passive_wait = passive_wait->valueint;
		LOG_INF("SETTING PASSIVE_WAIT TO: %d", passive_wait->valueint);
		change_passive_wait = true;
	}

	if (mov_timeout != NULL && cloud_data->mov_timeout != mov_timeout->valueint) {
		cloud_data->mov_timeout = mov_timeout->valueint;
		LOG_INF("SETTING MOVEMENT TIMEOUT TO: %d", mov_timeout->valueint);
		change_mov_timeout = true;
	}

	if (acc_thres != NULL && cloud_data->acc_thres != acc_thres->valueint) {
		cloud_data->acc_thres = acc_thres->valueint;
		LOG_INF("SETTING ACCEL THRESHOLD TIMEOUT TO: %d", acc_thres->valueint);
		change_acc_thres = true;
	}
exit:
	cJSON_Delete(root_obj);
	return 0;
}

int cloud_encode_gps_buffer(struct cloud_msg *output,
			    struct cloud_data_gps *cir_buf_gps)
{
	int err = 0;
	char *buffer;
	int encoded_counter = 0;

	struct twins_gps_buf twins_gps_buf[CONFIG_CIRCULAR_SENSOR_BUFFER_MAX];

	err = date_time_uptime_to_unix_time_ms(&cir_buf_gps->gps_ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		cir_buf_gps->gps_ts = 0;
		return err;
	}

	cJSON *root_obj = cJSON_CreateObject();
	cJSON *state_obj = cJSON_CreateObject();
	cJSON *reported_obj = cJSON_CreateObject();
	cJSON *gps_obj = cJSON_CreateArray();

	for (int i = 0; i < CONFIG_CIRCULAR_SENSOR_BUFFER_MAX; i++) {
		twins_gps_buf[i].gps_buf_objects = NULL;
		twins_gps_buf[i].gps_buf_objects = cJSON_CreateObject();
		twins_gps_buf[i].gps_buf_val_objects = NULL;
		twins_gps_buf[i].gps_buf_val_objects = cJSON_CreateObject();
	}

	if (root_obj == NULL || state_obj == NULL || reported_obj == NULL ||
	    gps_obj == NULL) {
		cJSON_Delete(root_obj);
		cJSON_Delete(state_obj);
		cJSON_Delete(reported_obj);
		cJSON_Delete(gps_obj);
		for (int i = 0; i < CONFIG_CIRCULAR_SENSOR_BUFFER_MAX; i++) {
			cJSON_Delete(twins_gps_buf[i].gps_buf_objects);
			cJSON_Delete(twins_gps_buf[i].gps_buf_val_objects);
		}
		return -ENOMEM;
	}

	err += json_add_obj(reported_obj, "gps", gps_obj);
	err += json_add_obj(state_obj, "reported", reported_obj);
	err += json_add_obj(root_obj, "state", state_obj);

	for (int i = 0; i < CONFIG_CIRCULAR_SENSOR_BUFFER_MAX; i++) {
		if (cir_buf_gps[i].queued &&
		    (encoded_counter < CONFIG_MAX_PER_ENCODED_ENTRIES)) {
			err += json_add_number(
				twins_gps_buf[i].gps_buf_val_objects, "lng",
				cir_buf_gps[i].longitude);
			err += json_add_number(
				twins_gps_buf[i].gps_buf_val_objects, "lat",
				cir_buf_gps[i].latitude);
			err += json_add_number(
				twins_gps_buf[i].gps_buf_val_objects, "acc",
				cir_buf_gps[i].accuracy);
			err += json_add_number(
				twins_gps_buf[i].gps_buf_val_objects, "alt",
				cir_buf_gps[i].altitude);
			err += json_add_number(
				twins_gps_buf[i].gps_buf_val_objects, "spd",
				cir_buf_gps[i].speed);
			err += json_add_number(
				twins_gps_buf[i].gps_buf_val_objects, "hdg",
				cir_buf_gps[i].heading);
			err += json_add_obj(
				twins_gps_buf[i].gps_buf_objects, "v",
				twins_gps_buf[i].gps_buf_val_objects);
			err += json_add_number(twins_gps_buf[i].gps_buf_objects,
					       "ts", cir_buf_gps->gps_ts);
			err += json_add_obj_array(
				gps_obj, twins_gps_buf[i].gps_buf_objects);
			cir_buf_gps[i].queued = false;
			encoded_counter++;
		}
	}

	encoded_counter = 0;

	if (err) {
		cJSON_Delete(root_obj);
		cJSON_Delete(state_obj);
		cJSON_Delete(reported_obj);
		cJSON_Delete(gps_obj);
		for (int i = 0; i < CONFIG_CIRCULAR_SENSOR_BUFFER_MAX; i++) {
			cJSON_Delete(twins_gps_buf[i].gps_buf_objects);
			cJSON_Delete(twins_gps_buf[i].gps_buf_val_objects);
		}
		return -EAGAIN;
	}

	buffer = cJSON_Print(root_obj);
	cJSON_Delete(root_obj);

	printk("Encoded message: %s\n", buffer);

	output->buf = buffer;
	output->len = strlen(buffer);

	return 0;
}

int cloud_encode_cfg_data(struct cloud_msg *output,
			  struct cloud_data *cloud_data)
{
	int err = 0;
	int change_cnt = 0;
	char *buffer;

	cJSON *root_obj = cJSON_CreateObject();
	cJSON *state_obj = cJSON_CreateObject();
	cJSON *reported_obj = cJSON_CreateObject();
	cJSON *cfg_obj = cJSON_CreateObject();

	if (root_obj == NULL || state_obj == NULL || reported_obj == NULL ||
	    cfg_obj == NULL) {
		cJSON_Delete(root_obj);
		cJSON_Delete(state_obj);
		cJSON_Delete(reported_obj);
		cJSON_Delete(cfg_obj);
		return -ENOMEM;
	}

	/*CFG*/

	if (change_gpst) {
		err += json_add_number(cfg_obj, "gpst",
				       cloud_data->gps_timeout);
		change_cnt++;
	}

	if (change_active) {
		err += json_add_bool(cfg_obj, "act", cloud_data->active);
		change_cnt++;
	}

	if (change_active_wait) {
		err += json_add_number(cfg_obj, "actwt",
				       cloud_data->active_wait);
		change_cnt++;
	}

	if (change_passive_wait) {
		err += json_add_number(cfg_obj, "mvres",
				       cloud_data->passive_wait);
		change_cnt++;
	}

	if (change_mov_timeout) {
		err += json_add_number(cfg_obj, "mvt",
				       cloud_data->mov_timeout);
		change_cnt++;
	}

	if (change_acc_thres) {
		err += json_add_number(cfg_obj, "acct",
				       cloud_data->acc_thres);
		change_cnt++;
	}

	if (change_cnt == 0) {
		cJSON_Delete(root_obj);
		cJSON_Delete(state_obj);
		cJSON_Delete(reported_obj);
		cJSON_Delete(cfg_obj);
		return -EAGAIN;
	}

	err += json_add_obj(reported_obj, "cfg", cfg_obj);
	err += json_add_obj(state_obj, "reported", reported_obj);
	err += json_add_obj(root_obj, "state", state_obj);

	if (err) {
		goto exit;
	}

	buffer = cJSON_Print(root_obj);

	printk("Encoded message: %s\n", buffer);

	output->buf = buffer;
	output->len = strlen(buffer);

	change_gpst			= false;
	change_active			= false;
	change_active_wait		= false;
	change_passive_wait		= false;
	change_mov_timeout		= false;
	change_acc_thres		= false;

exit:
	cJSON_Delete(root_obj);
	return err;
}

int cloud_encode_sensor_data(struct cloud_msg *output,
			     struct cloud_data *cloud_data,
			     struct cloud_data_gps *cir_buf_gps,
			     struct modem_param_info *modem_info)
{
	int err = 0;
	char *buffer;

	const char lte_string[] = "LTE-M";
	const char nbiot_string[] = "NB-IoT";
	const char gps_string[] = " GPS";

	err = date_time_uptime_to_unix_time_ms(&cloud_data->bat_ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		cloud_data->mod_ts  = 0;
		cloud_data->acc_ts  = 0;
		cloud_data->bat_ts  = 0;
		cir_buf_gps->gps_ts = 0;
		cloud_data->hum_ts  = 0;
		cloud_data->temp_ts  = 0;
		return err;
	}

	err = date_time_uptime_to_unix_time_ms(&cloud_data->acc_ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		cloud_data->mod_ts  = 0;
		cloud_data->acc_ts  = 0;
		cloud_data->bat_ts  = 0;
		cir_buf_gps->gps_ts = 0;
		cloud_data->hum_ts  = 0;
		cloud_data->temp_ts  = 0;
		return err;
	}

	err = date_time_uptime_to_unix_time_ms(&cir_buf_gps->gps_ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		cloud_data->mod_ts  = 0;
		cloud_data->acc_ts  = 0;
		cloud_data->bat_ts  = 0;
		cir_buf_gps->gps_ts = 0;
		cloud_data->hum_ts  = 0;
		cloud_data->temp_ts  = 0;
		return err;
	}

	err = date_time_uptime_to_unix_time_ms(&cloud_data->mod_ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		cloud_data->mod_ts  = 0;
		cloud_data->acc_ts  = 0;
		cloud_data->bat_ts  = 0;
		cir_buf_gps->gps_ts = 0;
		cloud_data->hum_ts  = 0;
		cloud_data->temp_ts  = 0;
		return err;
	}

	err = date_time_uptime_to_unix_time_ms(&cloud_data->hum_ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		cloud_data->mod_ts  = 0;
		cloud_data->acc_ts  = 0;
		cloud_data->bat_ts  = 0;
		cir_buf_gps->gps_ts = 0;
		cloud_data->hum_ts  = 0;
		cloud_data->temp_ts  = 0;
		return err;
	}

	err = date_time_uptime_to_unix_time_ms(&cloud_data->temp_ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		cloud_data->mod_ts  = 0;
		cloud_data->acc_ts  = 0;
		cloud_data->bat_ts  = 0;
		cir_buf_gps->gps_ts = 0;
		cloud_data->hum_ts  = 0;
		cloud_data->temp_ts  = 0;
		return err;
	}

	cJSON *root_obj		= cJSON_CreateObject();
	cJSON *state_obj	= cJSON_CreateObject();
	cJSON *reported_obj	= cJSON_CreateObject();
	cJSON *bat_obj		= cJSON_CreateObject();
	cJSON *acc_obj		= cJSON_CreateObject();
	cJSON *acc_v_obj	= cJSON_CreateObject();
	cJSON *gps_obj		= cJSON_CreateObject();
	cJSON *gps_val_obj	= cJSON_CreateObject();
	cJSON *static_m_data	= cJSON_CreateObject();
	cJSON *static_m_data_v	= cJSON_CreateObject();
	cJSON *dynamic_m_data	= cJSON_CreateObject();
	cJSON *dynamic_m_data_v = cJSON_CreateObject();
	cJSON *temp_obj		= cJSON_CreateObject();
	cJSON *hum_obj		= cJSON_CreateObject();

	if (root_obj	    == NULL || state_obj        == NULL ||
	    reported_obj    == NULL || gps_obj 	        == NULL ||
	    bat_obj	    == NULL || acc_obj		== NULL ||
	    gps_val_obj	    == NULL || static_m_data    == NULL ||
	    dynamic_m_data  == NULL || dynamic_m_data_v == NULL ||
	    static_m_data_v == NULL || acc_v_obj	== NULL ||
	    hum_obj         == NULL || temp_obj		== NULL) {
		cJSON_Delete(root_obj);
		cJSON_Delete(state_obj);
		cJSON_Delete(reported_obj);
		cJSON_Delete(bat_obj);
		cJSON_Delete(acc_obj);
		cJSON_Delete(acc_v_obj);
		cJSON_Delete(gps_obj);
		cJSON_Delete(gps_val_obj);
		cJSON_Delete(static_m_data);
		cJSON_Delete(static_m_data_v);
		cJSON_Delete(dynamic_m_data);
		cJSON_Delete(dynamic_m_data_v);
		cJSON_Delete(temp_obj);
		cJSON_Delete(hum_obj);
		return -ENOMEM;
	}

	if (modem_info->network.lte_mode.value == 1) {
		strcat(modem_info->network.network_mode, lte_string);
	} else if (modem_info->network.nbiot_mode.value == 1) {
		strcat(modem_info->network.network_mode, nbiot_string);
	}

	if (modem_info->network.gps_mode.value == 1) {
		strcat(modem_info->network.network_mode, gps_string);
	}

	/*MODEM DATA*/
	err += json_add_number(static_m_data_v, "band", modem_info->network.current_band.value);
	err += json_add_str(static_m_data_v, "nw", modem_info->network.network_mode);
	err += json_add_str(static_m_data_v, "iccid", modem_info->sim.iccid.value_string);
	err += json_add_str(static_m_data_v, "modV", modem_info->device.modem_fw.value_string);
	err += json_add_str(static_m_data_v, "brdV", modem_info->device.board);
	err += json_add_str(static_m_data_v, "appV", CONFIG_CAT_TRACKER_APP_VERSION);
	err += json_add_number(dynamic_m_data_v, "rsrp", cloud_data->rsrp);
	err += json_add_number(dynamic_m_data_v, "area", modem_info->network.area_code.value);
	err += json_add_number(dynamic_m_data_v, "mccmnc", strtol(modem_info->network.current_operator.value_string, NULL, 10));
	err += json_add_number(dynamic_m_data_v, "cell", modem_info->network.cellid_dec);
	err += json_add_str(dynamic_m_data_v, "ip", modem_info->network.ip_address.value_string);

	if (cloud_data->synch) {
		err += json_add_obj(static_m_data, "v", static_m_data_v);
		err += json_add_number(static_m_data, "ts", cloud_data->mod_ts);
		err += json_add_obj(dynamic_m_data, "v", dynamic_m_data_v);
		err += json_add_number(dynamic_m_data, "ts", cloud_data->mod_ts);
		err += json_add_obj(reported_obj, "dev", static_m_data);
		err += json_add_obj(reported_obj, "roam", dynamic_m_data);
	} else {
		err += json_add_obj(dynamic_m_data, "v", dynamic_m_data_v);
		err += json_add_number(dynamic_m_data, "ts", cloud_data->mod_ts);
		err += json_add_obj(reported_obj, "roam", dynamic_m_data);
	}

	/*Temperature*/
	err += json_add_number(temp_obj, "v", cloud_data->temp);
	err += json_add_number(temp_obj, "ts", cloud_data->temp_ts);

	/*Humidity*/
	err += json_add_number(hum_obj, "v", cloud_data->hum);
	err += json_add_number(hum_obj, "ts", cloud_data->hum_ts);

	/*BAT*/
	err += json_add_number(bat_obj, "v", modem_info->device.battery.value);
	err += json_add_number(bat_obj, "ts", cloud_data->mod_ts);

	/*ACC*/
	err += json_add_number(acc_v_obj, "x", cloud_data->acc[0]);
	err += json_add_number(acc_v_obj, "y", cloud_data->acc[1]);
	err += json_add_number(acc_v_obj, "z", cloud_data->acc[2]);
	err += json_add_obj(acc_obj, "v", acc_v_obj);
	err += json_add_number(acc_obj, "ts", cloud_data->acc_ts);

	/*GPS*/
	err += json_add_number(gps_val_obj, "lng", cir_buf_gps->longitude);
	err += json_add_number(gps_val_obj, "lat", cir_buf_gps->latitude);
	err += json_add_number(gps_val_obj, "acc", cir_buf_gps->accuracy);
	err += json_add_number(gps_val_obj, "alt", cir_buf_gps->altitude);
	err += json_add_number(gps_val_obj, "spd", cir_buf_gps->speed);
	err += json_add_number(gps_val_obj, "hdg", cir_buf_gps->heading);

	/*Parameters included depending on mode and obtained gps fix*/
	if (cloud_data->active && !cloud_data->gps_found) {
		err += json_add_obj(reported_obj, "bat", bat_obj);
		err += json_add_obj(reported_obj, "temp", temp_obj);
		err += json_add_obj(reported_obj, "hum", hum_obj);
	}

	if (cloud_data->active && cloud_data->gps_found) {
		err += json_add_obj(reported_obj, "bat", bat_obj);
		err += json_add_obj(reported_obj, "temp", temp_obj);
		err += json_add_obj(reported_obj, "hum", hum_obj);
		err += json_add_obj(gps_obj, "v", gps_val_obj);
		err += json_add_number(gps_obj, "ts", cir_buf_gps->gps_ts);
		err += json_add_obj(reported_obj, "gps", gps_obj);
	}

	if (!cloud_data->active && !cloud_data->gps_found) {
		err += json_add_obj(reported_obj, "bat", bat_obj);
		err += json_add_obj(reported_obj, "temp", temp_obj);
		err += json_add_obj(reported_obj, "hum", hum_obj);
		if (cloud_data->acc_trig) {
			err += json_add_obj(reported_obj, "acc", acc_obj);
		}
	}

	if (!cloud_data->active && cloud_data->gps_found) {
		err += json_add_obj(reported_obj, "bat", bat_obj);
		err += json_add_obj(reported_obj, "temp", temp_obj);
		err += json_add_obj(reported_obj, "hum", hum_obj);
		if (cloud_data->acc_trig) {
			err += json_add_obj(reported_obj, "acc", acc_obj);
		}

		err += json_add_obj(gps_obj, "v", gps_val_obj);
		err += json_add_number(gps_obj, "ts", cir_buf_gps->gps_ts);
		err += json_add_obj(reported_obj, "gps", gps_obj);
	}

	err += json_add_obj(state_obj, "reported", reported_obj);
	err += json_add_obj(root_obj, "state", state_obj);

	if (err) {
		goto exit;
	}

	buffer = cJSON_Print(root_obj);

	printk("Encoded message: %s\n", buffer);

	output->buf = buffer;
	output->len = strlen(buffer);

exit:
	cJSON_Delete(root_obj);

	/* Clear network mode string */
	memset(modem_info->network.network_mode, 0,
	       sizeof(modem_info->network.network_mode));

	/*Delete objects which are not associated*/
	if (!cloud_data->synch) {
		cJSON_Delete(static_m_data);
		cJSON_Delete(static_m_data_v);
	}

	if (cloud_data->active && !cloud_data->gps_found) {
		cJSON_Delete(acc_obj);
		cJSON_Delete(gps_obj);
		cJSON_Delete(gps_val_obj);
	}

	if (cloud_data->active && cloud_data->gps_found) {
		cJSON_Delete(acc_obj);
	}

	if (!cloud_data->active && !cloud_data->gps_found) {
		cJSON_Delete(gps_obj);
		cJSON_Delete(gps_val_obj);

		if (!cloud_data->acc_trig) {
			cJSON_Delete(acc_obj);
		}
	}

	if (!cloud_data->active && cloud_data->gps_found) {
		if (!cloud_data->acc_trig) {
			cJSON_Delete(acc_obj);
		}
	}

	cloud_data->mod_ts  = 0;
	cloud_data->acc_ts  = 0;
	cloud_data->bat_ts  = 0;
	cir_buf_gps->gps_ts = 0;
	cloud_data->hum_ts  = 0;
	cloud_data->temp_ts  = 0;

	return err;
}

int cloud_encode_button_message_data(struct cloud_msg *output,
				     struct cloud_data *cloud_data)
{
	int err = 0;
	char *buffer;

	err = date_time_uptime_to_unix_time_ms(&cloud_data->btn_ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		cloud_data->btn_ts = 0;
		return err;
	}

	cJSON *root_obj = cJSON_CreateObject();
	cJSON *btn_obj = cJSON_CreateObject();

	if (root_obj == NULL || btn_obj == NULL) {
		cJSON_Delete(root_obj);
		cJSON_Delete(btn_obj);
		return -ENOMEM;
	}

	err += json_add_number(btn_obj, "v", cloud_data->btn_number);
	err += json_add_number(btn_obj, "ts", cloud_data->btn_ts);
	err += json_add_obj(root_obj, "btn", btn_obj);

	if (err) {
		goto exit;
	}

	buffer = cJSON_Print(root_obj);

	printk("Encoded message: %s\n", buffer);

	output->buf = buffer;
	output->len = strlen(buffer);

exit:
	cJSON_Delete(root_obj);

	cloud_data->btn_ts = 0;

	return err;
}
