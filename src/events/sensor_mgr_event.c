/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <stdio.h>

#include "sensor_mgr_event.h"

static int log_sensor_mgr_event(const struct event_header *eh, char *buf,
			    size_t buf_len)
{
	const struct sensor_mgr_event *event = cast_sensor_mgr_event(eh);
	char event_name[50] = "\0";

	switch (event->type) {
	case SENSOR_MGR_EVT_MOVEMENT_DATA_READY:
		strcpy(event_name, "SENSOR_MGR_EVT_MOVEMENT_DATA_READY");
		break;
	case SENSOR_MGR_EVT_ENVIRONMENTAL_DATA_READY:
		strcpy(event_name, "SENSOR_MGR_EVT_ENVIRONMENTAL_DATA_READY");
		break;
	case SENSOR_MGR_EVT_SHUTDOWN_READY:
		strcpy(event_name, "SENSOR_MGR_EVT_SHUTDOWN_READY");
		break;
	case SENSOR_MGR_EVT_ERROR:
		strcpy(event_name, "SENSOR_MGR_EVT_ERROR");
		return snprintf(buf, buf_len, "%s - Error code %d",
				event_name, event->data.err);
	default:
		strcpy(event_name, "Unknown event");
		break;
	}

	return snprintf(buf, buf_len, "%s", event_name);
}

EVENT_TYPE_DEFINE(sensor_mgr_event,
		  CONFIG_SENSOR_MGR_EVENTS_LOG,
		  log_sensor_mgr_event,
		  NULL);
