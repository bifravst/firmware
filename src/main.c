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

#include <logging/log.h>
LOG_MODULE_REGISTER(cat_tracker, CONFIG_CAT_TRACKER_LOG_LEVEL);

#define AWS_CLOUD_CLIENT_ID_LEN 15
#define AWS "$aws/things/"
#define AWS_LEN (sizeof(AWS) - 1)
#define CFG_TOPIC AWS "%s/shadow/get/accepted/desired/cfg"
#define CFG_TOPIC_LEN (AWS_LEN + AWS_CLOUD_CLIENT_ID_LEN + 32)
#define BATCH_TOPIC "%s/batch"
#define BATCH_TOPIC_LEN (AWS_CLOUD_CLIENT_ID_LEN + 6)

static struct cloud_data_gps cir_buf_gps[CONFIG_CIRCULAR_SENSOR_BUFFER_MAX];

static struct cloud_data cloud_data = {
				.gps_timeout = 60,
				.active = true,
				.active_wait = 60,
				.passive_wait = 60,
				.movement_timeout = 3600,
				.accel_threshold = 100,
				.gps_found = false };

static struct cloud_endpoint sub_ep_topics_sub[1];
static struct cloud_endpoint pub_ep_topics_sub[1];

static char client_id_buf[AWS_CLOUD_CLIENT_ID_LEN + 1];
static char batch_topic[BATCH_TOPIC_LEN + 1];
static char cfg_topic[CFG_TOPIC_LEN + 1];

static struct modem_param_info modem_param;
static struct cloud_backend *cloud_backend;

static bool queued_entries;
static bool cloud_connected;

static int rsrp;
static int head_cir_buf;
static int num_queued_entries;

static struct k_delayed_work cloud_config_get_work;
static struct k_delayed_work cloud_send_sensor_data_work;
static struct k_delayed_work cloud_send_modem_data_work;
static struct k_delayed_work cloud_send_modem_data_dyn_work;
static struct k_delayed_work cloud_send_cfg_work;
static struct k_delayed_work cloud_send_buffered_data_work;
static struct k_delayed_work set_led_device_mode_work;
static struct k_delayed_work movement_timeout_work;

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

static void set_current_time(struct gps_data gps_data)
{
	struct tm gps_time;

	/* Change datetime.year and datetime.month to accomodate the
	 * correct input format. */
	gps_time.tm_year = gps_data.pvt.datetime.year + 100;
	gps_time.tm_mon = gps_data.pvt.datetime.month - 1;
	gps_time.tm_mday = gps_data.pvt.datetime.day;
	gps_time.tm_hour = gps_data.pvt.datetime.hour;
	gps_time.tm_min = gps_data.pvt.datetime.minute;
	gps_time.tm_sec = gps_data.pvt.datetime.seconds;

	date_time_set(&gps_time);
}

static void set_led_device_mode(void)
{
	if (!cloud_data.active) {
		ui_led_set_pattern(UI_LED_PASSIVE_MODE);
	} else {
		ui_led_set_pattern(UI_LED_ACTIVE_MODE);
	}
}

static double get_accel_thres(void)
{
	double accel_threshold_double;

	if (cloud_data.accel_threshold == 0) {
		accel_threshold_double = 0;
	} else {
		accel_threshold_double = cloud_data.accel_threshold / 10;
	}

	return accel_threshold_double;
}

static void populate_gps_buffer(struct gps_data gps_data)
{
	cloud_data.gps_found = true;

	head_cir_buf += 1;
	if (head_cir_buf == CONFIG_CIRCULAR_SENSOR_BUFFER_MAX - 1) {
		head_cir_buf = 0;
	}

	cir_buf_gps[head_cir_buf].longitude = gps_data.pvt.longitude;
	cir_buf_gps[head_cir_buf].latitude = gps_data.pvt.latitude;
	cir_buf_gps[head_cir_buf].altitude = gps_data.pvt.altitude;
	cir_buf_gps[head_cir_buf].accuracy = gps_data.pvt.accuracy;
	cir_buf_gps[head_cir_buf].speed = gps_data.pvt.speed;
	cir_buf_gps[head_cir_buf].heading = gps_data.pvt.heading;
	cir_buf_gps[head_cir_buf].gps_timestamp = k_uptime_get();
	cir_buf_gps[head_cir_buf].queued = true;

	LOG_INF("Entry: %d in gps_buffer filled", head_cir_buf);
}

