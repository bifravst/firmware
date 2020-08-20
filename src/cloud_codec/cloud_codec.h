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

/** @brief Combinations of data entries to be encoded. */
enum cloud_data_encode_schema {
	CLOUD_DATA_ENCODE_MSTAT_MDYN_SENS_BAT,
	CLOUD_DATA_ENCODE_MSTAT_MDYN_SENS_BAT_GPS,
	CLOUD_DATA_ENCODE_MSTAT_MDYN_SENS_BAT_GPS_ACCEL,
	CLOUD_DATA_ENCODE_MSTAT_MDYN_SENS_BAT_ACCEL,
	CLOUD_DATA_ENCODE_MDYN_SENS_BAT,
	CLOUD_DATA_ENCODE_MDYN_SENS_BAT_GPS,
	CLOUD_DATA_ENCODE_MDYN_SENS_BAT_GPS_ACCEL,
	CLOUD_DATA_ENCODE_MDYN_SENS_BAT_ACCEL,
	CLOUD_DATA_ENCODE_UI
};

/** @brief Structure containing battery data published to cloud. */
struct cloud_data_battery {
	uint16_t bat;

	int64_t bat_ts;

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
	/** Flag signifying if the GPS fix is to be published, aux variable. */
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

	bool queued;
};

struct cloud_data_sensors {
	/** Environmental sensors timestamp. UNIX milliseconds. */
	int64_t env_ts;
	/** Temperature in celcius */
	double temp;
	/** Humidity level in percentage */
	double hum;

	bool queued;
};

struct cloud_data_modem {
	/** Modem data timestamp. UNIX milliseconds. */
	int64_t mod_ts;
	int64_t mod_ts_static;
	uint16_t area;
	uint16_t cell;
	uint16_t bnd;
	uint16_t nw_gps;
	uint16_t nw_lte_m;
	uint16_t nw_nb_iot;
	uint16_t rsrp;
	char *ip;
	char *mccmnc;
	char *appv;
	const char *brdv;
	char *fw;
	char *iccid;
	bool queued;
};

struct cloud_data_ui {
	int64_t btn_ts;
	int btn;
	bool queued;
};

int cloud_codec_decode_response(char *input, struct cloud_data_cfg *cfg);

int cloud_codec_encode_cfg_data(struct cloud_msg *output,
				struct cloud_data_cfg *cfg_buffer);

int cloud_codec_encode_data(struct cloud_msg *output,
			    struct cloud_data_gps *gps_buf,
			    struct cloud_data_sensors *sensor_buf,
			    struct cloud_data_modem *modem_buf,
			    struct cloud_data_ui *ui_buf,
			    struct cloud_data_accelerometer *accel_buf,
			    struct cloud_data_battery *bat_buf,
			    enum cloud_data_encode_schema encode_schema);

int cloud_codec_encode_gps_buffer(struct cloud_msg *output,
				  struct cloud_data_gps *data);

int cloud_codec_encode_modem_buffer(struct cloud_msg *output,
				    struct cloud_data_modem *data);

int cloud_codec_encode_sensor_buffer(struct cloud_msg *output,
				     struct cloud_data_sensors *data);

int cloud_codec_encode_ui_buffer(struct cloud_msg *output,
				 struct cloud_data_ui *data);

int cloud_codec_encode_accel_buffer(struct cloud_msg *output,
				    struct cloud_data_accelerometer *data);

int cloud_codec_encode_bat_buffer(struct cloud_msg *output,
				  struct cloud_data_battery *data);

static inline void cloud_codec_release_data(struct cloud_msg *output)
{
	free(output->buf);
}

#ifdef __cplusplus
}
#endif
#endif
