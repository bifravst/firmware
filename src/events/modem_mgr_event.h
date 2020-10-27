/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#ifndef _MODEM_MGR_EVENT_H_
#define _MODEM_MGR_EVENT_H_

/**
 * @brief Modem Event
 * @defgroup modem_mgr_event Modem Event
 * @{
 */

#include "cloud_codec.h"
#include "event_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Modem event types submitted by Modem manager. */
enum modem_mgr_event_types {
	MODEM_MGR_EVT_LTE_CONNECTED,
	MODEM_MGR_EVT_LTE_DISCONNECTED,
	MODEM_MGR_EVT_LTE_CONNECTING,
	MODEM_MGR_EVT_MODEM_DATA_READY,
	MODEM_MGR_EVT_BATTERY_DATA_READY,
	MODEM_MGR_EVT_DATE_TIME_OBTAINED,
	MODEM_MGR_EVT_SHUTDOWN_READY,
	MODEM_MGR_EVT_ERROR
};

/** @brief Modem event. */
struct modem_mgr_event {
	struct event_header header;
	enum modem_mgr_event_types type;
	union {
		struct cloud_data_modem modem;
		struct cloud_data_battery bat;
		int err;
	} data;
};

EVENT_TYPE_DECLARE(modem_mgr_event);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _MODEM_MGR_EVENT_H_ */
