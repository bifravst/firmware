#include <bifravst_cloud.h>
#include <net/mqtt.h>
#include <net/socket.h>
#include <net/cloud.h>
#include <net/cloud_backend.h>
#include <lte_lc.h>
#include <nrf_socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <gps.h>
#include <logging/log.h>

LOG_MODULE_REGISTER(bifravst_cloud_transport, CONFIG_BIFRAVST_CLOUD_LOG_LEVEL);

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

#define BATCH_TOPIC AWS "%s/batch"
#define BATCH_TOPIC_LEN (AWS_LEN + AWS_CLOUD_CLIENT_ID_LEN + 6)

#define CC_SUBSCRIBE_ID 1234

static char client_id_buf[AWS_CLOUD_CLIENT_ID_LEN + 1];
static char shadow_base_topic[SHADOW_BASE_TOPIC_LEN + 1];
static char accepted_topic[ACCEPTED_TOPIC_LEN + 1];
static char rejected_topic[REJECTED_TOPIC_LEN + 1];
static char update_delta_topic[UPDATE_DELTA_TOPIC_LEN + 1];
static char update_topic[UPDATE_TOPIC_LEN + 1];
static char get_topic[SHADOW_GET_LEN + 1];
static char batch_topic[BATCH_TOPIC_LEN + 1];

static const struct mqtt_topic cc_rx_list[] = {
	{ .topic = { .utf8 = accepted_topic, .size = ACCEPTED_TOPIC_LEN },
	  .qos = MQTT_QOS_1_AT_LEAST_ONCE },
	{ .topic = { .utf8 = rejected_topic, .size = REJECTED_TOPIC_LEN },
	  .qos = MQTT_QOS_1_AT_LEAST_ONCE },
	{ .topic = { .utf8 = update_delta_topic,
		     .size = UPDATE_DELTA_TOPIC_LEN },
	  .qos = MQTT_QOS_1_AT_LEAST_ONCE }
};

struct bifravst_tx_data {
	char *buf;
	size_t len;
	u8_t *topic;
};

struct bifravst_tx_data tx_data;

static u8_t rx_buffer[CONFIG_BIFRAVST_CLOUD_BUFFER_SIZE];
static u8_t tx_buffer[CONFIG_BIFRAVST_CLOUD_BUFFER_SIZE];
static u8_t payload_buf[CONFIG_BIFRAVST_CLOUD_BUFFER_SIZE];

static struct mqtt_client client;

static struct sockaddr_storage broker;

static struct pollfd fds;

static struct cloud_backend *bifravst_cloud_backend;

static int client_id_get(char *id)
{
	int at_socket_fd;
	int bytes_written;
	int bytes_read;
	char imei_buf[IMEI_LEN + 1];
	int err;

	at_socket_fd = nrf_socket(NRF_AF_LTE, 0, NRF_PROTO_AT);
	__ASSERT_NO_MSG(at_socket_fd >= 0);

	bytes_written = nrf_write(at_socket_fd, "AT+CGSN", 7);
	__ASSERT_NO_MSG(bytes_written == 7);

	bytes_read = nrf_read(at_socket_fd, imei_buf, IMEI_LEN);
	__ASSERT_NO_MSG(bytes_read == IMEI_LEN);
	imei_buf[IMEI_LEN] = 0;

	snprintf(id, AWS_CLOUD_CLIENT_ID_LEN + 1, "%s", imei_buf);

	err = nrf_close(at_socket_fd);
	__ASSERT_NO_MSG(err == 0);

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

	err = snprintf(batch_topic, sizeof(batch_topic), BATCH_TOPIC,
		       client_id_buf);
	if (err != BATCH_TOPIC_LEN) {
		return -ENOMEM;
	}

	return 0;
}

static int bifravst_cloud_init()
{
	int err;

	err = topics_populate();
	if (err) {
		return err;
	}

	return err;
}

static int bifravst_init(const struct cloud_backend *const backend,
			 cloud_evt_handler_t handler)
{
	backend->config->handler = handler;
	bifravst_cloud_backend = (struct cloud_backend *)backend;

	return bifravst_cloud_init();
}

static void data_print(u8_t *prefix, u8_t *data, size_t len)
{
	char buf[len + 1];

	memcpy(buf, data, len);
	buf[len] = 0;
	printk("%s%s\n", prefix, buf);
}

static int data_publish(struct mqtt_client *c, enum mqtt_qos qos, u8_t *data,
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

	data_print("Publishing: \n", data, len);
	printk("to topic: %s len: %u\n", topic, (unsigned int)strlen(topic));

	return mqtt_publish(c, &param);
}

static int subscribe(void)
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

static int publish_get_payload(struct mqtt_client *c, size_t length)
{
	u8_t *buf = payload_buf;
	u8_t *end = buf + length;

	if (length > sizeof(payload_buf)) {
		return -EMSGSIZE;
	}

	while (buf < end) {
		int err = mqtt_read_publish_payload(c, buf, end - buf);

		if (err < 0) {

			if (err != -EAGAIN) {
				return err;
			}

			LOG_DBG("mqtt_read_publish_payload: EAGAIN");

			err = poll(&fds, 1, K_SECONDS(CONFIG_MQTT_KEEPALIVE));
			if (err > 0 && (fds.revents & POLLIN) == POLLIN) {
				continue;
			} else {
				return -EIO;
			}
		}

		if (err == 0) {
			return -EIO;
		}

		buf += err;
	}

	return 0;
}

