#ifndef BATSTAT_H__
#define BATSTAT_H__

#ifdef __cplusplus
extern "C" {
#endif

void at_cmd_handler(char *state);

int request_battery_status();

#ifdef __cplusplus
}
#endif

#endif