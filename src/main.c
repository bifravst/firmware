#include <zephyr.h>
#include <stdio.h>
#include <string.h>
#include <logging/log_ctrl.h>
#include <power/reboot.h>
#include <device.h>
#include <drivers/sensor.h>
#include <drivers/gps.h>
#include <gps_controller.h>
#include <ui.h>
#include <net/cloud.h>
#include <cloud_codec.h>
#include <modem/lte_lc.h>
#include <stdlib.h>
#include <modem/modem_info.h>
#include <time.h>
#include <net/socket.h>
#include <dfu/mcuboot.h>
#include <date_time.h>
#include <dk_buttons_and_leds.h>
#include "watchdog.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(cat_tracker, CONFIG_CAT_TRACKER_LOG_LEVEL);

#define AWS_CLOUD_CLIENT_ID_LEN 15
#define AWS "$aws/things/"
#define AWS_LEN (sizeof(AWS) - 1)
#define CFG_TOPIC AWS "%s/shadow/get/accepted/desired/cfg"
#define CFG_TOPIC_LEN (AWS_LEN + AWS_CLOUD_CLIENT_ID_LEN + 32)
#define BATCH_TOPIC "%s/batch"
#define BATCH_TOPIC_LEN (AWS_CLOUD_CLIENT_ID_LEN + 6)
#define MESSAGES_TOPIC "%s/messages"
#define MESSAGES_TOPIC_LEN (AWS_CLOUD_CLIENT_ID_LEN + 9)

enum app_endpoint_type {
	CLOUD_EP_TOPIC_MESSAGES = CLOUD_EP_PRIV_START
};

static struct cloud_data_gps cir_buf_gps[CONFIG_CIRCULAR_SENSOR_BUFFER_MAX];

static struct cloud_data cloud_data = {
				.gps_timeout = 60,
				.active = true,
				.active_wait = 60,
				.passive_wait = 60,
				.mov_timeout = 3600,
				.acc_thres = 100,
				.gps_found = false,
				.synch = true,
				.acc_trig = false};

static struct cloud_endpoint sub_ep_topics_sub[1];
static struct cloud_endpoint pub_ep_topics_sub[2];

static char client_id_buf[AWS_CLOUD_CLIENT_ID_LEN + 1];
static char batch_topic[BATCH_TOPIC_LEN + 1];
static char cfg_topic[CFG_TOPIC_LEN + 1];
static char messages_topic[MESSAGES_TOPIC_LEN + 1];

static struct modem_param_info modem_param;
static struct cloud_backend *cloud_backend;

static bool queued_entries;
static bool cloud_connected;

static int head_cir_buf;
static int num_queued_entries;

static struct k_delayed_work cloud_configuration_get_work;
static struct k_delayed_work cloud_configuration_send_work;
static struct k_delayed_work cloud_sensor_data_send_work;
static struct k_delayed_work cloud_buffered_data_send_work;
static struct k_delayed_work cloud_button_message_send_work;
static struct k_delayed_work led_device_mode_set_work;
static struct k_delayed_work mov_timeout_work;

K_SEM_DEFINE(accel_trig_sem, 0, 1);
K_SEM_DEFINE(gps_timeout_sem, 0, 1);
K_SEM_DEFINE(cloud_conn_sem, 0, 1);

void error_handler(int err_code)
{
	LOG_ERR("err_handler, error code: %d", err_code);
	ui_led_set_pattern(UI_LED_ERROR_SYSTEM_FAULT);

#if !defined(CONFIG_DEBUG) && defined(CONFIG_REBOOT)
	LOG_PANIC();
	sys_reboot(0);
#else
	while (true) {
		k_cpu_idle();
	}
#endif
}

void bsd_recoverable_error_handler(uint32_t err)
{
	error_handler((int)err);
}

void k_sys_fatal_error_handler(unsigned int reason, const z_arch_esf_t *esf)
{
	ARG_UNUSED(esf);

	LOG_PANIC();
	LOG_ERR("k_sys_fatal_error_handler, error: %d", reason);
	error_handler(reason);
	CODE_UNREACHABLE;
}

