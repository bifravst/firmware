#include <mqtt_codec.h>

#include <stdbool.h>
#include <string.h>
#include <zephyr.h>
#include <zephyr/types.h>

#include "cJSON.h"
#include "cJSON_os.h"

bool change_gpst;
bool change_active;
bool change_active_wait;
bool change_passive_wait;
bool change_movement_timeout;
bool change_accel_threshold;
bool change_config;

typedef struct Digital_twin {
	cJSON *gpst;
	cJSON *active;
	cJSON *active_wait;
	cJSON *passive_wait;
	cJSON *movement_timeout;
	cJSON *accel_threshold;
} Digital_twin;

static int json_add_obj(cJSON *parent, const char *str, cJSON *item)
{
	cJSON_AddItemToObject(parent, str, item);

	return 0;
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

int decode_response(char *input, struct Sync_data *sync_data)
{
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
		goto end;
	}

get_data:

	if (gpst != NULL) {
		sync_data->gps_timeout = gpst->valueint;
		printk("SETTING GPST TO: %d\n", gpst->valueint);
		change_gpst = true;
		change_config = true;
	}

	if (active != NULL) {
		sync_data->active = active->valueint;
		printk("SETTING ACTIVE TO: %d\n", active->valueint);
		change_active = true;
		change_config = true;
	}

	if (active_wait != NULL) {
		sync_data->active_wait = active_wait->valueint;
		printk("SETTING ACTIVE WAIT TO: %d\n", active_wait->valueint);
		change_active_wait = true;
		change_config = true;
	}

	if (passive_wait != NULL) {
		sync_data->passive_wait = passive_wait->valueint;
		printk("SETTING PASSIVE_WAIT TO: %d\n", passive_wait->valueint);
		change_passive_wait = true;
		change_config = true;
	}

	if (movement_timeout != NULL) {
		sync_data->movement_timeout = movement_timeout->valueint;
		printk("SETTING MOVEMENT TIMEOUT TO: %d\n",
		       movement_timeout->valueint);
		change_movement_timeout = true;
		change_config = true;
	}

	if (accel_threshold != NULL) {
		sync_data->accel_threshold = accel_threshold->valueint;
		printk("SETTING ACCEL THRESHOLD TIMEOUT TO: %d\n",
		       accel_threshold->valueint);
		change_accel_threshold = true;
		change_config = true;
	}
end:
	cJSON_Delete(root_obj);
	return 0;
}

