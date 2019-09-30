#include <zephyr.h>
#include <stdio.h>
#include <uart.h>
#include <string.h>
#include <logging/log.h>
#include <logging/log_ctrl.h>
#include <misc/reboot.h>
#include <modem_data.h>
#include <device.h>
#include <sensor.h>
#include <gps_controller.h>
#include <ui.h>
#include <net/cloud.h>
#include <bifravst_cloud_codec.h>
#include <lte_lc.h>
#include <stdlib.h>

#define AT_CMD_SIZE(x) (sizeof(x) - 1)

enum governing_states {
	LTE_CHECK_CONNECTION,
	CHECK_MODE,
	ACTIVE_MODE,
	PASSIVE_MODE,
	GPS_SEARCH,
};

struct cloud_data_gps_t cir_buf_gps[CONFIG_CIRCULAR_SENSOR_BUFFER_MAX];

struct cloud_data_t cloud_data = { .gps_timeout = 1000,
				   .active = true,
				   .active_wait = 60,
				   .passive_wait = 300,
				   .movement_timeout = 3600,
				   .accel_threshold = 100,
				   .gps_found = false };

enum governing_states state = CHECK_MODE;

static struct cloud_backend *cloud_backend;

static bool active;

K_SEM_DEFINE(gps_timeout_sem, 0, 1);
K_SEM_DEFINE(accel_trig_sem, 0, 1);
K_SEM_DEFINE(connect_sem, 0, 1);

// static struct k_work cloud_report_work;
// static struct k_work cloud_report_fix_work;
// static struct k_work cloud_pair_work;
// static struct k_work gps_control_start_work;
// static struct k_work gps_control_stop_work;

static int head_cir_buf;
// static int num_queued_entries;

// static bool queued_entries;
// static bool include_static_modem_data;

enum error_type {
	ERROR_BSD_RECOVERABLE,
	ERROR_BSD_IRRECOVERABLE,
	ERROR_SYSTEM_FAULT,
	ERROR_CLOUD
};

void error_handler(enum error_type err_type, int err_code)
{
#if !defined(CONFIG_DEBUG) && defined(CONFIG_REBOOT)
	LOG_PANIC();
	sys_reboot(0);
#else

	switch (err_type) {
	case ERROR_BSD_RECOVERABLE:
		printk("Error of type ERROR_BSD_RECOVERABLE: %d\n", err_code);
		ui_led_set_pattern(UI_LED_ERROR_BSD_REC);
		break;
	case ERROR_BSD_IRRECOVERABLE:
		printk("Error of type ERROR_BSD_IRRECOVERABLE: %d\n", err_code);
		ui_led_set_pattern(UI_LED_ERROR_BSD_IRREC);
		break;

	case ERROR_SYSTEM_FAULT:
		printk("Error of type ERROR_SYSTEM_FAULT: %d\n", err_code);
		ui_led_set_pattern(UI_LED_ERROR_SYSTEM_FAULT);
		break;

	case ERROR_CLOUD:
		printk("Error of type ERROR_CLOUD: %d\n", err_code);
		ui_led_set_pattern(UI_LED_ERROR_CLOUD);
		break;

	default:
		printk("Unknown error type: %d, code: %d\n", err_type,
		       err_code);
		ui_led_set_pattern(UI_LED_ERROR_UNKNOWN);
		break;
	}

	while (true) {
		k_cpu_idle();
	}
#endif
}

void k_sys_fatal_error_handler(unsigned int reason, const z_arch_esf_t *esf)
{
	ARG_UNUSED(esf);

	LOG_PANIC();
	z_fatal_print("Running main.c error handler");
	error_handler(ERROR_SYSTEM_FAULT, reason);
	CODE_UNREACHABLE;
}

void cloud_error_handler(int err)
{
	error_handler(ERROR_CLOUD, err);
}

// change names and shorten syntax here
int check_mode(void)
{
	if (cloud_data.active) {
		return true;
	} else {
		return false;
	}
}