static int modem_configure(void)
{
	int err;

	ui_led_set_pattern(UI_LTE_CONNECTING);

	LOG_INF("Connecting to LTE network. ");
	LOG_INF("This may take several minutes.");
	err = lte_lc_init_and_connect();
	if (err) {
		LOG_ERR("lte_lc_init_connect, error: %d", err);
		return err;
	}

	LOG_INF("Connected to LTE network");

	/* Sleep to make sure the network has pushed time to modem. */
	k_sleep(K_SECONDS(5));

	/* Update time before any publication. */
	date_time_update();

	k_sleep(K_SECONDS(10));

	k_sem_give(&cloud_conn_sem);

	return 0;
}

static int lte_connection_check(void)
{
	int err;

	enum lte_lc_nw_reg_status nw_reg_status;

	err = lte_lc_nw_reg_status_get(&nw_reg_status);
	if (err) {
		LOG_ERR("lte_lc_nw_reg_status, error: %d", err);
		return err;
	}

	LOG_INF("Checking LTE connection...");

	switch (nw_reg_status) {
	case LTE_LC_NW_REG_REGISTERED_HOME:
	case LTE_LC_NW_REG_REGISTERED_ROAMING:
		break;
	default:
		goto exit;
	}

	LOG_INF("LTE link maintained");

	k_sem_give(&cloud_conn_sem);

	return 0;

exit:

	LOG_ERR("LTE link not maintained");

	k_sem_take(&cloud_conn_sem, K_NO_WAIT);

	return 0;
}

static int check_active_wait(void)
{
	if (!cloud_data.active) {
		return cloud_data.passive_wait;
	}

	return cloud_data.active_wait;
}

static void set_current_time(struct gps_pvt *gps_data)
{
	struct tm gps_time;

	/* Change datetime.year and datetime.month to accomodate the
	 * correct input format. */
	gps_time.tm_year = gps_data->datetime.year  - 1900;
	gps_time.tm_mon  = gps_data->datetime.month - 1;
	gps_time.tm_mday = gps_data->datetime.day;
	gps_time.tm_hour = gps_data->datetime.hour;
	gps_time.tm_min  = gps_data->datetime.minute;
	gps_time.tm_sec  = gps_data->datetime.seconds;

	date_time_set(&gps_time);
}

static void led_device_mode_set(void)
{
	if (!gps_control_is_active()) {
		if (!cloud_data.active) {
			ui_led_set_pattern(UI_LED_PASSIVE_MODE);
		} else {
			ui_led_set_pattern(UI_LED_ACTIVE_MODE);
		}
	} else {
		ui_led_set_pattern(UI_LED_GPS_SEARCHING);
	}
}

static double get_accel_thres(void)
{
	double acc_thres_double;

	if (cloud_data.acc_thres == 0) {
		acc_thres_double = 0;
	} else {
		acc_thres_double = cloud_data.acc_thres / 10;
	}

	return acc_thres_double;
}

static void populate_gps_buffer(struct gps_pvt *gps_data)
{
	cloud_data.gps_found = true;

	head_cir_buf += 1;
	if (head_cir_buf == CONFIG_CIRCULAR_SENSOR_BUFFER_MAX - 1) {
		head_cir_buf = 0;
	}

	cir_buf_gps[head_cir_buf].longitude = gps_data->longitude;
	cir_buf_gps[head_cir_buf].latitude  = gps_data->latitude;
	cir_buf_gps[head_cir_buf].altitude  = gps_data->altitude;
	cir_buf_gps[head_cir_buf].accuracy  = gps_data->accuracy;
	cir_buf_gps[head_cir_buf].speed     = gps_data->speed;
	cir_buf_gps[head_cir_buf].heading   = gps_data->heading;
	cir_buf_gps[head_cir_buf].gps_ts    = k_uptime_get();
	cir_buf_gps[head_cir_buf].queued    = true;

	LOG_INF("Entry: %d in gps_buffer filled", head_cir_buf);
}

static void cloud_configuration_get(void)
{
	int err;

	ui_led_set_pattern(UI_CLOUD_PUBLISHING);

	struct cloud_msg msg = { .qos = CLOUD_QOS_AT_MOST_ONCE,
				 .endpoint.type = CLOUD_EP_TOPIC_STATE,
				 .buf = "",
				 .len = 0 };

	err = cloud_send(cloud_backend, &msg);
	if (err) {
		LOG_ERR("Cloud send failed, err: %d", err);
	}
}

