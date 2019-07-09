#ifndef BATSTAT_H__
#define BATSTAT_H__

#ifdef __cplusplus
extern "C" {
#endif

void at_cmd_handler(char *state);

void request_battery_status(char *gps_dummy_string);

#ifdef __cplusplus
}
#endif

#endif