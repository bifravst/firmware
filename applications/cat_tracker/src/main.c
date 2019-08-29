#include <zephyr.h>
#include <stdio.h>
#include <uart.h>
#include <string.h>
#include <logging/log.h>
#include <logging/log_ctrl.h>
#include <misc/reboot.h>
#include <mqtt_behaviour.h>
#include <modem_data.h>
#include <device.h>
#include <sensor.h>
#include <gps_controller.h>
#include <mqtt_codec.h>
#include <leds.h>
#include <net/cloud.h>

#define NORMAL_OPERATION false
#define SYNCRONIZATION true
#define NO_GPS_FIX false
#define GPS_FIX true
#define INCLUDE_MOD_D true

#define AT_CMD_SIZE(x) (sizeof(x) - 1)

#define LTE_CONN_TIMEOUT 10

static struct cloud_backend *cloud_backend;

static bool active;
static bool lte_connected = false;

static struct k_sem connect_sem;

K_SEM_DEFINE(gps_timeout_sem, 0, 1);
K_SEM_DEFINE(accel_trig_sem, 0, 1);

struct k_poll_event events[2] = {
	K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
					K_POLL_MODE_NOTIFY_ONLY,
					&gps_timeout_sem, 0),
	K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_SEM_AVAILABLE,
					K_POLL_MODE_NOTIFY_ONLY,
					&accel_trig_sem, 0)
};

static struct k_work cloud_report_work;

enum error_type {
	ERROR_BSD_RECOVERABLE,
	ERROR_BSD_IRRECOVERABLE,
	ERROR_SYSTEM_FAULT,
	ERROR_CLOUD
};

