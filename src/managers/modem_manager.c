/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <zephyr.h>
#include <stdio.h>
#include <stdio.h>
#include <event_manager.h>

#include <modem/lte_lc.h>
#include <modem/modem_info.h>
#include <date_time.h>

#define MODULE modem_manager

#include "events/app_mgr_event.h"
#include "events/data_mgr_event.h"
#include "events/modem_mgr_event.h"
#include "events/util_mgr_event.h"
#include "events/cloud_mgr_event.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(MODULE, CONFIG_CAT_TRACKER_LOG_LEVEL);

extern atomic_t manager_count;

struct modem_msg_data {
	union {
		struct app_mgr_event app;
		struct cloud_mgr_event cloud;
		struct util_mgr_event util;
	} manager;
};

/* Struct that holds data from the modem information module. */
static struct modem_param_info modem_param;

/* Value that always holds the latest RSRP value. */
static uint16_t rsrp_value_latest;

K_MSGQ_DEFINE(msgq_modem, sizeof(struct modem_msg_data), 10, 4);

static void notify_modem_manager(struct modem_msg_data *data)
{
	while (k_msgq_put(&msgq_modem, data, K_NO_WAIT) != 0) {
		/* message queue is full: purge old data & try again */
		k_msgq_purge(&msgq_modem);
		LOG_ERR("Modem manager message queue full, queue purged");
	}
}

static void signal_error(int err)
{
	struct modem_mgr_event *modem_mgr_event = new_modem_mgr_event();

	modem_mgr_event->type = MODEM_MGR_EVT_ERROR;
	modem_mgr_event->data.err = err;

	EVENT_SUBMIT(modem_mgr_event);
}

static void signal_lte_connected(void)
{
	struct modem_mgr_event *modem_mgr_event = new_modem_mgr_event();

	modem_mgr_event->type = MODEM_MGR_EVT_LTE_CONNECTED;

	EVENT_SUBMIT(modem_mgr_event);
}

static void signal_lte_disconnected(void)
{
	struct modem_mgr_event *modem_mgr_event = new_modem_mgr_event();

	modem_mgr_event->type = MODEM_MGR_EVT_LTE_DISCONNECTED;

	EVENT_SUBMIT(modem_mgr_event);
}

static void signal_lte_connecting(void)
{
	struct modem_mgr_event *modem_mgr_event = new_modem_mgr_event();

	modem_mgr_event->type = MODEM_MGR_EVT_LTE_CONNECTING;

	EVENT_SUBMIT(modem_mgr_event);
}

static void date_time_mgr_event_handler(const struct date_time_evt *evt)
{
	struct modem_mgr_event *modem_mgr_event = new_modem_mgr_event();

	switch (evt->type) {
	case DATE_TIME_OBTAINED_MODEM:
		/* Fall through. */
	case DATE_TIME_OBTAINED_NTP:
		/* Fall through. */
	case DATE_TIME_OBTAINED_EXT:
		modem_mgr_event->type = MODEM_MGR_EVT_DATE_TIME_OBTAINED;
		EVENT_SUBMIT(modem_mgr_event);

		/* De-register handler. At this point the application will have
		 * date time to depend on indefinitely until a reboot occurs.
		 */
		date_time_register_handler(NULL);
		break;
	case DATE_TIME_NOT_OBTAINED:
		break;
	default:
		break;
	}
}

static void lte_evt_handler(const struct lte_lc_evt *const evt)
{
	switch (evt->type) {
	case LTE_LC_EVT_NW_REG_STATUS:

		if (evt->nw_reg_status == LTE_LC_NW_REG_UICC_FAIL) {
			LOG_ERR("No SIM card detected!");
			signal_error(-ENOTSUP);
			break;
		}

		if ((evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_HOME) &&
		    (evt->nw_reg_status != LTE_LC_NW_REG_REGISTERED_ROAMING)) {
			signal_lte_disconnected();
			break;
		}

		LOG_DBG("Network registration status: %s",
			evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ?
			"Connected - home network" : "Connected - roaming");

		signal_lte_connected();
		break;
	case LTE_LC_EVT_PSM_UPDATE:
		LOG_DBG("PSM parameter update: TAU: %d, Active time: %d",
			evt->psm_cfg.tau, evt->psm_cfg.active_time);
		break;
	case LTE_LC_EVT_EDRX_UPDATE: {
		char log_buf[60];
		ssize_t len;

		len = snprintf(log_buf, sizeof(log_buf),
			       "eDRX parameter update: eDRX: %f, PTW: %f",
			       evt->edrx_cfg.edrx, evt->edrx_cfg.ptw);
		if (len > 0) {
			LOG_DBG("%s", log_strdup(log_buf));
		}
		break;
	}
	case LTE_LC_EVT_RRC_UPDATE:
		LOG_DBG("RRC mode: %s",
			evt->rrc_mode == LTE_LC_RRC_MODE_CONNECTED ?
			"Connected" : "Idle");
		break;
	case LTE_LC_EVT_CELL_UPDATE:
		LOG_DBG("LTE cell changed: Cell ID: %d, Tracking area: %d",
			evt->cell.id, evt->cell.tac);
		break;
	default:
		break;
	}
}

