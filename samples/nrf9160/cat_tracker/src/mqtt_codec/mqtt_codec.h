#ifndef MQTT_CODEC_H__
#define MQTT_CODEC_H__

#include <zephyr.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*This struct is too large, can be divided up */

typedef struct Sync_data {
	int bat_voltage;
	long int bat_timestamp;

	double longitude;
	double latitude;
	float altitude;
	float accuracy;
	float speed;
	float heading;
	long int gps_timestamp;

	double acc[3];
	long int acc_timestamp;

	int gps_timeout;
	bool active;
	int active_wait;
	int passive_wait;
	int movement_timeout;
	int accel_threshold;

	bool gps_found;

} Sync_data;

struct Transmit_data {
	char *buf;
	size_t len;
	u8_t *topic;
};

int decode_response(char *input, struct Sync_data *sync_data);

int encode_message(struct Transmit_data *output, struct Sync_data *sync_data);

bool check_config_change();

#ifdef __cplusplus
}
#endif

#endif