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
static bool change_movement_timeout = true;
static bool change_accel_threshold = true;

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

static int json_add_DoubleArray(cJSON *parent, const char *str, double *item)
{
	cJSON *json_double;

	json_double = cJSON_CreateDoubleArray(item, 3);
	if (json_double == NULL) {
		return -ENOMEM;
	}

	return json_add_obj(parent, str, json_double);
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
	cJSON *movement_timeout = NULL;
	cJSON *accel_threshold = NULL;

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
		movement_timeout = cJSON_GetObjectItem(group_obj, "mvt");
		accel_threshold = cJSON_GetObjectItem(group_obj, "acct");
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
			movement_timeout =
				cJSON_GetObjectItem(subgroup_obj, "mvt");
			accel_threshold =
				cJSON_GetObjectItem(subgroup_obj, "acct");
		}
	} else {
		goto exit;
	}

get_data:

	if (gpst != NULL) {
		cloud_data->gps_timeout = gpst->valueint;
		LOG_INF("SETTING GPST TO: %d", gpst->valueint);
		change_gpst = true;
	}

	if (active != NULL) {
		cloud_data->active = active->valueint;
		LOG_INF("SETTING ACTIVE TO: %d", active->valueint);
		change_active = true;
	}

	if (active_wait != NULL) {
		cloud_data->active_wait = active_wait->valueint;
		LOG_INF("SETTING ACTIVE WAIT TO: %d", active_wait->valueint);
		change_active_wait = true;
	}

	if (passive_wait != NULL) {
		cloud_data->passive_wait = passive_wait->valueint;
		LOG_INF("SETTING PASSIVE_WAIT TO: %d", passive_wait->valueint);
		change_passive_wait = true;
	}

	if (movement_timeout != NULL) {
		cloud_data->movement_timeout = movement_timeout->valueint;
		LOG_INF("SETTING MOVEMENT TIMEOUT TO: %d",
		       movement_timeout->valueint);
		change_movement_timeout = true;
	}

	if (accel_threshold != NULL) {
		cloud_data->accel_threshold = accel_threshold->valueint;
		LOG_INF("SETTING ACCEL THRESHOLD TIMEOUT TO: %d",
		       accel_threshold->valueint);
		change_accel_threshold = true;
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

	err = date_time_uptime_to_unix_time_ms(&cir_buf_gps->gps_timestamp);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
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
					       "ts", cir_buf_gps->gps_timestamp);
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

