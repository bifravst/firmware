#include <mqtt_behaviour.h>

#include <net/mqtt.h>
#include <net/socket.h>
#include <lte_lc.h>

#include <stdio.h>
#include <stdlib.h>

#include <stdbool.h>
#include <string.h>

#include <zephyr.h>
#include <zephyr/types.h>

#include <cJSON.h>
#include <cJSON_os.h>

#include <time.h>

#include <gps.h>

#include <mqtt_codec.h>

#define APP_SLEEP_MS 1000
#define APP_CONNECT_TRIES 10

#if defined(CONFIG_MQTT_LIB_TLS)
#include "nrf_inbuilt_key.h"
#include "certificates.h"
#endif

#define CONFIG_MQTT_GET_TOPIC "$aws/things/Cat-Tracker/shadow/get"
#define CONFIG_MQTT_ACCEPTED_TOPIC "$aws/things/Cat-Tracker/shadow/get/accepted"
#define CONFIG_MQTT_REJECTED_TOPIC "$aws/things/Cat-Tracker/shadow/get/rejected"
#define CONFIG_MQTT_DELTA_TOPIC "$aws/thing/Cat-Tracker/shadow/update/delta"

// typedef struct Sync_data {
// 	int bat_voltage;
// 	char bat_timestamp[50];

// 	double longitude;
// 	double latitude;
// 	float altitude;
// 	float accuracy;
// 	float speed;
// 	float heading;
// 	char gps_timestamp[50];

// 	double acc[3];
// 	char acc_timestamp[50];

// 	int gps_timeout;
// 	bool active;
// 	int active_wait;
// 	int passive_wait;

// 	int movement_timeout;

// } Sync_data;

Sync_data sync_data = { .gps_timeout = 720,
			.active = true,
			.active_wait = 60,
			.passive_wait = 60,
			.movement_timeout = 3600 };

static u8_t rx_buffer[CONFIG_MQTT_MESSAGE_BUFFER_SIZE];
static u8_t tx_buffer[CONFIG_MQTT_MESSAGE_BUFFER_SIZE];
static u8_t payload_buf[CONFIG_MQTT_PAYLOAD_BUFFER_SIZE];

static struct mqtt_client client;

static struct sockaddr_storage broker;

static struct pollfd fds;

static bool connected;

static bool initial_connection = true;

static int nfds;

int check_mode(void)
{
	if (sync_data.active) {
		return true;
	} else {
		return false;
	}
}

int check_active_wait(bool mode)
{
	if (mode) {
		return sync_data.active_wait;
	} else {
		return sync_data.passive_wait;
	}
}

int check_gps_timeout(void)
{
	return sync_data.gps_timeout;
}

int check_mov_timeout(void)
{
	return sync_data.movement_timeout;
}

void attach_gps_data(struct gps_data gps_data)
{
	// struct tm tm;
	// long int epoch;

	sync_data.longitude = gps_data.pvt.longitude;
	sync_data.latitude = gps_data.pvt.latitude;
	sync_data.altitude = gps_data.pvt.altitude;
	sync_data.accuracy = gps_data.pvt.accuracy;
	sync_data.speed = gps_data.pvt.speed;
	sync_data.heading = gps_data.pvt.heading;

	// tm.tm_year = gps_datetime.year - 1900;
	// tm.tm_mon = gps_datetime.month - 1;
	// tm.tm_mday = gps_datetime.day;
	// tm.tm_hour = gps_datetime.hour;
	// tm.tm_min = gps_datetime.minute;
	// tm.tm_sec = gps_datetime.seconds;

	// epoch = mktime(&tm);
	// sync_data.gps_timestamp = epoch;
}

void attach_battery_data(int battery_voltage)
{
	sync_data.bat_voltage = battery_voltage;
}

void attach_accel_data(double x, double y, double z)
{
	sync_data.acc[0] = x;
	sync_data.acc[1] = y;
	sync_data.acc[2] = z;
}

void data_print(u8_t *prefix, u8_t *data, size_t len)
{
	char buf[len + 1];

	memcpy(buf, data, len);
	buf[len] = 0;
	printk("%s%s\n", prefix, buf);
}

