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

#define NORMAL_OPERATION false
#define SYNCRONIZATION true
#define GPS_FIX true
#define NO_GPS_FIX false

#define MODE_INDICATION_TIME CONFIG_MODE_INDICATION_TIME
#define LTE_CONN_TIMEOUT CONFIG_LTE_CONN_TIMEOUT
#define MODEM_TIME_RETRIES CONFIG_MODEM_TIME_RETRIES

#define AT_CMD_SIZE(x) (sizeof(x) - 1)

enum governing_states {
	LTE_CHECK_CONNECTION,
	CHECK_MODE,
	ACTIVE_MODE,
	PASSIVE_MODE,
	GPS_SEARCH,
};

enum governing_states state = CHECK_MODE;

static struct cloud_backend *cloud_backend;

static bool active;

K_SEM_DEFINE(gps_timeout_sem, 0, 1);
K_SEM_DEFINE(accel_trig_sem, 0, 1);
K_SEM_DEFINE(connect_sem, 0, 1);

static struct k_work cloud_report_work;
static struct k_work cloud_report_fix_work;
static struct k_work cloud_pair_work;
static struct k_work gps_control_start_work;
static struct k_work gps_control_stop_work;

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

static void cloud_report(bool gps_fix)
{
	int err;

	ui_led_set_pattern(UI_CLOUD_PUBLISHING);
	attach_battery_data(request_battery_status());

	set_gps_found(true);

	err = cloud_report_and_update(cloud_backend, CLOUD_REPORT);
	if (err) {
		printk("cloud_report_and_update failed: %d\n", err);
	}
}

static void cloud_pair(void)
{
	int err;

	ui_led_set_pattern(UI_CLOUD_PUBLISHING);
	attach_battery_data(request_battery_status());

	set_gps_found(false);

	err = cloud_report_and_update(cloud_backend, CLOUD_PAIR);
	if (err) {
		printk("cloud_report_and_update failed: %d\n", err);
	}
}

static void cloud_pair_work_fn(struct k_work *work)
{
	cloud_pair();
}

static void cloud_report_work_fn(struct k_work *work)
{
	cloud_report(false);
}

static void cloud_report_fix_work_fn(struct k_work *work)
{
	cloud_report(true);
}

static void gps_control_start_work_fn(struct k_work *work)
{
	gps_control_start();
}

static void gps_control_stop_work_fn(struct k_work *work)
{
	gps_control_stop();
}

static void work_init(void)
{
	k_work_init(&cloud_report_work, cloud_report_work_fn);
	k_work_init(&cloud_report_fix_work, cloud_report_fix_work_fn);
	k_work_init(&cloud_pair_work, cloud_pair_work_fn);
	k_work_init(&gps_control_start_work, gps_control_start_work_fn);
	k_work_init(&gps_control_stop_work, gps_control_stop_work_fn);
}

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

		if ((abs(sensor_value_to_double(&accel[0])) >
		     check_accel_thres()) ||
		    (abs(sensor_value_to_double(&accel[1])) >
		     check_accel_thres()) ||
		    (abs(sensor_value_to_double(&accel[2])) >
		     check_accel_thres())) {
			attach_accel_data(sensor_value_to_double(&accel[0]),
					  sensor_value_to_double(&accel[1]),
					  sensor_value_to_double(&accel[2]));
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

	switch (trigger->type) {
	case GPS_TRIG_FIX:
		printk("gps control handler triggered!\n");
		gps_control_on_trigger();
		gps_channel_get(dev, GPS_CHAN_PVT, &gps_data);
		set_current_time(gps_data);
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

void movement_timeout_handler(struct k_work *work)
{
	if (check_mode() == false) {
		k_work_submit(&cloud_report_work);
	}
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

	modem_time_fetch:

		err = modem_time_get();
		if (err != 0) {
			printk("Modem giving old network time, trying again in %d seconds\n",
			       CONFIG_MODEM_TIME_RETRIES);
			k_sleep(K_SECONDS(CONFIG_MODEM_TIME_RETRIES));
			goto modem_time_fetch;
		}

		cloud_pair();
		lte_lc_psm_req(true);
	} else {
		printk("LTE not connected within %d minutes, starting gps search\n",
		       CONFIG_LTE_CONN_TIMEOUT);
		lte_lc_gps_mode();
	}
}

void main(void)
{
	int err;

	work_init();

#if defined(CONFIG_USE_UI_MODULE)
	ui_init();
#endif

	printk("The cat tracker has started\n");

	cloud_backend = cloud_get_binding("BIFRAVST_CLOUD");
	__ASSERT(cloud_backend != NULL, "Cat Cloud backend not found");

	err = cloud_init_config(cloud_backend);
	if (err) {
		printk("Cloud backend could not be initialized, error: %d\n",
		       err);
		cloud_error_handler(err);
	}

	adxl362_init();
	gps_control_init(gps_control_handler);
	lte_connect();

	while (true) {
		switch (state) {
		case CHECK_MODE:
			start_restart_mov_timer();
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
			ui_led_set_pattern(UI_LED_GPS_SEARCHING);
			gps_control_start();
			if (!k_sem_take(&gps_timeout_sem,
					K_SECONDS(check_gps_timeout()))) {
				cloud_report(true);
			} else {
				gps_control_stop();
				cloud_report(false);
			}
			ui_stop_leds();
			k_sleep(K_SECONDS(check_active_wait(active)));
			state = CHECK_MODE;
			break;

		default:
			printk("Unknown governing state\n");
			break;
		}
	}
}
