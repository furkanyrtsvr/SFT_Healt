#include "ble_health.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <bluetooth/services/nus.h>

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)
#define BLE_LINE_MAX 128
#define BLE_CHUNK_MAX 20

static struct bt_conn *current_conn;
static atomic_t sync_requested;
static atomic_t time_update_requested;
static atomic_t profile_update_requested;
static atomic_t profile_delete_requested;
static atomic_t profile_select_requested;
static atomic_t session_end_requested;
static struct rtc_datetime requested_time;
static struct ble_profile_update requested_profile_update;
static int requested_profile_delete;
static int requested_profile_select;
static char rx_line[80];
static size_t rx_length;

static const struct bt_data advertising_data[] = {
	BT_DATA_BYTES(
		BT_DATA_FLAGS,
		BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR
	),
	BT_DATA(
		BT_DATA_NAME_COMPLETE,
		DEVICE_NAME,
		DEVICE_NAME_LEN
	),
};

static const struct bt_data scan_response[] = {
	BT_DATA_BYTES(
		BT_DATA_UUID128_ALL,
		BT_UUID_NUS_VAL
	),
};

static void start_advertising(void)
{
	(void)bt_le_adv_start(
		BT_LE_ADV_CONN_FAST_2,
		advertising_data,
		ARRAY_SIZE(advertising_data),
		scan_response,
		ARRAY_SIZE(scan_response)
	);
}

static void connected(
	struct bt_conn *conn,
	uint8_t error
)
{
	if (error != 0) {
		return;
	}

	if (current_conn != NULL) {
		bt_conn_unref(current_conn);
	}

	current_conn = bt_conn_ref(conn);
}

static void disconnected(
	struct bt_conn *conn,
	uint8_t reason
)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(reason);

	if (current_conn != NULL) {
		bt_conn_unref(current_conn);
		current_conn = NULL;
	}

	rx_length = 0;
	start_advertising();
}

