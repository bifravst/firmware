#ifndef MQTT_FUNC_H__
#define MQTT_FUNC_H__

#include <net/mqtt.h>
#include <net/socket.h>
#include <lte_lc.h>

#include <gps.h>

#ifdef __cplusplus
extern "C" {
#endif

void set_client_id_imei(char *imei);

int check_mode(void);

int check_active_wait(bool mode);

int check_idle_threshold(void);

int check_gps_timeout(void);

int check_mov_timeout(void);

double check_accel_thres(void);

void attach_gps_data(struct gps_data gps_data);

void attach_battery_data(int battery_voltage);

void attach_accel_data(double x, double y, double z);

int fds_init(struct mqtt_client *c);

void data_print_set_mode(u8_t *prefix, u8_t *data, size_t len);

void data_print(u8_t *prefix, u8_t *data, size_t len);

int data_publish(struct mqtt_client *c, enum mqtt_qos qos, u8_t *data,
		 size_t len, u8_t *topic);

int subscribe(u8_t *sub_topic);

int publish_get_payload(struct mqtt_client *c, u8_t *write_buf, size_t length);

void mqtt_evt_handler(struct mqtt_client *const c, const struct mqtt_evt *evt);

void broker_init(void);

void client_init(struct mqtt_client *client);

int mqtt_enable(struct mqtt_client *client);

void clear_fds(void);

void wait(int timeout);

int process_mqtt_and_sleep(struct mqtt_client *client, int timeout);

int publish_data(bool op);

int provision_certificates(void);

#ifdef __cplusplus
}
#endif

#endif
