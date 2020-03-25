/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <zephyr.h>
#include <sys/util.h>
#include <drivers/gps.h>
#include <modem/lte_lc.h>

#include "ui.h"
#include "gps_controller.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(gps_control, CONFIG_CAT_TRACKER_LOG_LEVEL);

/* Structure to hold GPS work information */
static struct device *gps_dev;
static struct k_delayed_work start_work;
static struct k_delayed_work stop_work;
static atomic_t gps_is_active;
static struct gps_config gps_cfg = {
	.nav_mode = GPS_NAV_MODE_SINGLE_FIX,
	.power_mode = GPS_POWER_MODE_DISABLED
};

static void start(struct k_work *work)
{
	ARG_UNUSED(work);
	int err;

	if (gps_dev == NULL) {
		LOG_ERR("GPS controller is not initialized properly");
		return;
	}

	err = gps_start(gps_dev, &gps_cfg);
	if (err) {
		LOG_ERR("Failed to enable GPS, error: %d", err);
		return;
	}

	gps_control_set_active(true);
}

static void stop(struct k_work *work)
{
	ARG_UNUSED(work);
	int err;

	if (gps_dev == NULL) {
		LOG_ERR("GPS controller is not initialized");
		return;
	}

	err = gps_stop(gps_dev);
	if (err) {
		LOG_ERR("Failed to disable GPS, error: %d", err);
		return;
	}

	gps_control_set_active(false);

	LOG_INF("GPS operation was stopped");
}

void gps_control_start(u32_t delay_ms, u32_t timeout)
{
	if (timeout == 0) {
		LOG_ERR("No GPS timeout set");
		return;
	}

	gps_cfg.timeout = timeout;
	k_delayed_work_submit(&start_work, delay_ms);
}

void gps_control_stop(u32_t delay_ms)
{
	k_delayed_work_submit(&stop_work, delay_ms);
}

bool gps_control_is_active(void)
{
	return atomic_get(&gps_is_active);
}

bool gps_control_set_active(bool active)
{
	return atomic_set(&gps_is_active, active ? 1 : 0);
}

/** @brief Configures and starts the GPS device. */
int gps_control_init(gps_event_handler_t handler)
{
	int err;
	static bool is_init;

	if (is_init) {
		return -EALREADY;
	}

	if (handler == NULL) {
		return -EINVAL;
	}

	gps_dev = device_get_binding(CONFIG_GPS_DEV_NAME);
	if (gps_dev == NULL) {
		LOG_ERR("Could not get %s device",
			log_strdup(CONFIG_GPS_DEV_NAME));
		return -ENODEV;
	}

	err = gps_init(gps_dev, handler);
	if (err) {
		LOG_ERR("Could not initialize GPS, error: %d", err);
		return err;
	}

	k_delayed_work_init(&start_work, start);
	k_delayed_work_init(&stop_work, stop);

	LOG_INF("GPS initialized");

	is_init = true;

	return err;
}
