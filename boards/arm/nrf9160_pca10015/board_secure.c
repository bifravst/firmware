/*
 * Copyright (c) 2019 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <init.h>
#include <drivers/adp536x.h>
#include <device.h>
#include <gpio.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(board_secure, CONFIG_BOARD_LOG_LEVEL);

#define ADP536X_I2C_DEV_NAME DT_NORDIC_NRF_I2C_I2C_2_LABEL
#define LC_MAX_READ_LENGTH 128

#define HPWR 0
#define SUSP 1
#define FSEL 2
#define STBY 3

struct gpio_pin {
	const char *const port;
	const u8_t number;
};

static const struct gpio_pin ltc_pins[] = {
	{ "HPWR", 28 },
	{ "SUSP", 27 },
	{ "FSEL", 29 },
	{ "STBY", 26 },
};

static struct device *ltc_devs[ARRAY_SIZE(ltc_pins)];

static int power_mgmt_init(void)
{
	int err;

	err = adp536x_init(ADP536X_I2C_DEV_NAME);
	if (err) {
		LOG_ERR("ADP536X failed to initialize, error: %d\n", err);
		return err;
	}

	err = adp536x_buck_1v8_set();
	if (err) {
		LOG_ERR("Could not set buck to 1.8 V, error: %d\n", err);
		return err;
	}

	err = adp536x_buckbst_3v3_set();
	if (err) {
		LOG_ERR("Could not set buck/boost to 3.3 V, error: %d\n", err);
		return err;
	}

	err = adp536x_buckbst_enable(true);
	if (err) {
		LOG_ERR("Could not enable buck/boost output, error: %d\n", err);
		return err;
	}

	/* Enables discharge resistor for buck regulator that brings the voltage
	 * on its output faster down when it's inactive. Needed because some
	 * components require to boot up from ~0V.
	 */
	err = adp536x_buck_discharge_set(true);
	if (err) {
		return err;
	}

	/* Sets the VBUS current limit to 500 mA. */
	err = adp536x_vbus_current_set(ADP536X_VBUS_ILIM_500mA);
	if (err) {
		LOG_ERR("Could not set VBUS current limit, error: %d\n", err);
		return err;
	}

	/* Sets the charging current to 320 mA. */
	err = adp536x_charger_current_set(ADP536X_CHG_CURRENT_320mA);
	if (err) {
		LOG_ERR("Could not set charging current, error: %d\n", err);
		return err;
	}

	/* Sets the charge current protection threshold to 400 mA. */
	err = adp536x_oc_chg_current_set(ADP536X_OC_CHG_THRESHOLD_400mA);
	if (err) {
		LOG_ERR("Could not set charge current protection, error: %d\n",
			err);
		return err;
	}

	err = adp536x_charging_enable(true);
	if (err) {
		LOG_ERR("Could not enable charging: %d\n", err);
		return err;
	}

	return 0;
}

static int ltc3554_config(void)
{
	int err;

	for (size_t i = 0; i < ARRAY_SIZE(ltc_pins); i++) {
		ltc_devs[i] = device_get_binding(ltc_pins[i].port);

		err = gpio_pin_configure(ltc_devs[i], ltc_pins[i].number,
					 GPIO_DIR_OUT);
		if (err != 0) {
			LOG_ERR("Could not configure ltc pins");
			return err;
		}
	}

	err = gpio_pin_write(ltc_devs[HPWR], ltc_pins[HPWR].number, true);
	if (err != 0) {
		LOG_ERR("Unsuccessful setting initial state HPWR: %d\n", err);
		return err;
	}

	err = gpio_pin_write(ltc_devs[SUSP], ltc_pins[SUSP].number, true);
	if (err != 0) {
		LOG_ERR("Unsuccessful setting initial state SUSP: %d\n", err);
		return err;
	}

	err = gpio_pin_write(ltc_devs[FSEL], ltc_pins[FSEL].number, true);
	if (err != 0) {
		LOG_ERR("Unsuccessful setting initial state FSEL: %d\n", err);
		return err;
	}

	err = gpio_pin_write(ltc_devs[STBY], ltc_pins[STBY].number, false);
	if (err != 0) {
		LOG_ERR("Unsuccessful setting initial state STBY: %d\n", err);
		return err;
	}

	return 0;
}

static int pca10015_board_init(struct device *dev)
{
	int err;

	err = power_mgmt_init();
	if (err) {
		LOG_ERR("power_mgmt_init failed with error: %d", err);
		return err;
	}

	err = ltc3554_config();
	if (err != 0) {
		LOG_ERR("power_mgmt_init failed with error: %d", err);
		return err;
	}

	return 0;
}

SYS_INIT(pca10015_board_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
