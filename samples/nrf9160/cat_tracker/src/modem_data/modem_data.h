#ifndef MODEM_DATA_H__
#define MODEM_DATA_H__

#include <gps.h>
#include <time.h>
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

int request_battery_status();

int modem_time_get();

time_t get_current_time();

void set_current_time(struct gps_data gps_data);

int get_modem_info(cJSON *modem_data_obj);

#ifdef __cplusplus
}
#endif

#endif