static int modem_configure_low_power(void)
{
	int err;

	err = lte_lc_psm_req(true);
	if (err) {
		LOG_ERR("PSM request failed, error: %d", err);
	}

	LOG_DBG("PSM requested");

	return 0;
}

static int modem_configure(void)
{
	int err;

	LOG_DBG("Configuring the modem...");

	err = lte_lc_init_and_connect_async(lte_evt_handler);
	if (err) {
		LOG_ERR("lte_lc_init_connect_async, error: %d", err);
	}

	signal_lte_connecting();

	return err;
}

static void modem_rsrp_handler(char rsrp_value)
{
	/* RSRP raw values that represent actual signal strength are
	 * 0 through 97 (per "nRF91 AT Commands" v1.1).
	 */

	if (rsrp_value > 97) {
		return;
	}

	/* Set temporary variable to hold RSRP value. RSRP callbacks and other
	 * data from the modem info module are retrieved separately.
	 * This temporarily saves the latest value which are sent to
	 * the data manager upon a modem data request.
	 */
	rsrp_value_latest = rsrp_value;

	LOG_DBG("Incoming RSRP status message, RSRP value is %d",
		rsrp_value_latest);
}

static int modem_data_init(void)
{
	int err;

	err = modem_info_init();
	if (err) {
		LOG_INF("modem_info_init, error: %d", err);
		return err;
	}

	err = modem_info_params_init(&modem_param);
	if (err) {
		LOG_INF("modem_info_params_init, error: %d", err);
		return err;
	}

	err = modem_info_rsrp_register(modem_rsrp_handler);
	if (err) {
		LOG_INF("modem_info_rsrp_register, error: %d", err);
		return err;
	}

	return 0;
}

/* Produce a warning if modem firmware version is unexpected. */
static void check_modem_fw_version(void)
{
	static bool modem_fw_version_checked;

	if (modem_fw_version_checked) {
		return;
	}
	if (strcmp(modem_param.device.modem_fw.value_string,
		   CONFIG_EXPECTED_MODEM_FIRMWARE_VERSION) != 0) {
		LOG_WRN("Unsupported modem firmware version: %s",
			log_strdup(modem_param.device.modem_fw.value_string));
		LOG_WRN("Expected firmware version: %s",
			CONFIG_EXPECTED_MODEM_FIRMWARE_VERSION);
		LOG_WRN("You can change the expected version through the");
		LOG_WRN("EXPECTED_MODEM_FIRMWARE_VERSION setting.");
		LOG_WRN("Please upgrade: http://bit.ly/nrf9160-mfw-update");
	} else {
		LOG_DBG("Board is running expected modem firmware version: %s",
			log_strdup(modem_param.device.modem_fw.value_string));
	}
	modem_fw_version_checked = true;
}

static int modem_manager_modem_data_get(void)
{
	int err;

	/* Request data from modem information module. */
	err = modem_info_params_get(&modem_param);
	if (err) {
		LOG_ERR("modem_info_params_get, error: %d", err);
		return err;
	}

	check_modem_fw_version();

	struct modem_mgr_event *modem_mgr_event = new_modem_mgr_event();

	modem_mgr_event->data.modem.rsrp =
		rsrp_value_latest;
	modem_mgr_event->data.modem.ip =
		modem_param.network.ip_address.value_string;
	modem_mgr_event->data.modem.cell =
		modem_param.network.cellid_dec;
	modem_mgr_event->data.modem.mccmnc =
		modem_param.network.current_operator.value_string;
	modem_mgr_event->data.modem.area =
		modem_param.network.area_code.value;
	modem_mgr_event->data.modem.appv =
		CONFIG_CAT_TRACKER_APP_VERSION;
	modem_mgr_event->data.modem.brdv =
		modem_param.device.board;
	modem_mgr_event->data.modem.fw =
		modem_param.device.modem_fw.value_string;
	modem_mgr_event->data.modem.iccid =
		modem_param.sim.iccid.value_string;
	modem_mgr_event->data.modem.nw_lte_m =
		modem_param.network.lte_mode.value;
	modem_mgr_event->data.modem.nw_nb_iot =
		modem_param.network.nbiot_mode.value;
	modem_mgr_event->data.modem.nw_gps =
		modem_param.network.gps_mode.value;
	modem_mgr_event->data.modem.bnd =
		modem_param.network.current_band.value;
	modem_mgr_event->data.modem.mod_ts =
		k_uptime_get();
	modem_mgr_event->data.modem.mod_ts_static =
		k_uptime_get();
	modem_mgr_event->data.modem.queued =
		true;
	modem_mgr_event->type =
		MODEM_MGR_EVT_MODEM_DATA_READY;

	EVENT_SUBMIT(modem_mgr_event);

	return 0;
}

