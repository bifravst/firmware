#ifndef BIFRAVST_CLOUD_H__
#define BIFRAVST_CLOUD_H__

#include <net/mqtt.h>
#include <net/socket.h>
#include <lte_lc.h>
#include <gps.h>

#ifdef __cplusplus
extern "C" {
#endif

void set_gps_found(bool gps_found);

int check_mode(void);

int check_active_wait(bool mode);

int check_gps_timeout(void);

int check_mov_timeout(void);

double check_accel_thres(void);

void attach_gps_data(struct gps_data gps_data);

void attach_battery_data(int battery_voltage);

void attach_accel_data(double x, double y, double z);

#ifdef __cplusplus
}
#endif
#endif
