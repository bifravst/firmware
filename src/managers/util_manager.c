
/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <zephyr.h>
#include <power/reboot.h>
#include <logging/log.h>

#define MODULE util_manager

#include "events/app_mgr_event.h"
#include "events/cloud_mgr_event.h"
#include "events/data_mgr_event.h"
#include "events/sensor_mgr_event.h"
#include "events/ui_mgr_event.h"
#include "events/util_mgr_event.h"
#include "events/gps_mgr_event.h"
#include "events/modem_mgr_event.h"
#include "events/output_mgr_event.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_CAT_TRACKER_LOG_LEVEL);

/* Atomic variable that is incremented by other managers in the application.
 * Used to keep track of shutdown request acknowledgments from managers.
 */
atomic_t manager_count;

struct util_msg_data {
	union {
		struct cloud_mgr_event cloud;
		struct ui_mgr_event ui;
		struct sensor_mgr_event sensor;
		struct data_mgr_event data;
		struct app_mgr_event app;
		struct gps_mgr_event gps;
		struct modem_mgr_event modem;
		struct output_mgr_event output;
	} manager;
};

static struct k_delayed_work reboot_work;

K_MSGQ_DEFINE(msgq_util, sizeof(struct util_msg_data), 10, 4);

static void notify_util_manager(struct util_msg_data *data)
{
	while (k_msgq_put(&msgq_util, data, K_NO_WAIT) != 0) {
		/* message queue is full: purge old data & try again */
		k_msgq_purge(&msgq_util);
		LOG_ERR("Utility manager message queue full, queue purged");
	}
}

static void reboot(void)
{
	LOG_ERR("Rebooting!");
#if !defined(CONFIG_DEBUG) && defined(CONFIG_REBOOT)
	LOG_PANIC();
	sys_reboot(0);
#else
	while (true) {
		k_cpu_idle();
	}
#endif
}

static void reboot_work_fn(struct k_work *work)
{
	reboot();
}

static void signal_reboot_request(void)
{
	/* Flag ensuring that multiple reboot requests are not emitted
	 * upon an error from multiple managers.
	 */
	static bool error_signaled;

	if (!error_signaled) {
		struct util_mgr_event *util_mgr_event = new_util_mgr_event();

		util_mgr_event->type = UTIL_MGR_EVT_SHUTDOWN_REQUEST;

		k_delayed_work_submit(&reboot_work,
				      K_SECONDS(CONFIG_REBOOT_TIMEOUT));

		EVENT_SUBMIT(util_mgr_event);

		error_signaled = true;
	}
}

void bsd_recoverable_error_handler(uint32_t err)
{
	signal_reboot_request();
}

void k_sys_fatal_error_handler(unsigned int reason, const z_arch_esf_t *esf)
{
	ARG_UNUSED(esf);

	LOG_PANIC();
	signal_reboot_request();
}

static bool event_handler(const struct event_header *eh)
{
	if (is_modem_mgr_event(eh)) {
		struct modem_mgr_event *event = cast_modem_mgr_event(eh);
		struct util_msg_data util_msg = {
			.manager.modem = *event
		};

		notify_util_manager(&util_msg);
	}

	if (is_cloud_mgr_event(eh)) {
		struct cloud_mgr_event *event = cast_cloud_mgr_event(eh);
		struct util_msg_data util_msg = {
			.manager.cloud = *event
		};

		notify_util_manager(&util_msg);
	}

	if (is_gps_mgr_event(eh)) {
		struct gps_mgr_event *event = cast_gps_mgr_event(eh);
		struct util_msg_data util_msg = {
			.manager.gps = *event
		};

		notify_util_manager(&util_msg);
	}

	if (is_sensor_mgr_event(eh)) {
		struct sensor_mgr_event *event = cast_sensor_mgr_event(eh);
		struct util_msg_data util_msg = {
			.manager.sensor = *event
		};

		notify_util_manager(&util_msg);
	}

	if (is_ui_mgr_event(eh)) {
		struct ui_mgr_event *event = cast_ui_mgr_event(eh);
		struct util_msg_data util_msg = {
			.manager.ui = *event
		};

		notify_util_manager(&util_msg);
	}

	if (is_app_mgr_event(eh)) {
		struct app_mgr_event *event = cast_app_mgr_event(eh);
		struct util_msg_data util_msg = {
			.manager.app = *event
		};

		notify_util_manager(&util_msg);
	}

	if (is_data_mgr_event(eh)) {
		struct data_mgr_event *event = cast_data_mgr_event(eh);
		struct util_msg_data util_msg = {
			.manager.data = *event
		};

		notify_util_manager(&util_msg);
	}

	if (is_output_mgr_event(eh)) {
		struct output_mgr_event *event = cast_output_mgr_event(eh);
		struct util_msg_data util_msg = {
			.manager.output = *event
		};

		notify_util_manager(&util_msg);
	}

	return false;
}

