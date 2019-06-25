#ifndef MQTT_FUNC_H__
#define MQTT_FUNC_H__

#include <net/mqtt.h>
#include <net/socket.h>
#include <lte_lc.h>

int fds_init(struct mqtt_client *c);

void data_print(u8_t *prefix, u8_t *data, size_t len);

int data_publish(struct mqtt_client *c, enum mqtt_qos qos,
	u8_t *data, size_t len);

void mqtt_evt_handler(struct mqtt_client *const c,
		      const struct mqtt_evt *evt);

void broker_init(void);

void client_init(struct mqtt_client *client);

int mqtt_enable(struct mqtt_client *client);

void clear_fds();

void wait();

int process_mqtt_and_sleep(struct mqtt_client *client, int timeout);

void publish_gps_data(u8_t *gps_publish_data_stream_head, size_t gps_data_len);

#endif
