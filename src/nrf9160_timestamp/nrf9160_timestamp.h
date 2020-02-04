/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#ifndef NRF9160_TIMESTAMP_H__
#define NRF9160_TIMESTAMP_H__

#include <zephyr/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Initiate nRF9160 time module.
 * 
 *  @return 0 If the operation was successful.
 *            Otherwise, a (negative) error code is returned.
 */
void nrf9160_time_init(void);

/** @brief Set current date time (UTC).
 *
 *  @param new_date_time Pointer to a tm structure.
 * 
 *  @return 0 If the operation was successful.
 *            Otherwise, a (negative) error code is returned.
 */
void date_time_set(struct tm *new_date_time);

/** @brief Get the current time UTC when the data was sampled.
 *         This function requires that k_uptime_get has been called
 *         on the passing variable unix_timestamp_ms variable at a point
 *         prior to calling get_date_time.
 *
 *  @param timestamp Pointer to the timestamp structure.
 * 
 *  @param type Desired format of timestamp.
 *
 *  @retval 0 If the operation was successful.
 *            Otherwise, a (negative) error code is returned.
 */
int date_time_get(s64_t *unix_timestamp_ms);

#ifdef __cplusplus
}
#endif

#endif /* NRF9160_TIMESTAMP__ */
