/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#ifndef _cloud_mgr_event_H_
#define _cloud_mgr_event_H_

/**
 * @brief Cloud Event
 * @defgroup cloud_mgr_event Cloud Event
 * @{
 */

#include "event_manager.h"
#include "cloud_codec.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Cloud event types submitted by Cloud manager. */
enum cloud_mgr_event_types {
	CLOUD_MGR_EVT_CONNECTED,
	CLOUD_MGR_EVT_DISCONNECTED,
	CLOUD_MGR_EVT_CONNECTING,
	CLOUD_MGR_EVT_CONFIG_RECEIVED,
	CLOUD_MGR_EVT_FOTA_DONE,
	CLOUD_MGR_EVT_SHARED_DATA_DONE,
	CLOUD_MGR_EVT_SHUTDOWN_READY,
	CLOUD_MGR_EVT_ERROR
};

struct cloud_mgr_event_data {
	char *buf;
	size_t len;
};

/** @brief Cloud event. */
struct cloud_mgr_event {
	struct event_header header;
	enum cloud_mgr_event_types type;

	union {
		struct cloud_data_cfg config;
		int err;
	} data;
};

EVENT_TYPE_DECLARE(cloud_mgr_event);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _cloud_mgr_event_H_ */
