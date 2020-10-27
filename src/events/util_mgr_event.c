/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <stdio.h>

#include "util_mgr_event.h"

static int log_util_mgr_event(const struct event_header *eh, char *buf,
			  size_t buf_len)
{
	const struct util_mgr_event *event = cast_util_mgr_event(eh);
	char event_name[50] = "\0";

	switch (event->type) {
	case UTIL_MGR_EVT_SHUTDOWN_REQUEST:
		strcpy(event_name, "UTIL_MGR_EVT_SHUTDOWN_REQUEST");
		break;
	default:
		break;
	}

	return snprintf(buf, buf_len, "%s", event_name);
}

EVENT_TYPE_DEFINE(util_mgr_event,
		  CONFIG_UTIL_MGR_EVENTS_LOG,
		  log_util_mgr_event,
		  NULL);
