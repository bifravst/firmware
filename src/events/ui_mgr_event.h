/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#ifndef _UI_MGR_EVENT_H_
#define _UI_MGR_EVENT_H_

/**
 * @brief UI Event
 * @defgroup ui_mgr_event UI Event
 * @{
 */

#include "cloud_codec.h"
#include "event_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief UI event types submitted by UI manager. */
enum ui_mgr_event_types {
	UI_MGR_EVT_BUTTON_DATA_READY,
	UI_MGR_EVT_SHUTDOWN_READY,
	UI_MGR_EVT_ERROR
};

/** @brief UI event. */
struct ui_mgr_event {
	struct event_header header;
	enum ui_mgr_event_types type;

	union {
		struct cloud_data_ui ui;
		int err;
	} data;
};

EVENT_TYPE_DECLARE(ui_mgr_event);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _UI_MGR_EVENT_H_ */