int data_publish(struct mqtt_client *c, enum mqtt_qos qos, u8_t *data,
		 size_t len, u8_t *topic)
{
	struct mqtt_publish_param param;

	param.message.topic.qos = qos;
	param.message.topic.topic.utf8 = topic;
	param.message.topic.topic.size = strlen(topic);
	param.message.payload.data = data;
	param.message.payload.len = len;
	param.message_id = sys_rand32_get();
	param.dup_flag = 0;
	param.retain_flag = 0;

	data_print("Publishing: ", data, len);
	printk("to topic: %s len: %u\n", topic, (unsigned int)strlen(topic));

	return mqtt_publish(c, &param);
}

int subscribe(u8_t *sub_topic)
{
	struct mqtt_topic subscribe_topic = {
		.topic = { .utf8 = sub_topic, .size = strlen(sub_topic) },
		.qos = MQTT_QOS_1_AT_LEAST_ONCE
	};

	const struct mqtt_subscription_list subscription_list = {
		.list = &subscribe_topic, .list_count = 1, .message_id = 1234
	};

	printk("Subscribing to: %s len %u\n", sub_topic,
	       (unsigned int)strlen(sub_topic));

	return mqtt_subscribe(&client, &subscription_list);
}

int publish_get_payload(struct mqtt_client *c, u8_t *write_buf, size_t length)
{
	u8_t *buf = write_buf;
	u8_t *end = buf + length;

	if (length > sizeof(payload_buf)) {
		return -EMSGSIZE;
	}
	while (buf < end) {
		int ret = mqtt_read_publish_payload_blocking(c, buf, end - buf);

		if (ret < 0) {
			return ret;
		} else if (ret == 0) {
			return -EIO;
		}
		buf += ret;
	}
	return 0;
}

// static int decode_response(char const *input)
// {
// 	cJSON *state = NULL;
// 	cJSON *desired = NULL;
// 	cJSON *cfg = NULL;

// 	cJSON *gpst = NULL;
// 	cJSON *active = NULL;
// 	cJSON *active_wait = NULL;
// 	cJSON *passive_wait = NULL;
// 	cJSON *movement_timeout = NULL;

// 	cJSON *root_obj = cJSON_Parse(input);
// 	if (root_obj == NULL) {
// 		return -ENOENT;
// 	}

// 	if (!initial_connection) {
// 		state = cJSON_GetObjectItem(root_obj, "state");
// 		desired = cJSON_GetObjectItem(state, "desired");
// 		cfg = cJSON_GetObjectItem(desired, "cfg");

// 		gpst = cJSON_GetObjectItem(cfg, "gpst");
// 		active = cJSON_GetObjectItem(cfg, "act");
// 		active_wait = cJSON_GetObjectItem(cfg, "actwt");
// 		passive_wait = cJSON_GetObjectItem(cfg, "mvres");
// 		movement_timeout = cJSON_GetObjectItem(cfg, "mvt");

// 		initial_connection = true;
// 	} else {
// 		state = cJSON_GetObjectItem(root_obj, "state");
// 		cfg = cJSON_GetObjectItem(state, "cfg");

// 		gpst = cJSON_GetObjectItem(cfg, "gpst");
// 		active = cJSON_GetObjectItem(cfg, "act");
// 		active_wait = cJSON_GetObjectItem(cfg, "actwt");
// 		passive_wait = cJSON_GetObjectItem(cfg, "mvres");
// 		movement_timeout = cJSON_GetObjectItem(cfg, "mvt");
// 	}

// 	if (gpst != NULL) {
// 		sync_data.gps_timeout = gpst->valueint;
// 		printk("SETTING GPST TO: %d\n", gpst->valueint);
// 	}

// 	if (active != NULL) {
// 		sync_data.active = active->valueint;
// 		printk("SETTING ACTIVE TO: %d\n", active->valueint);
// 	}

// 	if (active_wait != NULL) {
// 		sync_data.active_wait = active_wait->valueint;
// 		printk("SETTING ACTIVE WAIT TO: %d\n", active_wait->valueint);
// 	}

// 	if (passive_wait != NULL) {
// 		sync_data.passive_wait = passive_wait->valueint;
// 		printk("SETTING PASSIVE_WAIT TO: %d\n", passive_wait->valueint);
// 	}

