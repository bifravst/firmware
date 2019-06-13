#ifndef MQTT_FUNC_H__
#define MQTT_FUNC_H__

#include <net/mqtt.h>
#include <net/socket.h>
#include <lte_lc.h>

// // /* The mqtt client struct */
// struct mqtt_client client;

// struct pollfd fds;

int fds_init(struct mqtt_client *c);

void data_print(u8_t *prefix, u8_t *data, size_t len);

int data_publish(struct mqtt_client *c, enum mqtt_qos qos,
	u8_t *data, size_t len);

void mqtt_evt_handler(struct mqtt_client *const c,
		      const struct mqtt_evt *evt);

void broker_init(void);

void client_init(struct mqtt_client *client);

#endif