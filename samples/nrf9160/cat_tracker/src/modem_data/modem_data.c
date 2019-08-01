#include <modem_data.h>
#include <stdio.h>
#include <zephyr.h>
#include <kernel.h>
#include <at_cmd.h>
#include <stdlib.h>
#include <string.h>

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