static int get_voltage_level(void)
{
	int err;

	/* This solution of requesting all modem parameters should
	   be replaced with only requesting battery */
	err = modem_info_params_get(&modem_param);
	if (err) {
		LOG_ERR("modem_info_params_get, error: %d", err);
		return err;
	}

	cloud_data.bat_voltage = modem_param.device.battery.value;
	cloud_data.bat_timestamp = k_uptime_get();

	return 0;
}

static int modem_data_get(void)
{
	int err;

	cloud_data.roam_modem_data_ts = k_uptime_get();
	cloud_data.dev_modem_data_ts = k_uptime_get();

	err = modem_info_params_get(&modem_param);
	if (err) {
		LOG_ERR("Error getting modem_info: %d", err);
		return err;
	}

	return 0;
}

static void cloud_config_get(void)
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

static void cloud_send_cfg(void)
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
		return;
	}
}

static void cloud_send_sensor_data(void)
{
	int err;

	ui_led_set_pattern(UI_CLOUD_PUBLISHING);

	struct cloud_msg msg = {
		.qos = CLOUD_QOS_AT_MOST_ONCE,
		.endpoint.type = CLOUD_EP_TOPIC_MSG,
	};

	err = get_voltage_level();
	if (err) {
		LOG_ERR("Error requesting voltage level %d", err);
		return;
	}

	err = cloud_encode_sensor_data(&msg, &cloud_data,
				       &cir_buf_gps[head_cir_buf]);
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
	cir_buf_gps[head_cir_buf].queued = false;
}

static void cloud_send_modem_data(bool include_dev_data)
{
	int err;

	ui_led_set_pattern(UI_CLOUD_PUBLISHING);

	struct cloud_msg msg = {
		.qos = CLOUD_QOS_AT_MOST_ONCE,
		.endpoint.type = CLOUD_EP_TOPIC_MSG,
	};

	err = modem_data_get();
	if (err) {
		LOG_ERR("modem_data_get, error: %d", err);
		return;
	}

	err = cloud_encode_modem_data(&msg, &cloud_data, &modem_param,
				      include_dev_data, rsrp);
	if (err) {
		LOG_ERR("Error encoding modem data, error: %d", err);
		return;
	}

	err = cloud_send(cloud_backend, &msg);
	cloud_release_data(&msg);
	if (err) {
		LOG_ERR("Cloud send failed, err: %d", err);
		return;
	}
}

static void cloud_send_buffered_data(void)
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

static void cloud_synchronize(void)
{
	k_delayed_work_submit(&cloud_config_get_work, K_NO_WAIT);
	k_delayed_work_submit(&cloud_send_cfg_work, K_SECONDS(5));
	k_delayed_work_submit(&cloud_send_modem_data_work, K_SECONDS(5));
}

static void cloud_update(void)
{
	if (k_sem_count_get(&cloud_conn_sem) && cloud_connected) {
		k_delayed_work_submit(&cloud_send_sensor_data_work,
				      K_NO_WAIT);
		k_delayed_work_submit(&cloud_send_modem_data_dyn_work,
				      K_SECONDS(10));
		k_delayed_work_submit(&cloud_send_buffered_data_work,
				      K_SECONDS(10));
		k_delayed_work_submit(&set_led_device_mode_work,
				      K_SECONDS(10));
	}
}

static void set_led_device_mode_work_fn(struct k_work *work)
{
	set_led_device_mode();
}

static void cloud_config_get_work_fn(struct k_work *work)
{
	cloud_config_get();
}

static void cloud_send_sensor_data_work_fn(struct k_work *work)
{
	cloud_send_sensor_data();
}

static void cloud_send_cfg_work_fn(struct k_work *work)
{
	cloud_send_cfg();
}

static void cloud_send_modem_data_work_fn(struct k_work *work)
{
	cloud_send_modem_data(true);
}

static void cloud_send_modem_data_dyn_work_fn(struct k_work *work)
{
	cloud_send_modem_data(false);
}

static void cloud_send_buffered_data_work_fn(struct k_work *work)
{
	cloud_send_buffered_data();
}

static void movement_timeout_work_fn(struct k_work *work)
{
	if (!cloud_data.active) {
		LOG_INF("Movement timeout triggered");
		int err = lte_connection_check();
		if (err) {
			LOG_ERR("lte_connection_check, error: %d", err);
			error_handler(err);
		}
		cloud_update();
	}

	k_delayed_work_submit(&movement_timeout_work,
			      K_SECONDS(cloud_data.movement_timeout));
}

