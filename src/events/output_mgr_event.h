/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#ifndef _OUTPUT_MGR_EVENT_H_
#define _OUTPUT_MGR_EVENT_H_

/**
 * @brief Output Event
 * @defgroup output_mgr_event Output Event
 * @{
 */

#include "event_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief output event types submitted by output manager. */
enum output_mgr_event_types {
	OUTPUT_MGR_EVT_SHUTDOWN_READY,
	OUTPUT_MGR_EVT_ERROR
};

/** @brief output event. */
struct output_mgr_event {
	struct event_header header;
	enum output_mgr_event_types type;
	int err;
};

EVENT_TYPE_DECLARE(output_mgr_event);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _OUTPUT_MGR_EVENT_H_ */
