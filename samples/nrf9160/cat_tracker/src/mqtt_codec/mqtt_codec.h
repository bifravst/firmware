#ifndef MQTT_CODEC_H__
#define MQTT_CODEC_H__

#include <zephyr.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Sync_data {
	int bat_voltage;
	char bat_timestamp[50];

	double longitude;
	double latitude;
	float altitude;
	float accuracy;
	float speed;
	float heading;
	char gps_timestamp[50];

	double acc[3];
	char acc_timestamp[50];

	int gps_timeout;
	bool active;
	int active_wait;
	int passive_wait;
	int movement_timeout;
	int accel_threshold;

} Sync_data;

struct Transmit_data {
	char *buf;
	size_t len;
	u8_t *topic;
};

int decode_response(char *input, struct Sync_data *sync_data,
		    bool inital_connection);

int encode_message(struct Transmit_data *output, struct Sync_data *sync_data);

bool check_config_change();

#ifdef __cplusplus
}
#endif

#endif