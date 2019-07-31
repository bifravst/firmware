#ifndef LEDS_H__
#define LEDS_H__

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void leds_init(void);

void led_notif_lte(bool connected);

void led_notif_gps_search(bool searching);

void led_notif_publish(void);

#ifdef __cplusplus
}
#endif

#endif