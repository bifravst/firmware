#include <net/mqtt.h>
#include <net/socket.h>
#include <net/cloud.h>
#include <net/cloud_backend.h>
#include <at_cmd.h>
#include <stdio.h>

#if defined(CONFIG_AWS_FOTA)
#include <net/aws_fota.h>
#endif

#include <logging/log.h>

LOG_MODULE_REGISTER(bifravst_cloud, CONFIG_BIFRAVST_CLOUD_LOG_LEVEL);

BUILD_ASSERT_MSG(sizeof(CONFIG_AWS_IOT_BROKER_HOST_NAME) > 1,
		 "Bifravst Cloud hostname not set");

#if defined(CONFIG_BIFRAVST_CLOUD_IPV6)
#define BIFRAVST_CLOUD_AF_FAMILY AF_INET6
#else
#define BIFRAVST_CLOUD_AF_FAMILY AF_INET
#endif

#define AWS_CLOUD_CLIENT_ID_LEN 15

#define AWS "$aws/things/"
#define AWS_LEN (sizeof(AWS) - 1)

#define SHADOW_BASE_TOPIC AWS "%s/shadow"
#define SHADOW_BASE_TOPIC_LEN (AWS_LEN + AWS_CLOUD_CLIENT_ID_LEN + 7)

#define ACCEPTED_TOPIC AWS "%s/shadow/get/accepted"
#define ACCEPTED_TOPIC_LEN (AWS_LEN + AWS_CLOUD_CLIENT_ID_LEN + 20)

#define REJECTED_TOPIC AWS "%s/shadow/get/rejected"
#define REJECTED_TOPIC_LEN (AWS_LEN + AWS_CLOUD_CLIENT_ID_LEN + 20)

#define CFG_TOPIC AWS "%s/shadow/get/accepted/desired/cfg"
#define CFG_TOPIC_LEN (AWS_LEN + AWS_CLOUD_CLIENT_ID_LEN + 32)

#define UPDATE_DELTA_TOPIC AWS "%s/shadow/update/delta"
#define UPDATE_DELTA_TOPIC_LEN (AWS_LEN + AWS_CLOUD_CLIENT_ID_LEN + 20)

#define UPDATE_TOPIC AWS "%s/shadow/update"
#define UPDATE_TOPIC_LEN (AWS_LEN + AWS_CLOUD_CLIENT_ID_LEN + 14)

#define SHADOW_GET AWS "%s/shadow/get"
#define SHADOW_GET_LEN (AWS_LEN + AWS_CLOUD_CLIENT_ID_LEN + 11)

#define BATCH_TOPIC AWS "%s/batch"
#define BATCH_TOPIC_LEN (AWS_LEN + AWS_CLOUD_CLIENT_ID_LEN + 6)

#define SUBSCRIBE_ID 1234

static char client_id_buf[AWS_CLOUD_CLIENT_ID_LEN + 1];
static char shadow_base_topic[SHADOW_BASE_TOPIC_LEN + 1];
static char accepted_topic[ACCEPTED_TOPIC_LEN + 1];
static char cfg_topic[CFG_TOPIC_LEN + 1];
static char rejected_topic[REJECTED_TOPIC_LEN + 1];
static char update_delta_topic[UPDATE_DELTA_TOPIC_LEN + 1];
static char update_topic[UPDATE_TOPIC_LEN + 1];
static char get_topic[SHADOW_GET_LEN + 1];
static char batch_topic[BATCH_TOPIC_LEN + 1];

static const struct mqtt_topic cc_rx_list[] = {
	{ .topic = {
		.utf8 = rejected_topic, .size = REJECTED_TOPIC_LEN },
		.qos = MQTT_QOS_1_AT_LEAST_ONCE },
	{ .topic = {
		.utf8 = cfg_topic, .size = CFG_TOPIC_LEN },
		.qos = MQTT_QOS_1_AT_LEAST_ONCE },
	{ .topic = {
		.utf8 = update_delta_topic, .size = UPDATE_DELTA_TOPIC_LEN },
		.qos = MQTT_QOS_1_AT_LEAST_ONCE },
};

struct bifravst_cloud_tx_data {
	char *buf;
	size_t len;
	char *topic;
	enum mqtt_qos qos;
};

