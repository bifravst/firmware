#include <mqtt_behaviour.h>

#include <net/mqtt.h>
#include <net/socket.h>
#include <lte_lc.h>

#define APP_SLEEP					70000 //default 70 seconds
#define APP_CONNECT_TRIES			5
#define CMDT_ENABLE_REAL_TIME_T		"CMDT+ENBRTT"
#define CMDT_DISABLE_REAL_TIME_T	"CMDT+DISBRTT"

static u8_t rx_buffer[CONFIG_MQTT_MESSAGE_BUFFER_SIZE];
static u8_t tx_buffer[CONFIG_MQTT_MESSAGE_BUFFER_SIZE];
static u8_t payload_buf[CONFIG_MQTT_PAYLOAD_BUFFER_SIZE];

static struct mqtt_client client;

static struct sockaddr_storage broker;

static struct pollfd fds;

static bool connected;

static int nfds;

static bool real_time_tracking = false;

void data_print_set_mode(u8_t *prefix, u8_t *data, size_t len)
{
	char buf[len + 1];

	memcpy(buf, data, len);
	buf[len] = 0;

	/*This code section checks if incomming is a know commando */
	if (strcmp(buf, CMDT_ENABLE_REAL_TIME_T)) {
		real_time_tracking = true;
		printk("%s%s -> Real time tracking mode set\n", prefix, buf);
	} else if (strcmp(buf, CMDT_DISABLE_REAL_TIME_T)) {
		real_time_tracking = false;
		printk("%s%s -> Real time tracking mode disabled\n", prefix, buf);
	} else {
		printk("%s%s -> Could not identify commando\n", prefix, buf);
	}
}

void data_print(u8_t *prefix, u8_t *data, size_t len)
{
	char buf[len + 1];

	memcpy(buf, data, len);
	buf[len] = 0;
	printk("%s%s\n", prefix, buf);
}

int data_publish(struct mqtt_client *c, enum mqtt_qos qos,
	u8_t *data, size_t len)
{
	struct mqtt_publish_param param;

	param.message.topic.qos = qos;
	param.message.topic.topic.utf8 = CONFIG_MQTT_PUB_TOPIC;
	param.message.topic.topic.size = strlen(CONFIG_MQTT_PUB_TOPIC);
	param.message.payload.data = data;
	param.message.payload.len = len;
	param.message_id = sys_rand32_get();
	param.dup_flag = 0;
	param.retain_flag = 0; //Retain message by the broker, needs to be set eventually

	data_print("Publishing: ", data, len);
	printk("to topic: %s len: %u\n",
		CONFIG_MQTT_PUB_TOPIC,
		(unsigned int)strlen(CONFIG_MQTT_PUB_TOPIC));

	return mqtt_publish(c, &param);
}

int subscribe(void)
{
	struct mqtt_topic subscribe_topic = {
		.topic = { .utf8 = CONFIG_MQTT_SUB_TOPIC,
			   .size = strlen(CONFIG_MQTT_SUB_TOPIC) },
		.qos = MQTT_QOS_1_AT_LEAST_ONCE
	};

	const struct mqtt_subscription_list subscription_list = {
		.list = &subscribe_topic, .list_count = 1, .message_id = 1234
	};

	printk("Subscribing to: %s len %u\n", CONFIG_MQTT_SUB_TOPIC,
	       (unsigned int)strlen(CONFIG_MQTT_SUB_TOPIC));

	return mqtt_subscribe(&client, &subscription_list);
}

int publish_get_payload(struct mqtt_client *c, size_t length)
{
	u8_t *buf = payload_buf;
	u8_t *end = buf + length;

	if (length > sizeof(payload_buf)) {
		return -EMSGSIZE;
	}

	while (buf < end) {
		int ret = mqtt_read_publish_payload(c, buf, end - buf);

		if (ret < 0) {
			int err;

			if (ret != -EAGAIN) {
				return ret;
			}

			printk("mqtt_read_publish_payload: EAGAIN\n");

			err = poll(&fds, 1, K_SECONDS(CONFIG_MQTT_KEEPALIVE));
			if (err > 0 && (fds.revents & POLLIN) == POLLIN) {
				continue;
			} else {
				return -EIO;
			}
		}

		if (ret == 0) {
			return -EIO;
		}

		buf += ret;
	}

	return 0;
}

void mqtt_evt_handler(struct mqtt_client *const c,
		      const struct mqtt_evt *evt)
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
		//subscribe();
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
		err = publish_get_payload(c, p->message.payload.len);
		if (err >= 0) {
			data_print_set_mode("Received: ", payload_buf, p->message.payload.len);
		} else {
			printk("mqtt_read_publish_payload: Failed! %d\n", err);
			printk("Disconnecting MQTT client...\n");

			err = mqtt_disconnect(c);
			if (err) {
				printk("Could not disconnect: %d\n", err);
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
		printk("[%s:%d] default: %d\n", __func__, __LINE__,
				evt->type);
		break;
	}
}

void broker_init(void)
{
	int err;
	struct addrinfo *result;
	struct addrinfo *addr;
	struct addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM
	};

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

			inet_ntop(AF_INET, &broker4->sin_addr.s_addr,
				  ipv4_addr, sizeof(ipv4_addr));
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
	client->transport.type = MQTT_TRANSPORT_NON_SECURE;
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

int mqtt_enable(struct mqtt_client *client) {

	int err, i = 0;

	while (i++ < APP_CONNECT_TRIES && !connected) {
		client_init(client);
		
		err = mqtt_connect(client);
		if (err != 0) {
			printk("ERROR: mqtt_connect %d\n", err);
			//k_sleep(APP_SLEEP); why sleep here?
			continue;
		}

		fds.fd = client->transport.tcp.sock;
		fds.events = ZSOCK_POLLIN;
		nfds = 1;

		wait(APP_SLEEP);
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

void publish_gps_data(u8_t *gps_publish_data_stream_head, size_t gps_data_len) {
	int err;

	err = mqtt_enable(&client);
	if (err) {
		printk("Could not connect to client\n");
	}

	err = mqtt_ping(&client);
	if (err) {
		printk("Could not ping server\n");
	}

    data_publish(&client, MQTT_QOS_1_AT_LEAST_ONCE,
        gps_publish_data_stream_head, gps_data_len);

	err = process_mqtt_and_sleep(&client, APP_SLEEP);
	if (err) {
		printk("mqtt processing failed\n");
	}

	err = mqtt_disconnect(&client);
	if (err) {
		printk("Could not disconnect\n");
	}

	wait(APP_SLEEP);
	err = mqtt_input(&client);
	if (err) {
		printk("Could not input data\n");
	}
	
}
