#include <modem_data.h>
#include <stdio.h>
#include <zephyr.h>
#include <kernel.h>
#include <at_cmd.h>
#include <stdlib.h>
#include <string.h>

#define AT_BATSTAT "AT%XVBAT"
#define AT_IMEI "AT+CGSN"

static const char cmd[] = AT_BATSTAT;
static const char cmd_imei[] = AT_IMEI;

static enum at_cmd_state state;

static char buf_imei[50];
size_t buf_len_imei = sizeof(buf_imei);

void at_cmd_handler(char *state)
{
	switch (*state) {
	case AT_CMD_OK:
		printk("AT_CMD_OK\n");
		break;
	case AT_CMD_ERROR_CMS:
		printk("AT_CMD_ERROR_CMS\n");
		break;
	case AT_CMD_ERROR_CME:
		printk("AT_CMD_ERROR_CME\n");
		break;
	case AT_CMD_NOTIFICATION:
		printk("AT_CMD_NOTIFICATION\n");
		break;
	default:
		printk("MODEM UNDEF EVENT\n");
		break;
	}
}

int request_battery_status()
{
	int err;
	char buf[50];
	size_t buf_len = sizeof(buf);
	char battery_level[4];
	int battery_percentage;

	err = at_cmd_write(cmd, buf, buf_len, &state);
	if (err != 0) {
		printk("Error in response from modem\n");
	}

	/*Get voltage level from modem response */
	for (int i = 8; i < 12; i++) {
		battery_level[i - 8] = buf[i];
	}

	battery_percentage = atoi(battery_level);

	return battery_percentage;
}

char *request_init_modem_data()
{
	int err;
	// char buf_imei[50];
	// size_t buf_len = sizeof(buf);

	err = at_cmd_write(cmd_imei, buf_imei, buf_len_imei, &state);
	if (err != 0) {
		printk("Error in response from modem\n");
	}

	return buf_imei;
}