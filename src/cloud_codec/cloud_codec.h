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

/** @brief Structure containing GPS data published to cloud. */
struct cloud_data_gps {
	/** GPS data timestamp. UNIX milliseconds. */
	s64_t gps_ts;
	/** Longitude */
	double longitude;
	/** Latitude */
	double latitude;
	/** Altitude above WGS-84 ellipsoid in meters. */
	float altitude;
	/** Accuracy in (2D 1-sigma) in meters. */
	float accuracy;
	/** Horizontal speed in meters. */
	float speed;
	/** Heading of movement in degrees. */
	float heading;
	/** Flag signifying if the GPS fix is to be
	 *  published, aux variable.
	 */
	bool queued;
};

/** @brief Structure containing data published to cloud. */
struct cloud_data {
	/** Batter voltage timestamp. UNIX milliseconds. */
	s64_t bat_ts;
	/** Accelerometer readings timestamp. UNIX milliseconds. */
	s64_t acc_ts;
	/** Modem data timestamp. UNIX milliseconds. */
	s64_t mod_ts;
	/** Button pushed timestamp. UNIX milliseconds. */
	s64_t btn_ts;
	/** Environmental sensors timestamp. UNIX milliseconds. */
	s64_t env_ts;
	/** Accelerometer readings. */
	double acc[3];
	/** Device mode configurations. */
	bool active;
	/** GPS search timeout. */
	int gps_timeout;
	/** Time between cloud publications
	 *  in active mode.
	 */
	int active_wait;
	/** Time between cloud publications
	 *  in passive mode.
	 */
	int passive_wait;
	/** Time between cloud publications
	 *  regardless of mode.
	 */
	int mov_timeout;
	/** Accelerometer trigger threshold value. */
	int acc_thres;
	/** Button number published to cloud. */
	int btn_number;
	/** Modem RSRP value. */
	int rsrp;
	/** Temperature in celcius */
	double temp;
	/** Humidity level in percentage */
	double hum;
	/** GPS found trigger flag, aux variable. */
	bool gps_found;
	/** Accelerometer trigger flag, aux variable. */
	bool acc_trig;
	/** Cloud synchronization flag, aux variable. */
	bool synch;
};

/**
 * @brief Decodes a receiving cloud response/message.
 *
 * @param[in]  input      Pointer to a cloud response message.
 * @param[out] cloud_data Pointer to a structure containing cloud data.
 *
 * @return 0 on success or negative error value on failure.
 */
int cloud_decode_response(char *input, struct cloud_data *cloud_data);

/**
 * @brief Encodes sensor data.
 *
 * @param[out] output      Pointer to a structure containing the encoded data
 *                         to be published.
 * @param[in]  cloud_data  Pointer to a structure containing cloud data.
 * @param[in]  cir_buf_gps Pointer to a structure containing GPS data.
 * @param[in]  modem_info  Pointer to a structure containing modem data.
 *
 * @return 0 on success or negative error value on failure.
 */
int cloud_encode_sensor_data(struct cloud_msg *output,
			     struct cloud_data *cloud_data,
			     struct cloud_data_gps *cir_buf_gps,
			     struct modem_param_info *modem_info);

/**
 * @brief Encodes buffered GPS fix entries from the circular GPS buffer.
 *
 * @param[out] output      Pointer to a structure containing the encoded data
 *                         to be published.
 * @param[in]  cir_buf_gps Pointer to a structure containing GPS data.
 *
 * @return 0 on success or negative error value on failure.
 */
int cloud_encode_gps_buffer(struct cloud_msg *output,
			    struct cloud_data_gps *cir_buf_gps);

/**
 * @brief Encodes device configuration.
 *
 * @param[out] output      Pointer to a structure containing the encoded data
 *                         to be published.
 * @param[in]  cloud_data  Pointer to a structure containing device configuraton.
 *
 * @return 0 on success or negative error value on failure.
 */
int cloud_encode_cfg_data(struct cloud_msg *output,
			  struct cloud_data *cloud_data);

/**
 * @brief Encodes messages triggered by button push.
 *
 * @param[out] output      Pointer to a structure containing the encoded data
 *                         to be published.
 * @param[in]  cloud_data  Pointer to a structure containing button data.
 *
 * @return 0 on success or negative error value on failure.
 */
int cloud_encode_button_message_data(struct cloud_msg *output,
				     struct cloud_data *cloud_data);

/**
 * @brief Releases dynamically allocated data allocated by the library.
 *
 * @param[in] data Pointer to data to be freed.
 */
static inline void cloud_release_data(struct cloud_msg *data)
{
	free(data->buf);
}

#ifdef __cplusplus
}
#endif
#endif
