/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <stdio.h>

#include "cloud_mgr_event.h"

static int log_cloud_mgr_event(const struct event_header *eh, char *buf,
			   size_t buf_len)
{
	const struct cloud_mgr_event *event = cast_cloud_mgr_event(eh);
	char event_name[50] = "\0";

	switch (event->type) {
	case CLOUD_MGR_EVT_CONNECTED:
		strcpy(event_name, "CLOUD_MGR_EVT_CONNECTED");
		break;
	case CLOUD_MGR_EVT_DISCONNECTED:
		strcpy(event_name, "CLOUD_MGR_EVT_DISCONNECTED");
		break;
	case CLOUD_MGR_EVT_CONNECTING:
		strcpy(event_name, "CLOUD_MGR_EVT_CONNECTING");
		break;
	case CLOUD_MGR_EVT_CONFIG_RECEIVED:
		strcpy(event_name, "CLOUD_MGR_EVT_CONFIG_RECEIVED");
		break;
	case CLOUD_MGR_EVT_SHARED_DATA_DONE:
		strcpy(event_name, "CLOUD_MGR_EVT_SHARED_DATA_DONE");
		break;
	case CLOUD_MGR_EVT_SHUTDOWN_READY:
		strcpy(event_name, "CLOUD_MGR_SHUTDOWN_READY");
		break;
	case CLOUD_MGR_EVT_ERROR:
		strcpy(event_name, "CLOUD_MGR_EVT_ERROR");
		return snprintf(buf, buf_len, "%s - Error code %d",
				event_name, event->data.err);
	default:
		strcpy(event_name, "Unknown event");
		break;
	}

	return snprintf(buf, buf_len, "%s", event_name);
}

EVENT_TYPE_DEFINE(cloud_mgr_event,
		  CONFIG_CLOUD_MGR_EVENTS_LOG,
		  log_cloud_mgr_event,
		  NULL);