int check_active_wait(bool mode)
{
	if (mode) {
		return cloud_data.active_wait;
	} else {
		return cloud_data.passive_wait;
	}
}

double check_accel_thres(void)
{
	double accel_threshold_double;

	if (cloud_data.accel_threshold == 0) {
		accel_threshold_double = 0;
	} else {
		accel_threshold_double = cloud_data.accel_threshold / 10;
	}

	return accel_threshold_double;
}

static void attach_gps_data(struct gps_data gps_data)
{
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

	printk("Entry: %d in gps_buffer filled", head_cir_buf);
}

static int request_voltage_level(void)
{
	cloud_data.bat_voltage = request_battery_status();
	cloud_data.bat_timestamp = k_uptime_get();

	return 0;
}

static void cloud_pair(void)
{
	int err;

	struct cloud_msg msg = {
		.qos = CLOUD_QOS_AT_LEAST_ONCE,
		.endpoint.type = CLOUD_EP_TOPIC_PAIR,
		.buf = "",
		.len = 0
	};

	cloud_connect(cloud_backend);

	ui_led_set_pattern(UI_CLOUD_PUBLISHING);

	err = cloud_send(cloud_backend, &msg);
	if (err != 0) {
		printk("Cloud send failed, err: %d\n", err);
	}

	cloud_disconnect(cloud_backend);
}

static void cloud_send_sensor_data(void)
{
	int err;

	struct cloud_msg msg = {
		.qos = CLOUD_QOS_AT_LEAST_ONCE,
		.endpoint.type = CLOUD_EP_TOPIC_MSG
	};

	err = request_voltage_level();
	if (err != 0) {
		printk("Error requesting voltage level %d\n", err);
	}

	err = encode_message(&msg, &cloud_data,
			     &cir_buf_gps[head_cir_buf]);
	if (err != 0) {
		printk("Error enconding message %d\n", err);
	}

	ui_led_set_pattern(UI_CLOUD_PUBLISHING);

	err = cloud_send(cloud_backend, &msg);
	if (err != 0) {
		printk("Cloud send failed, err: %d\n", err);
	}
}

static void cloud_ack_config_change(void)
{

	int err;

	struct cloud_msg msg = {
		.qos = CLOUD_QOS_AT_LEAST_ONCE,
		.endpoint.type = CLOUD_EP_TOPIC_MSG
	};

	if (check_config_change()) {
		err = encode_message(&msg, &cloud_data,
				     &cir_buf_gps[head_cir_buf]);
		if (err != 0) {
			printk("error encoding modem configurations\n");
		}

		ui_led_set_pattern(UI_CLOUD_PUBLISHING);

		err = cloud_send(cloud_backend, &msg);
		if (err != 0) {
			printk("Cloud send failed, err: %d\n", err);
		}
	}
}

// static void cloud_send_modem_data(void)
// {
//      int err;

//      struct cloud_msg msg = {
//              .qos = CLOUD_QOS_AT_LEAST_ONCE,
//              .endpoint.type = CLOUD_EP_TOPIC_MSG
//      };

//      err = encode_modem_data(&msg, true);
//      if (err != 0) {
//              printk("Error encoding modem data");
//      }

//      ui_led_set_pattern(UI_CLOUD_PUBLISHING);

//      err = cloud_send(cloud_backend, &msg);
//      if (err) {
//              printk("Cloud send failed, err: %d\n", err);
//      }
// }

// static void cloud_send_buffered_data(void)
// {
//      // for (int i = 0; i < CONFIG_CIRCULAR_SENSOR_BUFFER_MAX; i++) {
//      //      if (cir_buf_gps[i].queued) {
//      //              queued_entries = true;
//      //              num_queued_entries++;
//      //      }
//      // }

//      // if (queued_entries) {
//      //      while (num_queued_entries > 0) {
//      //              err = encode_gps_buffer(
//      //                      &transmit_data, cir_buf_gps,
//      //                      CONFIG_CIRCULAR_SENSOR_BUFFER_MAX);
//      //              if (err != 0) {
//      //                      goto end;
//      //              }
//      //              transmit_data.topic = batch_topic;