// 	if (movement_timeout != NULL) {
// 		sync_data.movement_timeout = movement_timeout->valueint;
// 		printk("SETTING MOVEMENT TIMEOUT TO: %d\n",
// 		       movement_timeout->valueint);
// 	}

// 	cJSON_Delete(root_obj);

// 	return 0;
// }

void mqtt_evt_handler(struct mqtt_client *const c, const struct mqtt_evt *evt)
{
	int err;

	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result != 0) {
			printk("MQTT connect failed %d\n", evt->result);
			break;
		}
		connected = true;
		printk("[%s:%d] MQTT client connected!\n", __func__, __LINE__);

		if (!initial_connection) {
			subscribe(CONFIG_MQTT_ACCEPTED_TOPIC);
			// subscribe(CONFIG_MQTT_REJECTED_TOPIC);
		} else {
			subscribe(CONFIG_MQTT_SUB_TOPIC);
		}

		break;

	case MQTT_EVT_DISCONNECT:
		printk("[%s:%d] MQTT client disconnected %d\n", __func__,
		       __LINE__, evt->result);

		connected = false;
		clear_fds();
		break;

	case MQTT_EVT_PUBLISH: {
		const struct mqtt_publish_param *p = &evt->param.publish;

		printk("[%s:%d] MQTT PUBLISH result=%d len=%d\n", __func__,
		       __LINE__, evt->result, p->message.payload.len);
		err = publish_get_payload(c, payload_buf,
					  p->message.payload.len);
		if (err) {
			printk("mqtt_read_publish_payload: Failed! %d\n", err);
			printk("Disconnecting MQTT client...\n");

			err = mqtt_disconnect(c);
			if (err) {
				printk("Could not disconnect: %d\n", err);
			}
		}

		err = decode_response(payload_buf, &sync_data,
				      initial_connection);
		if (err != 0) {
			printk("Could not decode response\n%d", err);
		}

	} break;

	case MQTT_EVT_PUBACK:
		if (evt->result != 0) {
			printk("MQTT PUBACK error %d\n", evt->result);
			break;
		}

		printk("[%s:%d] PUBACK packet id: %u\n", __func__, __LINE__,
		       evt->param.puback.message_id);
		break;

	case MQTT_EVT_SUBACK:
		if (evt->result != 0) {
			printk("MQTT SUBACK error %d\n", evt->result);
			break;
		}

		printk("[%s:%d] SUBACK packet id: %u\n", __func__, __LINE__,
		       evt->param.suback.message_id);
		break;

	default:
		printk("[%s:%d] default: %d\n", __func__, __LINE__, evt->type);
		break;
	}
}

void broker_init(void)
{
	int err;
	struct addrinfo *result;
	struct addrinfo *addr;
	struct addrinfo hints = { .ai_family = AF_INET,
				  .ai_socktype = SOCK_STREAM };

	err = getaddrinfo(CONFIG_MQTT_BROKER_HOSTNAME, NULL, &hints, &result);
	if (err) {
		printk("ERROR: getaddrinfo failed %d\n", err);

		return;
	}

	addr = result;
	err = -ENOENT;

	/* Look for address of the broker. */
	while (addr != NULL) {
		/* IPv4 Address. */
		if (addr->ai_addrlen == sizeof(struct sockaddr_in)) {
			struct sockaddr_in *broker4 =
				((struct sockaddr_in *)&broker);
			char ipv4_addr[NET_IPV4_ADDR_LEN];

			broker4->sin_addr.s_addr =
				((struct sockaddr_in *)addr->ai_addr)
					->sin_addr.s_addr;
			broker4->sin_family = AF_INET;
			broker4->sin_port = htons(CONFIG_MQTT_BROKER_PORT);

			inet_ntop(AF_INET, &broker4->sin_addr.s_addr, ipv4_addr,
				  sizeof(ipv4_addr));
			printk("IPv4 Address found %s\n", ipv4_addr);

			break;
		} else {
			printk("ai_addrlen = %u should be %u or %u\n",
			       (unsigned int)addr->ai_addrlen,
			       (unsigned int)sizeof(struct sockaddr_in),
			       (unsigned int)sizeof(struct sockaddr_in6));
		}

		addr = addr->ai_next;
		break;
	}

	freeaddrinfo(result);
}

