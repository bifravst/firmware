#ifndef CLOUD_CODEC_H__
#define CLOUD_CODEC_H__

#include <zephyr.h>
#include <stdbool.h>
#include <stdint.h>
#include <net/cloud.h>
#include <modem_info.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cloud_data_gps {
	double longitude;
	double latitude;
	float altitude;
	float accuracy;
	float speed;
	float heading;
	s64_t gps_timestamp;
	bool queued;
};

struct cloud_data {
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

	s64_t roam_modem_data_ts;
	s64_t dev_modem_data_ts;
};

int cloud_decode_response(char *input, struct cloud_data *cloud_data);

int cloud_encode_sensor_data(struct cloud_msg *output,
			     struct cloud_data *cloud_data,
			     struct cloud_data_gps *cir_buf_gps);

int cloud_encode_gps_buffer(struct cloud_msg *output,
			    struct cloud_data_gps *cir_buf_gps);

int cloud_encode_modem_data(struct cloud_msg *output,
			    struct cloud_data *cloud_data,
			    struct modem_param_info *modem_info,
			    bool include_dev_data, int rsrp);

int cloud_encode_cfg_data(struct cloud_msg *output,
			  struct cloud_data *cloud_data);

static inline void cloud_release_data(struct cloud_msg *data)
{
	free(data->buf);
}

#ifdef __cplusplus
}
#endif
#endif
