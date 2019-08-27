#ifndef LEDS_H__
#define LEDS_H__

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum led_events {
    GPS_SEARCH_E,
    GPS_SEARCH_STOP_E,
    GPS_SEARCH_STOP_FIX_E,
    LTE_CONNECTING_E,
    LTE_CONNECTED_E,
    LTE_NOT_CONNECTED_E,
    PUBLISH_DATA_E,
    PUBLISH_DATA_STOP_E
};

void set_led_state(enum led_events);

#ifdef __cplusplus
}
#endif
#endif