static void cloud_button_message_send(void)
{
	int err;

	ui_led_set_pattern(UI_CLOUD_PUBLISHING);

	struct cloud_msg msg = {
		.qos = CLOUD_QOS_AT_MOST_ONCE,
		.endpoint = pub_ep_topics_sub[1],
	};

	err = cloud_encode_button_message_data(&msg, &cloud_data);
	if (err) {
		LOG_ERR("cloud_encode_button_message_data, error: %d", err);
		return;
	}

	err = cloud_send(cloud_backend, &msg);
	cloud_release_data(&msg);
	if (err) {
		LOG_ERR("Cloud send failed, err: %d", err);
	}
}

static void cloud_configuration_send(void)
{
	int err;

	ui_led_set_pattern(UI_CLOUD_PUBLISHING);

	struct cloud_msg msg = {
		.qos = CLOUD_QOS_AT_MOST_ONCE,
		.endpoint.type = CLOUD_EP_TOPIC_MSG,
	};

	err = cloud_encode_cfg_data(&msg, &cloud_data);
	if (err == -EAGAIN) {
		LOG_INF("No change in device configuration");
		return;
	} else if (err) {
		LOG_ERR("Device configuration not encoded, error: %d", err);
		return;
	}

	err = cloud_send(cloud_backend, &msg);
	cloud_release_data(&msg);
	if (err) {
		LOG_ERR("Cloud send failed, err: %d", err);
	}
}

static void cloud_sensor_data_send(void)
{
	int err;

	ui_led_set_pattern(UI_CLOUD_PUBLISHING);

	struct cloud_msg msg = {
		.qos = CLOUD_QOS_AT_MOST_ONCE,
		.endpoint.type = CLOUD_EP_TOPIC_MSG,
	};

	err = modem_info_params_get(&modem_param);
	if (err) {
		LOG_ERR("modem_info_params_get, error: %d", err);
		return;
	}

	/* set modem data sample uptime. */
	cloud_data.mod_ts = k_uptime_get();

	err = cloud_encode_sensor_data(&msg, &cloud_data,
				       &cir_buf_gps[head_cir_buf],
				       &modem_param);
	if (err) {
		LOG_ERR("Error enconding message %d", err);
		return;
	}

	err = cloud_send(cloud_backend, &msg);
	cloud_release_data(&msg);
	if (err) {
		LOG_ERR("Cloud send failed, err: %d", err);
		return;
	}

	cloud_data.gps_found = false;
	cloud_data.acc_trig = false;
	cir_buf_gps[head_cir_buf].queued = false;
}

static void cloud_buffered_data_send(void)
{
	int err;

	ui_led_set_pattern(UI_CLOUD_PUBLISHING);

	struct cloud_msg msg = {
		.qos = CLOUD_QOS_AT_MOST_ONCE,
		.endpoint = pub_ep_topics_sub[0],
	};

	/* Check if it exists queued entries in the gps buffer. */
	for (int i = 0; i < CONFIG_CIRCULAR_SENSOR_BUFFER_MAX; i++) {
		if (cir_buf_gps[i].queued) {
			queued_entries = true;
			num_queued_entries++;
		}
	}

	/* Encode and send queued entries in batches. */
	while (num_queued_entries > 0 && queued_entries) {
		err = cloud_encode_gps_buffer(&msg, cir_buf_gps);
		if (err) {
			LOG_ERR("Error encoding circular buffer: %d", err);
			goto exit;
		}

		err = cloud_send(cloud_backend, &msg);
		cloud_release_data(&msg);
		if (err) {
			LOG_ERR("Cloud send failed, err: %d", err);
			goto exit;
		}

		num_queued_entries -= CONFIG_CIRCULAR_SENSOR_BUFFER_MAX;
	}

exit:
	num_queued_entries = 0;
	queued_entries = false;
}

static void cloud_synch(void)
{
	int err;

	err = lte_connection_check();
	if (err) {
		LOG_ERR("lte_connection_check, error: %d", err);
		error_handler(err);
	}

	if (k_sem_count_get(&cloud_conn_sem) && cloud_connected) {
		k_delayed_work_submit(&cloud_configuration_get_work,
					K_NO_WAIT);
		k_delayed_work_submit(&cloud_configuration_send_work,
					K_NO_WAIT);
		k_delayed_work_submit(&cloud_sensor_data_send_work,
					K_NO_WAIT);
	}
}

static void cloud_update(void)
{
	int err;

	err = lte_connection_check();
	if (err) {
		LOG_ERR("lte_connection_check, error: %d", err);
		error_handler(err);
	}

	cloud_data.synch = false;

	if (k_sem_count_get(&cloud_conn_sem) && cloud_connected) {
		k_delayed_work_submit(&cloud_sensor_data_send_work,
				      K_NO_WAIT);
		k_delayed_work_submit(&cloud_buffered_data_send_work,
				      K_NO_WAIT);
	}
}

