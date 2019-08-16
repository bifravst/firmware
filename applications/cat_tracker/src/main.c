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

#define NORMAL_OPERATION false
#define SYNCRONIZATION true
#define NO_GPS_FIX false
#define GPS_FIX true
#define INCLUDE_MOD_D true

static bool active;

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

enum error_type {
	ERROR_BSD_RECOVERABLE,
	ERROR_BSD_IRRECOVERABLE,
	ERROR_LTE_LC,
	ERROR_SYSTEM_FAULT
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
		break;
	case ERROR_BSD_IRRECOVERABLE:
		printk("Error of type ERROR_BSD_IRRECOVERABLE: %d\n", err_code);
		break;
	default:
		printk("Unknown error type: %d, code: %d\n", err_type,
		       err_code);
		break;
	}

	while (true) {
		k_cpu_idle();
	}
#endif /* CONFIG_DEBUG */
}

void k_sys_fatal_error_handler(unsigned int reason, const z_arch_esf_t *esf)
{
	ARG_UNUSED(esf);

	LOG_PANIC();
	z_fatal_print("Running main.c error handler");
	error_handler(ERROR_SYSTEM_FAULT, reason);
	CODE_UNREACHABLE;
}

static void lte_connect(void)
{
	int err;

	if (IS_ENABLED(CONFIG_LTE_AUTO_INIT_AND_CONNECT)) {
		/* Do nothing, modem is already turned on
		 * and connected.
		 */
	} else {
		int err;

		printk("LTE Link Connecting ...\n");
		lte_connecting_led_start();
		err = lte_lc_init_and_connect();
		__ASSERT(err == 0, "LTE link could not be established.");
		printk("LTE Link Connected!\n");
		lte_connecting_led_stop();
	}
	lte_lc_psm_req(true);

	k_sleep(5000);

	err = modem_time_get();
	if (err != 0) {
		printk("Error fetching modem time\n");
	}
}

static void cloud_publish(bool gps_fix, bool action, bool inc_modem_d)
{
	int err;

	attach_battery_data(request_battery_status(), get_current_time());

	set_gps_found(gps_fix);

	err = publish_data(action, inc_modem_d);
	if (err != 0) {
		printk("Error publishing data: %d", err);
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
					  sensor_value_to_double(&accel[2]),
					  get_current_time());

			printf("x: %.1f, y: %.1f, z: %.1f (m/s^2)\n",
			       sensor_value_to_double(&accel[0]),
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
		gps_sample_fetch(dev);
		gps_channel_get(dev, GPS_CHAN_PVT, &gps_data);
		set_current_time(gps_data);
		attach_gps_data(gps_data, get_current_time());
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
	cloud_publish(NO_GPS_FIX, NORMAL_OPERATION, INCLUDE_MOD_D);
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

void main(void)
{
	printk("The cat tracker has started\n");
	cloud_configuration_init();
	lte_connect();
	adxl362_init();
	gps_control_init(gps_control_handler);
	cloud_publish(NO_GPS_FIX, SYNCRONIZATION, INCLUDE_MOD_D);
	start_restart_mov_timer();

check_mode:
	if (check_mode()) {
		active = true;
		goto active;
	} else {
		active = false;
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
		cloud_publish(GPS_FIX, NORMAL_OPERATION, active);
	} else {
		gps_control_stop();
		cloud_publish(NO_GPS_FIX, NORMAL_OPERATION, active);
	}
	events[1].state = K_POLL_STATE_NOT_READY;
	events[0].state = K_POLL_STATE_NOT_READY;
	start_restart_mov_timer();
	k_sleep(K_SECONDS(check_active_wait(active)));

	goto check_mode;
}
