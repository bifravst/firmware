/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <stdio.h>

#include "output_mgr_event.h"

static int log_output_mgr_event(const struct event_header *eh, char *buf,
				size_t buf_len)
{
	const struct output_mgr_event *event = cast_output_mgr_event(eh);
	char event_name[50] = "\0";

	switch (event->type) {
	case OUTPUT_MGR_EVT_SHUTDOWN_READY:
		strcpy(event_name, "OUTPUT_MGR_EVT_SHUTDOWN_READY");
		break;
	case OUTPUT_MGR_EVT_ERROR:
		strcpy(event_name, "OUTPUT_MGR_EVT_ERROR");
		return snprintf(buf, buf_len, "%s - Error code %d",
				event_name, event->err);
	default:
		strcpy(event_name, "Unknown event");
		break;
	}

	return snprintf(buf, buf_len, "%s", event_name);
}

EVENT_TYPE_DEFINE(output_mgr_event,
		  CONFIG_OUTPUT_MGR_EVENTS_LOG,
		  log_output_mgr_event,
		  NULL);
