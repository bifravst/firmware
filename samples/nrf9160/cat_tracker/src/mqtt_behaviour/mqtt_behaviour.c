#include <mqtt_behaviour.h>

#include <net/mqtt.h>
#include <net/socket.h>
#include <lte_lc.h>

#include <stdio.h>
#include <stdlib.h>

#include <time.h>

#include <leds.h>

#include <gps.h>

#include <mqtt_codec.h>

#define APP_SLEEP_MS 3000
#define APP_CONNECT_TRIES 10

#if defined(CONFIG_MQTT_LIB_TLS)
#include "nrf_inbuilt_key.h"
#include "certificates.h"
#endif

Sync_data sync_data = { .gps_timeout = 15,
			.active = true,
			.active_wait = 30,
			.passive_wait = 30,
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

static bool initial_connection = false;

static int nfds;

static char client_id_imei[100] = "Cat-Tracker";

static char get_topic[100] = "$aws/things/Cat-Tracker/shadow/get";

static char get_accepted_desired_cfg_topic[100] =
	"$aws/things/Cat-Tracker/shadow/get/accepted/desired/cfg";

static char get_rejected_topic[100] =
	"$aws/things/Cat-Tracker/shadow/get/rejected";

static char update_topic[100] = "$aws/things/Cat-Tracker/shadow/update";

static char update_delta_topic[100] =
	"$aws/things/Cat-Tracker/shadow/update/delta";

static char broker_name[100] = "a2zs8l7txlw7wc-ats.iot.us-west-2.amazonaws.com";
//"a2zs8l7txlw7wc-ats.iot.us-west-2.amazonaws.com";
//"a34x44yyrk96tg-ats.iot.eu-central-1.amazonaws.com";
//352656100247819

char *replaceWord(const char *s, const char *oldW, const char *newW)
{
	char *result;
	int i, cnt = 0;
	int newWlen = strlen(newW);
	int oldWlen = strlen(oldW);

	// Counting the number of times old word
	// occur in the string
	for (i = 0; s[i] != '\0'; i++) {
		if (strstr(&s[i], oldW) == &s[i]) {
			cnt++;

			// Jumping to index after the old word.
			i += oldWlen - 1;
		}
	}

	// Making new string of enough length
	result = (char *)malloc(i + cnt * (newWlen - oldWlen) + 1);

	i = 0;
	while (*s) {
		// compare the substring with the result
		if (strstr(s, oldW) == s) {
			strcpy(&result[i], newW);
			i += newWlen;
			s += oldWlen;
		} else
			result[i++] = *s++;
	}

	result[i] = '\0';
	return result;
}

void set_gps_found(bool gps_found)
{
	sync_data.gps_found = gps_found;
}

void set_client_id_imei(char *imei)
{
	// strcpy(client_id_imei, imei);
	// strcpy(get_topic,
	//        replaceWord(CONFIG_MQTT_AWS_GET_TOPIC, "{thingname}", imei));
	// strcpy(get_accepted_desired_cfg_topic,
	//        replaceWord(CONFIG_MQTT_AWS_GET_ACCEPTED_TOPIC, "{thingname}",
	// 		   imei));
	// strcpy(update_topic,
	//        replaceWord(CONFIG_MQTT_AWS_UPDATE_TOPIC, "{thingname}", imei));
	// strcpy(update_delta_topic,
	//        replaceWord(CONFIG_MQTT_AWS_UPDATE_DELTA_TOPIC, "{thingname}",
	// 		   imei));

	// printk("GET TOPIC %s\n", get_topic);
	// printk("GET ACCEPTED TOPIC %s\n", get_accepted_desired_cfg_topic);
	// printk("UPDATE %s\n", update_topic);
	// printk("UPDATE DELTA %s\n", update_delta_topic);
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

	accel_threshold_double = sync_data.accel_threshold / 10;

	return accel_threshold_double;
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

		if (initial_connection) {
			subscribe(update_delta_topic);
		} else {
			subscribe(get_accepted_desired_cfg_topic);
			//subscribe(get_rejected_topic);
		}

		initial_connection = true;
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

	err = getaddrinfo(broker_name, NULL, &hints, &result);
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
			broker4->sin_port = htons(CONFIG_MQTT_AWS_BROKER_PORT);

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
	client->client_id.utf8 = (u8_t *)client_id_imei;
	client->client_id.size = strlen(client_id_imei);
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
	tls_config->hostname = broker_name;
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

int publish_data(bool op)
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

	if (op) {
		transmit_data.buf = "";
		transmit_data.len = strlen(transmit_data.buf);
		transmit_data.topic = get_topic;
	} else {
		err = encode_message(&transmit_data, &sync_data);
		if (err != 0) {
			printk("ERROR when enconding message: %d\n", err);
		}
		transmit_data.topic = update_topic;
	}

	led_notif_publish();

	data_publish(&client, MQTT_QOS_1_AT_LEAST_ONCE, transmit_data.buf,
		     transmit_data.len, transmit_data.topic);

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
