#include <modem_data.h>
#include <stdio.h>
#include <zephyr.h>
#include <kernel.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <nrf_socket.h>

#define TIME_LEN 28
#define BAT_LEN 28

s64_t update_time;

s64_t epoch;

s64_t multiplier = 1000;

int get_time_info(char *datetime_string, int min, int max)
{
	char buf[50];

	for (int i = min; i < max + 1; i++) {
		buf[i - min] = datetime_string[i];
	}

	return atoi(buf);
}

int request_battery_status()
{
	int err;
	char battery_level[4];

	int at_socket_fd;
	int bytes_written;
	int bytes_read;
	char bat_buf[BAT_LEN + 1];

	at_socket_fd = nrf_socket(NRF_AF_LTE, 0, NRF_PROTO_AT);
	__ASSERT_NO_MSG(at_socket_fd >= 0);

	bytes_written = nrf_write(at_socket_fd, "AT%XVBAT", 8);
	__ASSERT_NO_MSG(bytes_written == 8);

	bytes_read = nrf_read(at_socket_fd, bat_buf, BAT_LEN);
	__ASSERT_NO_MSG(bytes_read == BAT_LEN);
	bat_buf[BAT_LEN] = 0;

	for (int i = 8; i < 12; i++) {
		battery_level[i - 8] = bat_buf[i];
	}

	err = nrf_close(at_socket_fd);
	__ASSERT_NO_MSG(err == 0);

	return atoi(battery_level);
}

int modem_time_get()
{
	int err;
	int at_socket_fd;
	int bytes_written;
	int bytes_read;
	char modem_ts[TIME_LEN + 1];
	char modem_ts_buf[TIME_LEN + 1];
	struct tm info;

	at_socket_fd = nrf_socket(NRF_AF_LTE, 0, NRF_PROTO_AT);
	__ASSERT_NO_MSG(at_socket_fd >= 0);

	bytes_written = nrf_write(at_socket_fd, "AT+CCLK?", 8);
	__ASSERT_NO_MSG(bytes_written == 8);

	bytes_read = nrf_read(at_socket_fd, modem_ts_buf, TIME_LEN);
	__ASSERT_NO_MSG(bytes_read == TIME_LEN);
	modem_ts_buf[TIME_LEN] = 0;

	for (int i = 8; i < 28; i++) {
		modem_ts[i - 8] = modem_ts_buf[i];
	}

	err = nrf_close(at_socket_fd);
	__ASSERT_NO_MSG(err == 0);

	info.tm_year = get_time_info(modem_ts, 0, 1) + 2000 - 1900;
	info.tm_mon = get_time_info(modem_ts, 3, 4);
	info.tm_mday = get_time_info(modem_ts, 6, 7);
	info.tm_hour = get_time_info(modem_ts, 9, 10);
	info.tm_min = get_time_info(modem_ts, 12, 13);
	info.tm_sec = get_time_info(modem_ts, 15, 16);

	epoch = mktime(&info) * multiplier;

	printk(" This is the EPOCH value: %llu\n", epoch);

	update_time = k_uptime_get();

	return 0;
}

void set_current_time(struct gps_data gps_data)
{
	struct tm info;

	info.tm_year = gps_data.pvt.datetime.year - 1900;
	info.tm_mon = gps_data.pvt.datetime.month;
	info.tm_mday = gps_data.pvt.datetime.day;
	info.tm_hour = gps_data.pvt.datetime.hour;
	info.tm_min = gps_data.pvt.datetime.minute;
	info.tm_sec = gps_data.pvt.datetime.seconds;

	epoch = mktime(&info) * multiplier;

	update_time = k_uptime_get();
}

long int get_current_time()
{
	return epoch + k_uptime_get() - update_time;
}
