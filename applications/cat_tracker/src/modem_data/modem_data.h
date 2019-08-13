#ifndef MODEM_DATA_H__
#define MODEM_DATA_H__

#include <gps.h>
#include <time.h>
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

int request_battery_status(void);

int modem_time_get(void);

time_t get_current_time(void);

void set_current_time(struct gps_data gps_data);

struct modem_param_info *get_modem_info(void);

int get_rsrp_values(void);

#ifdef __cplusplus
}
#endif
#endif
