/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

/**@file
 *@brief Cloud codec library header.
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

/**@file
 *
 * @defgroup cloud_codec Cloud codec
 * @brief    Module that encodes and decodes cloud communication.
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Structure containing battery data published to cloud. */
struct cloud_data_battery {
	/** Battery voltage level. */
	uint16_t bat;
	/** Battery data timestamp. UNIX milliseconds. */
	int64_t bat_ts;
	/** Flag signifying that the data entry is to be published. */
	bool queued;
};

/** @brief Structure containing GPS data published to cloud. */
struct cloud_data_gps {
	/** GPS data timestamp. UNIX milliseconds. */
	int64_t gps_ts;
	/** Longitude */
	double longi;
	/** Latitude */
	double lat;
	/** Altitude above WGS-84 ellipsoid in meters. */
	float alt;
	/** Accuracy in (2D 1-sigma) in meters. */
	float acc;
	/** Horizontal speed in meters. */
	float spd;
	/** Heading of movement in degrees. */
	float hdg;
	/** Flag signifying that the data entry is to be published. */
	bool queued;
};

struct cloud_data_cfg {
	/** Device mode configurations. */
	bool act;
	/** GPS search timeout. */
	int gpst;
	/** Time between cloud publications in active mode. */
	int actw;
	/** Time between cloud publications in passive mode. */
	int pasw;
	/** Time between cloud publications regardless of mode. */
	int movt;
	/** Accelerometer trigger threshold value. */
	int acct;
};

struct cloud_data_accelerometer {
	/** Accelerometer readings timestamp. UNIX milliseconds. */
	int64_t ts;
	/** Accelerometer readings. */
	double values[3];
	/** Flag signifying that the data entry is to be published. */
	bool queued;
};

struct cloud_data_sensors {
	/** Environmental sensors timestamp. UNIX milliseconds. */
	int64_t env_ts;
	/** Temperature in celcius */
	double temp;
	/** Humidity level in percentage */
	double hum;
	/** Flag signifying that the data entry is to be published. */
	bool queued;
};

struct cloud_data_modem {
	/** Dynamic modem data timestamp. UNIX milliseconds. */
	int64_t mod_ts;
	/** Static modem data timestamp. UNIX milliseconds. */
	int64_t mod_ts_static;
	/** Area code. */
	uint16_t area;
	/** Cell id. */
	uint16_t cell;
	/** Band number. */
	uint16_t bnd;
	/** Network mode GPS. */
	uint16_t nw_gps;
	/** Network mode LTE-M. */
	uint16_t nw_lte_m;
	/** Network mode NB-IoT. */
	uint16_t nw_nb_iot;
	/** Reference Signal Received Power. */
	uint16_t rsrp;
	/** Internet Protocol Address. */
	char *ip;
	/* Mobile Country Code*/
	char *mccmnc;
	/** Application version and Mobile Network Code. */
	char *appv;
	/** Device board version. */
	const char *brdv;
	/** Modem firmware. */
	char *fw;
	/** Integrated Circuit Card Identifier. */
	char *iccid;
	/** Flag signifying that the data entry is to be published. */
	bool queued;
};

struct cloud_data_ui {
	/** Button number. */
	int btn;
	/** Button data timestamp. UNIX milliseconds. */
	int64_t btn_ts;
	/** Flag signifying that the data entry is to be published. */
	bool queued;
};

struct cloud_codec_data {
	/** Encoded output. */
	char *buf;
	/** Length of encoded output. */
	size_t len;
};

int cloud_codec_decode_response(char *input, struct cloud_data_cfg *cfg);

int cloud_codec_encode_cfg_data(struct cloud_codec_data *output,
				struct cloud_data_cfg *cfg_buffer);

int cloud_codec_encode_data(struct cloud_codec_data *output,
			    struct cloud_data_gps *gps_buf,
			    struct cloud_data_sensors *sensor_buf,
			    struct cloud_data_modem *modem_buf,
			    struct cloud_data_ui *ui_buf,
			    struct cloud_data_accelerometer *accel_buf,
			    struct cloud_data_battery *bat_buf);

int cloud_codec_encode_ui_data(struct cloud_codec_data *output,
			       struct cloud_data_ui *ui_buf);

int cloud_codec_encode_gps_buffer(struct cloud_codec_data *output,
				  struct cloud_data_gps *data);

int cloud_codec_encode_modem_buffer(struct cloud_codec_data *output,
				    struct cloud_data_modem *data);

int cloud_codec_encode_sensor_buffer(struct cloud_codec_data *output,
				     struct cloud_data_sensors *data);

int cloud_codec_encode_ui_buffer(struct cloud_codec_data *output,
				 struct cloud_data_ui *data);

int cloud_codec_encode_accel_buffer(struct cloud_codec_data *output,
				    struct cloud_data_accelerometer *data);

int cloud_codec_encode_bat_buffer(struct cloud_codec_data *output,
				  struct cloud_data_battery *data);

void cloud_codec_populate_sensor_buffer(
				struct cloud_data_sensors *sensor_buffer,
				struct cloud_data_sensors *new_sensor_data,
				int *head_sensor_buf);

void cloud_codec_populate_ui_buffer(struct cloud_data_ui *ui_buffer,
				    struct cloud_data_ui *new_ui_data,
				    int *head_ui_buf);

void cloud_codec_populate_accel_buffer(
				struct cloud_data_accelerometer *accel_buf,
				struct cloud_data_accelerometer *new_accel_data,
				int *head_accel_buf);

void cloud_codec_populate_bat_buffer(struct cloud_data_battery *bat_buffer,
				     struct cloud_data_battery *new_bat_data,
				     int *head_bat_buf);

void cloud_codec_populate_gps_buffer(struct cloud_data_gps *gps_buffer,
				     struct cloud_data_gps *new_gps_data,
				     int *head_gps_buf);

void cloud_codec_populate_modem_buffer(struct cloud_data_modem *modem_buffer,
				       struct cloud_data_modem *new_modem_data,
				       int *head_modem_buf);

static inline void cloud_codec_release_data(struct cloud_codec_data *output)
{
	free(output->buf);
}

#ifdef __cplusplus
}
#endif
#endif
