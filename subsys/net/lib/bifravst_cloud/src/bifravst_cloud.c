#include <bifravst_cloud.h>
#include <net/mqtt.h>
#include <net/socket.h>
#include <net/cloud.h>
#include <net/cloud_backend.h>
#include <nrf_socket.h>
#include <stdio.h>

#include <logging/log.h>

#if defined(CONFIG_AWS_FOTA)
#include <net/aws_fota.h>
#include <dfu/mcuboot.h>
#include <misc/reboot.h>
#endif

LOG_MODULE_REGISTER(bifravst_cloud, CONFIG_BIFRAVST_CLOUD_LOG_LEVEL);

#if defined(CONFIG_BIFRAVST_CLOUD_IPV6)
#define BIFRAVST_CLOUD_AF_FAMILY AF_INET6
#else
#define BIFRAVST_CLOUD_AF_FAMILY AF_INET
#endif

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
	{ .topic = { .utf8 = update_delta_topic, .size = UPDATE_DELTA_TOPIC_LEN },
	  .qos = MQTT_QOS_1_AT_LEAST_ONCE }
};

struct bifravst_cloud_tx_data {
	char *buf;
	size_t len;
	u8_t *topic;
	enum mqtt_qos qos;
};

static u8_t rx_buffer[CONFIG_BIFRAVST_CLOUD_BUFFER_SIZE];
static u8_t tx_buffer[CONFIG_BIFRAVST_CLOUD_BUFFER_SIZE];
static u8_t payload_buf[CONFIG_BIFRAVST_CLOUD_PAYLOAD_SIZE];

static struct mqtt_client client;

static struct sockaddr_storage broker;

static struct cloud_backend *bifravst_cloud_backend;