void client_init(struct mqtt_client *client)
{
	mqtt_client_init(client);

	broker_init();

	client->broker = &broker;
	client->evt_cb = mqtt_evt_handler;
	client->client_id.utf8 = (u8_t *)CONFIG_MQTT_CLIENT_ID;
	client->client_id.size = strlen(CONFIG_MQTT_CLIENT_ID);
	client->password = NULL;
	client->user_name = NULL;
	client->protocol_version = MQTT_VERSION_3_1_1;
	client->rx_buf = rx_buffer;
	client->rx_buf_size = sizeof(rx_buffer);
	client->tx_buf = tx_buffer;
	client->tx_buf_size = sizeof(tx_buffer);

#if defined(CONFIG_MQTT_LIB_TLS)
	client->transport.type = MQTT_TRANSPORT_SECURE;

	static sec_tag_t sec_tag_list[] = { CONFIG_CLOUD_CERT_SEC_TAG };
	struct mqtt_sec_config *tls_config = &(client->transport).tls.config;

	tls_config->peer_verify = 2;
	tls_config->cipher_count = 0;
	tls_config->cipher_list = NULL;
	tls_config->sec_tag_count = ARRAY_SIZE(sec_tag_list);
	tls_config->sec_tag_list = sec_tag_list;
	tls_config->hostname = CONFIG_MQTT_BROKER_HOSTNAME;
#else
	client->transport.type = MQTT_TRANSPORT_NON_SECURE;
#endif
}

void clear_fds(void)
{
	nfds = 0;
}

void wait(int timeout)
{
	if (nfds > 0) {
		if (poll(&fds, nfds, timeout) < 0) {
			printk("poll error: %d\n", errno);
		}
	}
}

int process_mqtt_and_sleep(struct mqtt_client *client, int timeout)
{
	s64_t remaining = timeout;
	s64_t start_time = k_uptime_get();
	int err;

	while (remaining > 0 && connected) {
		wait(remaining);

		err = mqtt_live(client);
		if (err != 0) {
			printk("mqtt_live error\n");
			return err;
		}

		err = mqtt_input(client);
		if (err != 0) {
			printk("mqtt_input error\n");
			return err;
		}

		remaining = timeout + start_time - k_uptime_get();
	}

	return 0;
}

int mqtt_enable(struct mqtt_client *client)
{
	int err, i = 0;

	while (i++ < APP_CONNECT_TRIES && !connected) {
		client_init(client);

		err = mqtt_connect(client);
		if (err != 0) {
			printk("ERROR: mqtt_connect %d\n", err);
			k_sleep(APP_SLEEP_MS);
			continue;
		}

#if defined(CONFIG_MQTT_LIB_TLS)
		fds.fd = client->transport.tls.sock;
#else
		fds.fd = client->transport.tcp.sock;
#endif

		fds.events = POLLIN;
		nfds = 1;

		wait(APP_SLEEP_MS);
		mqtt_input(client);

		if (!connected) {
			mqtt_abort(client);
		}
	}

	if (connected) {
		return 0;
	}

	return 0;
}

// static int json_add_obj(cJSON *parent, const char *str, cJSON *item)
// {
// 	cJSON_AddItemToObject(parent, str, item);

// 	return 0;
// }

// static int json_add_str(cJSON *parent, const char *str, const char *item)
// {
// 	cJSON *json_str;

// 	json_str = cJSON_CreateString(item);
// 	if (json_str == NULL) {
// 		return -ENOMEM;
// 	}

// 	return json_add_obj(parent, str, json_str);
// }

// static int json_add_number(cJSON *parent, const char *str, double item)
// {
// 	cJSON *json_num;

// 	json_num = cJSON_CreateNumber(item);
// 	if (json_num == NULL) {
// 		return -ENOMEM;
// 	}

// 	return json_add_obj(parent, str, json_num);
// }

// static int json_add_bool(cJSON *parent, const char *str, int item)
// {
// 	cJSON *json_bool;

