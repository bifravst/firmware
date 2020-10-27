/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#ifndef _SENSOR_MGR_EVENT_H_
#define _SENSOR_MGR_EVENT_H_

/**
 * @brief Sensor Event
 * @defgroup sensor_mgr_event Sensor Event
 * @{
 */

#include "cloud_codec.h"
#include "event_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Sensor event types submitted by Sensor manager. */
enum sensor_mgr_event_types {
	SENSOR_MGR_EVT_MOVEMENT_DATA_READY,
	SENSOR_MGR_EVT_ENVIRONMENTAL_DATA_READY,
	SENSOR_MGR_EVT_SHUTDOWN_READY,
	SENSOR_MGR_EVT_ERROR
};

/** @brief Sensor event. */
struct sensor_mgr_event {
	struct event_header header;
	enum sensor_mgr_event_types type;
	union {
		struct cloud_data_sensors sensors;
		struct cloud_data_accelerometer accel;
		int err;
	} data;
};

EVENT_TYPE_DECLARE(sensor_mgr_event);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _SENSOR_MGR_EVENT_H_ */
