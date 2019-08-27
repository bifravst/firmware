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
#define LTE_NOT_CONNECTED 0x1343

#define LED_PULSE_CYCLE 1000
#define PROLONGED_DELAY 3500
#define MID_DELAY 2000

struct k_poll_signal signal_led;
static int state;

struct gpio_pin {
	const char *const port;
	const u8_t number;
};

static const struct gpio_pin led_pins[] = {
#ifdef DT_ALIAS_LED0_GPIOS_PIN
	{ DT_ALIAS_LED0_GPIOS_CONTROLLER, DT_ALIAS_LED0_GPIOS_PIN },
#endif
#ifdef DT_ALIAS_LED1_GPIOS_PIN
	{ DT_ALIAS_LED1_GPIOS_CONTROLLER, DT_ALIAS_LED1_GPIOS_PIN },
#endif
#ifdef DT_ALIAS_LED2_GPIOS_PIN
	{ DT_ALIAS_LED2_GPIOS_CONTROLLER, DT_ALIAS_LED2_GPIOS_PIN },
#endif
};

static struct device *led_devs[ARRAY_SIZE(led_pins)];

void led_FSM(void)
{
	int err, cnt = 0;

	k_poll_signal_init(&signal_led);

	struct k_poll_event events[1] = { K_POLL_EVENT_INITIALIZER(
		K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &signal_led) };

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

				k_poll(events, 1, MID_DELAY);

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
			gpio_pin_write(led_devs[LED1], led_pins[LED1].number,
				       false);
			gpio_pin_write(led_devs[LED3], led_pins[LED3].number,
				       true);
			k_sleep(2000);
			gpio_pin_write(led_devs[LED3], led_pins[LED3].number,
				       false);
			k_poll(events, 1, K_FOREVER);
			break;

		case LTE_CONNECTING:

			while (1) {
				gpio_pin_write(led_devs[LED2],
					       led_pins[LED2].number, cnt % 2);
				cnt++;

				k_poll(events, 1, LED_PULSE_CYCLE);

				if (state == LTE_CONNECTING) {
					break;
				}
			}

			break;
		case LTE_CONNECTED:

			gpio_pin_write(led_devs[LED2], led_pins[LED2].number,
				       true);
			gpio_pin_write(led_devs[LED3], led_pins[LED3].number,
				       true);
			gpio_pin_write(led_devs[LED1], led_pins[LED1].number,
				       true);
			k_sleep(PROLONGED_DELAY);
			gpio_pin_write(led_devs[LED2], led_pins[LED2].number,
				       false);
			gpio_pin_write(led_devs[LED3], led_pins[LED3].number,
				       false);
			gpio_pin_write(led_devs[LED1], led_pins[LED1].number,
				       false);
			k_poll(events, 1, K_FOREVER);
			break;

		case LTE_NOT_CONNECTED:
			gpio_pin_write(led_devs[LED2], led_pins[LED2].number,
				       false);

			k_poll(events, 1, K_FOREVER);
			break;

		case PUBLISH_DATA:

			while (1) {
				gpio_pin_write(led_devs[LED3],
					       led_pins[LED3].number, true);

				k_sleep(250);

				gpio_pin_write(led_devs[LED3],
					       led_pins[LED3].number, false);

				k_sleep(250);

				gpio_pin_write(led_devs[LED3],
					       led_pins[LED3].number, true);

				k_sleep(300);

				gpio_pin_write(led_devs[LED3],
					       led_pins[LED3].number, false);

				k_poll(events, 1, PROLONGED_DELAY);

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
			printk("Unknown state\n");
			break;
		}
	}
}

void led_work(void)
{
	led_FSM();
}

K_THREAD_DEFINE(led_work_id, STACKSIZE, led_work, NULL, NULL, NULL, PRIORITY, 0,
		K_NO_WAIT);

void set_led_state(enum led_events led_event)
{
	switch (led_event) {
	case GPS_SEARCH_E:
		k_poll_signal_raise(&signal_led, GPS_SEARCH);
		break;

	case GPS_SEARCH_STOP_E:
		k_poll_signal_raise(&signal_led, GPS_SEARCH_STOP);
		break;

	case GPS_SEARCH_STOP_FIX_E:
		k_poll_signal_raise(&signal_led, GPS_SEARCH_STOP_FIX);
		break;

	case LTE_CONNECTING_E:
		k_poll_signal_raise(&signal_led, LTE_CONNECTING);
		break;

	case LTE_CONNECTED_E:
		k_poll_signal_raise(&signal_led, LTE_CONNECTED);
		break;

	case LTE_NOT_CONNECTED_E:
		k_poll_signal_raise(&signal_led, LTE_NOT_CONNECTED);
		break;

	case PUBLISH_DATA_E:
		k_poll_signal_raise(&signal_led, PUBLISH_DATA);
		break;

	case PUBLISH_DATA_STOP_E:
		k_poll_signal_raise(&signal_led, PUBLISH_DATA_STOP);
		break;

	default:
		break;
	}
}
