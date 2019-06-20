#ifndef ACCELEROMETER_H__
#define ACCELEROMETER_H__

#include <zephyr.h>
#include <stdio.h>
#include <device.h>
#include <sensor.h>

void trigger_handler(struct device *dev, struct sensor_trigger *trig);

void accel_enable(void);

#endif