// 	json_bool = cJSON_CreateBool(item);
// 	if (json_bool == NULL) {
// 		return -ENOMEM;
// 	}

// 	return json_add_obj(parent, str, json_bool);
// }

// static int json_add_DoubleArray(cJSON *parent, const char *str, double *item)
// {
// 	cJSON *json_double;

// 	json_double = cJSON_CreateDoubleArray(item, 3);
// 	if (json_double == NULL) {
// 		return -ENOMEM;
// 	}

// 	return json_add_obj(parent, str, json_double);
// }

int publish_data(void)
{
	int err;
	struct Transmit_data transmit_data;

	err = mqtt_enable(&client);
	if (err) {
		printk("Could not connect to client\n");
	}

	err = mqtt_ping(&client);
	if (err) {
		printk("Could not ping server\n");
	}

	// if (sync) {
	// 	data_publish(&client, MQTT_QOS_1_AT_LEAST_ONCE, "",
	// 		     0, CONFIG_MQTT_GET_TOPIC);
	// } else
	// {

	err = encode_message(&transmit_data, &sync_data);
	if (err != 0) {
		printk("ERROR when enconding message: %d\n", err);
	}

	//*start, this section and all containing json should be relocated to its own module*/
	// cJSON *root_obj = cJSON_CreateObject();
	// cJSON *state_obj = cJSON_CreateObject();
	// cJSON *reported_obj = cJSON_CreateObject();
	// cJSON *bat_obj = cJSON_CreateObject();
	// cJSON *acc_obj = cJSON_CreateObject();
	// cJSON *gps_obj = cJSON_CreateObject();
	// cJSON *cfg_obj = cJSON_CreateObject();
	// cJSON *gps_val_obj = cJSON_CreateObject();

	// if (root_obj == NULL || state_obj == NULL || reported_obj == NULL ||
	//     bat_obj == NULL || acc_obj == NULL || gps_obj == NULL ||
	//     cfg_obj == NULL || gps_val_obj == NULL) {
	// 	cJSON_Delete(root_obj);
	// 	cJSON_Delete(state_obj);
	// 	cJSON_Delete(reported_obj);
	// 	cJSON_Delete(bat_obj);
	// 	cJSON_Delete(acc_obj);
	// 	cJSON_Delete(gps_obj);
	// 	cJSON_Delete(cfg_obj);
	// 	cJSON_Delete(gps_val_obj);
	// 	return -ENOMEM;
	// }

	// /*BAT*/
	// err = json_add_number(bat_obj, "v", sync_data.bat_voltage);
	// err += json_add_str(bat_obj, "ts", sync_data.bat_timestamp);

	// /*ACC*/
	// err += json_add_DoubleArray(acc_obj, "v", sync_data.acc);
	// err += json_add_str(acc_obj, "ts", sync_data.acc_timestamp);

	// /*GPS*/
	// err += json_add_number(gps_val_obj, "lng", sync_data.longitude);
	// err += json_add_number(gps_val_obj, "lat", sync_data.latitude);
	// err += json_add_number(gps_val_obj, "acc", sync_data.accuracy);
	// err += json_add_number(gps_val_obj, "alt", sync_data.altitude);
	// err += json_add_number(gps_val_obj, "spd", sync_data.speed);
	// err += json_add_number(gps_val_obj, "hdg", sync_data.heading);

	// /*CFG*/
	// err += json_add_number(cfg_obj, "gpst", sync_data.gps_timeout);
	// err += json_add_bool(cfg_obj, "act", sync_data.active);
	// err += json_add_number(cfg_obj, "actwt", sync_data.active_wait);
	// err += json_add_number(cfg_obj, "mvres", sync_data.passive_wait);
	// err += json_add_number(cfg_obj, "mvt", sync_data.movement_timeout);

	// if (err != 0) {
	// 	cJSON_Delete(root_obj);
	// 	cJSON_Delete(state_obj);
	// 	cJSON_Delete(reported_obj);
	// 	cJSON_Delete(bat_obj);
	// 	cJSON_Delete(acc_obj);
	// 	cJSON_Delete(gps_obj);
	// 	cJSON_Delete(cfg_obj);
	// 	cJSON_Delete(gps_val_obj);
	// 	return -ENOMEM;
	// }

	// err = json_add_obj(reported_obj, "bat", bat_obj);
	// err += json_add_obj(reported_obj, "acc", acc_obj);

	// err += json_add_obj(gps_obj, "v", gps_val_obj);
	// err += json_add_str(gps_obj, "ts", sync_data.acc_timestamp);

	// err += json_add_obj(reported_obj, "gps", gps_obj);

	// err += json_add_obj(reported_obj, "cfg", cfg_obj);

	// err += json_add_obj(state_obj, "reported", reported_obj);
	// err += json_add_obj(root_obj, "state", state_obj);

	// if (err != 0) {
	// 	cJSON_Delete(root_obj);
	// 	cJSON_Delete(state_obj);
	// 	cJSON_Delete(reported_obj);
	// 	cJSON_Delete(bat_obj);
	// 	cJSON_Delete(acc_obj);
	// 	cJSON_Delete(gps_obj);
	// 	cJSON_Delete(cfg_obj);
	// 	cJSON_Delete(gps_val_obj);
	// 	return -EAGAIN;
	// }

	// char *buffer;

	// buffer = cJSON_Print(root_obj);

	// cJSON_Delete(root_obj);

	/*end*/

	data_publish(&client, MQTT_QOS_1_AT_LEAST_ONCE, transmit_data.buf,
		     transmit_data.len, CONFIG_MQTT_PUB_TOPIC);
	// }

	err = process_mqtt_and_sleep(&client, APP_SLEEP_MS);
	if (err) {
		printk("mqtt processing failed\n");
	}

	err = mqtt_disconnect(&client);
	if (err) {
		printk("Could not disconnect\n");
	}

	wait(APP_SLEEP_MS);
	err = mqtt_input(&client);
	if (err) {
		printk("Could not input data\n");
	}

	return 0;
}

