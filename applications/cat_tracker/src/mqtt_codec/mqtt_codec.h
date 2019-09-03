#ifndef MQTT_CODEC_H__
#define MQTT_CODEC_H__

#include <zephyr.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAX_CIR_BUF 10

struct Sync_data_GPS {
	double longitude;
	double latitude;
	float altitude;
	float accuracy;
	float speed;
	float heading;
	s64_t gps_timestamp;
	bool queued;
};

struct Sync_data {
	int bat_voltage;
	s64_t bat_timestamp;

	double acc[3];
	s64_t acc_timestamp;

	int gps_timeout;
	bool active;
	int active_wait;
	int passive_wait;
	int movement_timeout;
	int accel_threshold;

	bool gps_found;
};

struct Transmit_data {
	char *buf;
	size_t len;
	u8_t *topic;
};

struct Modem_data {
	s64_t static_timestamp;
	s64_t dynamic_timestamp;
};

int decode_response(char *input, struct Sync_data *sync_data);

int encode_message(struct Transmit_data *output, struct Sync_data *sync_data,
		   struct Sync_data_GPS *cir_buf_gps);

int encode_gps_buffer(struct Transmit_data *output,
		      struct Sync_data_GPS *cir_buf_gps, int max_per_publish);

int encode_modem_data(struct Transmit_data *output, bool syncronization);

bool check_config_change(void);

#ifdef __cplusplus
}
#endif
#endif