static int modem_manager_battery_data_get(void)
{
	int err;

	/* Replace this function with a function that specifically
	 * requests battery.
	 */
	err = modem_info_params_get(&modem_param);
	if (err) {
		LOG_ERR("modem_info_params_get, error: %d", err);
		return err;
	}

	struct modem_mgr_event *modem_mgr_event = new_modem_mgr_event();

	modem_mgr_event->data.bat.bat = modem_param.device.battery.value;
	modem_mgr_event->data.bat.bat_ts = k_uptime_get();
	modem_mgr_event->data.bat.queued = true;
	modem_mgr_event->type = MODEM_MGR_EVT_BATTERY_DATA_READY;

	EVENT_SUBMIT(modem_mgr_event);

	return 0;
}

static int lte_manager_setup(void)
{
	int err;

	err = modem_configure_low_power();
	if (err) {
		LOG_ERR("modem_configure_low_power, error: %d", err);
		return err;
	}

	err = modem_configure();
	if (err) {
		LOG_ERR("modem_configure, error: %d", err);
		return err;
	}

	err = modem_data_init();
	if (err) {
		LOG_ERR("modem_data_init, error: %d", err);
		return err;
	}

	return 0;
}

static bool event_handler(const struct event_header *eh)
{
	if (is_app_mgr_event(eh)) {
		struct app_mgr_event *event = cast_app_mgr_event(eh);
		struct modem_msg_data modem_msg = {
			.manager.app = *event
		};

		notify_modem_manager(&modem_msg);
	}

	if (is_cloud_mgr_event(eh)) {
		struct cloud_mgr_event *event = cast_cloud_mgr_event(eh);
		struct modem_msg_data modem_msg = {
			.manager.cloud = *event
		};

		notify_modem_manager(&modem_msg);
	}

	if (is_util_mgr_event(eh)) {
		struct util_mgr_event *event = cast_util_mgr_event(eh);
		struct modem_msg_data modem_msg = {
			.manager.util = *event
		};

		notify_modem_manager(&modem_msg);
	}

	return false;
}

static bool modem_data_requested(struct app_mgr_event_data *data_list,
				size_t count)
{
	for (int i = 0; i < count; i++) {
		if (strcmp(data_list[i].buf, APP_DATA_MODEM) == 0) {
			return true;
		}
	}

	return false;
}

static bool battery_data_requested(struct app_mgr_event_data *data_list,
				   size_t count)
{
	for (int i = 0; i < count; i++) {
		if (strcmp(data_list[i].buf, APP_DATA_BATTERY) == 0) {
			return true;
		}
	}

	return false;
}

static void state_agnostic_manager_events(struct modem_msg_data *modem_msg)
{
	if (is_app_mgr_event(&modem_msg->manager.app.header) &&
	    modem_msg->manager.app.type == APP_MGR_EVT_DATA_GET) {
		if (modem_data_requested(modem_msg->manager.app.data_list,
					 modem_msg->manager.app.count)) {

			int err;

			err = modem_manager_modem_data_get();
			if (err) {
				signal_error(err);
			}
		}

		if (battery_data_requested(modem_msg->manager.app.data_list,
					   modem_msg->manager.app.count)) {

			int err;

			err = modem_manager_battery_data_get();
			if (err) {
				signal_error(err);
			}
		}
	}

	if (is_cloud_mgr_event(&modem_msg->manager.cloud.header) &&
	    modem_msg->manager.cloud.type == CLOUD_MGR_EVT_CONNECTED) {
		date_time_update_async(date_time_mgr_event_handler);
	}

	if (is_util_mgr_event(&modem_msg->manager.util.header) &&
	    modem_msg->manager.util.type == UTIL_MGR_EVT_SHUTDOWN_REQUEST) {

		lte_lc_power_off();

		struct modem_mgr_event *modem_mgr_event = new_modem_mgr_event();

		modem_mgr_event->type = MODEM_MGR_EVT_SHUTDOWN_READY;
		EVENT_SUBMIT(modem_mgr_event);
	}
}

static void modem_manager(void)
{
	int err;

	struct modem_msg_data modem_msg;

	atomic_inc(&manager_count);

	err = lte_manager_setup();
	if (err) {
		LOG_ERR("lte_manager_setup, error: %d", err);
		signal_error(err);
	}

	/* State agnostic manager. */
	while (true) {
		k_msgq_get(&msgq_modem, &modem_msg, K_FOREVER);
		state_agnostic_manager_events(&modem_msg);
	}
}

K_THREAD_DEFINE(modem_manager_thread, CONFIG_MODEM_MGR_THREAD_STACK_SIZE,
		modem_manager, NULL, NULL, NULL,
		K_HIGHEST_APPLICATION_THREAD_PRIO, 0, -1);

EVENT_LISTENER(MODULE, event_handler);
EVENT_SUBSCRIBE(MODULE, app_mgr_event);
EVENT_SUBSCRIBE(MODULE, cloud_mgr_event);
EVENT_SUBSCRIBE_FINAL(MODULE, util_mgr_event);
