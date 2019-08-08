#include <modem_data.h>
#include <stdio.h>
#include <zephyr.h>
#include <kernel.h>
#include <at_cmd.h>
#include <stdlib.h>
#include <string.h>
#include <nrf_socket.h>

#define AT_BATSTAT "AT%XVBAT"

static const char cmd[] = AT_BATSTAT;

static enum at_cmd_state state;

/*This module needs rework, essentially a dummed down version of modem_info.c */
int request_battery_status()
{
	int err;
	char buf[50];
	char battery_level[4];

	err = at_cmd_write(cmd, buf, sizeof(buf), &state);
	if (err != 0) {
		printk("Error in response from modem\n");
	}

	/*Get voltage level from modem response */
	for (int i = 8; i < 12; i++) {
		battery_level[i - 8] = buf[i];
	}

	return atoi(battery_level);
}

// static char client_id_buf[50 + 1];

// #define IMEI_LEN 50

// static int client_id_get(char *id)
// {
// 	int at_socket_fd;
// 	int bytes_written;
// 	int bytes_read;
// 	char imei_buf[IMEI_LEN + 1];
// 	int ret;
// 	int id[IMEI_LEN + 1];

// 	at_socket_fd = nrf_socket(NRF_AF_LTE, 0, NRF_PROTO_AT);
// 	__ASSERT_NO_MSG(at_socket_fd >= 0);

// 	bytes_written = nrf_write(at_socket_fd, "AT+CCLK?", 8);
// 	__ASSERT_NO_MSG(bytes_written == 8);

// 	bytes_read = nrf_read(at_socket_fd, imei_buf, IMEI_LEN);
// 	__ASSERT_NO_MSG(bytes_read == IMEI_LEN);
// 	imei_buf[IMEI_LEN] = 0;

// 	//snprintf(id, AWS_CLOUD_CLIENT_ID_LEN + 1, "%s", imei_buf);

// 	ret = nrf_close(at_socket_fd);
// 	__ASSERT_NO_MSG(ret == 0);

// 	printk("timestamp: %s\n", )

// 	return 0;
// }