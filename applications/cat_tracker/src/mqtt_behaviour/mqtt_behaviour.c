#include <mqtt_behaviour.h>
#include <net/mqtt.h>
#include <net/socket.h>
#include <lte_lc.h>
#include <nrf_socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <gps.h>
#include <mqtt_codec.h>
#include <leds.h>

#define APP_SLEEP_MS 5000
#define APP_CONNECT_TRIES 5

struct Sync_data sync_data = { .gps_timeout = 180,
			       .active = true,
			       .active_wait = 60,
			       .passive_wait = 300,
			       .movement_timeout = 3600,
			       .accel_threshold = 100,
			       .gps_found = false };

static u8_t rx_buffer[CONFIG_MQTT_MESSAGE_BUFFER_SIZE];
static u8_t tx_buffer[CONFIG_MQTT_MESSAGE_BUFFER_SIZE];
static u8_t payload_buf[CONFIG_MQTT_PAYLOAD_BUFFER_SIZE];

static struct mqtt_client client;

static struct sockaddr_storage broker;

static struct pollfd fds;

static bool connected;

static int nfds;

#define CLOUD_HOSTNAME CONFIG_CLOUD_HOST_NAME
#define CLOUD_PORT CONFIG_CLOUD_PORT

#define IMEI_LEN 15
#define AWS_CLOUD_CLIENT_ID_LEN (IMEI_LEN)

#define AWS "$aws/things/"
#define AWS_LEN (sizeof(AWS) - 1)

#define SHADOW_BASE_TOPIC AWS "%s/shadow"
#define SHADOW_BASE_TOPIC_LEN (AWS_LEN + AWS_CLOUD_CLIENT_ID_LEN + 7)

#define ACCEPTED_TOPIC AWS "%s/shadow/get/accepted/desired/cfg"
#define ACCEPTED_TOPIC_LEN (AWS_LEN + AWS_CLOUD_CLIENT_ID_LEN + 32)

#define REJECTED_TOPIC AWS "%s/shadow/get/rejected"
#define REJECTED_TOPIC_LEN (AWS_LEN + AWS_CLOUD_CLIENT_ID_LEN + 20)

#define UPDATE_DELTA_TOPIC AWS "%s/shadow/update/delta"
#define UPDATE_DELTA_TOPIC_LEN (AWS_LEN + AWS_CLOUD_CLIENT_ID_LEN + 20)

#define UPDATE_TOPIC AWS "%s/shadow/update"
#define UPDATE_TOPIC_LEN (AWS_LEN + AWS_CLOUD_CLIENT_ID_LEN + 14)

#define SHADOW_GET AWS "%s/shadow/get"
#define SHADOW_GET_LEN (AWS_LEN + AWS_CLOUD_CLIENT_ID_LEN + 11)

#define CC_SUBSCRIBE_ID 1234

static char client_id_buf[AWS_CLOUD_CLIENT_ID_LEN + 1];
static char shadow_base_topic[SHADOW_BASE_TOPIC_LEN + 1];
static char accepted_topic[ACCEPTED_TOPIC_LEN + 1];
static char rejected_topic[REJECTED_TOPIC_LEN + 1];
static char update_delta_topic[UPDATE_DELTA_TOPIC_LEN + 1];
static char update_topic[UPDATE_TOPIC_LEN + 1];
static char get_topic[SHADOW_GET_LEN + 1];

static const struct mqtt_topic cc_rx_list[] = {
	{ .topic = { .utf8 = accepted_topic, .size = ACCEPTED_TOPIC_LEN },
	  .qos = MQTT_QOS_1_AT_LEAST_ONCE },
	{ .topic = { .utf8 = rejected_topic, .size = REJECTED_TOPIC_LEN },
	  .qos = MQTT_QOS_1_AT_LEAST_ONCE },
	{ .topic = { .utf8 = update_delta_topic,
		     .size = UPDATE_DELTA_TOPIC_LEN },
	  .qos = MQTT_QOS_1_AT_LEAST_ONCE }
};

