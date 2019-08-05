#include <zephyr.h>
#include <device.h>
#include <drivers/gpio.h>
#include <sys/printk.h>
#include <sys/__assert.h>
#include <string.h>
#include <leds.h>

#define STACKSIZE 1024
#define PRIORITY 7

static struct device *led_devs[ARRAY_SIZE(led_pins)];

static const struct gpio_pin led_pins[] = {
#ifdef LED0_GPIO_PIN
	{
		LED0_GPIO_CONTROLLER,
		LED0_GPIO_PIN,
	},
#endif
#ifdef LED1_GPIO_PIN
	{
		LED1_GPIO_CONTROLLER,
		LED1_GPIO_PIN,
	},
#endif
#ifdef LED2_GPIO_PIN
	{
		LED2_GPIO_CONTROLLER,
		LED2_GPIO_PIN,
	},
#endif
#ifdef LED3_GPIO_PIN
	{
		LED3_GPIO_CONTROLLER,
		LED3_GPIO_PIN,
	},
#endif
};

void blink(u32_t sleep_ms, u32_t led1, u32_t led2)
{
	int cnt = 0;

	led_devs[led1] = device_get_binding(led_pins[led1].port);
	if (!led_devs[led1]) {
		LOG_ERR("Cannot bind gpio device");
		return -ENODEV;
	}

	led_devs[led2] = device_get_binding(led_pins[led2].port);
	if (!led_devs[led2]) {
		LOG_ERR("Cannot bind gpio device");
		return -ENODEV;
	}

	err = gpio_pin_configure(led_devs[led1], led_pins[led1].number,
				 GPIO_DIR_OUT);
	if (err) {
		LOG_ERR("Cannot configure LED gpio");
		return err;
	}

	err = gpio_pin_configure(led_devs[led2], led_pins[led2].number,
				 GPIO_DIR_OUT);
	if (err) {
		LOG_ERR("Cannot configure LED gpio");
		return err;
	}

	//	err = gpio_pin_write(led_devs[led1], led_pins[led1].number,
	//			IS_ENABLED(CONFIG_DK_LIBRARY_INVERT_LEDS) ? !val : val);
	//	if (err) {
	//		LOG_ERR("Cannot write LED gpio");
	//	}

	while (1) {
		gpio_pin_write(led_devs[led1], led_pins[led1].number, cnt % 2);
		gpio_pin_write(led_devs[led1], led_pins[led1].number,
			       (cnt + 1) % 2);
		k_sleep(sleep_ms);
		cnt++
	}
}

void blink1(void)
{
	blink(500, LED1, LED2);
}

K_THREAD_DEFINE(blink1_id, STACKSIZE, blink1, NULL, NULL, NULL, PRIORITY, 0,
		K_FOREVER);

void gps_search_led_start()
{
	k_thread_start(blink1_id);
}

void gps_search_led_stop()
{
	k_thread_abort(blink1_id);
}
