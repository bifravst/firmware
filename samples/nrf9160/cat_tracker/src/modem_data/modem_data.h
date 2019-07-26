#ifndef MODEM_DATA_H__
#define MODEM_DATA_H__

#ifdef __cplusplus
extern "C" {
#endif

void at_cmd_handler(char *state);

int request_battery_status();

char *request_init_modem_data();

#ifdef __cplusplus
}
#endif

#endif