static void led_device_mode_set_work_fn(struct k_work *work)
{
	led_device_mode_set();
}

static void cloud_configuration_get_work_fn(struct k_work *work)
{
	cloud_configuration_get();
}

static void cloud_configuration_send_work_fn(struct k_work *work)
{
	cloud_configuration_send();
}

static void cloud_sensor_data_send_work_fn(struct k_work *work)
{
	cloud_sensor_data_send();
}

static void cloud_buffered_data_send_work_fn(struct k_work *work)
{
	cloud_buffered_data_send();
}

static void cloud_button_message_send_work_fn(struct k_work *work)
{
	cloud_button_message_send();
}

static void mov_timeout_work_fn(struct k_work *work)
{
	if (!cloud_data.active) {
		LOG_INF("Movement timeout triggered");
		cloud_update();
	}

	k_delayed_work_submit(&mov_timeout_work,
			      K_SECONDS(cloud_data.mov_timeout));
	k_delayed_work_submit(&led_device_mode_set_work, K_SECONDS(15));
}

static void work_init(void)
{
	k_delayed_work_init(&cloud_configuration_get_work,
			    cloud_configuration_get_work_fn);
	k_delayed_work_init(&cloud_sensor_data_send_work,
			    cloud_sensor_data_send_work_fn);
	k_delayed_work_init(&cloud_configuration_send_work,
			    cloud_configuration_send_work_fn);
	k_delayed_work_init(&cloud_buffered_data_send_work,
			    cloud_buffered_data_send_work_fn);
	k_delayed_work_init(&led_device_mode_set_work,
			    led_device_mode_set_work_fn);
	k_delayed_work_init(&mov_timeout_work,
			    mov_timeout_work_fn);
	k_delayed_work_init(&cloud_button_message_send_work,
			    cloud_button_message_send_work_fn);
}

static void adxl362_trigger_handler(struct device *dev,
				    struct sensor_trigger *trig)
{
	int err = 0;
	struct sensor_value accel[3];

	switch (trig->type) {
	case SENSOR_TRIG_THRESHOLD:

		if (sensor_sample_fetch(dev) < 0) {
			LOG_ERR("Sample fetch error");
			return;
		}

		err = sensor_channel_get(dev, SENSOR_CHAN_ACCEL_X, &accel[0]);
		err += sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Y, &accel[1]);
		err += sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Z, &accel[2]);

		if (err) {
			LOG_ERR("sensor_channel_get, error: %d", err);
			return;
		}

		double x = sensor_value_to_double(&accel[0]);
		double y = sensor_value_to_double(&accel[1]);
		double z = sensor_value_to_double(&accel[2]);

		if ((abs(x) > get_accel_thres()) ||
		    (abs(y) > get_accel_thres()) ||
		    (abs(z) > get_accel_thres())) {
			cloud_data.acc[0] = x;
			cloud_data.acc[1] = y;
			cloud_data.acc[2] = z;
			cloud_data.acc_ts = k_uptime_get();
			cloud_data.acc_trig = true;
			k_sem_give(&accel_trig_sem);
		}

		break;
	default:
		LOG_ERR("Unknown trigger");
	}
}

static void gps_trigger_handler(struct device *dev, struct gps_event *evt)
{
	switch (evt->type) {
	case GPS_EVT_SEARCH_STARTED:
		LOG_INF("GPS_EVT_SEARCH_STARTED");
		break;
	case GPS_EVT_SEARCH_STOPPED:
		LOG_INF("GPS_EVT_SEARCH_STOPPED");
		break;
	case GPS_EVT_SEARCH_TIMEOUT:
		LOG_INF("GPS_EVT_SEARCH_TIMEOUT");
		gps_control_set_active(false);
		k_sem_give(&gps_timeout_sem);
		break;
	case GPS_EVT_PVT:
		/* Don't spam logs */
		break;
	case GPS_EVT_PVT_FIX:
		LOG_INF("GPS_EVT_PVT_FIX");
		gps_control_set_active(false);
		set_current_time(&evt->pvt);
		populate_gps_buffer(&evt->pvt);
		k_sem_give(&gps_timeout_sem);
		break;
	case GPS_EVT_NMEA:
		/* Don't spam logs */
		break;
	case GPS_EVT_NMEA_FIX:
		LOG_INF("Position fix with NMEA data");
		break;
	case GPS_EVT_OPERATION_BLOCKED:
		LOG_INF("GPS_EVT_OPERATION_BLOCKED");
		break;
	case GPS_EVT_OPERATION_UNBLOCKED:
		LOG_INF("GPS_EVT_OPERATION_UNBLOCKED");
		break;
	case GPS_EVT_AGPS_DATA_NEEDED:
		LOG_INF("GPS_EVT_AGPS_DATA_NEEDED");
		break;
	case GPS_EVT_ERROR:
		LOG_INF("GPS_EVT_ERROR\n");
		break;
	default:
		break;
	}
}

