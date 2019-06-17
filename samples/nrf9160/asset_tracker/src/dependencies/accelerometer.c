#include <accelerometer.h>

// /*
//  * Copyright (c) 2019 Brett Witherspoon
//  *
//  * SPDX-License-Identifier: Apache-2.0
//  */

// K_SEM_DEFINE(sem, 0, 1);

// void trigger_handler(struct device *dev, struct sensor_trigger *trig)
// {
// 	switch (trig->type) {
// 	case SENSOR_TRIG_DATA_READY:
// 		if (sensor_sample_fetch(dev) < 0) {
// 			printk("Sample fetch error\n");
// 			return;
// 		}
// 		k_sem_give(&sem);
// 		break;
// 	case SENSOR_TRIG_THRESHOLD:
// 		printk("Threshold trigger\n");
// 		break;
// 	default:
// 		printk("Unknown trigger\n");
// 	}
// }

// void accel_enable(void)
// {
// 	struct sensor_value accel[3];

// 	struct device *dev = device_get_binding(DT_ADI_ADXL362_0_LABEL); //does not find the label, DT_ADI_ADXL362_0_LABEL
// 	if (dev == NULL) {
// 		printk("Device get binding device\n");
// 		return;
// 	}

// 	if (IS_ENABLED(CONFIG_ADXL362_TRIGGER)) {
// 		struct sensor_trigger trig = { .chan = SENSOR_CHAN_ACCEL_XYZ };

// 		trig.type = SENSOR_TRIG_THRESHOLD;
// 		if (sensor_trigger_set(dev, &trig, trigger_handler)) {
// 			printk("Trigger set error\n");
// 			return;
// 		}

// 		trig.type = SENSOR_TRIG_DATA_READY;
// 		if (sensor_trigger_set(dev, &trig, trigger_handler)) {
// 			printk("Trigger set error\n");
// 		}
// 	}

// 	while (true) {
// 		if (IS_ENABLED(CONFIG_ADXL362_TRIGGER)) {
// 			k_sem_take(&sem, K_FOREVER);
// 		} else {
// 			k_sleep(1000);
// 			if (sensor_sample_fetch(dev) < 0) {
// 				printk("Sample fetch error\n");
// 				return;
// 			}
// 		}

// 		if (sensor_channel_get(dev, SENSOR_CHAN_ACCEL_X, &accel[0]) < 0) {
// 			printk("Channel get error\n");
// 			return;
// 		}

// 		if (sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Y, &accel[1]) < 0) {
// 			printk("Channel get error\n");
// 			return;
// 		}

// 		if (sensor_channel_get(dev, SENSOR_CHAN_ACCEL_Z, &accel[2]) < 0) {
// 			printk("Channel get error\n");
// 			return;
// 		}

// 		printk("x: %.1f, y: %.1f, z: %.1f (m/s^2)\n",
// 		       sensor_value_to_double(&accel[0]),
// 		       sensor_value_to_double(&accel[1]),
// 		       sensor_value_to_double(&accel[2]));
// 	}
// }