//      //              data_publish(&client, MQTT_QOS_1_AT_LEAST_ONCE,
//      //                           transmit_data.buf, transmit_data.len,
//      //                           transmit_data.topic);
//      //              if (err != 0) {
//      //                      goto end;
//      //              }

//      //              err = process_mqtt_and_sleep(
//      //                      &client,
//      //                      CONFIG_BIFRAVST_MQTT_TRANSMISSION_SLEEP);
//      //              if (err != 0) {
//      //                      goto end;
//      //              }

//      //              num_queued_entries -=
//      //                      CONFIG_CIRCULAR_SENSOR_BUFFER_MAX;
//      //      }
//      // }


//      // num_queued_entries = 0;
//      // queued_entries = false;
// }

static void cloud_process_cycle(void)
{
	cloud_connect(cloud_backend);
	cloud_send_sensor_data();
	cloud_ack_config_change();
	// cloud_send_modem_data();
	// cloud_send_buffered_data();
	cloud_disconnect(cloud_backend);

}

// static void cloud_pair_work_fn(struct k_work *work)
// {
//      cloud_pair();
// }

// static void cloud_report_work_fn(struct k_work *work)
// {
//      cloud_report(false);
// }

// static void cloud_report_fix_work_fn(struct k_work *work)
// {
//      cloud_report(true);
// }

// static void gps_control_start_work_fn(struct k_work *work)
// {
//      gps_control_start();
// }

// static void gps_control_stop_work_fn(struct k_work *work)
// {
//      gps_control_stop();
// }

// static void work_init(void)
// {
//      // k_work_init(&cloud_report_work, cloud_report_work_fn);
//      // k_work_init(&cloud_report_fix_work, cloud_report_fix_work_fn);
//      k_work_init(&cloud_pair_work, cloud_pair_work_fn);
//      // k_work_init(&gps_control_start_work, gps_control_start_work_fn);
//      // k_work_init(&gps_control_stop_work, gps_control_stop_work_fn);
// }

static const char home[] = "+CEREG: 1";
static const char home_2[] = "+CEREG:1";
static const char roam[] = "+CEREG: 5";
static const char roam_2[] = "+CEREG:5";
static const char home_unreg[] = "+CEREG: 1,\"FFFE\"";
static const char home_unreg2[] = "+CEREG:1,\"FFFE\"";
static const char roam_unreg[] = "+CEREG: 5,\"FFFE\"";
static const char roam_unreg2[] = "+CEREG:5,\"FFFE\"";

void connection_handler(char *response)
{
	printk("Incoming network registration status: %s", response);

	if (!memcmp(home_unreg, response, AT_CMD_SIZE(home_unreg)) ||
	    !memcmp(roam_unreg, response, AT_CMD_SIZE(roam_unreg)) ||
	    !memcmp(home_unreg2, response, AT_CMD_SIZE(home_unreg2)) ||
	    !memcmp(roam_unreg2, response, AT_CMD_SIZE(roam_unreg2))) {
		goto no_connection;
	}

	if (!memcmp(home, response, AT_CMD_SIZE(home)) ||
	    !memcmp(roam, response, AT_CMD_SIZE(roam)) ||
	    !memcmp(home_2, response, AT_CMD_SIZE(home_2)) ||
	    !memcmp(roam_2, response, AT_CMD_SIZE(roam_2))) {
		goto connection;
	}

	goto no_connection;

connection:
	if (!k_sem_count_get(&connect_sem)) {
		k_sem_give(&connect_sem);
	}

	return;

no_connection:
	if (k_sem_count_get(&connect_sem)) {
		k_sem_take(&connect_sem, 0);
	}
}

static void adxl362_trigger_handler(struct device *dev,
				    struct sensor_trigger *trig)
{
	static struct sensor_value accel[3];

