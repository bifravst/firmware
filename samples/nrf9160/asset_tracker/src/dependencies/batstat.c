#include <batstat.h>
#include <stdio.h>
#include <zephyr.h>
#include <kernel.h>
#include <at_cmd.h>
#include <stdlib.h>

#define AT_BATSTAT	"AT%XVBAT"
#define BAT_ROOF	5500
#define BAT_FLOOR	3000

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

int convert_to_percentage(int bat_voltage_roof, int bat_voltage_floor, int current_voltage_level) {
	return (((current_voltage_level - bat_voltage_floor) * 100) / (bat_voltage_roof - bat_voltage_floor));
}

void request_battery_status() {
	int err;
	int battery_percentage;

	at_cmd_set_notification_handler(at_cmd_handler);

	err = at_cmd_write(cmd, buf, buf_len, &state);
	if (err != 0) {
		printk("Error in response from modem\n");
	}

	char battery_level[4];
	//char battery_percentage_string[4];

	for (int i = 8; i < 12; i++) { //Voltage level output from modem
		battery_level[i - 8] = buf[i];
	}

	battery_percentage = convert_to_percentage(BAT_ROOF, BAT_FLOOR, atoi(battery_level));

	printk("%d\n", battery_percentage);

	//sprintf(battery_percentage_string, "%d", battery_percentage);

}