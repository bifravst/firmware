#ifndef MQTT_FUNC_H__
#define MQTT_FUNC_H__

#include <net/mqtt.h>
#include <net/socket.h>
#include <lte_lc.h>

#ifdef __cplusplus
extern "C" {
#endif

// extern bool tracker_mode;

int check_mode(void);

void insert_gps_data(double longitude, double latitude);

void insert_battery_data(int battery_percentage);

int fds_init(struct mqtt_client *c);

void data_print_set_mode(u8_t *prefix, u8_t *data, size_t len);

void data_print(u8_t *prefix, u8_t *data, size_t len);

int data_publish(struct mqtt_client *c, enum mqtt_qos qos,
	u8_t *data, size_t len, u8_t *topic);

int sync_broker(void);

int subscribe(u8_t *sub_topic);

int publish_get_payload(struct mqtt_client *c, size_t length);

void mqtt_evt_handler(struct mqtt_client *const c,
		      const struct mqtt_evt *evt);

void broker_init(void);

void client_init(struct mqtt_client *client);

int mqtt_enable(struct mqtt_client *client);

void clear_fds();

void wait();

int process_mqtt_and_sleep(struct mqtt_client *client, int timeout);

int publish_gps_data();

int provision_certificates(void);

#ifdef __cplusplus
}
#endif

#endif
