#include <batstat.h>
#include <stdio.h>
#include <zephyr.h>
#include <kernel.h>
#include <at_cmd.h>
#include <stdlib.h>
#include <string.h>

#define AT_BATSTAT	"AT%XVBAT"
#define BAT_ROOF	5500
#define BAT_FLOOR	3000

static const char     cmd[] = AT_BATSTAT;
static enum at_cmd_state state;
static int battery_percentage;

void at_cmd_handler(char *state) {

	switch(*state) {
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

int convert_to_percentage(int bat_voltage_roof, int bat_voltage_floor, int current_voltage_level) {
	return (((current_voltage_level - bat_voltage_floor) * 100) / (bat_voltage_roof - bat_voltage_floor));
}

void request_battery_status(char *gps_dummy_string) {
	int err;
	char buf[100]; //magic
	char temp[2];
	size_t buf_len = sizeof(buf); //magic
	char battery_level[4]; //magic
	
	
	at_cmd_set_notification_handler(at_cmd_handler);

	err = at_cmd_write(cmd, buf, buf_len, &state);
	if (err != 0) {
		printk("Error in response from modem\n");
	}

	/*Get voltage level from modem response */
	for (int i = 8; i < 12; i++) {
		battery_level[i - 8] = buf[i];
	}

	battery_percentage = convert_to_percentage(BAT_ROOF, BAT_FLOOR, atoi(battery_level));
	
	sprintf(temp, "%d", battery_percentage);

	strcat(gps_dummy_string, temp);

	//printk("battery addition%s\n", gps_dummy_string);
}