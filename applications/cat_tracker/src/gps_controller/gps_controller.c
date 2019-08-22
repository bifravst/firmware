#include <zephyr.h>
#include <misc/util.h>
#include <misc/reboot.h>
#include <gps.h>
#include <lte_lc.h>
#include <leds.h>

#include "gps_controller.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(gps_control, CONFIG_GPS_CONTROL_LOG_LEVEL);

#define GPS_THREAD_DEADLINE 60

/* Structure to hold GPS work information */
static struct {
	enum { GPS_WORK_START, GPS_WORK_STOP } type;
	struct k_work work;
	struct device *dev;
	u32_t failed_fix_attempts;
	u32_t fix_count;
} gps_work;

K_SEM_DEFINE(gps_stop_sem, 0, 1);

struct k_poll_event gps_events[1] = { K_POLL_EVENT_STATIC_INITIALIZER(
	K_POLL_TYPE_SEM_AVAILABLE, K_POLL_MODE_NOTIFY_ONLY, &gps_stop_sem, 0) };

static void gps_work_handler(struct k_work *work)
{
	int err;

	if (gps_work.type == GPS_WORK_START) {
		err = gps_start(gps_work.dev);
		if (err) {
			LOG_DBG("GPS could not be started, error: %d\n", err);
			return;
		}

		LOG_DBG("GPS started successfully.\nSearching for satellites ");
		LOG_DBG("to get position fix. This may take several minutes.\n");

		return;
	} else if (gps_work.type == GPS_WORK_STOP) {
		err = gps_stop(gps_work.dev);
		if (err) {
			LOG_DBG("GPS could not be stopped, error: %d\n", err);
			return;
		}

		k_sem_give(gps_events[0].sem);

		return;
	}
}

void gps_control_stop(void)
{
	lte_lc_psm_req(false);

	gps_work.type = GPS_WORK_STOP;
	k_work_submit(&gps_work.work);
	gps_search_led_stop();
	k_poll(gps_events, 1, K_SECONDS(GPS_THREAD_DEADLINE));

	if (gps_events[0].state == K_POLL_STATE_SEM_AVAILABLE) {
		k_sem_take(gps_events[0].sem, 0);
	} else {
		sys_reboot(0);
		/*Should be forwarded to an error handler */
	}
	gps_events[0].state = K_POLL_STATE_NOT_READY;
}

void gps_control_start(void)
{
	lte_lc_psm_req(true);

	gps_work.type = GPS_WORK_START;
	k_work_submit(&gps_work.work);
	gps_search_led_start();
}

void gps_control_on_trigger(void)
{
	gps_search_led_stop_fix();
	gps_control_stop();

	if (++gps_work.fix_count == CONFIG_GPS_CONTROL_FIX_COUNT) {
		gps_work.fix_count = 0;
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
		LOG_DBG("Could not get %s device\n", CONFIG_GPS_DEV_NAME);
		return -ENODEV;
	}

	err = gps_trigger_set(gps_dev, &gps_trig, handler);
	if (err) {
		LOG_DBG("Could not set trigger, error code: %d\n", err);
		return err;
	}

	k_work_init(&gps_work.work, gps_work_handler);

	gps_work.dev = gps_dev;
	gps_work.type = GPS_WORK_STOP;

	LOG_DBG("GPS initialized\n");

	return 0;
}
