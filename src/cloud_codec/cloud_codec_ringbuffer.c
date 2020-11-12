#include <cloud_codec.h>
#include <zephyr.h>
#include <cJSON.h>
#include <cJSON_os.h>
#include <math.h>

#include <logging/log.h>
LOG_MODULE_REGISTER(cloud_codec_ringbuffer, CONFIG_CAT_TRACKER_LOG_LEVEL);

#define ACCELEROMETER_TOTAL_AXIS 3

void cloud_codec_populate_sensor_buffer(struct cloud_data_sensors *sensor_buffer,
					struct cloud_data_sensors *new_sensor_data,
					int *head_sensor_buf)
{
	if (!new_sensor_data->queued) {
		return;
	}

	/* Go to start of buffer if end is reached. */
	*head_sensor_buf += 1;
	if (*head_sensor_buf == CONFIG_SENSOR_BUFFER_MAX) {
		*head_sensor_buf = 0;
	}

	sensor_buffer[*head_sensor_buf] = *new_sensor_data;

	LOG_DBG("Entry: %d of %d in sensor buffer filled", *head_sensor_buf,
		CONFIG_SENSOR_BUFFER_MAX - 1);
}

void cloud_codec_populate_ui_buffer(struct cloud_data_ui *ui_buffer,
				   struct cloud_data_ui *new_ui_data,
				   int *head_ui_buf)
{

	if (!new_ui_data->queued) {
		return;
	}

	/* Go to start of buffer if end is reached. */
	*head_ui_buf += 1;
	if (*head_ui_buf == CONFIG_UI_BUFFER_MAX) {
		*head_ui_buf = 0;
	}

	ui_buffer[*head_ui_buf] = *new_ui_data;

	LOG_DBG("Entry: %d of %d in UI buffer filled", *head_ui_buf,
		CONFIG_UI_BUFFER_MAX - 1);
}

void cloud_codec_populate_accel_buffer(struct cloud_data_accelerometer *mov_buf,
				      struct cloud_data_accelerometer *new_accel_data,
				      int *head_mov_buf)
{
	double buf_lowest_val = 0;
	double buf_highest_val = 0;
	double new_entry_highest_val = 0;
	int64_t newest_time = 0;

	if (!new_accel_data->queued) {
		return;
	}

	/** Populate the next available unqueued entry. */
	for (int k = 0; k < CONFIG_ACCEL_BUFFER_MAX; k++) {
		if (!mov_buf[k].queued) {
			*head_mov_buf = k;
			goto populate_buffer;
		}
	}

	/* Set the initial value of buf_lowest_val to the highest in the
	 * first accelerometer buffer entry.
	 */
	for (int j = 0; j < CONFIG_ACCEL_BUFFER_MAX; j++) {
		for (int m = 0; m < ACCELEROMETER_TOTAL_AXIS; m++) {
			if (buf_lowest_val < fabs(mov_buf[j].values[m])) {
				buf_lowest_val = fabs(mov_buf[j].values[m]);
			}
		}
	}

	/* Find the lowest of the highest values in the current accelerometer
	 * buffer.
	 */
	for (int j = 0; j < CONFIG_ACCEL_BUFFER_MAX; j++) {
		for (int m = 0; m < ACCELEROMETER_TOTAL_AXIS; m++) {
			if (buf_highest_val < fabs(mov_buf[j].values[m])) {
				buf_highest_val = fabs(mov_buf[j].values[m]);
			}
		}

		if (buf_highest_val < buf_lowest_val) {
			buf_lowest_val = buf_highest_val;
			*head_mov_buf = j;
		}

		buf_highest_val = 0;
	}

	/* Find the highest value in the new accelerometer buffer entry. */
	for (int n = 0; n < ACCELEROMETER_TOTAL_AXIS; n++) {
		if (new_entry_highest_val < fabs(new_accel_data->values[n])) {
			new_entry_highest_val = fabs(new_accel_data->values[n]);
		}
	}

	/* If the lowest of the highest accelerometer values in the current
	 * buffer is higher than the new acceleromter data entry, do nothing.
	 * If infact a value in the new accelerometer reading is higher,
	 * replace it with the lowest of the highest values in the current
	 * acceleromter buffer.
	 */
	if (buf_lowest_val > new_entry_highest_val) {
		goto find_newest_entry;
	}

populate_buffer:

	mov_buf[*head_mov_buf] = *new_accel_data;

	LOG_DBG("Entry: %d of %d in accelerometer buffer filled",
		*head_mov_buf, CONFIG_ACCEL_BUFFER_MAX - 1);

find_newest_entry:
	/* Always point head of buffer to the newest sampled value. */
	for (int i = 0; i < CONFIG_ACCEL_BUFFER_MAX; i++) {
		if (newest_time < mov_buf[i].ts && mov_buf[i].queued) {
			newest_time = mov_buf[i].ts;
			*head_mov_buf = i;
		}
	}
}

void cloud_codec_populate_bat_buffer(struct cloud_data_battery *bat_buffer,
				     struct cloud_data_battery *new_bat_data,
				     int *head_bat_buf)
{
	if (!new_bat_data->queued) {
		return;
	}

	/* Go to start of buffer if end is reached. */
	*head_bat_buf += 1;
	if (*head_bat_buf == CONFIG_BAT_BUFFER_MAX) {
		*head_bat_buf = 0;
	}

	bat_buffer[*head_bat_buf] = *new_bat_data;

	LOG_DBG("Entry: %d of %d in battery buffer filled", *head_bat_buf,
		CONFIG_BAT_BUFFER_MAX - 1);
}

void cloud_codec_populate_gps_buffer(struct cloud_data_gps *gps_buffer,
				    struct cloud_data_gps *new_gps_data,
				    int *head_gps_buf)
{
	if (!new_gps_data->queued) {
		return;
	}

	/* Go to start of buffer if end is reached. */
	*head_gps_buf += 1;
	if (*head_gps_buf == CONFIG_GPS_BUFFER_MAX) {
		*head_gps_buf = 0;
	}

	gps_buffer[*head_gps_buf] = *new_gps_data;

	LOG_DBG("Entry: %d of %d in GPS buffer filled", *head_gps_buf,
		CONFIG_GPS_BUFFER_MAX - 1);
}

void cloud_codec_populate_modem_buffer(struct cloud_data_modem *modem_buffer,
				      struct cloud_data_modem *new_modem_data,
				      int *head_modem_buf)
{
	if (!new_modem_data->queued) {
		return;
	}

	/* Go to start of buffer if end is reached. */
	*head_modem_buf += 1;
	if (*head_modem_buf == CONFIG_MODEM_BUFFER_MAX) {
		*head_modem_buf = 0;
	}

	modem_buffer[*head_modem_buf] = *new_modem_data;

	LOG_DBG("Entry: %d of %d in modem buffer filled", *head_modem_buf,
		CONFIG_MODEM_BUFFER_MAX - 1);
}