static void adxl362_init(void)
{
	struct device *dev = device_get_binding(DT_INST_0_ADI_ADXL362_LABEL);

	if (dev == NULL) {
		LOG_INF("Device get binding device");
		return;
	}

	if (IS_ENABLED(CONFIG_ADXL362_TRIGGER)) {
		struct sensor_trigger trig = { .chan = SENSOR_CHAN_ACCEL_XYZ };

		trig.type = SENSOR_TRIG_THRESHOLD;
		if (sensor_trigger_set(dev, &trig, adxl362_trigger_handler)) {
			LOG_ERR("Trigger set error");
			return;
		}
	}
}

void cloud_event_handler(const struct cloud_backend *const backend,
			 const struct cloud_event *const evt, void *user_data)
{
	ARG_UNUSED(user_data);

	int err;

	switch (evt->type) {
	case CLOUD_EVT_CONNECTED:
		LOG_INF("CLOUD_EVT_CONNECTED");
		cloud_connected = true;
		cloud_synch();
		boot_write_img_confirmed();
		k_delayed_work_submit(&mov_timeout_work,
				      K_SECONDS(cloud_data.mov_timeout));
		break;
	case CLOUD_EVT_READY:
		LOG_INF("CLOUD_EVT_READY");
		err = lte_lc_psm_req(true);
		if (err) {
			LOG_ERR("PSM request failed, error: %d", err);
		} else {
			LOG_INF("PSM enabled");
		}
		break;
	case CLOUD_EVT_DISCONNECTED:
		LOG_INF("CLOUD_EVT_DISCONNECTED");
		cloud_connected = false;
		break;
	case CLOUD_EVT_ERROR:
		LOG_ERR("CLOUD_EVT_ERROR");
		break;
	case CLOUD_EVT_FOTA_START:
		LOG_INF("CLOUD_EVT_FOTA_START");
		break;
	case CLOUD_EVT_FOTA_ERASE_PENDING:
		LOG_INF("CLOUD_EVT_FOTA_ERASE_PENDING");
		break;
	case CLOUD_EVT_FOTA_ERASE_DONE:
		LOG_INF("CLOUD_EVT_FOTA_ERASE_DONE");
		break;
	case CLOUD_EVT_FOTA_DONE:
		LOG_INF("CLOUD_EVT_FOTA_DONE");
		cloud_disconnect(cloud_backend);
		sys_reboot(0);
		break;
	case CLOUD_EVT_DATA_SENT:
		LOG_INF("CLOUD_EVT_DATA_SENT");
		break;
	case CLOUD_EVT_DATA_RECEIVED:
		LOG_INF("CLOUD_EVT_DATA_RECEIVED");
		err = cloud_decode_response(evt->data.msg.buf, &cloud_data);
		if (err) {
			LOG_ERR("Could not decode response %d", err);
		}
		k_delayed_work_submit(&cloud_configuration_send_work,
				      K_NO_WAIT);
		break;
	case CLOUD_EVT_PAIR_REQUEST:
		LOG_INF("CLOUD_EVT_PAIR_REQUEST");
		break;
	case CLOUD_EVT_PAIR_DONE:
		LOG_INF("CLOUD_EVT_PAIR_DONE");
		break;
	default:
		LOG_ERR("Unknown cloud event type: %d", evt->type);
		break;
	}
}