	switch (trig->type) {
	case SENSOR_TRIG_THRESHOLD:

		if (sensor_sample_fetch(dev) < 0) {
			printk("Sample fetch error\n");
			return;
		}

		sensor_channel_get(dev, SENSOR_CHAN_ACCEL_X, &accel[0]);
		sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Y, &accel[1]);
		sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Z, &accel[2]);

		double x = sensor_value_to_double(&accel[0]);
		double y = sensor_value_to_double(&accel[1]);
		double z = sensor_value_to_double(&accel[2]);

		if ((abs(x) > check_accel_thres()) ||
		    (abs(y) > check_accel_thres()) ||
		    (abs(z) > check_accel_thres())) {
			cloud_data.acc[0] = x;
			cloud_data.acc[1] = y;
			cloud_data.acc[2] = z;
			cloud_data.acc_timestamp = k_uptime_get();
			k_sem_give(&accel_trig_sem);
		}

		break;
	default:
		printk("Unknown trigger\n");
	}
}

static void gps_control_handler(struct device *dev, struct gps_trigger *trigger)
{
	static struct gps_data gps_data;
	int err;

	switch (trigger->type) {
	case GPS_TRIG_FIX:
		printk("gps control handler triggered!\n");
		gps_control_on_trigger();
		gps_channel_get(dev, GPS_CHAN_PVT, &gps_data);
		err = set_current_time(gps_data);
		if (err != 0) {
			printk("Error setting current time %d\n", err);
		}
		attach_gps_data(gps_data);
		k_sem_give(&gps_timeout_sem);
		break;

	default:
		break;
	}
}

static void adxl362_init(void)
{
	struct device *dev = device_get_binding(DT_INST_0_ADI_ADXL362_LABEL);

	if (dev == NULL) {
		printk("Device get binding device\n");
		return;
	}

	if (IS_ENABLED(CONFIG_ADXL362_TRIGGER)) {
		struct sensor_trigger trig = { .chan = SENSOR_CHAN_ACCEL_XYZ };

		trig.type = SENSOR_TRIG_THRESHOLD;
		if (sensor_trigger_set(dev, &trig, adxl362_trigger_handler)) {
			printk("Trigger set error\n");
			return;
		}
	}
}

void cloud_event_handler(const struct cloud_backend *const backend,
			 const struct cloud_event *const evt,
			 void *user_data)
{
	ARG_UNUSED(user_data);

	int err;

	switch (evt->type) {
	case CLOUD_EVT_CONNECTED:
		printk("CLOUD_EVT_CONNECTED\n");
		break;
	case CLOUD_EVT_READY:
		printk("CLOUD_EVT_READY\n");
		break;
	case CLOUD_EVT_DISCONNECTED:
		printk("CLOUD_EVT_DISCONNECTED\n");
		break;
	case CLOUD_EVT_ERROR:
		printk("CLOUD_EVT_ERROR\n");
		break;
	case CLOUD_EVT_DATA_SENT:
		printk("CLOUD_EVT_DATA_SENT\n");
		break;
	case CLOUD_EVT_DATA_RECEIVED:
		printk("CLOUD_EVT_DATA_RECEIVED\n");
		if (evt->data.msg.len > 2) {
			err = decode_response(evt->data.msg.buf, &cloud_data);
			if (err != 0) {
				printk("Could not decode response %d", err);
			}
		}
		break;
	case CLOUD_EVT_PAIR_REQUEST:
		printk("CLOUD_EVT_PAIR_REQUEST\n");
		break;
	case CLOUD_EVT_PAIR_DONE:
		printk("CLOUD_EVT_PAIR_DONE\n");
		break;
	default:
		printk("Unknown cloud event type: %d\n", evt->type);
		break;
	}
}

// void movement_timeout_handler(struct k_work *work)
// {
//      if (check_mode() == false) {
//              k_work_submit(&cloud_report_work);
//      }
// }

