/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <zephyr.h>
#include <stdio.h>
#include <dk_buttons_and_leds.h>
#include <event_manager.h>

#define MODULE ui_manager

#include "events/ui_mgr_event.h"
#include "events/sensor_mgr_event.h"
#include "events/util_mgr_event.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_CAT_TRACKER_LOG_LEVEL);

extern atomic_t manager_count;

struct ui_msg_data {
	union {
		struct util_mgr_event util;
	} manager;
};

K_MSGQ_DEFINE(msgq_ui, sizeof(struct ui_msg_data), 10, 4);

static void notify_ui_manager(struct ui_msg_data *data)
{
	while (k_msgq_put(&msgq_ui, data, K_NO_WAIT) != 0) {
		/* message queue is full: purge old data & try again */
		k_msgq_purge(&msgq_ui);
		LOG_ERR("UI manager message queue full, queue purged");
	}
}

static void signal_error(int err)
{
	struct ui_mgr_event *ui_mgr_event = new_ui_mgr_event();

	ui_mgr_event->type = UI_MGR_EVT_ERROR;
	ui_mgr_event->data.err = err;

	EVENT_SUBMIT(ui_mgr_event);
}

static void button_handler(uint32_t button_states, uint32_t has_changed)
{
	static int try_again_timeout;

	/* Publication of data due to button presses limited
	 * to 1 push every 2 seconds to avoid spamming.
	 */
	if ((has_changed & button_states & DK_BTN1_MSK) &&
	    k_uptime_get() - try_again_timeout > 2 * 1000) {
		LOG_DBG("Cloud publication by button 1 triggered, ");
		LOG_DBG("2 seconds to next allowed cloud publication ");
		LOG_DBG("triggered by button 1");

		struct ui_mgr_event *ui_mgr_event = new_ui_mgr_event();

		ui_mgr_event->type = UI_MGR_EVT_BUTTON_DATA_READY;
		ui_mgr_event->data.ui.btn = 1;
		ui_mgr_event->data.ui.btn_ts = k_uptime_get();
		ui_mgr_event->data.ui.queued = true;

		EVENT_SUBMIT(ui_mgr_event);

		try_again_timeout = k_uptime_get();
	}

#if defined(CONFIG_BOARD_NRF9160DK_NRF9160NS)
	/* Fake motion. The nRF9160 DK does not have an accelerometer by
	 * default.
	 */
	if (has_changed & button_states & DK_BTN2_MSK) {
		LOG_DBG("Button 2 on DK triggered");
		LOG_DBG("Fake movement");

		/* Send sensor event signifying that movement has been
		 * triggered. Set queued flag to false to signify that
		 * no data is carried in the message.
		 */
		struct sensor_mgr_event *sensor_mgr_event =
				new_sensor_mgr_event();

		sensor_mgr_event->type = SENSOR_MGR_EVT_MOVEMENT_DATA_READY;
		sensor_mgr_event->data.accel.queued = false;

		EVENT_SUBMIT(sensor_mgr_event);
	}
#endif
}

static int ui_manager_setup(void)
{
	int err;

	err = dk_buttons_init(button_handler);
	if (err) {
		LOG_ERR("dk_buttons_init, error: %d", err);
		return err;
	}

	return 0;
}

static bool event_handler(const struct event_header *eh)
{
	if (is_util_mgr_event(eh)) {
		struct util_mgr_event *event = cast_util_mgr_event(eh);
		struct ui_msg_data ui_msg = {
			.manager.util = *event
		};

		notify_ui_manager(&ui_msg);
	}

	return false;
}

static void state_agnostic_manager_events(struct ui_msg_data *ui_msg)
{
	if (is_util_mgr_event(&ui_msg->manager.util.header) &&
	    ui_msg->manager.util.type == UTIL_MGR_EVT_SHUTDOWN_REQUEST) {
		struct ui_mgr_event *ui_mgr_event = new_ui_mgr_event();

		ui_mgr_event->type = UI_MGR_EVT_SHUTDOWN_READY;
		EVENT_SUBMIT(ui_mgr_event);
	}
}

static void ui_manager(void)
{
	int err;

	struct ui_msg_data ui_msg;

	atomic_inc(&manager_count);

	err = ui_manager_setup();
	if (err) {
		LOG_ERR("ui_manager_setup, error: %d", err);
		signal_error(err);
	}

	while (true) {
		k_msgq_get(&msgq_ui, &ui_msg, K_FOREVER);
		state_agnostic_manager_events(&ui_msg);
	}
}

K_THREAD_DEFINE(ui_manager_thread, CONFIG_UI_MGR_THREAD_STACK_SIZE,
		ui_manager, NULL, NULL, NULL,
		K_HIGHEST_APPLICATION_THREAD_PRIO, 0, -1);

EVENT_LISTENER(MODULE, event_handler);
EVENT_SUBSCRIBE(MODULE, util_mgr_event);