static int mqtt_client_id_get(char *id)
{
	int err;
	int at_socket_fd;
	int bytes_written;
	int bytes_read;
	char imei_buf[IMEI_LEN + 1];

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

static int mqtt_topics_populate(void)
{
	int err;

	err = mqtt_client_id_get(client_id_buf);
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

#if defined(CONFIG_AWS_FOTA)
static void aws_fota_cb_handler(enum aws_fota_evt_id evt)
{
	switch (evt) {
	case AWS_FOTA_EVT_DONE:
		LOG_DBG("AWS_FOTA_EVT_DONE, rebooting to apply update.");
		mqtt_disconnect(&client);
		sys_reboot(0);
		break;

	case AWS_FOTA_EVT_ERROR:
		LOG_ERR("AWS_FOTA_EVT_ERROR");
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

	err = mqtt_topics_populate();
	if (err != 0) {
		LOG_DBG("Topics not populated, error: %d", err);
		return err;
	}

#if defined(CONFIG_AWS_FOTA)
	err = aws_fota_init(&client, CONFIG_APP_VERSION, aws_fota_cb_handler);
	if (err != 0) {
		LOG_ERR("ERROR: aws_fota_init %d", err);
		return err;
	}

	boot_write_img_confirmed();
#endif
	return err;	
}

static int mqtt_data_publish(struct mqtt_client *c, enum mqtt_qos qos, u8_t *data,
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

	LOG_DBG("Publishing to topic: %s", param.message.topic.topic.utf8);

	return mqtt_publish(c, &param);
}

static int mqtt_ep_subscribe(void)
{
	const struct mqtt_subscription_list subscription_list = {
		.list = (struct mqtt_topic *)&cc_rx_list,
		.list_count = ARRAY_SIZE(cc_rx_list),
		.message_id = CC_SUBSCRIBE_ID
	};

	for (int i = 0; i < subscription_list.list_count; i++) {
		LOG_DBG("Subscribing to: %s",
		       subscription_list.list[i].topic.utf8);
	}

	return mqtt_subscribe(&client, &subscription_list);
}

static int mqtt_publish_get_payload(struct mqtt_client *c, size_t length)
{
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
		LOG_ERR("aws_fota_mqtt_evt_handler: Failed! %d", err);
		// LOG_ERR("Disconnecting MQTT client...");
		// err = mqtt_disconnect(c);
		// if (err) {
		// 	LOG_ERR("Could not disconnect: %d", err);
		// }
	}
#endif

	switch (mqtt_evt->type) {
	case MQTT_EVT_CONNACK:
		LOG_DBG("MQTT client connected!");

		mqtt_ep_subscribe();	

		cloud_evt.type = CLOUD_EVT_CONNECTED;
		cloud_notify_event(bifravst_cloud_backend, &cloud_evt,
				   config->user_data);
		break;

	case MQTT_EVT_DISCONNECT:
		LOG_DBG("MQTT_EVT_DISCONNECT: result=%d", mqtt_evt->result);

		cloud_evt.type = CLOUD_EVT_DISCONNECTED;
		cloud_notify_event(bifravst_cloud_backend, &cloud_evt,
				   config->user_data);
		break;

	case MQTT_EVT_PUBLISH: {
		const struct mqtt_publish_param *p = &mqtt_evt->param.publish;

		LOG_DBG("MQTT_EVT_PUBLISH: id=%d len=%d ",
			p->message_id,
			p->message.payload.len);
		
		err = mqtt_publish_get_payload(c, p->message.payload.len);
		if (err) {
			LOG_ERR("mqtt_read_publish_payload: Failed! %d", err);
			break;
		}

		cloud_evt.type = CLOUD_EVT_DATA_RECEIVED;
		cloud_evt.data.msg.buf = payload_buf;
		cloud_evt.data.msg.len = p->message.payload.len;

		cloud_notify_event(bifravst_cloud_backend, &cloud_evt,
				   config->user_data);
	} break;

	case MQTT_EVT_PUBACK:
		LOG_DBG("MQTT_EVT_PUBACK: id=%d result=%d",
			mqtt_evt->param.puback.message_id,
			mqtt_evt->result);
		break;

	case MQTT_EVT_SUBACK:
		LOG_DBG("MQTT_EVT_SUBACK: id=%d result=%d",
			mqtt_evt->param.suback.message_id,
			mqtt_evt->result);
		break;

	default:	
		break;
	}
}

#if defined(CONFIG_BIFRAVST_CLOUD_STATIC_IPV4)
static int mqtt_broker_init(void)
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
static int mqtt_broker_init(void)
{
	int err;
	struct addrinfo *result;
	struct addrinfo *addr;
	struct addrinfo hints = {
		.ai_family = BIFRAVST_CLOUD_AF_FAMILY,
		.ai_socktype = SOCK_STREAM
	};

	err = getaddrinfo(CONFIG_BIFRAVST_CLOUD_HOST_NAME, NULL, &hints, &result);
	if (err) {
		LOG_ERR("getaddrinfo failed %d", err);
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
			LOG_DBG("IPv4 Address found %s", ipv4_addr);	
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

			inet_ntop(AF_INET, &broker6->sin6_addr.s6_addr, ipv6_addr,
				  sizeof(ipv6_addr));
			LOG_DBG("IPv4 Address found %s", ipv6_addr);
			break;
		} else {
			LOG_DBG("ai_addrlen = %u should be %u or %u",
				(unsigned int)addr->ai_addrlen,
				(unsigned int)sizeof(struct sockaddr_in),
				(unsigned int)sizeof(struct sockaddr_in6));
		}

		addr = addr->ai_next;
		break;
	}

	freeaddrinfo(result);

	return 0;
}
#endif

static int mqtt_client_broker_init(struct mqtt_client *client)
{
	int err;

	mqtt_client_init(client);

	err = mqtt_broker_init();
	if (err != 0) {
		return err;
	}

	client->broker			= &broker;
	client->evt_cb			= mqtt_evt_handler;
	client->client_id.utf8		= (u8_t *)client_id_buf;
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
	tls_cfg->hostname		= CONFIG_BIFRAVST_CLOUD_HOST_NAME;

	return 0;
}

static int bifravst_connect(const struct cloud_backend *const backend)
{
	int err;

	err = mqtt_client_broker_init(&client);
	if (err != 0) {
		LOG_ERR("Client not initialized, error: %d", err);
		return err;
	}

	err = mqtt_connect(&client);
	if (err != 0) {
		LOG_ERR("MQTT not connected, error: %d", err);
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
		.buf 	= msg->buf,
		.len 	= msg->len,
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

	return mqtt_data_publish(&client, cloud_tx_data.qos, cloud_tx_data.buf,
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