// K_WORK_DEFINE(my_work, movement_timeout_handler);

// void movement_timer_handler(struct k_timer *dummy)
// {
//      k_work_submit(&my_work);
// }

// K_TIMER_DEFINE(mov_timer, movement_timer_handler, NULL);

// static void start_restart_mov_timer(void)
// {
//      k_timer_start(&mov_timer, K_SECONDS(cloud_data.movement_timeout),
//                    K_SECONDS(cloud_data.movement_timeout));
// }

static void lte_connect(void)
{
	int err;

	ui_led_set_pattern(UI_LTE_CONNECTING);

	err = lte_lc_init_connect_manager(connection_handler);
	if (err != 0) {
		printk("Error setting lte connection manager: %d\n", err);
	}

	printk("Searching for LTE connection... timeout in %d minutes\n",
	       CONFIG_LTE_CONN_TIMEOUT);

	if (!k_sem_take(&connect_sem, K_MINUTES(CONFIG_LTE_CONN_TIMEOUT))) {
		k_sem_give(&connect_sem);

		printk("LTE connected!\nFetching modem time...\n");

		k_sleep(5000);

		// need to add some sort of fallback here, incease time is not obtained
		// preferably in modem_data module

		err = modem_time_get();
		if (err != 0) {
			printk("Error fetching modem time: %d\n", err);
		}

		cloud_pair();
		cloud_ack_config_change();
		lte_lc_psm_req(true);
	} else {
		printk("LTE not connected within %d minutes, starting gps search...\n",
		       CONFIG_LTE_CONN_TIMEOUT);
		lte_lc_gps_mode();
	}
}

void main(void)
{
	int err;

	// work_init();

#if defined(CONFIG_USE_UI_MODULE)
	ui_init();
#endif

	printk("The cat tracker has started\n");

	cloud_backend = cloud_get_binding("BIFRAVST_CLOUD");
	__ASSERT(cloud_backend != NULL, "Bifravst Cloud backend not found");

	err = cloud_init(cloud_backend, cloud_event_handler);
	if (err) {
		printk("Cloud backend could not be initialized, error: %d\n ",
		       err);
		cloud_error_handler(err);
	}

	printk("Cloud backend initialized\n");

	adxl362_init();
	gps_control_init(gps_control_handler);
	lte_connect();

	while (true) {
		switch (state) {
		case CHECK_MODE:
			// start_restart_mov_timer();
			if (check_mode()) {
				ui_led_set_pattern(UI_LED_ACTIVE_MODE);
				active = true;
				k_sleep(CONFIG_MODE_INDICATION_TIME);
				state = ACTIVE_MODE;
			} else {
				ui_led_set_pattern(UI_LED_PASSIVE_MODE);
				active = false;
				k_sleep(CONFIG_MODE_INDICATION_TIME);
				state = PASSIVE_MODE;
			}
			break;

		case ACTIVE_MODE:
			printk("ACTIVE MODE\n");
			state = LTE_CHECK_CONNECTION;
			break;

		case PASSIVE_MODE:
			printk("PASSIVE MODE\n");
			ui_stop_leds();
			if (!k_sem_take(&accel_trig_sem, K_FOREVER)) {
			}
			state = LTE_CHECK_CONNECTION;
			break;

		case LTE_CHECK_CONNECTION:
			if (!k_sem_count_get(&connect_sem)) {
				lte_connect();
			}
			state = GPS_SEARCH;
			break;

		case GPS_SEARCH:
			gps_control_start();
			if (!k_sem_take(&gps_timeout_sem,
					K_SECONDS(cloud_data.gps_timeout))) {
				cloud_data.gps_found = true;
				cloud_process_cycle();
			} else {
				gps_control_stop();
				cloud_data.gps_found = false;
				cloud_process_cycle();
			}
			k_sleep(K_SECONDS(check_active_wait(active)));
			state = CHECK_MODE;
			break;

		default:
			printk("Unknown governing state\n");
			break;
		}
	}
}