BT_CONN_CB_DEFINE(connection_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static void process_command(void)
{
	rx_line[rx_length] = '\0';

	if (strcmp(rx_line, "SYNC") == 0 ||
	    strcmp(rx_line, "GET ALL") == 0) {
		atomic_set(&sync_requested, 1);
	} else if (strcmp(rx_line, "SESSION_END") == 0) {
		atomic_set(&session_end_requested, 1);
	} else {
		unsigned int year;
		unsigned int month;
		unsigned int day;
		unsigned int hour;
		unsigned int minute;
		unsigned int second;
		unsigned int weekday;

		if (sscanf(
			    rx_line,
			    "TIME|%u|%u|%u|%u|%u|%u|%u",
			    &year,
			    &month,
			    &day,
			    &hour,
			    &minute,
			    &second,
			    &weekday) == 7) {
			requested_time = (struct rtc_datetime) {
				.year = (uint16_t)year,
				.month = (uint8_t)month,
				.day = (uint8_t)day,
				.hour = (uint8_t)hour,
				.minute = (uint8_t)minute,
				.second = (uint8_t)second,
				.weekday = (uint8_t)weekday,
				.valid = true,
			};
			atomic_set(&time_update_requested, 1);
			goto done;
		}

		unsigned int index;
		unsigned int age;
		unsigned int height;
		unsigned int weight;
		char name[PROFILE_NAME_MAX + 1] = { 0 };
		char gender;

		if (sscanf(rx_line,
			   "PROFILE_SET|%u|%8[^|]|%u|%c|%u|%u",
			   &index, name, &age, &gender, &height, &weight) == 6 &&
		    index < PROFILE_MAX) {
			memset(&requested_profile_update, 0,
			       sizeof(requested_profile_update));
			requested_profile_update.index = (int)index;
			strncpy(requested_profile_update.profile.name, name,
				PROFILE_NAME_MAX);
			requested_profile_update.profile.age =
				(uint8_t)CLAMP(age, 1U, 120U);
			requested_profile_update.profile.gender =
				gender == 'W' ? PROFILE_WOMAN : PROFILE_MAN;
			requested_profile_update.profile.height_cm =
				(uint16_t)CLAMP(height,
					PROFILE_MIN_HEIGHT_CM,
					PROFILE_MAX_HEIGHT_CM);
			requested_profile_update.profile.weight_kg =
				(uint16_t)CLAMP(weight,
					PROFILE_MIN_WEIGHT_KG,
					PROFILE_MAX_WEIGHT_KG);
			atomic_set(&profile_update_requested, 1);
		} else if (sscanf(rx_line, "PROFILE_DELETE|%u", &index) == 1 &&
			   index < PROFILE_MAX) {
			requested_profile_delete = (int)index;
			atomic_set(&profile_delete_requested, 1);
		} else if (sscanf(rx_line, "PROFILE_SELECT|%u", &index) == 1 &&
			   index < PROFILE_MAX) {
			requested_profile_select = (int)index;
			atomic_set(&profile_select_requested, 1);
		}
	}

	done:
	rx_length = 0;
}

static void received(
	struct bt_conn *conn,
	const uint8_t *const data,
	uint16_t length
)
{
	ARG_UNUSED(conn);

	for (uint16_t i = 0; i < length; i++) {
		char byte = (char)data[i];

		if (byte == '\r' || byte == '\n') {
			if (rx_length > 0) {
				process_command();
			}
			continue;
		}

		if (rx_length + 1 < sizeof(rx_line)) {
			rx_line[rx_length++] = byte;
		} else {
			rx_length = 0;
		}
	}
}

static struct bt_nus_cb nus_callbacks = {
	.received = received,
};

static void send_line(const char *line)
{
	if (current_conn == NULL || line == NULL) {
		return;
	}

	size_t length = strlen(line);

	for (size_t offset = 0; offset < length;) {
		size_t chunk = MIN(
			length - offset,
			(size_t)BLE_CHUNK_MAX
		);

		int result = bt_nus_send(
			current_conn,
			(const uint8_t *)&line[offset],
			(uint16_t)chunk
		);

		if (result == -ENOMEM || result == -EAGAIN) {
			k_sleep(K_MSEC(12));
			continue;
		}

		if (result != 0) {
			return;
		}

		offset += chunk;
		k_sleep(K_MSEC(4));
	}
}

int ble_health_init(void)
{
	atomic_clear(&sync_requested);
	atomic_clear(&time_update_requested);
	atomic_clear(&profile_update_requested);
	atomic_clear(&profile_delete_requested);
	atomic_clear(&profile_select_requested);
	atomic_clear(&session_end_requested);
	rx_length = 0;

	int result = bt_enable(NULL);

	if (result != 0) {
		return result;
	}

	result = bt_nus_init(&nus_callbacks);

	if (result != 0) {
		return result;
	}

	start_advertising();
	return 0;
}

bool ble_health_is_connected(void)
{
	return current_conn != NULL;
}

bool ble_health_take_sync_request(void)
{
	return atomic_cas(&sync_requested, 1, 0);
}

bool ble_health_take_time_update(struct rtc_datetime *time)
{
	if (time == NULL ||
	    !atomic_cas(&time_update_requested, 1, 0)) {
		return false;
	}

	*time = requested_time;
	return true;
}

bool ble_health_take_profile_update(struct ble_profile_update *update)
{
	if (update == NULL ||
	    !atomic_cas(&profile_update_requested, 1, 0)) {
		return false;
	}
	*update = requested_profile_update;
	return true;
}

bool ble_health_take_profile_delete(int *index)
{
	if (index == NULL ||
	    !atomic_cas(&profile_delete_requested, 1, 0)) {
		return false;
	}
	*index = requested_profile_delete;
	return true;
}

bool ble_health_take_profile_select(int *index)
{
	if (index == NULL ||
	    !atomic_cas(&profile_select_requested, 1, 0)) {
		return false;
	}
	*index = requested_profile_select;
	return true;
}

bool ble_health_take_session_end(void)
{
	return atomic_cas(&session_end_requested, 1, 0);
}

void ble_health_send_profile(
	int index,
	const struct user_profile *profile
)
{
	if (profile == NULL) {
		return;
	}

	char line[BLE_LINE_MAX];

	(void)snprintf(
		line,
		sizeof(line),
		"P|%d|%s|%u|%c|%u|%u|%u|%ld|%ld|%u|%ld|%ld|%u\n",
		index,
		profile->name,
		profile->age,
		profile->gender == PROFILE_MAN ? 'M' : 'W',
		profile->height_cm,
		profile->weight_kg,
		profile->last_bpm,
		(long)profile->last_mep_kpa_x100,
		(long)profile->last_mep_predicted_x100,
		profile->last_mep_percent,
		(long)profile->last_mip_kpa_x100,
		(long)profile->last_mip_predicted_x100,
		profile->last_mip_percent
	);

	send_line(line);
}

void ble_health_send_heart(
	int profile_index,
	uint16_t bpm
)
{
	char line[48];

	(void)snprintf(
		line,
		sizeof(line),
		"M|HR|%d|%u\n",
		profile_index,
		bpm
	);

	send_line(line);
}

void ble_health_send_mep(
	int profile_index,
	int32_t measured_kpa_x100,
	int32_t predicted_kpa_x100,
	uint16_t percent
)
{
	char line[64];

	(void)snprintf(
		line,
		sizeof(line),
		"M|MEP|%d|%ld|%ld|%u\n",
		profile_index,
		(long)measured_kpa_x100,
		(long)predicted_kpa_x100,
		percent
	);

	send_line(line);
}

void ble_health_send_mip(
	int profile_index,
	int32_t measured_kpa_x100,
	int32_t predicted_kpa_x100,
	uint16_t percent
)
{
	char line[64];

	(void)snprintf(
		line,
		sizeof(line),
		"M|MIP|%d|%ld|%ld|%u\n",
		profile_index,
		(long)measured_kpa_x100,
		(long)predicted_kpa_x100,
		percent
	);

	send_line(line);
}

void ble_health_send_environment(
	int16_t temperature_c_x10,
	uint16_t humidity_percent_x10,
	const char *quality
)
{
	char line[64];

	(void)snprintf(
		line,
		sizeof(line),
		"M|ENV|%d|%u|%s\n",
		temperature_c_x10,
		humidity_percent_x10,
		quality != NULL ? quality : "UNKNOWN"
	);

	send_line(line);
}

void ble_health_send_record(const struct measurement_record *record)
{
	if (record == NULL || !record->valid || !record->time.valid) {
		return;
	}

	const char *type = record->type == MEASUREMENT_HEART_RATE
		? "HR"
		: record->type == MEASUREMENT_MEP ? "MEP" : "MIP";
	char line[BLE_LINE_MAX];

	(void)snprintf(
		line,
		sizeof(line),
		"R|%lu|%s|%u|%ld|%ld|%u|%u|%u|%u|%u|%u|%u\n",
		(unsigned long)record->sequence,
		type,
		record->profile_index,
		(long)record->value_x100,
		(long)record->predicted_x100,
		record->percent,
		record->time.year,
		record->time.month,
		record->time.day,
		record->time.hour,
		record->time.minute,
		record->time.second
	);

	send_line(line);
}

void ble_health_send_sync_done(void)
{
	send_line("DONE\n");
}

void ble_health_send_status(const char *status)
{
	char line[48];
	(void)snprintf(line, sizeof(line), "STATUS|%s\n",
		status != NULL ? status : "OK");
	send_line(line);
}