static int client_id_get(char *id)
{
	int at_socket_fd;
	int bytes_written;
	int bytes_read;
	char imei_buf[IMEI_LEN + 1];
	int ret;

	at_socket_fd = nrf_socket(NRF_AF_LTE, 0, NRF_PROTO_AT);
	__ASSERT_NO_MSG(at_socket_fd >= 0);

	bytes_written = nrf_write(at_socket_fd, "AT+CGSN", 7);
	__ASSERT_NO_MSG(bytes_written == 7);

	bytes_read = nrf_read(at_socket_fd, imei_buf, IMEI_LEN);
	__ASSERT_NO_MSG(bytes_read == IMEI_LEN);
	imei_buf[IMEI_LEN] = 0;

	snprintf(id, AWS_CLOUD_CLIENT_ID_LEN + 1, "%s", imei_buf);

	ret = nrf_close(at_socket_fd);
	__ASSERT_NO_MSG(ret == 0);

	return 0;
}

static int topics_populate(void)
{
	int err;

	err = client_id_get(client_id_buf);
	if (err != 0) {
		return err;
	}

	err = snprintf(shadow_base_topic, sizeof(shadow_base_topic),
		       SHADOW_BASE_TOPIC, client_id_buf);
	if (err != SHADOW_BASE_TOPIC_LEN) {
		return -ENOMEM;
	}

	err = snprintf(accepted_topic, sizeof(accepted_topic), ACCEPTED_TOPIC,
		       client_id_buf);
	if (err != ACCEPTED_TOPIC_LEN) {
		return -ENOMEM;
	}

	err = snprintf(rejected_topic, sizeof(rejected_topic), REJECTED_TOPIC,
		       client_id_buf);
	if (err != REJECTED_TOPIC_LEN) {
		return -ENOMEM;
	}

	err = snprintf(update_delta_topic, sizeof(update_delta_topic),
		       UPDATE_DELTA_TOPIC, client_id_buf);
	if (err != UPDATE_DELTA_TOPIC_LEN) {
		return -ENOMEM;
	}

	err = snprintf(update_topic, sizeof(update_topic), UPDATE_TOPIC,
		       client_id_buf);
	if (err != UPDATE_TOPIC_LEN) {
		return -ENOMEM;
	}

	err = snprintf(get_topic, sizeof(get_topic), SHADOW_GET, client_id_buf);
	if (err != SHADOW_GET_LEN) {
		return -ENOMEM;
	}

	return 0;
}

void cloud_configuration_init(void)
{
	int err;

	err = topics_populate();
	if (err) {
		printk("Could not populate topics: %d\n", err);
	}
}

void set_gps_found(bool gps_found)
{
	sync_data.gps_found = gps_found;
}

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

double check_accel_thres(void)
{
	double accel_threshold_double;

	if (sync_data.accel_threshold == 0) {
		accel_threshold_double = 0;
	} else {
		accel_threshold_double = sync_data.accel_threshold / 10;
	}

	return accel_threshold_double;
}

void attach_gps_data(struct gps_data gps_data)
{
	sync_data.longitude = gps_data.pvt.longitude;
	sync_data.latitude = gps_data.pvt.latitude;
	sync_data.altitude = gps_data.pvt.altitude;
	sync_data.accuracy = gps_data.pvt.accuracy;
	sync_data.speed = gps_data.pvt.speed;
	sync_data.heading = gps_data.pvt.heading;
	sync_data.gps_timestamp = k_uptime_get();
}

void attach_battery_data(int battery_voltage)
{
	sync_data.bat_voltage = battery_voltage;
	sync_data.bat_timestamp = k_uptime_get();
}

