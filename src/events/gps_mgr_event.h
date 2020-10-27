/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#ifndef _GPS_MGR_EVENT_H_
#define _GPS_MGR_EVENT_H_

/**
 * @brief GPS Event
 * @defgroup gps_mgr_event GPS Event
 * @{
 */

#include <drivers/gps.h>

#include "event_manager.h"
#include "cloud_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief GPS event types submitted by GPS manager. */
enum gps_mgr_event_types {
	GPS_MGR_EVT_DATA_READY,
	GPS_MGR_EVT_TIMEOUT,
	GPS_MGR_EVT_ACTIVE,
	GPS_MGR_EVT_INACTIVE,
	GPS_MGR_EVT_SHUTDOWN_READY,
	GPS_MGR_EVT_AGPS_NEEDED,
	GPS_MGR_EVT_ERROR
};

/** @brief GPS event. */
struct gps_mgr_event {
	struct event_header header;
	enum gps_mgr_event_types type;

	union {
		struct cloud_data_gps gps;
		struct gps_agps_request agps_request;
		int err;
	} data;
};

EVENT_TYPE_DECLARE(gps_mgr_event);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _GPS_MGR_EVENT_H_ */
