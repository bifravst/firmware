#ifndef ACCELEROMETER_H__
#define ACCELEROMETER_H__

#include <zephyr.h>
#include <stdio.h>
#include <device.h>
#include <sensor.h>

void adxl362_trigger_handler(struct device *dev, struct sensor_trigger *trig);

void adxl362_init(void);

#endif