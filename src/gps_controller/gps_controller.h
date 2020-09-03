/*
 * Copyright (c) 2019-2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

/**@file
 *
 * @brief   GPS module for asset tracker
 */

#ifndef GPS_CONTROLLER_H__
#define GPS_CONTROLLER_H__

#include <zephyr.h>

#ifdef __cplusplus
extern "C" {
#endif

int gps_control_init(gps_event_handler_t handler);

void gps_control_stop(uint32_t delay_ms);

void gps_control_start(uint32_t delay_ms, uint32_t timeout);

bool gps_control_is_active(void);

bool gps_control_set_active(bool active);

#ifdef __cplusplus
}
#endif

#endif /* GPS_CONTROLLER_H__ */
