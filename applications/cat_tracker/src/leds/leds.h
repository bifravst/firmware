#ifndef LEDS_H__
#define LEDS_H__

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void gps_search_led_start(void);

void gps_search_led_stop(void);

void gps_search_led_stop_fix(void);

void lte_connecting_led_start(void);

void lte_connecting_led_stop(void);

void publish_data_led(void);

#ifdef __cplusplus
}
#endif
#endif