void attach_accel_data(double x, double y, double z)
{
	sync_data.acc[0] = x;
	sync_data.acc[1] = y;
	sync_data.acc[2] = z;
	sync_data.acc_timestamp = k_uptime_get();
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

int subscribe(void)
{
	const struct mqtt_subscription_list subscription_list = {
		.list = (struct mqtt_topic *)&cc_rx_list,
		.list_count = ARRAY_SIZE(cc_rx_list),
		.message_id = CC_SUBSCRIBE_ID
	};

	for (int i = 0; i < subscription_list.list_count; i++) {
		printk("Subscribing to: %s\n",
		       subscription_list.list[i].topic.utf8);
	}

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

		subscribe();

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

			break;
		}

		if (p->message.payload.len > 2) {
			err = decode_response(payload_buf, &sync_data);
			if (err != 0) {
				printk("Could not decode response\n%d", err);
			}
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

	err = getaddrinfo(CLOUD_HOSTNAME, NULL, &hints, &result);
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
			broker4->sin_port = htons(CLOUD_PORT);

			inet_ntop(AF_INET, &broker4->sin_addr.s_addr, ipv4_addr,
				  sizeof(ipv4_addr));
			printk("IPv4 Address found %s\n", ipv4_addr);

			break;
		}
		printk("ai_addrlen = %u should be %u or %u\n",
		       (unsigned int)addr->ai_addrlen,
		       (unsigned int)sizeof(struct sockaddr_in),
		       (unsigned int)sizeof(struct sockaddr_in6));

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
	client->client_id.utf8 = (u8_t *)client_id_buf;
	client->client_id.size = strlen(client_id_buf);
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
	tls_config->hostname = CLOUD_HOSTNAME;
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

	return -EINVAL;
}

int publish_data(bool syncronization, bool pub_modem_d)
{
	int err;
	struct Transmit_data transmit_data;

	err = mqtt_enable(&client);
	if (err) {
		printk("Could not connect to client\n");
	}

	if (connected) {
		publish_data_led();

		if (syncronization) {
			transmit_data.buf = "";
			transmit_data.len = strlen(transmit_data.buf);
			transmit_data.topic = get_topic;
		} else {
			err = encode_message(&transmit_data, &sync_data);
			if (err != 0) {
				printk("ERROR when enconding message: %d\n",
				       err);
			}
			transmit_data.topic = update_topic;
		}

		err = data_publish(&client, MQTT_QOS_1_AT_LEAST_ONCE,
				   transmit_data.buf, transmit_data.len,
				   transmit_data.topic);
		if (err != 0) {
			printk("Could not publish data: %d\n", err);
		}

		err = process_mqtt_and_sleep(&client, APP_SLEEP_MS);
		if (err != 0) {
			printk("Could not process data broker: %d\n", err);
		}

		if (check_config_change()) {
			err = encode_message(&transmit_data, &sync_data);
			if (err != 0) {
				printk("ERROR when enconding message: %d\n",
				       err);
			}
			transmit_data.topic = update_topic;

			data_publish(&client, MQTT_QOS_1_AT_LEAST_ONCE,
				     transmit_data.buf, transmit_data.len,
				     transmit_data.topic);
			if (err != 0) {
				printk("Could not publish configuration update: %d\n",
				       err);
			}

			err = process_mqtt_and_sleep(&client, APP_SLEEP_MS);
			if (err != 0) {
				printk("Could not process data broker: %d\n",
				       err);
			}
		}

		if (pub_modem_d) {
			err = encode_modem_data(&transmit_data, syncronization);
			if (err != 0) {
				printk("ERROR when enconding modem data: %d\n",
				       err);
			}
			transmit_data.topic = update_topic;

			data_publish(&client, MQTT_QOS_1_AT_LEAST_ONCE,
				     transmit_data.buf, transmit_data.len,
				     transmit_data.topic);
			if (err != 0) {
				printk("Could not publish configuration update: %d\n",
				       err);
			}

			err = process_mqtt_and_sleep(&client, APP_SLEEP_MS);
			if (err != 0) {
				printk("Could not process data broker: %d\n",
				       err);
			}
		}
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