static void state_agnostic_manager_events(struct util_msg_data *util_msg)
{
	static int reboot_ack_cnt;

	if (is_cloud_mgr_event(&util_msg->manager.cloud.header)) {
		switch (util_msg->manager.cloud.type) {
		case CLOUD_MGR_EVT_ERROR:
			signal_reboot_request();
			break;
		case CLOUD_MGR_EVT_FOTA_DONE:
			signal_reboot_request();
			break;
		case CLOUD_MGR_EVT_SHUTDOWN_READY:
			reboot_ack_cnt++;
			break;
		default:
			break;
		}
	}

	if (is_modem_mgr_event(&util_msg->manager.modem.header)) {
		switch (util_msg->manager.modem.type) {
		case MODEM_MGR_EVT_ERROR:
			signal_reboot_request();
			break;
		case MODEM_MGR_EVT_SHUTDOWN_READY:
			reboot_ack_cnt++;
			break;
		default:
			break;
		}
	}

	if (is_sensor_mgr_event(&util_msg->manager.sensor.header)) {
		switch (util_msg->manager.sensor.type) {
		case SENSOR_MGR_EVT_ERROR:
			signal_reboot_request();
			break;
		case SENSOR_MGR_EVT_SHUTDOWN_READY:
			reboot_ack_cnt++;
			break;
		default:
			break;
		}
	}

	if (is_gps_mgr_event(&util_msg->manager.gps.header)) {
		switch (util_msg->manager.gps.type) {
		case GPS_MGR_EVT_ERROR:
			signal_reboot_request();
			break;
		case GPS_MGR_EVT_SHUTDOWN_READY:
			reboot_ack_cnt++;
			break;
		default:
			break;
		}
	}

	if (is_data_mgr_event(&util_msg->manager.data.header)) {
		switch (util_msg->manager.data.type) {
		case DATA_MGR_EVT_ERROR:
			signal_reboot_request();
			break;
		case DATA_MGR_EVT_SHUTDOWN_READY:
			reboot_ack_cnt++;
			break;
		default:
			break;
		}
	}

	if (is_app_mgr_event(&util_msg->manager.app.header)) {
		switch (util_msg->manager.app.type) {
		case APP_MGR_EVT_ERROR:
			signal_reboot_request();
			break;
		case APP_MGR_EVT_SHUTDOWN_READY:
			reboot_ack_cnt++;
			break;
		default:
			break;
		}
	}

	if (is_ui_mgr_event(&util_msg->manager.ui.header)) {
		switch (util_msg->manager.ui.type) {
		case UI_MGR_EVT_ERROR:
			signal_reboot_request();
			break;
		case UI_MGR_EVT_SHUTDOWN_READY:
			reboot_ack_cnt++;
			break;
		default:
			break;
		}
	}

	if (is_output_mgr_event(&util_msg->manager.output.header)) {
		switch (util_msg->manager.output.type) {
		case OUTPUT_MGR_EVT_ERROR:
			signal_reboot_request();
			break;
		case OUTPUT_MGR_EVT_SHUTDOWN_READY:
			reboot_ack_cnt++;
			break;
		default:
			break;
		}
	}

	/* Reboot if after a shorter timeout if all managers has acknowledged
	 * that the application is ready to shutdown. This ensures a graceful
	 * shutdown.
	 */
	if (reboot_ack_cnt >= manager_count) {
		k_delayed_work_submit(&reboot_work,
				      K_SECONDS(5));
	}
}

static void util_manager(void)
{
	struct util_msg_data util_msg;

	k_delayed_work_init(&reboot_work, reboot_work_fn);

	/* State agnostic manager. */
	while (true) {
		k_msgq_get(&msgq_util, &util_msg, K_FOREVER);
		state_agnostic_manager_events(&util_msg);
	}
}

K_THREAD_DEFINE(util_manager_thread, CONFIG_UTIL_MGR_THREAD_STACK_SIZE,
		util_manager, NULL, NULL, NULL,
		K_HIGHEST_APPLICATION_THREAD_PRIO, 0, -1);

EVENT_LISTENER(MODULE, event_handler);
EVENT_SUBSCRIBE_EARLY(MODULE, app_mgr_event);
EVENT_SUBSCRIBE_EARLY(MODULE, modem_mgr_event);
EVENT_SUBSCRIBE_EARLY(MODULE, cloud_mgr_event);
EVENT_SUBSCRIBE_EARLY(MODULE, gps_mgr_event);
EVENT_SUBSCRIBE_EARLY(MODULE, ui_mgr_event);
EVENT_SUBSCRIBE_EARLY(MODULE, sensor_mgr_event);
EVENT_SUBSCRIBE_EARLY(MODULE, data_mgr_event);
EVENT_SUBSCRIBE_EARLY(MODULE, output_mgr_event);
