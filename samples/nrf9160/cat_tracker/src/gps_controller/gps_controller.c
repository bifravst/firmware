/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <zephyr.h>
#include <misc/util.h>
#include <gps.h>
#include <lte_lc.h>
#include <leds.h>

#include "gps_controller.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(gps_control, CONFIG_GPS_CONTROL_LOG_LEVEL);

/* Structure to hold GPS work information */
static struct {
	enum { GPS_WORK_START, GPS_WORK_STOP } type;
	struct k_work work;
	struct device *dev;
	u32_t failed_fix_attempts;
	u32_t fix_count;
} gps_work;

static void gps_work_handler(struct k_work *work)
{
	int err;

	if (gps_work.type == GPS_WORK_START) {
		err = gps_start(gps_work.dev);
		if (err) {
			printk("GPS could not be started, error: %d\n", err);
			return;
		}

		printk("GPS started successfully.\nSearching for satellites ");
		printk("to get position fix. This may take several minutes.\n");

		return;
	} else if (gps_work.type == GPS_WORK_STOP) {
		err = gps_stop(gps_work.dev);
		if (err) {
			printk("GPS could not be stopped, error: %d\n", err);
			return;
		}

		return;
	}
}

void gps_control_stop()
{
	gps_work.type = GPS_WORK_STOP;
	k_work_submit(&gps_work.work);
	gps_search_led_stop();
}

void gps_control_start()
{
	gps_work.type = GPS_WORK_START;
	k_work_submit(&gps_work.work);
	gps_search_led_start();
}

void gps_control_on_trigger(void)
{
	//k_delayed_work_cancel(&gps_work.work);

	gps_search_led_stop_fix();

	if (++gps_work.fix_count == CONFIG_GPS_CONTROL_FIX_COUNT) {
		gps_work.fix_count = 0;
		gps_control_stop();
	}
}

/** @brief Configures and starts the GPS device. */
int gps_control_init(gps_trigger_handler_t handler)
{
	int err;
	struct device *gps_dev;

	struct gps_trigger gps_trig = { .type = GPS_TRIG_FIX,
					.chan = GPS_CHAN_NMEA };

	gps_dev = device_get_binding(CONFIG_GPS_DEV_NAME);
	if (gps_dev == NULL) {
		printk("Could not get %s device\n", CONFIG_GPS_DEV_NAME);
		return -ENODEV;
	}

	err = gps_trigger_set(gps_dev, &gps_trig, handler);
	if (err) {
		printk("Could not set trigger, error code: %d\n", err);
		return err;
	}

	k_work_init(&gps_work.work, gps_work_handler);

	gps_work.dev = gps_dev;
	gps_work.type = GPS_WORK_STOP;

	// k_work_submit(&gps_work.work);

	LOG_DBG("GPS initialized\n");

	return 0;
}