int provision_certificates(void)
{
#if defined(CONFIG_MQTT_LIB_TLS)
	{
		int err;

		/* Delete certificates */
		nrf_sec_tag_t sec_tag = CONFIG_CLOUD_CERT_SEC_TAG;

		for (nrf_key_mgnt_cred_type_t type = 0; type < 3; type++) {
			err = nrf_inbuilt_key_delete(sec_tag, type);
			printk("nrf_inbuilt_key_delete(%lu, %d) => result=%d\n",
			       sec_tag, type, err);
		}

		/* Provision CA Certificate. */
		err = nrf_inbuilt_key_write(CONFIG_CLOUD_CERT_SEC_TAG,
					    NRF_KEY_MGMT_CRED_TYPE_CA_CHAIN,
					    CLOUD_CA_CERTIFICATE,
					    strlen(CLOUD_CA_CERTIFICATE));
		printk("nrf_inbuilt_key_write => result=%d\n", err);
		if (err) {
			printk("CLOUD_CA_CERTIFICATE err: %d", err);
			return err;
		}

		/* Provision Private Certificate. */
		err = nrf_inbuilt_key_write(CONFIG_CLOUD_CERT_SEC_TAG,
					    NRF_KEY_MGMT_CRED_TYPE_PRIVATE_CERT,
					    CLOUD_CLIENT_PRIVATE_KEY,
					    strlen(CLOUD_CLIENT_PRIVATE_KEY));
		printk("nrf_inbuilt_key_write => result=%d\n", err);
		if (err) {
			printk("NRF_CLOUD_CLIENT_PRIVATE_KEY err: %d", err);
			return err;
		}

		/* Provision Public Certificate. */
		err = nrf_inbuilt_key_write(
			CONFIG_CLOUD_CERT_SEC_TAG,
			NRF_KEY_MGMT_CRED_TYPE_PUBLIC_CERT,
			CLOUD_CLIENT_PUBLIC_CERTIFICATE,
			strlen(CLOUD_CLIENT_PUBLIC_CERTIFICATE));
		printk("nrf_inbuilt_key_write => result=%d\n", err);
		if (err) {
			printk("CLOUD_CLIENT_PUBLIC_CERTIFICATE err: %d", err);
			return err;
		}
	}
#endif
	return 0;
}