void cloud_poll(void)
{
	int err;

connect:

	k_sem_take(&cloud_conn_sem, K_FOREVER);

	err = cloud_connect(cloud_backend);
	if (err) {
		LOG_ERR("cloud_connect failed: %d", err);
		goto connect;
	}

	struct pollfd fds[] = { { .fd = cloud_backend->config->socket,
				  .events = POLLIN } };

	while (true) {
		err = poll(fds, ARRAY_SIZE(fds),
			   cloud_keepalive_time_left(cloud_backend));

		if (err < 0) {
			LOG_ERR("poll, error: %d", err);
			error_handler(err);
			continue;
		}

		if (err == 0) {
			cloud_ping(cloud_backend);
			LOG_INF("Cloud ping!");
			continue;
		}

		if ((fds[0].revents & POLLIN) == POLLIN) {
			cloud_input(cloud_backend);
		}

		if ((fds[0].revents & POLLNVAL) == POLLNVAL) {
			LOG_ERR("Socket error: POLLNVAL");
			LOG_ERR("The cloud socket was unexpectedly closed.");
			break;
		}

		if ((fds[0].revents & POLLHUP) == POLLHUP) {
			LOG_ERR("Socket error: POLLHUP");
			LOG_ERR("Connection was closed by the cloud.");
			error_handler(-EIO);
			return;
		}

		if ((fds[0].revents & POLLERR) == POLLERR) {
			LOG_ERR("Socket error: POLLERR");
			LOG_ERR("Cloud connection was unexpectedly closed.");
			error_handler(-EIO);
			return;
		}
	}

	cloud_disconnect(cloud_backend);
	goto connect;
}

K_THREAD_DEFINE(cloud_poll_thread, CONFIG_CLOUD_POLL_STACKSIZE, cloud_poll,
		NULL, NULL, NULL, CONFIG_CLOUD_POLL_PRIORITY, 0, K_NO_WAIT);