static char rx_buffer[CONFIG_BIFRAVST_CLOUD_MQTT_RX_TX_BUFFER_LEN];
static char tx_buffer[CONFIG_BIFRAVST_CLOUD_MQTT_RX_TX_BUFFER_LEN];
static char payload_buf[CONFIG_BIFRAVST_CLOUD_MQTT_PAYLOAD_BUFFER_LEN];

static struct mqtt_client client;
static struct sockaddr_storage broker;
static struct cloud_backend *bifravst_cloud_backend;

static int client_id_get(char *const id)
{
	int err;
	char imei_buf[AWS_CLOUD_CLIENT_ID_LEN + 1];

	err = at_cmd_write("AT+CGSN", imei_buf,
		(AWS_CLOUD_CLIENT_ID_LEN + 5), NULL);
	if (err) {
		LOG_ERR("Could not get device IMEI, error: %d", err);
		return err;
	}

	err = snprintf(id, sizeof(client_id_buf), "%s", imei_buf);
	if (err != AWS_CLOUD_CLIENT_ID_LEN + 2) {
		return -ENOMEM;
	}

	return 0;
}

static int topics_populate(void)
{
	int err;

	err = client_id_get(client_id_buf);
	if (err) {
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

	err = snprintf(cfg_topic, sizeof(cfg_topic), CFG_TOPIC,
		       client_id_buf);
	if (err != CFG_TOPIC_LEN) {
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

#if defined(CONFIG_AWS_FOTA)
static void aws_fota_cb_handler(enum aws_fota_evt_id evt)
{
	struct cloud_backend_config *config = bifravst_cloud_backend->config;
	struct cloud_event cloud_evt = { 0 };

	switch (evt) {
	case AWS_FOTA_EVT_DONE:
		LOG_DBG("AWS_FOTA_EVT_DONE");
		cloud_evt.type = CLOUD_EVT_FOTA_DONE;
		cloud_notify_event(bifravst_cloud_backend, &cloud_evt,
				   config->user_data);
		break;
	case AWS_FOTA_EVT_ERROR:
		LOG_ERR("AWS_FOTA_EVT_ERROR");
		cloud_evt.type = CLOUD_EVT_ERROR;
		cloud_notify_event(bifravst_cloud_backend, &cloud_evt,
				   config->user_data);
		break;
	}
}
#endif

static int bifravst_init(const struct cloud_backend *const backend,
			 cloud_evt_handler_t handler)
{
	int err;

	backend->config->handler = handler;
	bifravst_cloud_backend = (struct cloud_backend *)backend;

	err = topics_populate();
	if (err) {
		LOG_ERR("topics_populate, error: %d", err);
		return err;
	}

#if defined(CONFIG_AWS_FOTA)
	err = aws_fota_init(&client, "v0.0.0",
		aws_fota_cb_handler);
	if (err) {
		LOG_ERR("aws_fota_init, error: %d", err);
		return err;
	}
#endif
	return err;
}

static int data_publish(struct mqtt_client *const c, enum mqtt_qos qos,
			char *data, size_t len, char *topic)
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

	LOG_DBG("Publishing to topic: %s",
		log_strdup(param.message.topic.topic.utf8));

	return mqtt_publish(c, &param);
}

static int topic_subscribe(void)
{
	const struct mqtt_subscription_list subscription_list = {
		.list = (struct mqtt_topic *)&cc_rx_list,
		.list_count = ARRAY_SIZE(cc_rx_list),
		.message_id = SUBSCRIBE_ID
	};

	for (size_t i = 0; i < subscription_list.list_count; i++) {
		LOG_DBG("Subscribing to: %s",
		       log_strdup(subscription_list.list[i].topic.utf8));
	}

	return mqtt_subscribe(&client, &subscription_list);
}

static int publish_get_payload(struct mqtt_client *const c, size_t length)
{
	if (length > sizeof(payload_buf)) {
		return -EMSGSIZE;
	}

	return mqtt_readall_publish_payload(c, payload_buf, length);
}

static void mqtt_evt_handler(struct mqtt_client *const c,
			     const struct mqtt_evt *mqtt_evt)
{
	int err;
	struct cloud_backend_config *config = bifravst_cloud_backend->config;
	struct cloud_event cloud_evt = { 0 };

#if defined(CONFIG_AWS_FOTA)
	err = aws_fota_mqtt_evt_handler(c, mqtt_evt);
	if (err > 0) {
		/* Event handled by FOTA library so we can skip it */
		return;
	} else if (err < 0) {
		LOG_ERR("aws_fota_mqtt_evt_handler, error: %d", err);
		cloud_evt.type = CLOUD_EVT_ERROR;
		cloud_notify_event(bifravst_cloud_backend, &cloud_evt,
				   config->user_data);
	}
#endif

	switch (mqtt_evt->type) {
	case MQTT_EVT_CONNACK:
		LOG_DBG("MQTT client connected!");

		topic_subscribe();

		cloud_evt.type = CLOUD_EVT_CONNECTED;
		cloud_notify_event(bifravst_cloud_backend, &cloud_evt,
				   config->user_data);
		break;

	case MQTT_EVT_DISCONNECT:
		LOG_DBG("MQTT_EVT_DISCONNECT: result = %d", mqtt_evt->result);

		cloud_evt.type = CLOUD_EVT_DISCONNECTED;
		cloud_notify_event(bifravst_cloud_backend, &cloud_evt,
				   config->user_data);
		break;

	case MQTT_EVT_PUBLISH: {
		const struct mqtt_publish_param *p = &mqtt_evt->param.publish;

		LOG_DBG("MQTT_EVT_PUBLISH: id = %d len = %d ",
			p->message_id,
			p->message.payload.len);

		err = publish_get_payload(c, p->message.payload.len);
		if (err) {
			LOG_ERR("publish_get_payload, error: %d", err);
			break;
		}

		if (p->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
			const struct mqtt_puback_param ack = {
				.message_id = p->message_id
			};

			mqtt_publish_qos1_ack(c, &ack);
		}

		cloud_evt.type = CLOUD_EVT_DATA_RECEIVED;
		cloud_evt.data.msg.buf = payload_buf;
		cloud_evt.data.msg.len = p->message.payload.len;

		cloud_notify_event(bifravst_cloud_backend, &cloud_evt,
				config->user_data);

	} break;

	case MQTT_EVT_PUBACK:
		LOG_DBG("MQTT_EVT_PUBACK: id = %d result = %d",
			mqtt_evt->param.puback.message_id,
			mqtt_evt->result);
		break;

	case MQTT_EVT_SUBACK:
		LOG_DBG("MQTT_EVT_SUBACK: id = %d result = %d",
			mqtt_evt->param.suback.message_id,
			mqtt_evt->result);
		break;

	default:
		break;
	}
}

#if defined(CONFIG_BIFRAVST_CLOUD_STATIC_IPV4)
static int broker_init(void)
{
	int err;

	struct sockaddr_in *broker =
		((struct sockaddr_in *)&broker);

	inet_pton(AF_INET, CONFIG_BIFRAVST_CLOUD_STATIC_IPV4_ADDR,
		  &broker->sin_addr);
	broker->sin_family = AF_INET;
	broker->sin_port = htons(CONFIG_BIFRAVST_CLOUD_PORT);

	LOG_DBG("IPv4 Address %s", CONFIG_BIFRAVST_CLOUD_STATIC_IPV4_ADDR);

	return err;
}
#else
static int broker_init(void)
{
	int err;
	struct addrinfo *result;
	struct addrinfo *addr;
	struct addrinfo hints = {
		.ai_family = BIFRAVST_CLOUD_AF_FAMILY,
		.ai_socktype = SOCK_STREAM
	};

	err = getaddrinfo(CONFIG_AWS_IOT_BROKER_HOST_NAME,
				NULL, &hints, &result);
	if (err) {
		LOG_ERR("getaddrinfo, error %d", err);
		return err;
	}

	addr = result;

	while (addr != NULL) {
		if ((addr->ai_addrlen == sizeof(struct sockaddr_in)) &&
		    (BIFRAVST_CLOUD_AF_FAMILY == AF_INET)) {
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
			LOG_DBG("IPv4 Address found %s", log_strdup(ipv4_addr));
			break;
		} else if ((addr->ai_addrlen == sizeof(struct sockaddr_in6)) &&
			   (BIFRAVST_CLOUD_AF_FAMILY == AF_INET6)) {
			struct sockaddr_in6 *broker6 =
				((struct sockaddr_in6 *)&broker);
			char ipv6_addr[NET_IPV6_ADDR_LEN];

			memcpy(broker6->sin6_addr.s6_addr,
				((struct sockaddr_in6 *)addr->ai_addr)
				->sin6_addr.s6_addr,
				sizeof(struct in6_addr));
			broker6->sin6_family = AF_INET6;
			broker6->sin6_port = htons(CONFIG_BIFRAVST_CLOUD_PORT);

			inet_ntop(AF_INET6, &broker6->sin6_addr.s6_addr,
				ipv6_addr,
				sizeof(ipv6_addr));
			LOG_DBG("IPv4 Address found %s", log_strdup(ipv6_addr));
			break;
		}

		LOG_DBG("ai_addrlen = %u should be %u or %u",
			(unsigned int)addr->ai_addrlen,
			(unsigned int)sizeof(struct sockaddr_in),
			(unsigned int)sizeof(struct sockaddr_in6));

		addr = addr->ai_next;
		break;
	}

	freeaddrinfo(result);

	return 0;
}
#endif

static int client_broker_init(struct mqtt_client *const client)
{
	int err;

	mqtt_client_init(client);

	err = broker_init();
	if (err) {
		return err;
	}

	client->broker			= &broker;
	client->evt_cb			= mqtt_evt_handler;
	client->client_id.utf8		= (char *)client_id_buf;
	client->client_id.size		= strlen(client_id_buf);
	client->password		= NULL;
	client->user_name		= NULL;
	client->protocol_version	= MQTT_VERSION_3_1_1;
	client->rx_buf			= rx_buffer;
	client->rx_buf_size		= sizeof(rx_buffer);
	client->tx_buf			= tx_buffer;
	client->tx_buf_size		= sizeof(tx_buffer);
	client->transport.type		= MQTT_TRANSPORT_SECURE;

	static sec_tag_t sec_tag_list[] = { CONFIG_BIFRAVST_CLOUD_SEC_TAG };
	struct mqtt_sec_config *tls_cfg = &(client->transport).tls.config;

	tls_cfg->peer_verify		= 2;
	tls_cfg->cipher_count		= 0;
	tls_cfg->cipher_list		= NULL;
	tls_cfg->sec_tag_count		= ARRAY_SIZE(sec_tag_list);
	tls_cfg->sec_tag_list		= sec_tag_list;
	tls_cfg->hostname		= CONFIG_AWS_IOT_BROKER_HOST_NAME;

	return 0;
}

static int bifravst_connect(const struct cloud_backend *const backend)
{
	int err;

	err = client_broker_init(&client);
	if (err) {
		LOG_ERR("client_broker_init, error: %d", err);
		return err;
	}

	err = mqtt_connect(&client);
	if (err) {
		LOG_ERR("mqtt_connect, error: %d", err);
		return err;
	}

	backend->config->socket = client.transport.tls.sock;

	return 0;
}

static int bifravst_disconnect(const struct cloud_backend *const backend)
{
	return mqtt_disconnect(&client);
}

static int bifravst_send(const struct cloud_backend *const backend,
			 const struct cloud_msg *const msg)
{
	struct bifravst_cloud_tx_data cloud_tx_data = {
		.buf	= msg->buf,
		.len	= msg->len,
		.qos	= msg->qos,
	};

	switch (msg->endpoint.type) {
	case CLOUD_EP_TOPIC_PAIR:
		cloud_tx_data.topic = get_topic;
		break;
	case CLOUD_EP_TOPIC_MSG:
		cloud_tx_data.topic = update_topic;
		break;
	case CLOUD_EP_TOPIC_BATCH:
		cloud_tx_data.topic = batch_topic;
		break;
	default:
		break;
	}

	return data_publish(&client, cloud_tx_data.qos, cloud_tx_data.buf,
				cloud_tx_data.len, cloud_tx_data.topic);
}

static int bifravst_input(const struct cloud_backend *const backend)
{
	return mqtt_input(&client);
}

static int bifravst_ping(const struct cloud_backend *const backend)
{
	return mqtt_live(&client);
}

static const struct cloud_api bifravst_cloud_api = {
	.init		= bifravst_init,
	.connect	= bifravst_connect,
	.disconnect	= bifravst_disconnect,
	.send		= bifravst_send,
	.ping		= bifravst_ping,
	.input		= bifravst_input
};

CLOUD_BACKEND_DEFINE(BIFRAVST_CLOUD, bifravst_cloud_api);
