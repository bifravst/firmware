#include <accelerometer.h>

// extern bool real_time_tracking;

// void adxl362_trigger_handler(struct device *dev,
// 				    struct sensor_trigger *trig)
// {
// 	switch (trig->type) {
// 	case SENSOR_TRIG_THRESHOLD:
// 		printk("The cat has awoken, send cat data to the broker\n");
// 		break;
// 	default:
// 		printk("Unknown trigger\n");
// 	}
// }

// void adxl362_init(void)
// {
// 	struct device *dev = device_get_binding(DT_INST_0_ADI_ADXL362_LABEL);
// 	if (dev == NULL) {
// 		printk("Device get binding device\n");
// 		return;
// 	}

// 	if (IS_ENABLED(CONFIG_ADXL362_TRIGGER)) {
// 		struct sensor_trigger trig = { .chan = SENSOR_CHAN_ACCEL_XYZ };

// 		trig.type = SENSOR_TRIG_THRESHOLD;
// 		if (sensor_trigger_set(dev, &trig, adxl362_trigger_handler)) {
// 			printk("Trigger set error\n");
// 			return;
// 		}
// 	}
// }