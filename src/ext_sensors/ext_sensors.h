/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

/**@file
 *@brief External sensors library header.
 */

#ifndef EXT_SENSORS_H__
#define EXT_SENSORS_H__

#include <cloud_codec.h>

/**@file
 *
 * @defgroup External sensors ext_sensors
 * @brief    Module that manages external sensors.
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Number of accelerometer channels. */
#define ACCELEROMETER_CHANNELS 3

/** @brief Enum containing callback events from library. */
enum ext_sensor_evt_type {
	EXT_SENSOR_EVT_ACCELEROMETER_TRIGGER,
};

/** @brief Structure containing external sensor data. */
struct ext_sensor_evt {
	/** Sensor type. */
	enum ext_sensor_evt_type type;
	/** Event data. */
	union {
		/** Array of external sensor values. */
		double value_array[ACCELEROMETER_CHANNELS];
		/** Single external sensor value. */
		double value;
	};
};

/** @brief External sensors library asynchronous event handler.
 *
 *  @param[in] evt The event and any associated parameters.
 */
typedef void (*ext_sensor_handler_t)(
	const struct ext_sensor_evt *const evt);

/**
 * @brief Initializes the library, sets callback handler.
 *
 * @param[in] handler Pointer to callback handler.
 *
 * @return 0 on success or negative error value on failure.
 */
int ext_sensors_init(ext_sensor_handler_t handler);

/**
 * @brief Get temperature from library.
 *
 * @param[out] cloud_data Pointer to structure containing cloud data.
 *
 * @return 0 on success or negative error value on failure.
 */
int ext_sensors_temperature_get(double *ext_temp);

/**
 * @brief Get humidity from library.
 *
 * @param[out] cloud_data Pointer to structure containing cloud data.
 *
 * @return 0 on success or negative error value on failure.
 */
int ext_sensors_humidity_get(double *ext_hum);

/**
 * @brief Set the threshold that triggeres callback on accelerometer data.
 *
 * @param[in] cloud_data Pointer to structure containing cloud data.
 *
 * @return 0 on success or negative error value on failure.
 */
void ext_sensors_mov_thres_set(int acc_thresh);

#ifdef __cplusplus
}
#endif
#endif