static void work_init(void)
{
	k_delayed_work_init(&cloud_config_get_work,
			    cloud_config_get_work_fn);
	k_delayed_work_init(&cloud_send_sensor_data_work,
			    cloud_send_sensor_data_work_fn);
	k_delayed_work_init(&cloud_send_cfg_work,
			    cloud_send_cfg_work_fn);
	k_delayed_work_init(&cloud_send_modem_data_work,
			    cloud_send_modem_data_work_fn);
	k_delayed_work_init(&cloud_send_modem_data_dyn_work,
			    cloud_send_modem_data_dyn_work_fn);
	k_delayed_work_init(&cloud_send_buffered_data_work,
			    cloud_send_buffered_data_work_fn);
	k_delayed_work_init(&set_led_device_mode_work,
			    set_led_device_mode_work_fn);
	k_delayed_work_init(&movement_timeout_work,
			    movement_timeout_work_fn);
}

static void adxl362_trigger_handler(struct device *dev,
				    struct sensor_trigger *trig)
{
	struct sensor_value accel[3];

	switch (trig->type) {
	case SENSOR_TRIG_THRESHOLD:

		if (sensor_sample_fetch(dev) < 0) {
			LOG_ERR("Sample fetch error");
			return;
		}

		sensor_channel_get(dev, SENSOR_CHAN_ACCEL_X, &accel[0]);
		sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Y, &accel[1]);
		sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Z, &accel[2]);

		double x = sensor_value_to_double(&accel[0]);
		double y = sensor_value_to_double(&accel[1]);
		double z = sensor_value_to_double(&accel[2]);

		if ((abs(x) > get_accel_thres()) ||
		    (abs(y) > get_accel_thres()) ||
		    (abs(z) > get_accel_thres())) {
			cloud_data.acc[0] = x;
			cloud_data.acc[1] = y;
			cloud_data.acc[2] = z;
			cloud_data.acc_timestamp = k_uptime_get();
			k_sem_give(&accel_trig_sem);
		}

		break;
	default:
		LOG_ERR("Unknown trigger");
	}
}

static void gps_trigger_handler(struct device *dev, struct gps_trigger *trigger)
{
	static u32_t fix_count;
	struct gps_data gps_data;

	ARG_UNUSED(trigger);

	if (++fix_count < CONFIG_GPS_CONTROL_FIX_COUNT) {
		return;
	}

	fix_count = 0;

	LOG_INF("gps control handler triggered!");

	gps_channel_get(dev, GPS_CHAN_PVT, &gps_data);
	set_current_time(gps_data);
	populate_gps_buffer(gps_data);
	k_sem_give(&gps_timeout_sem);
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
		cloud_synchronize();
		boot_write_img_confirmed();
		k_delayed_work_submit(&movement_timeout_work,
				      K_SECONDS(cloud_data.movement_timeout));
		cloud_connected = true;
		break;
	case CLOUD_EVT_READY:
		LOG_INF("CLOUD_EVT_READY");
		break;
	case CLOUD_EVT_DISCONNECTED:
		LOG_INF("CLOUD_EVT_DISCONNECTED");
		cloud_connected = false;
		break;
	case CLOUD_EVT_ERROR:
		LOG_ERR("CLOUD_EVT_ERROR");
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
		k_delayed_work_submit(&cloud_send_cfg_work, K_NO_WAIT);
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
			   K_SECONDS(CONFIG_MQTT_KEEPALIVE / 3));

		if (err < 0) {
			LOG_ERR("poll, error: %d", err);
			error_handler(err);
			continue;
		}

		if (err == 0) {
			cloud_ping(cloud_backend);
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

	rsrp = rsrp_value;

	LOG_INF("Incoming RSRP status message, RSRP value is %d", rsrp);
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

	work_init();
	adxl362_init();

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

	/*Sleep so that the device manages to adapt
	  to its new configuration before a GPS search*/
	k_sleep(K_SECONDS(20));

	while (true) {
		/*Check current device mode*/
		if (!cloud_data.active) {
			if (!k_sem_take(&accel_trig_sem, K_FOREVER)) {
				LOG_INF("The cat is moving!");
			}
		}

		/*Start GPS search*/
		gps_control_start(K_NO_WAIT);

		/*Wait for GPS search timeout*/
		k_sem_take(&gps_timeout_sem, K_SECONDS(cloud_data.gps_timeout));

		/*Stop GPS search*/
		gps_control_stop(K_NO_WAIT);

		/*Check lte connection*/
		err = lte_connection_check();
		if (err) {
			LOG_ERR("lte_connection_check, error: %d", err);
			error_handler(err);
		}

		/*Send update to cloud if a connection has been established*/
		cloud_update();

		/*Sleep*/
		LOG_INF("Going to sleep for: %d seconds", check_active_wait());
		k_sleep(K_SECONDS(check_active_wait()));
	}
}
