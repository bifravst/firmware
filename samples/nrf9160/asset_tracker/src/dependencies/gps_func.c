// #include <zephyr.h>
// #include <stdio.h>

#include <gps_func.h>

//GPS help functions

#define AT_XSYSTEMMODE "AT\%XSYSTEMMODE=0,0,1,0"
#define AT_CFUN        "AT+CFUN=1"

#ifdef CONFIG_BOARD_NRF9160_PCA10090NS
#define AT_MAGPIO      "AT\%XMAGPIO=1,0,0,1,1,1574,1577"
#endif

static const char     at_commands[][31]  = {
				AT_XSYSTEMMODE,
#ifdef CONFIG_BOARD_NRF9160_PCA10090NS
				AT_MAGPIO,
#endif
				AT_CFUN
			};

static int            fd;

static int gps_init = false;

static char           nmea_strings[10][NRF_GNSS_NMEA_MAX_LEN];
static u32_t          nmea_string_cnt;

static bool           got_first_fix;
static bool           update_terminal;
static u64_t          fix_timestamp;
static nrf_gnss_data_frame_t last_fix;

char gps_dummy_string[] = "$GPGGA,181908.00,3404.7041778,N,07044.3966270,W,4,13,1.00,495.144,M,29.200,M,0.10,0000*40"; //comment out for real data

int enable_gps(void)
{
	int  at_sock;
	int  bytes_sent;
	int  bytes_received;
	char buf[2];

	at_sock = socket(AF_LTE, 0, NPROTO_AT);
	if (at_sock < 0) {
		return -1;
	}

	for (int i = 0; i < ARRAY_SIZE(at_commands); i++) {
		bytes_sent = send(at_sock, at_commands[i],
				  strlen(at_commands[i]), 0);

		if (bytes_sent < 0) {
			close(at_sock);
			return -1;
		}

		do {
			bytes_received = recv(at_sock, buf, 2, 0);
		} while (bytes_received == 0);

		if (memcmp(buf, "OK", 2) != 0) {
			close(at_sock);
			return -1;
		}
	}

	close(at_sock);

	return 0;
}

int init_app(void)
{
	u16_t fix_retry     = 0;
	u16_t fix_interval  = 1;
	u16_t nmea_mask     = NRF_CONFIG_NMEA_GSV_MASK |
			      NRF_CONFIG_NMEA_GSA_MASK |
			      NRF_CONFIG_NMEA_GLL_MASK |
			      NRF_CONFIG_NMEA_GGA_MASK |
			      NRF_CONFIG_NMEA_RMC_MASK;
	int   retval;

	if (enable_gps() != 0) {
		printk("Failed to enable GPS, GPS already initialized or busy\n");
		return -1;
	}

	fd = nrf_socket(NRF_AF_LOCAL, NRF_SOCK_DGRAM, NRF_PROTO_GNSS);

	if (fd >= 0) {
		printk("Socket created\n");
	} else {
		printk("Could not init socket (err: %d)\n", fd);
		return -1;
	}

	retval = nrf_setsockopt(fd,
				NRF_SOL_GNSS,
				NRF_SO_GNSS_FIX_RETRY,
				&fix_retry,
				sizeof(uint16_t));

	if (retval != 0) {
		printk("Failed to set fix retry value\n");
		return -1;
	}

	retval = nrf_setsockopt(fd,
				NRF_SOL_GNSS,
				NRF_SO_GNSS_FIX_INTERVAL,
				&fix_interval,
				sizeof(uint16_t));

	if (retval != 0) {
		printk("Failed to set fix interval value\n");
		return -1;
	}

	retval = nrf_setsockopt(fd,
				NRF_SOL_GNSS,
				NRF_SO_GNSS_NMEA_MASK,
				&nmea_mask,
				sizeof(uint16_t));

	if (retval != 0) {
		printk("Failed to set nmea mask\n");
		return -1;
	}

	retval = nrf_setsockopt(fd,
				NRF_SOL_GNSS,
				NRF_SO_GNSS_START,
				NULL,
				0);

	if (retval != 0) {
		printk("Failed to start GPS\n");
		return -1;
	}

	return 0;
}

void print_pvt_data(nrf_gnss_data_frame_t *pvt_data)
{
	printf("Longitude:  %f\n", pvt_data->pvt.longitude);
	printf("Latitude:   %f\n", pvt_data->pvt.latitude);
	printf("Altitude:   %f\n", pvt_data->pvt.altitude);
	printf("Speed:      %f\n", pvt_data->pvt.speed);
	printf("Heading:    %f\n", pvt_data->pvt.heading);
	printk("Date:       %02u-%02u-%02u\n", pvt_data->pvt.datetime.day,
					       pvt_data->pvt.datetime.month,
					       pvt_data->pvt.datetime.year);
	printk("Time (UTC): %02u:%02u:%02u\n", pvt_data->pvt.datetime.hour,
					       pvt_data->pvt.datetime.minute,
					      pvt_data->pvt.datetime.seconds);
}

int process_gps_data(nrf_gnss_data_frame_t *gps_data)
{
	int retval;

	retval = nrf_recv(fd, gps_data, sizeof(nrf_gnss_data_frame_t), NRF_MSG_DONTWAIT);

	if (retval > 0) {

		switch (gps_data->data_id) {
		case NRF_GNSS_PVT_DATA_ID:

			if ((gps_data->pvt.flags &
				NRF_GNSS_PVT_FLAG_FIX_VALID_BIT)
				== NRF_GNSS_PVT_FLAG_FIX_VALID_BIT) {

				if (!got_first_fix) {
					got_first_fix = true;
				}

				fix_timestamp = k_uptime_get();
				memcpy(&last_fix, gps_data, sizeof(nrf_gnss_data_frame_t));

				nmea_string_cnt = 0;
				update_terminal = true;
			}
			break;

		case NRF_GNSS_NMEA_DATA_ID:
			if (nmea_string_cnt < 10) {
				memcpy(nmea_strings[nmea_string_cnt++],
				       gps_data->nmea,
				       retval);
			}
			break;

		default:
			break;
		}
	}

	return retval;
}

void print_nmea_data(void)
{
	printk("NMEA strings:\n");

	for (int i = 0; i < nmea_string_cnt; ++i) {
		printk("%s\n", nmea_strings[i]);
	}
}

u8_t *get_gps_data(void)
{
    nrf_gnss_data_frame_t gps_data;

	if (gps_init == false) {
		init_app();
		gps_init = true;
		printk("initialize gps en gang\n");
	}
	
	while (1) {

		do {
			/* Loop until we don't have more
			 * data to read
			 */
		} while (process_gps_data(&gps_data) > 0);
			got_first_fix = true; //comment out for use of gps data

			printk("Got gps fix\n");

			if (((k_uptime_get() - fix_timestamp) >= 1) &&
		     (got_first_fix)) {

				memcpy(nmea_strings, gps_dummy_string, sizeof(gps_dummy_string)); //comment out for use of gps data

				return nmea_strings[10];
			}
			k_sleep(K_MSEC(500));
	}
}