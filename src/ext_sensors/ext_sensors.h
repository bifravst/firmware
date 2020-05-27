/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */
/**@file
 *
 * @defgroup External sensors ext_sensors
 * @brief    Module that manages external sensors.
 * @{
 */

#ifndef EXT_SENSORS_H__
#define EXT_SENSORS_H__

#include <cloud_codec.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ACCELEROMETER_CHANNELS 3

enum ext_sensor_evt_type {
	EXT_SENSOR_EVT_ACCELEROMETER_TRIGGER,
};

struct ext_sensor_evt {
	/** Sensor type. */
	enum ext_sensor_evt_type type;
	/* Event data. */
	union {
		double value_array[ACCELEROMETER_CHANNELS];
		/* Single external sensor value. */
		double value;
	};
};

typedef void (*ext_sensors_evt_handler_t)(
	const struct ext_sensor_evt *const evt);

int ext_sensors_init(ext_sensors_evt_handler_t handler);

int ext_sensors_temperature_get(struct cloud_data *cloud_data);

int ext_sensors_humidity_get(struct cloud_data *cloud_data);

void ext_sensors_accelerometer_threshold_set(struct cloud_data *cloud_data);

#ifdef __cplusplus
}
#endif
#endif
