/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#ifndef _UTIL_MGR_EVENT_H_
#define _UTIL_MGR_EVENT_H_

/**
 * @brief Util Event
 * @defgroup util_mgr_event Util Event
 * @{
 */

#include "event_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

enum util_mgr_event_types {
	UTIL_MGR_EVT_SHUTDOWN_REQUEST
};

/** @brief Util event. */
struct util_mgr_event {
	struct event_header header;
	enum util_mgr_event_types type;
};

EVENT_TYPE_DECLARE(util_mgr_event);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* _UTIL_MGR_EVENT_H_ */
