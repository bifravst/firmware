/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#ifndef _APP_MGR_EVENT_H_
#define _APP_MGR_EVENT_H_

/**
 * @brief Application Event
 * @defgroup app_mgr_event Application Event
 * @{
 */

#include "cloud_codec.h"
#include "event_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_DATA_ENVIRONMENTALS "environmentals"
#define APP_DATA_MOVEMENT "movement"
#define APP_DATA_MODEM "modem"
#define APP_DATA_BATTERY "battery"
#define APP_DATA_GPS "gps"

#define APP_DATA_NUMBER_OF_TYPES_MAX 5

/** @brief Application event types submitted by Application manager. */
enum app_mgr_event_types {
	APP_MGR_EVT_DATA_GET,
	APP_MGR_EVT_CONFIG_GET,
	APP_MGR_EVT_CONFIG_SEND,
	APP_MGR_EVT_DATA_SEND,
	APP_MGR_EVT_UI_DATA_SEND,
	APP_MGR_EVT_SHUTDOWN_READY,
	APP_MGR_EVT_ERROR
};

struct app_mgr_event_data {
	const char *buf;
};

/** @brief Application event. */
struct app_mgr_event {
	struct event_header header;
	enum app_mgr_event_types type;
	struct app_mgr_event_data data_list[APP_DATA_NUMBER_OF_TYPES_MAX];

	int err;

	size_t count;

	/* The time each manager has to fetch data before what is available
	 * is transmitted.
	 */
	int timeout;
};

EVENT_TYPE_DECLARE(app_mgr_event);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _APP_MGR_EVENT_H_ */
