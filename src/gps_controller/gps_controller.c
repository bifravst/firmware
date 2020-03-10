/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <zephyr.h>
#include <sys/util.h>
#include <drivers/gps.h>
#include <lte_lc.h>

#include "ui.h"
#include "gps_controller.h"


#include <logging/log.h>
LOG_MODULE_REGISTER(gps_control, CONFIG_GPS_CONTROL_LOG_LEVEL);

#if !defined(CONFIG_GPS_SIM)
/* Structure to hold GPS work information */
static struct {
	enum {
		GPS_WORK_START,
		GPS_WORK_STOP
	} type;
	struct k_delayed_work work;
	struct device *dev;
} gps_work;

static atomic_t gps_is_active;
static atomic_t gps_is_enabled;

static int start(void)
{
	int err;

	if (IS_ENABLED(CONFIG_GPS_CONTROL_PSM_ENABLE_ON_START)) {
		LOG_INF("Enabling PSM");

		err = lte_lc_psm_req(true);
		if (err) {
			LOG_ERR("PSM mode could not be enabled");
			LOG_ERR(" or was already enabled.");
		} else {
			LOG_INF("PSM enabled");
		}
	}

	err = gps_start(gps_work.dev);
	if (err) {
		LOG_ERR("Failed starting GPS!");
		return err;
	}

	atomic_set(&gps_is_active, 1);

	LOG_INF("GPS started successfully.Searching for satellites");
	LOG_INF("to get position fix. This may take several minutes.");

	return 0;
}

static int stop(void)
{
	int err;

	if (IS_ENABLED(CONFIG_GPS_CONTROL_PSM_DISABLE_ON_STOP)) {
		LOG_INF("Disabling PSM");

		err = lte_lc_psm_req(false);
		if (err) {
			LOG_ERR("PSM mode could not be disabled");
		}
	}

	err = gps_stop(gps_work.dev);
	if (err) {
		return err;
	}

	return 0;
}

static void gps_work_handler(struct k_work *work)
{
	int err;

	if (gps_work.type == GPS_WORK_START) {
		err = start();
		if (err) {
			LOG_ERR("GPS could not be started, error: %d", err);
			return;
		}

		LOG_INF("GPS operation started");

		atomic_set(&gps_is_active, 1);
		ui_led_set_pattern(UI_LED_GPS_SEARCHING);

		return;
	} else if (gps_work.type == GPS_WORK_STOP) {
		err = stop();
		if (err) {
			LOG_ERR("GPS could not be stopped, error: %d", err);
			return;
		}

		LOG_INF("GPS operation was stopped");

		atomic_set(&gps_is_active, 0);

		if (atomic_get(&gps_is_enabled) == 0) {
			return;
		}

		gps_work.type = GPS_WORK_START;


		LOG_INF("The device will try to get fix again in %d seconds",
			CONFIG_GPS_CONTROL_FIX_CHECK_INTERVAL);

		k_delayed_work_submit(&gps_work.work,
			K_SECONDS(CONFIG_GPS_CONTROL_FIX_CHECK_INTERVAL));
	}
}
#endif /* !defined(GPS_SIM) */

bool gps_control_is_active(void)
{
#if !defined(CONFIG_GPS_SIM)
	return atomic_get(&gps_is_active);
#else
	return false;
#endif
}

bool gps_control_is_enabled(void)
{
#if !defined(CONFIG_GPS_SIM)
	return atomic_get(&gps_is_enabled);
#else
	return false;
#endif
}

void gps_control_enable(void)
{
#if !defined(CONFIG_GPS_SIM)
	atomic_set(&gps_is_enabled, 1);
	gps_control_start(K_SECONDS(1));
#endif
}

void gps_control_disable(void)
{
#if !defined(CONFIG_GPS_SIM)
	atomic_set(&gps_is_enabled, 0);
	gps_control_stop(K_NO_WAIT);
	ui_led_set_pattern(UI_CLOUD_CONNECTED);
#endif
}

void gps_control_stop(u32_t delay_ms)
{
#if !defined(CONFIG_GPS_SIM)
	k_delayed_work_cancel(&gps_work.work);
	gps_work.type = GPS_WORK_STOP;
	k_delayed_work_submit(&gps_work.work, delay_ms);
#endif
}

void gps_control_start(u32_t delay_ms)
{
#if !defined(CONFIG_GPS_SIM)
	k_delayed_work_cancel(&gps_work.work);
	gps_work.type = GPS_WORK_START;
	k_delayed_work_submit(&gps_work.work, delay_ms);
#endif
}

void gps_control_on_trigger(void)
{
#if !defined(CONFIG_GPS_SIM)

#endif
}

/** @brief Configures and starts the GPS device. */
int gps_control_init(gps_trigger_handler_t handler)
{
	int err;
	struct device *gps_dev;

#ifdef CONFIG_GPS_SIM
	struct gps_trigger gps_trig = {
		.type = GPS_TRIG_DATA_READY
	};
#else
	struct gps_trigger gps_trig = {
		.type = GPS_TRIG_FIX,
		.chan = GPS_CHAN_NMEA
	};
#endif /* CONFIG_GPS_SIM */

	gps_dev = device_get_binding(CONFIG_GPS_DEV_NAME);
	if (gps_dev == NULL) {
		LOG_ERR("Could not get %s device", log_strdup(CONFIG_GPS_DEV_NAME));
		return -ENODEV;
	}


	err = gps_trigger_set(gps_dev, &gps_trig, handler);
	if (err) {
		LOG_ERR("Could not set trigger, error code: %d", err);
		return err;
	}

#if !defined(CONFIG_GPS_SIM)
	k_delayed_work_init(&gps_work.work, gps_work_handler);

	gps_work.dev = gps_dev;
#endif
	LOG_INF("GPS initialized");

	return 0;
}