static void mqtt_evt_handler(struct mqtt_client *const c,
			     const struct mqtt_evt *bifravst_cloud_evt)
{
	int err;
	struct cloud_backend_config *config = bifravst_cloud_backend->config;
	struct cloud_event evt = { 0 };

	switch (bifravst_cloud_evt->type) {
	case MQTT_EVT_CONNACK:

		subscribe();

		evt.type = CLOUD_EVT_CONNECTED;
		cloud_notify_event(bifravst_cloud_backend, &evt,
				   config->user_data);
		break;

	case MQTT_EVT_DISCONNECT:

		evt.type = CLOUD_EVT_DISCONNECTED;
		cloud_notify_event(bifravst_cloud_backend, &evt,
				   config->user_data);
		break;

	case MQTT_EVT_PUBLISH: {
		const struct mqtt_publish_param *p =
			&bifravst_cloud_evt->param.publish;

		err = publish_get_payload(c, p->message.payload.len);
		if (err) {
			LOG_ERR("mqtt_read_publish_payload: Failed! %d", err);
		}

		evt.type = CLOUD_EVT_DATA_RECEIVED;
		evt.data.msg.buf = payload_buf;
		evt.data.msg.len = p->message.payload.len;

		cloud_notify_event(bifravst_cloud_backend, &evt,
				   config->user_data);
	} break;

	case MQTT_EVT_PUBACK:
		break;

	case MQTT_EVT_SUBACK:
		break;

	default:
		break;
	}
}

static int broker_init(void)
{
	int err;
	struct addrinfo *result;
	struct addrinfo *addr;
	struct addrinfo hints = { .ai_family = AF_INET,
				  .ai_socktype = SOCK_STREAM };

	err = getaddrinfo(CONFIG_BIFRAVST_CLOUD_HOST_NAME, NULL, &hints,
			  &result);
	if (err) {
		LOG_ERR("ERROR: getaddrinfo failed %d", err);

		return err;
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
			broker4->sin_port = htons(CONFIG_BIFRAVST_CLOUD_PORT);

			inet_ntop(AF_INET, &broker4->sin_addr.s_addr, ipv4_addr,
				  sizeof(ipv4_addr));
			printk("IPv4 Address found %s\n", ipv4_addr);

			break;
		}
		LOG_DBG("ai_addrlen = %u should be %u or %un",
			(unsigned int)addr->ai_addrlen,
			(unsigned int)sizeof(struct sockaddr_in),
			(unsigned int)sizeof(struct sockaddr_in6));

		addr = addr->ai_next;
		break;
	}

	freeaddrinfo(result);

	return 0;
}

static int client_init(struct mqtt_client *client)
{
	int err;

	mqtt_client_init(client);

	err = broker_init();
	if (err) {
		return err;
	}

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

	static sec_tag_t sec_tag_list[] = { CONFIG_BIFRAVST_CLOUD_SEC_TAG };
	struct mqtt_sec_config *tls_config = &(client->transport).tls.config;

	tls_config->peer_verify = 2;
	tls_config->cipher_count = 0;
	tls_config->cipher_list = NULL;
	tls_config->sec_tag_count = ARRAY_SIZE(sec_tag_list);
	tls_config->sec_tag_list = sec_tag_list;
	tls_config->hostname = CONFIG_BIFRAVST_CLOUD_HOST_NAME;
#else
	client->transport.type = MQTT_TRANSPORT_NON_SECURE;
#endif

	return 0;
}

static int bifravst_connect(const struct cloud_backend *const backend)
{
	int err;

	err = client_init(&client);
	if (err != 0) {
		return err;
	}

	backend->config->socket = client.transport.tls.sock;

	return mqtt_connect(&client);
}

static int bifravst_disconnect(const struct cloud_backend *const backend)
{
	return mqtt_disconnect(&client);
}

static int bifravst_send(const struct cloud_backend *const backend,
			 const struct cloud_msg *const msg)
{
	int err;

	tx_data.buf = msg->buf;
	tx_data.len = msg->len;

	switch (msg->endpoint.type) {
	case CLOUD_EP_TOPIC_PAIR:
		tx_data.topic = get_topic;
		break;
	case CLOUD_EP_TOPIC_MSG:
		tx_data.topic = update_topic;
		break;
	case CLOUD_EP_BATCH:
		tx_data.topic = batch_topic;
		break;
	default:
		break;
	}

	err = data_publish(&client, MQTT_QOS_1_AT_LEAST_ONCE, tx_data.buf,
			   tx_data.len, tx_data.topic);
	if (err != 0) {
		LOG_ERR("Publishing data failed, error %d", err);
		return err;
	}

	return 0;
}

static int bifravst_input(const struct cloud_backend *const backend)
{
	int err;

	err = mqtt_live(&client);
	if (err != 0) {
		return err;
	}

	err = mqtt_input(&client);
	if (err != 0) {
		return err;
	}

	return 0;
}

static int bifravst_ping(const struct cloud_backend *const backend)
{
	int err;

	err = mqtt_live(&client);
	if (err != 0) {
		return err;
	}

	err = mqtt_input(&client);
	if (err != 0) {
		return err;
	}

	return 0;
}

static const struct cloud_api bifravst_cloud_api = {
	.init = bifravst_init,
	.connect = bifravst_connect,
	.disconnect = bifravst_disconnect,
	.send = bifravst_send,
	.ping = bifravst_ping,
	.input = bifravst_input
};

CLOUD_BACKEND_DEFINE(BIFRAVST_CLOUD, bifravst_cloud_api);