int encode_message(struct Transmit_data *output, struct Sync_data *sync_data)
{
	int err;
	char *buffer;

	cJSON *root_obj = cJSON_CreateObject();
	cJSON *state_obj = cJSON_CreateObject();
	cJSON *reported_obj = cJSON_CreateObject();
	cJSON *bat_obj = cJSON_CreateObject();
	cJSON *acc_obj = cJSON_CreateObject();
	cJSON *gps_obj = cJSON_CreateObject();
	cJSON *cfg_obj = cJSON_CreateObject();
	cJSON *gps_val_obj = cJSON_CreateObject();

	if (root_obj == NULL || state_obj == NULL || reported_obj == NULL ||
	    bat_obj == NULL || acc_obj == NULL || cfg_obj == NULL ||
	    gps_obj == NULL || gps_val_obj == NULL) {
		cJSON_Delete(root_obj);
		cJSON_Delete(state_obj);
		cJSON_Delete(reported_obj);
		cJSON_Delete(bat_obj);
		cJSON_Delete(acc_obj);
		cJSON_Delete(gps_obj);
		cJSON_Delete(cfg_obj);
		cJSON_Delete(gps_val_obj);
		return -ENOMEM;
	}

	/*BAT*/
	err = json_add_number(bat_obj, "v", sync_data->bat_voltage);
	err += json_add_str(bat_obj, "ts", sync_data->bat_timestamp);

	/*ACC*/
	err += json_add_DoubleArray(acc_obj, "v", sync_data->acc);
	err += json_add_str(acc_obj, "ts", sync_data->acc_timestamp);

	/*GPS*/
	err += json_add_number(gps_val_obj, "lng", sync_data->longitude);
	err += json_add_number(gps_val_obj, "lat", sync_data->latitude);
	err += json_add_number(gps_val_obj, "acc", sync_data->accuracy);
	err += json_add_number(gps_val_obj, "alt", sync_data->altitude);
	err += json_add_number(gps_val_obj, "spd", sync_data->speed);
	err += json_add_number(gps_val_obj, "hdg", sync_data->heading);

	/*CFG*/

	if (change_gpst) {
		err += json_add_number(cfg_obj, "gpst", sync_data->gps_timeout);
	}

	if (change_active) {
		err += json_add_bool(cfg_obj, "act", sync_data->active);
	}

	if (change_active_wait) {
		err += json_add_number(cfg_obj, "actwt",
				       sync_data->active_wait);
	}

	if (change_passive_wait) {
		err += json_add_number(cfg_obj, "mvres",
				       sync_data->passive_wait);
	}

	if (change_movement_timeout) {
		err += json_add_number(cfg_obj, "mvt",
				       sync_data->movement_timeout);
	}

	if (change_accel_threshold) {
		err += json_add_number(cfg_obj, "acct",
				       sync_data->accel_threshold);
	}

	if (err != 0) {
		cJSON_Delete(root_obj);
		cJSON_Delete(state_obj);
		cJSON_Delete(reported_obj);
		cJSON_Delete(bat_obj);
		cJSON_Delete(acc_obj);
		cJSON_Delete(gps_obj);
		cJSON_Delete(cfg_obj);
		cJSON_Delete(gps_val_obj);
		return -ENOMEM;
	}

	if (!change_config) {
		if (sync_data->active == true &&
		    sync_data->gps_found == false) {
			err = json_add_obj(reported_obj, "bat", bat_obj);
		}

		if (sync_data->active == true && sync_data->gps_found == true) {
			err = json_add_obj(reported_obj, "bat", bat_obj);

			err += json_add_obj(gps_obj, "v", gps_val_obj);
			err += json_add_str(gps_obj, "ts",
					    sync_data->acc_timestamp);
			err += json_add_obj(reported_obj, "gps", gps_obj);
		}

		if (sync_data->active == false &&
		    sync_data->gps_found == false) {
			err = json_add_obj(reported_obj, "bat", bat_obj);

			err += json_add_obj(reported_obj, "acc", acc_obj);
		}

		if (sync_data->active == true && sync_data->gps_found == true) {
			err = json_add_obj(reported_obj, "bat", bat_obj);

			err += json_add_obj(reported_obj, "acc", acc_obj);

			err += json_add_obj(gps_obj, "v", gps_val_obj);
			err += json_add_str(gps_obj, "ts",
					    sync_data->acc_timestamp);
			err += json_add_obj(reported_obj, "gps", gps_obj);
		}
	}

	if (change_gpst || change_active || change_active_wait ||
	    change_passive_wait || change_movement_timeout ||
	    change_accel_threshold) {
		err += json_add_obj(reported_obj, "cfg", cfg_obj);
		change_gpst = change_active = change_active_wait =
			change_passive_wait = change_movement_timeout =
				change_accel_threshold = false;
		change_config = false;
	}

	err += json_add_obj(state_obj, "reported", reported_obj);
	err += json_add_obj(root_obj, "state", state_obj);

	if (err != 0) {
		cJSON_Delete(root_obj);
		cJSON_Delete(state_obj);
		cJSON_Delete(reported_obj);
		cJSON_Delete(bat_obj);
		cJSON_Delete(acc_obj);
		cJSON_Delete(gps_obj);
		cJSON_Delete(cfg_obj);
		cJSON_Delete(gps_val_obj);
		return -EAGAIN;
	}

	buffer = cJSON_Print(root_obj);

	output->buf = buffer;
	output->len = strlen(buffer);

	cJSON_Delete(root_obj);

	return 0;
}

bool check_config_change()
{
	return change_config;
}