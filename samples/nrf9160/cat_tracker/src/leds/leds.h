#ifndef LEDS_H__
#define LEDS_H__

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void gps_search_led_start();

void gps_search_led_stop();

void gps_search_led_stop_fix();

void lte_connecting_led_start();

void lte_connecting_led_stop();

void publish_data_led();

#ifdef __cplusplus
}
#endif

#endif