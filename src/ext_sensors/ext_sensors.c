#include <zephyr.h>
#include <stdio.h>
#include <string.h>
#include <drivers/sensor.h>
#include "ext_sensors.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(ext_sensors, CONFIG_CAT_TRACKER_LOG_LEVEL);

struct env_sensor {
	enum sensor_channel channel;
	u8_t *dev_name;
	struct device *dev;
	struct k_spinlock lock;
};

static struct env_sensor temp_sensor = { .channel = SENSOR_CHAN_AMBIENT_TEMP,
					 .dev_name =
						 CONFIG_MULTISENSOR_DEV_NAME };

static struct env_sensor humid_sensor = { .channel = SENSOR_CHAN_HUMIDITY,
					  .dev_name =
						  CONFIG_MULTISENSOR_DEV_NAME };

static struct env_sensor accel_sensor = {
	.channel = SENSOR_CHAN_ACCEL_XYZ,
	.dev_name = CONFIG_ACCELEROMETER_DEV_NAME
};

static ext_sensors_evt_handler_t m_evt_handler;
static double accelerometer_threshold;

static void accelerometer_trigger_handler(struct device *dev,
					  struct sensor_trigger *trig)
{
	int err = 0;
	struct sensor_value data[ACCELEROMETER_CHANNELS];
	struct ext_sensor_evt evt;

	switch (trig->type) {
	case SENSOR_TRIG_THRESHOLD:

		if (sensor_sample_fetch(dev) < 0) {
			LOG_ERR("Sample fetch error");
			return;
		}

		err = sensor_channel_get(dev, SENSOR_CHAN_ACCEL_X, &data[0]);
		err += sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Y, &data[1]);
		err += sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Z, &data[2]);

		if (err) {
			LOG_ERR("sensor_channel_get, error: %d", err);
			return;
		}

		evt.value_array[0] = sensor_value_to_double(&data[0]);
		evt.value_array[1] = sensor_value_to_double(&data[1]);
		evt.value_array[2] = sensor_value_to_double(&data[2]);

		if ((abs(evt.value_array[0]) > accelerometer_threshold ||
		     (abs(evt.value_array[1]) > accelerometer_threshold) ||
		     (abs(evt.value_array[2]) > accelerometer_threshold))) {
			evt.type = EXT_SENSOR_EVT_ACCELEROMETER_TRIGGER;
			m_evt_handler(&evt);
		}

		break;
	default:
		LOG_ERR("Unknown trigger");
	}
}

int ext_sensors_init(ext_sensors_evt_handler_t handler)
{
	if (handler == NULL) {
		LOG_INF("External sensor handler NULL!");
		return -EINVAL;
	}

	temp_sensor.dev = device_get_binding(temp_sensor.dev_name);
	if (temp_sensor.dev == NULL) {
		LOG_ERR("Could not get device binding %s",
			temp_sensor.dev_name);
		return -ENODATA;
	}

	humid_sensor.dev = device_get_binding(humid_sensor.dev_name);
	if (humid_sensor.dev == NULL) {
		LOG_ERR("Could not get device binding %s",
			humid_sensor.dev_name);
		return -ENODATA;
	}

	accel_sensor.dev = device_get_binding(accel_sensor.dev_name);
	if (accel_sensor.dev == NULL) {
		LOG_ERR("Could not get device binding %s",
			accel_sensor.dev_name);
		return -ENODATA;
	}

	if (IS_ENABLED(CONFIG_ACCELEROMETER_TRIGGER)) {
		struct sensor_trigger trig = { .chan = SENSOR_CHAN_ACCEL_XYZ };

		trig.type = SENSOR_TRIG_THRESHOLD;
		if (sensor_trigger_set(accel_sensor.dev, &trig,
				       accelerometer_trigger_handler)) {
			LOG_ERR("Could not set trigger for device %s",
				accel_sensor.dev_name);
			return -ENODATA;
		}
	}

	m_evt_handler = handler;

	return 0;
}

int ext_sensors_temperature_get(struct cloud_data *cloud_data)
{
	int err;
	struct sensor_value data;

	err = sensor_sample_fetch_chan(temp_sensor.dev, SENSOR_CHAN_ALL);
	if (err) {
		LOG_ERR("Failed to fetch data from %s, error: %d",
			log_strdup(temp_sensor.dev_name), err);
		return -ENODATA;
	}

	err = sensor_channel_get(temp_sensor.dev, temp_sensor.channel, &data);
	if (err) {
		LOG_ERR("Failed to fetch data from %s, error: %d",
			log_strdup(temp_sensor.dev_name), err);
		return -ENODATA;
	}

	k_spinlock_key_t key = k_spin_lock(&(temp_sensor.lock));
	cloud_data->temp = sensor_value_to_double(&data);
	k_spin_unlock(&(temp_sensor.lock), key);

	cloud_data->env_ts = k_uptime_get();

	return 0;
}

int ext_sensors_humidity_get(struct cloud_data *cloud_data)
{
	int err;
	struct sensor_value data;

	err = sensor_sample_fetch_chan(humid_sensor.dev, SENSOR_CHAN_ALL);
	if (err) {
		LOG_ERR("Failed to fetch data from %s, error: %d",
			log_strdup(humid_sensor.dev_name), err);
		return -ENODATA;
	}

	err = sensor_channel_get(humid_sensor.dev, humid_sensor.channel, &data);
	if (err) {
		LOG_ERR("Failed to fetch data from %s, error: %d",
			log_strdup(humid_sensor.dev_name), err);
		return -ENODATA;
	}

	k_spinlock_key_t key = k_spin_lock(&(humid_sensor.lock));
	cloud_data->hum = sensor_value_to_double(&data);
	k_spin_unlock(&(humid_sensor.lock), key);

	cloud_data->env_ts = k_uptime_get();

	return 0;
}

void ext_sensors_accelerometer_threshold_set(struct cloud_data *cloud_data)
{
	if (cloud_data->acc_thres == 0) {
		accelerometer_threshold = 0;
	} else {
		accelerometer_threshold = cloud_data->acc_thres / 10;
	}
}
