#include <zephyr.h>
#include <device.h>
#include <string.h>
#include <gpio.h>
#include <leds.h>

#define STACKSIZE 1024
#define PRIORITY 7

#define LED1 0
#define LED2 1
#define LED3 2

#define GPS_SEARCH_STOP_FIX 0x1336
#define GPS_SEARCH 0x1337
#define GPS_SEARCH_STOP 0x1338
#define LTE_CONNECTING 0x1339
#define LTE_CONNECTED 0x1340
#define PUBLISH_DATA 0x1341
#define PUBLISH_DATA_STOP 0x1342

struct k_poll_signal signal_led;
static int state;

struct gpio_pin {
	const char *const port;
	const u8_t number;
};

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
};

static struct device *led_devs[ARRAY_SIZE(led_pins)];

void blink(u32_t sleep_ms)
{
	int err, cnt = 0;

	k_poll_signal_init(&signal_led);

	struct k_poll_event events[1] = { K_POLL_EVENT_INITIALIZER(
		K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &signal_led) };

	/*LED INIT */
	for (size_t i = 0; i < ARRAY_SIZE(led_pins); i++) {
		led_devs[i] = device_get_binding(led_pins[i].port);
		if (!led_devs[i]) {
		}

		err = gpio_pin_configure(led_devs[i], led_pins[i].number,
					 GPIO_DIR_OUT);
		if (err) {
		}

		err = gpio_pin_write(led_devs[i], led_pins[i].number, false);
		if (err != 0) {
			printk("Unsuccessful setting initial state LEDs: %d\n",
			       err);
		}
	}

	k_poll(events, 1, K_FOREVER);

	while (1) {
		state = events[0].signal->result;
		events[0].signal->signaled = 0;
		events[0].state = K_POLL_STATE_NOT_READY;

		switch (state) {
		case GPS_SEARCH:

			while (1) {
				gpio_pin_write(led_devs[LED1],
					       led_pins[LED1].number, cnt % 2);
				cnt++;

				k_poll(events, 1, sleep_ms);

				if (state == GPS_SEARCH) {
					break;
				}
			}

			break;

		case GPS_SEARCH_STOP:

			gpio_pin_write(led_devs[LED1], led_pins[LED1].number,
				       false);

			k_poll(events, 1, K_FOREVER);
			break;

		case GPS_SEARCH_STOP_FIX:
			gpio_pin_write(led_devs[LED3], led_pins[LED3].number,
				       true);
			k_sleep(2000);
			gpio_pin_write(led_devs[LED3], led_pins[LED3].number,
				       false);
			k_poll(events, 1, K_FOREVER);

		case LTE_CONNECTING:

			while (1) {
				gpio_pin_write(led_devs[LED2],
					       led_pins[LED2].number, cnt % 2);
				cnt++;

				k_poll(events, 1, sleep_ms);

				if (state == LTE_CONNECTING) {
					break;
				}
			}

			break;
		case LTE_CONNECTED:

			gpio_pin_write(led_devs[LED2], led_pins[LED2].number,
				       true);
			k_sleep(2000);
			gpio_pin_write(led_devs[LED2], led_pins[LED2].number,
				       false);

			k_poll(events, 1, K_FOREVER);
			break;

		case PUBLISH_DATA:

			while (1) {
				gpio_pin_write(led_devs[LED3],
					       led_pins[LED3].number, true);

				k_poll(events, 1, sleep_ms);

				k_poll_signal_raise(&signal_led,
						    PUBLISH_DATA_STOP);

				if (state == PUBLISH_DATA) {
					break;
				}
			}
			break;

		case PUBLISH_DATA_STOP:

			gpio_pin_write(led_devs[LED3], led_pins[LED3].number,
				       false);

			k_poll(events, 1, K_FOREVER);

			break;

		default:
			printk("Unknown state \n");
			break;
		}
	}
}

void blink1(void)
{
	blink(1000);
}

K_THREAD_DEFINE(blink1_id, STACKSIZE, blink1, NULL, NULL, NULL, PRIORITY, 0,
		K_NO_WAIT);

void gps_search_led_start()
{
	k_poll_signal_raise(&signal_led, GPS_SEARCH);
}

void gps_search_led_stop()
{
	k_poll_signal_raise(&signal_led, GPS_SEARCH_STOP);
}

void gps_search_led_stop_fix()
{
	k_poll_signal_raise(&signal_led, GPS_SEARCH_STOP_FIX);
}

void lte_connecting_led_start()
{
	k_poll_signal_raise(&signal_led, LTE_CONNECTING);
}

void lte_connecting_led_stop()
{
	k_poll_signal_raise(&signal_led, LTE_CONNECTED);
}

void publish_data_led()
{
	k_poll_signal_raise(&signal_led, PUBLISH_DATA);
}