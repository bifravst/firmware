/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <stdio.h>

#include "modem_mgr_event.h"

static int log_modem_mgr_event(const struct event_header *eh, char *buf,
			   size_t buf_len)
{
	const struct modem_mgr_event *event = cast_modem_mgr_event(eh);
	char event_name[50] = "\0";

	switch (event->type) {
	case MODEM_MGR_EVT_LTE_CONNECTED:
		strcpy(event_name, "MODEM_MGR_EVT_LTE_CONNECTED");
		break;
	case MODEM_MGR_EVT_LTE_DISCONNECTED:
		strcpy(event_name, "MODEM_MGR_EVT_LTE_DISCONNECTED");
		break;
	case MODEM_MGR_EVT_LTE_CONNECTING:
		strcpy(event_name, "MODEM_MGR_EVT_LTE_CONNECTING");
		break;
	case MODEM_MGR_EVT_MODEM_DATA_READY:
		strcpy(event_name, "MODEM_MGR_EVT_MODEM_DATA_READY");
		break;
	case MODEM_MGR_EVT_BATTERY_DATA_READY:
		strcpy(event_name, "MODEM_MGR_EVT_BATTERY_DATA_READY");
		break;
	case MODEM_MGR_EVT_DATE_TIME_OBTAINED:
		strcpy(event_name, "MODEM_MGR_EVT_DATE_TIME_OBTAINED");
		break;
	case MODEM_MGR_EVT_SHUTDOWN_READY:
		strcpy(event_name, "MODEM_MGR_EVT_SHUTDOWN_READY");
		break;
	case MODEM_MGR_EVT_ERROR:
		strcpy(event_name, "MODEM_MGR_EVT_ERROR");
		return snprintf(buf, buf_len, "%s - Error code %d",
				event_name, event->data.err);
	default:
		strcpy(event_name, "Unknown event");
		break;
	}

	return snprintf(buf, buf_len, "%s", event_name);
}

EVENT_TYPE_DEFINE(modem_mgr_event,
		  CONFIG_MODEM_MGR_EVENTS_LOG,
		  log_modem_mgr_event,
		  NULL);