int cloud_encode_modem_data(struct cloud_msg *output,
			    struct cloud_data *cloud_data,
			    struct modem_param_info *modem_info,
			    bool include_dev_data, int rsrp)
{
	int err = 0;
	char *buffer;

	static const char lte_string[] = "LTE-M";
	static const char nbiot_string[] = "NB-IoT";
	static const char gps_string[] = " GPS";

	err = date_time_uptime_to_unix_time_ms(&cloud_data->roam_modem_data_ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	err = date_time_uptime_to_unix_time_ms(&cloud_data->dev_modem_data_ts);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	cJSON *root_obj = cJSON_CreateObject();
	cJSON *state_obj = cJSON_CreateObject();
	cJSON *reported_obj = cJSON_CreateObject();
	cJSON *static_m_data = cJSON_CreateObject();
	cJSON *static_m_data_v = cJSON_CreateObject();
	cJSON *dynamic_m_data = cJSON_CreateObject();
	cJSON *dynamic_m_data_v = cJSON_CreateObject();

	if (root_obj == NULL || state_obj == NULL || reported_obj == NULL ||
	    static_m_data == NULL || static_m_data_v == NULL ||
	    dynamic_m_data == NULL || dynamic_m_data_v == NULL) {
		cJSON_Delete(root_obj);
		cJSON_Delete(state_obj);
		cJSON_Delete(reported_obj);
		cJSON_Delete(static_m_data);
		cJSON_Delete(static_m_data_v);
		cJSON_Delete(dynamic_m_data);
		cJSON_Delete(dynamic_m_data_v);
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

	err += json_add_number(static_m_data_v, "band", modem_info->network.current_band.value);
	err += json_add_str(static_m_data_v, "nw", modem_info->network.network_mode);
	err += json_add_str(static_m_data_v, "iccid", modem_info->sim.iccid.value_string);
	err += json_add_str(static_m_data_v, "modV", modem_info->device.modem_fw.value_string);
	err += json_add_str(static_m_data_v, "brdV", modem_info->device.board);
	err += json_add_str(static_m_data_v, "appV", CONFIG_CAT_TRACKER_APP_VERSION);
	err += json_add_number(dynamic_m_data_v, "rsrp", rsrp);
	err += json_add_number(dynamic_m_data_v, "area", modem_info->network.area_code.value);
	err += json_add_number(dynamic_m_data_v, "mccmnc", strtol(modem_info->network.current_operator.value_string, NULL, 10));
	err += json_add_number(dynamic_m_data_v, "cell", modem_info->network.cellid_dec);
	err += json_add_str(dynamic_m_data_v, "ip", modem_info->network.ip_address.value_string);

	if (include_dev_data) {
		err += json_add_obj(static_m_data, "v", static_m_data_v);
		err += json_add_number(static_m_data, "ts", cloud_data->dev_modem_data_ts);

		err += json_add_obj(dynamic_m_data, "v", dynamic_m_data_v);
		err += json_add_number(dynamic_m_data, "ts", cloud_data->roam_modem_data_ts);

		err += json_add_obj(reported_obj, "dev", static_m_data);
		err += json_add_obj(reported_obj, "roam", dynamic_m_data);
	} else {
		err += json_add_obj(dynamic_m_data, "v", dynamic_m_data_v);
		err += json_add_number(dynamic_m_data, "ts", cloud_data->roam_modem_data_ts);

		err += json_add_obj(reported_obj, "roam", dynamic_m_data);
	}

	err += json_add_obj(state_obj, "reported", reported_obj);
	err += json_add_obj(root_obj, "state", state_obj);

	if (err) {
		goto exit;
	}

	buffer = cJSON_Print(root_obj);

	output->buf = buffer;
	output->len = strlen(buffer);

	printk("Encoded message: %s\n", buffer);

exit:

	/* Clear network mode string */
	memset(modem_info->network.network_mode, 0, sizeof(modem_info->network.network_mode));
	cJSON_Delete(root_obj);

	/*Delete objects which are not associated*/
	if (!include_dev_data) {
		cJSON_Delete(static_m_data);
		cJSON_Delete(static_m_data_v);
	}

	return err;;
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

	if (change_movement_timeout) {
		err += json_add_number(cfg_obj, "mvt",
				       cloud_data->movement_timeout);
		change_cnt++;
	}

	if (change_accel_threshold) {
		err += json_add_number(cfg_obj, "acct",
				       cloud_data->accel_threshold);
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
	change_movement_timeout		= false;
	change_accel_threshold		= false;

exit:
	cJSON_Delete(root_obj);
	return err;
}

int cloud_encode_sensor_data(struct cloud_msg *output,
			     struct cloud_data *cloud_data,
			     struct cloud_data_gps *cir_buf_gps)
{
	int err = 0;
	char *buffer;

	err = date_time_uptime_to_unix_time_ms(&cloud_data->bat_timestamp);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	err = date_time_uptime_to_unix_time_ms(&cloud_data->acc_timestamp);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	err = date_time_uptime_to_unix_time_ms(&cir_buf_gps->gps_timestamp);
	if (err) {
		LOG_ERR("date_time_uptime_to_unix_time_ms, error: %d", err);
		return err;
	}

	cJSON *root_obj = cJSON_CreateObject();
	cJSON *state_obj = cJSON_CreateObject();
	cJSON *reported_obj = cJSON_CreateObject();
	cJSON *bat_obj = cJSON_CreateObject();
	cJSON *acc_obj = cJSON_CreateObject();
	cJSON *gps_obj = cJSON_CreateObject();
	cJSON *gps_val_obj = cJSON_CreateObject();

	if (root_obj == NULL || state_obj == NULL || reported_obj == NULL ||
	    bat_obj == NULL || acc_obj == NULL || gps_obj == NULL ||
	    gps_val_obj == NULL) {
		cJSON_Delete(root_obj);
		cJSON_Delete(state_obj);
		cJSON_Delete(reported_obj);
		cJSON_Delete(bat_obj);
		cJSON_Delete(acc_obj);
		cJSON_Delete(gps_obj);
		cJSON_Delete(gps_val_obj);
		return -ENOMEM;
	}

	/*BAT*/
	err += json_add_number(bat_obj, "v", cloud_data->bat_voltage);
	err += json_add_number(bat_obj, "ts", cloud_data->bat_timestamp);

	/*ACC*/
	err += json_add_DoubleArray(acc_obj, "v", cloud_data->acc);
	err += json_add_number(acc_obj, "ts", cloud_data->acc_timestamp);

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
	}

	if (cloud_data->active && cloud_data->gps_found) {
		err += json_add_obj(reported_obj, "bat", bat_obj);
		err += json_add_obj(gps_obj, "v", gps_val_obj);
		err += json_add_number(gps_obj, "ts", cir_buf_gps->gps_timestamp);
		err += json_add_obj(reported_obj, "gps", gps_obj);
	}

	if (!cloud_data->active && !cloud_data->gps_found) {
		err += json_add_obj(reported_obj, "bat", bat_obj);
		err += json_add_obj(reported_obj, "acc", acc_obj);
	}

	if (!cloud_data->active && cloud_data->gps_found) {
		err += json_add_obj(reported_obj, "bat", bat_obj);
		err += json_add_obj(reported_obj, "acc", acc_obj);
		err += json_add_obj(gps_obj, "v", gps_val_obj);
		err += json_add_number(gps_obj, "ts", cir_buf_gps->gps_timestamp);
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

	/*Delete objects which are not associated*/
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
	}

	if (!cloud_data->active && cloud_data->gps_found) {
		/*All objects associated*/
	}

	return err;
}