static void modem_rsrp_handler(char rsrp_value)
{
	/* RSRP raw values that represent actual signal strength are
	 * 0 through 97 (per "nRF91 AT Commands" v1.1).
	 */

	if (rsrp_value > 97) {
		return;
	}

	cloud_data.rsrp = rsrp_value;

	LOG_INF("Incoming RSRP status message, RSRP value is %d",
		cloud_data.rsrp);
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

static void button_handler(u32_t button_states, u32_t has_changed)
{
	static int try_again_timeout;

	/* Publication of data due to button presses limited
	 * to 1 push every 2 seconds to avoid spamming the cloud socket. */
	if ((has_changed & button_states & DK_BTN1_MSK) &&
	    k_uptime_get() - try_again_timeout > K_SECONDS(2)) {

		int err = lte_connection_check();
		if (err) {
			LOG_ERR("lte_connection_check, error: %d", err);
			error_handler(err);
		}

		if (k_sem_count_get(&cloud_conn_sem) && cloud_connected) {

			LOG_INF("Cloud publication by button 1 triggered, ");
			LOG_INF("2 seconds to next allowed cloud publication ");
			LOG_INF("triggered by button 1");

			cloud_data.btn_number = 1;
			cloud_data.btn_ts = k_uptime_get();
			k_delayed_work_submit(&cloud_button_message_send_work,
					      K_NO_WAIT);
			k_delayed_work_submit(&led_device_mode_set_work,
					K_SECONDS(3));
		}

		try_again_timeout = k_uptime_get();
	}


#if defined(CONFIG_BOARD_NRF9160_PCA10090NS)
	/* Fake motion. The nRF9160 DK does not have an accelerometer by
	 * default. Reset accelerometer data.
	 */
	if (has_changed & button_states & DK_BTN2_MSK) {
		k_sem_give(&accel_trig_sem);
	}
#endif

}

static int populate_app_endpoint_topics()
{
	int err;

	err = snprintf(batch_topic, sizeof(batch_topic), BATCH_TOPIC,
		       client_id_buf);
	if (err != BATCH_TOPIC_LEN) {
		return -ENOMEM;
	}

	pub_ep_topics_sub[0].str = batch_topic;
	pub_ep_topics_sub[0].len = BATCH_TOPIC_LEN;
	pub_ep_topics_sub[0].type = CLOUD_EP_TOPIC_BATCH;

	err = snprintf(messages_topic, sizeof(messages_topic), MESSAGES_TOPIC,
		       client_id_buf);
	if (err != MESSAGES_TOPIC_LEN) {
		return -ENOMEM;
	}

	pub_ep_topics_sub[1].str = messages_topic;
	pub_ep_topics_sub[1].len = MESSAGES_TOPIC_LEN;
	pub_ep_topics_sub[1].type = CLOUD_EP_TOPIC_MESSAGES;

	err = snprintf(cfg_topic, sizeof(cfg_topic), CFG_TOPIC,
		       client_id_buf);
	if (err != CFG_TOPIC_LEN) {
		return -ENOMEM;
	}

	sub_ep_topics_sub[0].str = cfg_topic;
	sub_ep_topics_sub[0].len = CFG_TOPIC_LEN;
	sub_ep_topics_sub[0].type = CLOUD_EP_TOPIC_CONFIG;

	err = cloud_ep_subscriptions_add(cloud_backend,
					 sub_ep_topics_sub,
					 ARRAY_SIZE(sub_ep_topics_sub));
	if (err) {
		LOG_INF("cloud_ep_subscriptions_add, error: %d", err);
		error_handler(err);
	}

	return 0;
}

static int cloud_setup(void)
{
	int err;

	cloud_backend = cloud_get_binding(CONFIG_CLOUD_BACKEND);
	__ASSERT(cloud_backend != NULL, "%s cloud backend not found",
		 CONFIG_CLOUD_BACKEND);

	err = modem_info_string_get(MODEM_INFO_IMEI, client_id_buf);
	if (err != AWS_CLOUD_CLIENT_ID_LEN) {
		LOG_ERR("modem_info_string_get, error: %d", err);
		return err;
	}

	LOG_INF("Device IMEI: %s", log_strdup(client_id_buf));

	/* Fetch IMEI from modem data and set IMEI as cloud connection ID **/
	cloud_backend->config->id = client_id_buf;
	cloud_backend->config->id_len = sizeof(client_id_buf);

	err = cloud_init(cloud_backend, cloud_event_handler);
	if (err) {
		LOG_ERR("cloud_init, error: %d", err);
		return err;
	}

	/* Populate cloud spesific endpoint topics */
	err = populate_app_endpoint_topics();
	if (err) {
		LOG_ERR("populate_app_endpoint_topics, error: %d", err);
		return err;
	}

	return err;
}

void main(void)
{
	int err;

	LOG_INF("The cat tracker has started");
	LOG_INF("Version: %s", log_strdup(CONFIG_CAT_TRACKER_APP_VERSION));

	if (IS_ENABLED(CONFIG_WATCHDOG)) {
		watchdog_init_and_start();
	}

	work_init();
	adxl362_init();

	err = dk_buttons_init(button_handler);
	if (err) {
		LOG_INF("dk_buttons_init, error: %d", err);
		error_handler(err);
	}

	err = modem_data_init();
	if (err) {
		LOG_INF("modem_data_init, error: %d", err);
		error_handler(err);
	}

	err = cloud_setup();
	if (err) {
		LOG_INF("cloud_setup, error %d", err);
		error_handler(err);
	}

	err = ui_init();
	if (err) {
		LOG_INF("ui_init, error: %d", err);
		error_handler(err);
	}

	err = gps_control_init(gps_trigger_handler);
	if (err) {
		LOG_INF("gps_control_init, error %d", err);
		error_handler(err);
	}

	err = modem_configure();
	if (err) {
		LOG_INF("modem_configure, error: %d", err);
		error_handler(err);
	}

	LOG_INF("Getting device configuration...");

	/*Sleep so that the device manages to adapt
	  to its new configuration before a GPS search*/
	k_sleep(K_SECONDS(20));

	while (true) {

		if(cloud_data.active) {
			LOG_INF("Device in ACTIVE mode");
		} else {
			LOG_INF("Device in PASSIVE mode");
		}

		/*Check current device mode*/
		if (!cloud_data.active) {
			k_delayed_work_submit(&led_device_mode_set_work,
					      K_NO_WAIT);
			if (!k_sem_take(&accel_trig_sem, K_FOREVER)) {
				LOG_INF("The cat is moving!");
			}
		}

		/*Start GPS search*/
		gps_control_start(K_NO_WAIT, cloud_data.gps_timeout);

		/*Wait for GPS search timeout*/
		k_sem_take(&gps_timeout_sem, K_FOREVER);

		/*Send update to cloud if a connection has been established */
		cloud_update();

		/* Set device mode led behaviour */
		k_delayed_work_submit(&led_device_mode_set_work, K_SECONDS(15));

		/*Sleep*/
		LOG_INF("Going to sleep for: %d seconds", check_active_wait());
		k_sleep(K_SECONDS(check_active_wait()));
	}
}