void error_handler(enum error_type err_type, int err_code)
{
	set_led_state(ERROR_E);
	k_sleep(K_SECONDS(30));

#if !defined(CONFIG_DEBUG) && defined(CONFIG_REBOOT)
	LOG_PANIC();
	sys_reboot(0);
#else

	

	switch (err_type) {
	case ERROR_BSD_RECOVERABLE:
		printk("Error of type ERROR_BSD_RECOVERABLE: %d\n", err_code);
		break;
	case ERROR_BSD_IRRECOVERABLE:
		printk("Error of type ERROR_BSD_IRRECOVERABLE: %d\n", err_code);
		break;

	case ERROR_SYSTEM_FAULT:
		printk("Error of type ERROR_SYSTEM_FAULT: %d\n", err_code);
		break;

	case ERROR_CLOUD:
		printk("Error of type ERROR_CLOUD: %d\n", err_code);
		break;

	default:
		printk("Unknown error type: %d, code: %d\n", err_type,
		       err_code);
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

static void cloud_report(bool gps_fix)
{
	int err;

	if (lte_connected) {
		set_led_state(PUBLISH_DATA_E);
		attach_battery_data(request_battery_status());

		set_gps_found(gps_fix);

		err = cloud_report_and_update(cloud_backend, CLOUD_REPORT);
		if (err) {
			printk("cloud_report_and_update failed: %d\n", err);
		}

		set_led_state(PUBLISH_DATA_STOP_E);

	} else {
		printk("Publish of data denied, LTE not connected\n");
	}
}

static void cloud_report_work_fn(struct k_work *work)
{
	cloud_report(NO_GPS_FIX);
}

static void work_init(void)
{
	k_work_init(&cloud_report_work, cloud_report_work_fn);
}

static void cloud_pair(bool gps_fix)
{
	int err;

	if (lte_connected) {
		set_led_state(PUBLISH_DATA_E);
		attach_battery_data(request_battery_status());

		set_gps_found(gps_fix);

		err = cloud_report_and_update(cloud_backend, CLOUD_PAIR);
		if (err) {
			printk("cloud_report_and_update failed: %d\n", err);
		}

		set_led_state(PUBLISH_DATA_STOP_E);

	} else {
		printk("Publish of data denied, LTE not connected\n");
	}
}

static const char status1[] = "+CEREG: 1";
static const char status2[] = "+CEREG:1";
static const char status3[] = "+CEREG: 5";
static const char status4[] = "+CEREG:5";

void connection_handler(char *response)
{
	printk("Incoming network registration status: %s", response);

	if (!memcmp(status1, response, AT_CMD_SIZE(status1)) ||
	    !memcmp(status2, response, AT_CMD_SIZE(status2)) ||
	    !memcmp(status3, response, AT_CMD_SIZE(status3)) ||
	    !memcmp(status4, response, AT_CMD_SIZE(status4))) {
		if (!lte_connected) {
			printk("LTE connected\n");

			lte_connected = true;
			k_sem_give(&connect_sem);
		}

	} else {
		lte_connected = false;
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

		if ((abs(sensor_value_to_double(&accel[0])) >
		     check_accel_thres()) ||
		    (abs(sensor_value_to_double(&accel[1])) >
		     check_accel_thres()) ||
		    (abs(sensor_value_to_double(&accel[2])) >
		     check_accel_thres())) {
			attach_accel_data(sensor_value_to_double(&accel[0]),
					  sensor_value_to_double(&accel[1]),
					  sensor_value_to_double(&accel[2]));
			k_sem_give(events[1].sem);
		}

		break;
	default:
		printk("Unknown trigger\n");
	}
}

static void gps_control_handler(struct device *dev, struct gps_trigger *trigger)
{
	static struct gps_data gps_data;

	switch (trigger->type) {
	case GPS_TRIG_FIX:
		printk("gps control handler triggered!\n");
		gps_control_on_trigger();
		gps_channel_get(dev, GPS_CHAN_PVT, &gps_data);
		set_current_time(gps_data);
		attach_gps_data(gps_data);
		k_sem_give(events[0].sem);
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

void movement_timeout_handler(struct k_work *work)
{
	k_work_submit(&cloud_report_work);
}

K_WORK_DEFINE(my_work, movement_timeout_handler);

void movement_timer_handler(struct k_timer *dummy)
{
	k_work_submit(&my_work);
}

K_TIMER_DEFINE(mov_timer, movement_timer_handler, NULL);

static void start_restart_mov_timer(void)
{
	k_timer_start(&mov_timer, K_SECONDS(check_mov_timeout()),
		      K_SECONDS(check_mov_timeout()));
}

static void lte_connect()
{
	int err;

	set_led_state(LTE_CONNECTING_E);

	k_sem_init(&connect_sem, 0, 1);
	err = lte_lc_init_connect_manager(connection_handler);
	if (err != 0) {
		printk("Error setting lte_connect manager: %d\n", err);
	}

	printk("Searching for LTE connection... timeout in %d minutes\n",
	       LTE_CONN_TIMEOUT);

	if (k_sem_take(&connect_sem, K_MINUTES(LTE_CONN_TIMEOUT)) == 0) {
		set_led_state(LTE_CONNECTED_E);

		k_sleep(10000);

		err = modem_time_get();
		if (err != 0) {
			printk("Error fetching modem time\n");
		}

		cloud_pair(NO_GPS_FIX);
		lte_lc_psm_req(true);
	} else {
		printk("LTE not connected within %d minutes, starting gps search\n",
		       LTE_CONN_TIMEOUT);
		set_led_state(LTE_NOT_CONNECTED_E);
		lte_lc_gps_mode();
	}
}

void main(void)
{
	int err;

	work_init();

	printk("The cat tracker has started\n");

	cloud_backend = cloud_get_binding("CAT_CLOUD");
	__ASSERT(cloud_backend != NULL, "Cat Cloud backend not found");

	err = cloud_init_config(cloud_backend);
	if (err) {
		printk("Cloud backend could not be initialized, error: %d\n",
		       err);
		cloud_error_handler(err);
	}

	adxl362_init();
	lte_connect();
	gps_control_init(gps_control_handler);

check_mode:
	start_restart_mov_timer();
	if (check_mode()) {
		set_led_state(ACTIVE_MODE_E);
		active = true;
		k_sleep(15000);
		goto active;
	} else {
		set_led_state(PASSIVE_MODE_E);
		active = false;
		k_sleep(15000);
		goto passive;
	}

active:
	printk("ACTIVE MODE\n");
	goto gps_search;

passive:
	printk("PASSIVE MODE\n");
	k_poll(events, 2, K_FOREVER);
	if (events[1].state == K_POLL_STATE_SEM_AVAILABLE) {
		k_sem_take(events[1].sem, 0);
	}

	goto gps_search;

gps_search:
	gps_control_start();
	k_poll(events, 1, K_SECONDS(check_gps_timeout()));
	if (events[0].state == K_POLL_STATE_SEM_AVAILABLE) {
		k_sem_take(events[0].sem, 0);
		cloud_report(GPS_FIX);
	} else {
		gps_control_stop();
		cloud_report(NO_GPS_FIX);
	}
	events[1].state = K_POLL_STATE_NOT_READY;
	events[0].state = K_POLL_STATE_NOT_READY;
	k_sleep(K_SECONDS(check_active_wait(active)));

	if (!lte_connected) {
		lte_connect();
	}

	goto check_mode;
}
