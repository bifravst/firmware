/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */
/**@file
 *
 * @defgroup cloud_codec Cloud codec
 * @brief    Module that encodes and decodes cloud communication.
 * @{
 */

#ifndef CLOUD_CODEC_H__
#define CLOUD_CODEC_H__

#include <zephyr.h>
#include <stdbool.h>
#include <stdint.h>
#include <net/cloud.h>
#include <modem/modem_info.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cloud_data_gps {
	s64_t gps_ts;
	double longitude;
	double latitude;
	float altitude;
	float accuracy;
	float speed;
	float heading;
	bool queued;
};

struct cloud_data {
	s64_t bat_ts;
	s64_t acc_ts;
	s64_t mod_ts;
	s64_t btn_ts;
	s64_t env_ts;
	double acc[3];
	bool active;
	int gps_timeout;
	int active_wait;
	int passive_wait;
	int mov_timeout;
	int acc_thres;
	int btn_number;
	int rsrp;
	double temp;
	double hum;
	bool gps_found;
	bool acc_trig;
	bool synch;
};

int cloud_decode_response(char *input, struct cloud_data *cloud_data);

int cloud_encode_sensor_data(struct cloud_msg *output,
			     struct cloud_data *cloud_data,
			     struct cloud_data_gps *cir_buf_gps,
			     struct modem_param_info *modem_info);

int cloud_encode_gps_buffer(struct cloud_msg *output,
			     struct cloud_data_gps *cir_buf_gps);

int cloud_encode_cfg_data(struct cloud_msg *output,
			     struct cloud_data *cloud_data);

int cloud_encode_button_message_data(struct cloud_msg *output,
				 struct cloud_data *cloud_data);

static inline void cloud_release_data(struct cloud_msg *data)
{
	free(data->buf);
}

#ifdef __cplusplus
}
#endif
#endif
