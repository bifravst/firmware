#include <batstat.h>
#include <stdio.h>
#include "zephyr.h"
#include "kernel.h"
#include "at_cmd.h"

#define AT_BATSTAT       "AT%XVBAT"

static const char     cmd[] = AT_BATSTAT;
enum at_cmd_state state;
char buf[100]; //these are arbitrary sizes
size_t buf_len = sizeof(buf); //these are arbitrary sizes

void at_cmd_handler(char *state) {

	switch(*state) {
		case AT_CMD_OK:
			printk("Response from modem1\n");
			break;
		case AT_CMD_ERROR_CMS:
			printk("Response from modem2\n");
			break;
		case AT_CMD_ERROR_CME:
			printk("Response from modem3\n");
			break;
		case AT_CMD_NOTIFICATION:
			printk("Response from modem4\n");
			break;
		default:
			printk("Response from modem5\n");
			break;
	}

}

void request_battery_status() {
	int err;

	at_cmd_set_notification_handler(at_cmd_handler);

	err = at_cmd_write(cmd, buf, buf_len, &state);
	if (err != 0) {
		printk("Error in response from modem\n");
	}

	printk("%s\n", buf);

}