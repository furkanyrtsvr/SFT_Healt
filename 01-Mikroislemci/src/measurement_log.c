#include "measurement_log.h"
#include "profile_store.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/settings/settings.h>

#define LOG_MAGIC 0x5346544CUL
#define LOG_VERSION 1
#define LOG_SETTINGS_KEY "sft/records"

struct measurement_database {
	uint32_t magic;
	uint8_t version;
	uint8_t count;
	uint8_t head;
	uint8_t reserved;
	uint32_t next_sequence;
	struct measurement_record records[MEASUREMENT_LOG_CAPACITY];
};

struct direct_value {
	void *destination;
	size_t capacity;
	size_t loaded_length;
	bool fetched;
};

static struct measurement_database database;

static int direct_loader(
	const char *name,
	size_t len,
	settings_read_cb read_cb,
	void *cb_arg,
	void *param
)
{
	struct direct_value *value = param;

	if (name[0] != '\0' || len > value->capacity) {
		return name[0] == '\0' ? -E2BIG : 0;
	}

	int result = read_cb(cb_arg, value->destination, len);

	if (result >= 0) {
		value->fetched = true;
		value->loaded_length = len;
		return 0;
	}

	return result;
}

static void create_empty_database(void)
{
	memset(&database, 0, sizeof(database));
	database.magic = LOG_MAGIC;
	database.version = LOG_VERSION;
	database.next_sequence = 1;
}

static int save_database(void)
{
	return settings_save_one(
		LOG_SETTINGS_KEY,
		&database,
		sizeof(database)
	);
}

int measurement_log_init(void)
{
	create_empty_database();
	int result = settings_subsys_init();

	if (result != 0) {
		return result;
	}

	struct measurement_database loaded;
	memset(&loaded, 0, sizeof(loaded));
	struct direct_value value = {
		.destination = &loaded,
		.capacity = sizeof(loaded),
	};

	result = settings_load_subtree_direct(
		LOG_SETTINGS_KEY,
		direct_loader,
		&value
	);
	if (result != 0) {
		return result;
	}

	if (value.fetched &&
	    value.loaded_length == sizeof(loaded) &&
	    loaded.magic == LOG_MAGIC &&
	    loaded.version == LOG_VERSION &&
	    loaded.count <= MEASUREMENT_LOG_CAPACITY &&
	    loaded.head < MEASUREMENT_LOG_CAPACITY) {
		database = loaded;
		if (database.next_sequence == 0) {
			database.next_sequence = 1;
		}
	}

	return 0;
}

int measurement_log_add(
	enum measurement_type type,
	uint8_t profile_index,
	int32_t value_x100,
	int32_t predicted_x100,
	uint16_t percent,
	const struct rtc_datetime *time,
	struct measurement_record *saved
)
{
	if ((type != MEASUREMENT_HEART_RATE &&
	     type != MEASUREMENT_MEP &&
	     type != MEASUREMENT_MIP) ||
	    time == NULL || !time->valid || value_x100 <= 0) {
		return -EINVAL;
	}

	struct measurement_record record = {
		.sequence = database.next_sequence++,
		.valid = 1,
		.type = (uint8_t)type,
		.profile_index = profile_index,
		.value_x100 = value_x100,
		.predicted_x100 = predicted_x100,
		.percent = percent,
		.time = *time,
	};

	database.records[database.head] = record;
	database.head = (uint8_t)((database.head + 1U) %
		MEASUREMENT_LOG_CAPACITY);
	if (database.count < MEASUREMENT_LOG_CAPACITY) {
		database.count++;
	}

	int result = save_database();

	if (result == 0 && saved != NULL) {
		*saved = record;
	}

	return result;
}

size_t measurement_log_count(int profile_index)
{
	size_t count = 0;

	for (size_t offset = 0; offset < database.count; offset++) {
		size_t index = (database.head + MEASUREMENT_LOG_CAPACITY - 1U -
			offset) % MEASUREMENT_LOG_CAPACITY;
		const struct measurement_record *record = &database.records[index];

		if (record->valid &&
		    (profile_index < 0 || record->profile_index == profile_index)) {
			count++;
		}
	}

	return count;
}

int measurement_log_get_newest(
	int profile_index,
	size_t newest_index,
	struct measurement_record *record
)
{
	if (record == NULL) {
		return -EINVAL;
	}

	size_t found = 0;

	for (size_t offset = 0; offset < database.count; offset++) {
		size_t index = (database.head + MEASUREMENT_LOG_CAPACITY - 1U -
			offset) % MEASUREMENT_LOG_CAPACITY;
		const struct measurement_record *candidate = &database.records[index];

		if (!candidate->valid ||
		    (profile_index >= 0 && candidate->profile_index != profile_index)) {
			continue;
		}

		if (found == newest_index) {
			*record = *candidate;
			return 0;
		}

		found++;
	}

	return -ENOENT;
}

int measurement_log_delete_profile(int profile_index)
{
	if (profile_index < 0 || profile_index >= PROFILE_MAX) {
		return -EINVAL;
	}

	bool changed = false;
	for (size_t i = 0; i < MEASUREMENT_LOG_CAPACITY; i++) {
		if (database.records[i].valid &&
		    database.records[i].profile_index == profile_index) {
			database.records[i].valid = 0;
			changed = true;
		}
	}

	return changed ? save_database() : 0;
}
