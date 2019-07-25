#include <mqtt_codec.h>

#include <stdbool.h>
#include <string.h>
#include <zephyr.h>
#include <zephyr/types.h>

#include "cJSON.h"
#include "cJSON_os.h"

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

int decode_response(char *input, struct Sync_data *sync_data,
		    bool initial_connection)
{
	cJSON *state = NULL;
	cJSON *desired = NULL;
	cJSON *cfg = NULL;

	cJSON *gpst = NULL;
	cJSON *active = NULL;
	cJSON *active_wait = NULL;
	cJSON *passive_wait = NULL;
	cJSON *movement_timeout = NULL;

	cJSON *root_obj = cJSON_Parse(input);
	if (root_obj == NULL) {
		return -ENOENT;
	}

	if (!initial_connection) { //need to implement a way to make sure that the item is present, if not abort
		state = cJSON_GetObjectItem(root_obj, "state");
		desired = cJSON_GetObjectItem(state, "desired");
		cfg = cJSON_GetObjectItem(desired, "cfg");

		gpst = cJSON_GetObjectItem(cfg, "gpst");
		active = cJSON_GetObjectItem(cfg, "act");
		active_wait = cJSON_GetObjectItem(cfg, "actwt");
		passive_wait = cJSON_GetObjectItem(cfg, "mvres");
		movement_timeout = cJSON_GetObjectItem(cfg, "mvt");

		//initial_connection = true;
	} else {
		state = cJSON_GetObjectItem(root_obj, "state");
		cfg = cJSON_GetObjectItem(state, "cfg");

		gpst = cJSON_GetObjectItem(cfg, "gpst");
		active = cJSON_GetObjectItem(cfg, "act");
		active_wait = cJSON_GetObjectItem(cfg, "actwt");
		passive_wait = cJSON_GetObjectItem(cfg, "mvres");
		movement_timeout = cJSON_GetObjectItem(cfg, "mvt");
	}

	if (gpst != NULL) {
		sync_data->gps_timeout = gpst->valueint;
		printk("SETTING GPST TO: %d\n", gpst->valueint);
	}

	if (active != NULL) {
		sync_data->active = active->valueint;
		printk("SETTING ACTIVE TO: %d\n", active->valueint);
	}

	if (active_wait != NULL) {
		sync_data->active_wait = active_wait->valueint;
		printk("SETTING ACTIVE WAIT TO: %d\n", active_wait->valueint);
	}

	if (passive_wait != NULL) {
		sync_data->passive_wait = passive_wait->valueint;
		printk("SETTING PASSIVE_WAIT TO: %d\n", passive_wait->valueint);
	}

	if (movement_timeout != NULL) {
		sync_data->movement_timeout = movement_timeout->valueint;
		printk("SETTING MOVEMENT TIMEOUT TO: %d\n",
		       movement_timeout->valueint);
	}

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
	    bat_obj == NULL || acc_obj == NULL || gps_obj == NULL ||
	    cfg_obj == NULL || gps_val_obj == NULL) {
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
	err += json_add_number(cfg_obj, "gpst", sync_data->gps_timeout);
	err += json_add_bool(cfg_obj, "act", sync_data->active);
	err += json_add_number(cfg_obj, "actwt", sync_data->active_wait);
	err += json_add_number(cfg_obj, "mvres", sync_data->passive_wait);
	err += json_add_number(cfg_obj, "mvt", sync_data->movement_timeout);

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

	err = json_add_obj(reported_obj, "bat", bat_obj);
	err += json_add_obj(reported_obj, "acc", acc_obj);

	err += json_add_obj(gps_obj, "v", gps_val_obj);
	err += json_add_str(gps_obj, "ts", sync_data->acc_timestamp);

	err += json_add_obj(reported_obj, "gps", gps_obj);

	err += json_add_obj(reported_obj, "cfg", cfg_obj);

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