#include "profile_store.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/settings/settings.h>

#define PROFILE_MAGIC 0x5052464CUL
#define PROFILE_VERSION 4

#define PROFILE_SETTINGS_KEY \
	"pulse/profiles"

/* Eski surumde bellekte bulunan profil yapisi. */
struct user_profile_v1 {
	uint8_t valid;
	char name[PROFILE_NAME_MAX + 1];
	uint8_t age;
	uint8_t gender;
	uint16_t last_bpm;
};

struct profile_database_v1 {
	uint32_t magic;
	uint8_t version;
	struct user_profile_v1 profiles[PROFILE_MAX];
};

/* Boy ve kilo eklenmeden once kullanilan ikinci surum. */
struct user_profile_v2 {
	uint8_t valid;
	char name[PROFILE_NAME_MAX + 1];
	uint8_t age;
	uint8_t gender;
	uint16_t last_bpm;
	int32_t last_mep_kpa_x100;
	int32_t last_mep_predicted_x100;
	uint16_t last_mep_percent;
	uint8_t last_mep_repeatable;
	uint8_t reserved[3];
};

struct profile_database_v2 {
	uint32_t magic;
	uint8_t version;
	struct user_profile_v2 profiles[PROFILE_MAX];
};

/* MIP kayitlari eklenmeden once kullanilan ucuncu surum. */
struct user_profile_v3 {
	uint8_t valid;
	char name[PROFILE_NAME_MAX + 1];
	uint8_t age;
	uint8_t gender;
	uint16_t height_cm;
	uint16_t weight_kg;
	uint16_t last_bpm;
	int32_t last_mep_kpa_x100;
	int32_t last_mep_predicted_x100;
	uint16_t last_mep_percent;
	uint8_t last_mep_repeatable;
	uint8_t last_mep_reference_valid;
	uint8_t reserved[2];
};

struct profile_database_v3 {
	uint32_t magic;
	uint8_t version;
	struct user_profile_v3 profiles[PROFILE_MAX];
};

struct profile_database {
	uint32_t magic;
	uint8_t version;

	struct user_profile profiles[
		PROFILE_MAX
	];
};

union profile_load_buffer {
	struct profile_database current;
	struct profile_database_v3 version_three;
	struct profile_database_v2 version_two;
	struct profile_database_v1 old;
};

struct direct_value {
	void *destination;
	size_t capacity;
	size_t loaded_length;
	bool fetched;
};

static struct profile_database database;

static int direct_loader(
	const char *name,
	size_t len,
	settings_read_cb read_cb,
	void *cb_arg,
	void *param
)
{
	struct direct_value *value = param;

	if (name[0] != '\0') {
		return 0;
	}

	if (len > value->capacity) {
		return -E2BIG;
	}

	int result = read_cb(
		cb_arg,
		value->destination,
		len
	);

	if (result >= 0) {
		value->fetched = true;
		value->loaded_length = len;
		return 0;
	}

	return result;
}

static int save_database(void)
{
	return settings_save_one(
		PROFILE_SETTINGS_KEY,
		&database,
		sizeof(database)
	);
}

static void create_empty_database(void)
{
	memset(
		&database,
		0,
		sizeof(database)
	);

	database.magic = PROFILE_MAGIC;
	database.version = PROFILE_VERSION;
}

static void set_default_body_data(
	struct user_profile *profile
)
{
	profile->height_cm =
		PROFILE_DEFAULT_HEIGHT_CM;
	profile->weight_kg =
		PROFILE_DEFAULT_WEIGHT_KG;
}

static int migrate_version_one(
	const struct profile_database_v1 *old
)
{
	create_empty_database();

	for (int i = 0;
	     i < PROFILE_MAX;
	     i++) {
		database.profiles[i].valid =
			old->profiles[i].valid;

		memcpy(
			database.profiles[i].name,
			old->profiles[i].name,
			sizeof(old->profiles[i].name)
		);

		database.profiles[i]
			.name[PROFILE_NAME_MAX] = '\0';

		database.profiles[i].age =
			old->profiles[i].age;

		database.profiles[i].gender =
			old->profiles[i].gender;

		database.profiles[i].last_bpm =
			old->profiles[i].last_bpm;

		set_default_body_data(
			&database.profiles[i]
		);
	}

	return save_database();
}

static int migrate_version_two(
	const struct profile_database_v2 *old
)
{
	create_empty_database();

	for (int i = 0; i < PROFILE_MAX; i++) {
		struct user_profile *target =
			&database.profiles[i];
		const struct user_profile_v2 *source =
			&old->profiles[i];

		target->valid = source->valid;
		memcpy(
			target->name,
			source->name,
			sizeof(source->name)
		);
		target->name[PROFILE_NAME_MAX] = '\0';
		target->age = source->age;
		target->gender = source->gender;
		set_default_body_data(target);
		target->last_bpm = source->last_bpm;
		target->last_mep_kpa_x100 =
			source->last_mep_kpa_x100;
		target->last_mep_predicted_x100 =
			source->last_mep_predicted_x100;
		target->last_mep_percent =
			source->last_mep_percent;
		target->last_mep_repeatable =
			source->last_mep_repeatable;
		target->last_mep_reference_valid = 0;
	}

	return save_database();
}

static int migrate_version_three(
	const struct profile_database_v3 *old
)
{
	create_empty_database();

	for (int i = 0; i < PROFILE_MAX; i++) {
		struct user_profile *target =
			&database.profiles[i];
		const struct user_profile_v3 *source =
			&old->profiles[i];

		target->valid = source->valid;
		memcpy(target->name, source->name, sizeof(source->name));
		target->name[PROFILE_NAME_MAX] = '\0';
		target->age = source->age;
		target->gender = source->gender;
		target->height_cm = source->height_cm;
		target->weight_kg = source->weight_kg;
		target->last_bpm = source->last_bpm;
		target->last_mep_kpa_x100 = source->last_mep_kpa_x100;
		target->last_mep_predicted_x100 =
			source->last_mep_predicted_x100;
		target->last_mep_percent = source->last_mep_percent;
		target->last_mep_repeatable = source->last_mep_repeatable;
		target->last_mep_reference_valid =
			source->last_mep_reference_valid;
	}

	return save_database();
}

int profile_store_init(void)
{
	create_empty_database();

	int result = settings_subsys_init();

	if (result != 0) {
		return result;
	}

	union profile_load_buffer loaded;

	memset(
		&loaded,
		0,
		sizeof(loaded)
	);

	struct direct_value value = {
		.destination = &loaded,
		.capacity = sizeof(loaded),
		.loaded_length = 0,
		.fetched = false,
	};

	result = settings_load_subtree_direct(
		PROFILE_SETTINGS_KEY,
		direct_loader,
		&value
	);

	if (result != 0) {
		return result;
	}

	if (!value.fetched) {
		return 0;
	}

	if (value.loaded_length ==
		    sizeof(struct profile_database) &&
	    loaded.current.magic == PROFILE_MAGIC &&
	    loaded.current.version == PROFILE_VERSION) {
		database = loaded.current;
		return 0;
	}

	if (value.loaded_length ==
		    sizeof(struct profile_database_v1) &&
	    loaded.old.magic == PROFILE_MAGIC &&
	    loaded.old.version == 1) {
		return migrate_version_one(&loaded.old);
	}

	if (value.loaded_length ==
		    sizeof(struct profile_database_v2) &&
	    loaded.version_two.magic == PROFILE_MAGIC &&
	    loaded.version_two.version == 2) {
		return migrate_version_two(
			&loaded.version_two
		);
	}

	if (value.loaded_length ==
		    sizeof(struct profile_database_v3) &&
	    loaded.version_three.magic == PROFILE_MAGIC &&
	    loaded.version_three.version == 3) {
		return migrate_version_three(
			&loaded.version_three
		);
	}

	/* Taninmayan kayit varsa guvenli bos veritabani kullan. */
	create_empty_database();
	return 0;
}

const struct user_profile *
profile_store_get(int index)
{
	if (index < 0 ||
	    index >= PROFILE_MAX ||
	    !database.profiles[index].valid) {
		return NULL;
	}

	return &database.profiles[index];
}

int profile_store_first_free(void)
{
	for (int i = 0;
	     i < PROFILE_MAX;
	     i++) {
		if (!database.profiles[i].valid) {
			return i;
		}
	}

	return -ENOSPC;
}

int profile_store_save(
	int index,
	const struct user_profile *profile
)
{
	if (index < 0 ||
	    index >= PROFILE_MAX ||
	    profile == NULL) {
		return -EINVAL;
	}

	database.profiles[index] = *profile;
	database.profiles[index].valid = 1;

	if (database.profiles[index].height_cm <
		    PROFILE_MIN_HEIGHT_CM ||
	    database.profiles[index].height_cm >
		    PROFILE_MAX_HEIGHT_CM) {
		database.profiles[index].height_cm =
			PROFILE_DEFAULT_HEIGHT_CM;
	}

	if (database.profiles[index].weight_kg <
		    PROFILE_MIN_WEIGHT_KG ||
	    database.profiles[index].weight_kg >
		    PROFILE_MAX_WEIGHT_KG) {
		database.profiles[index].weight_kg =
			PROFILE_DEFAULT_WEIGHT_KG;
	}

	database.profiles[index]
		.name[PROFILE_NAME_MAX] = '\0';

	return save_database();
}

int profile_store_delete(int index)
{
	if (index < 0 || index >= PROFILE_MAX ||
	    !database.profiles[index].valid) {
		return -EINVAL;
	}

	memset(&database.profiles[index], 0,
	       sizeof(database.profiles[index]));
	return save_database();
}

int profile_store_set_last_bpm(
	int index,
	uint16_t bpm
)
{
	if (index < 0 ||
	    index >= PROFILE_MAX ||
	    !database.profiles[index].valid) {
		return -EINVAL;
	}

	database.profiles[index].last_bpm = bpm;

	return save_database();
}

int profile_store_set_last_mep(
	int index,
	int32_t measured_kpa_x100,
	int32_t predicted_kpa_x100,
	uint16_t percent,
	bool repeatable,
	bool reference_valid
)
{
	if (index < 0 ||
	    index >= PROFILE_MAX ||
	    !database.profiles[index].valid ||
	    measured_kpa_x100 <= 0) {
		return -EINVAL;
	}

	database.profiles[index].last_mep_kpa_x100 =
		measured_kpa_x100;

	database.profiles[index].last_mep_predicted_x100 =
		predicted_kpa_x100;

	database.profiles[index].last_mep_percent =
		percent;

	database.profiles[index].last_mep_repeatable =
		repeatable ? 1 : 0;

	database.profiles[index].last_mep_reference_valid =
		reference_valid ? 1 : 0;

	return save_database();
}

int profile_store_set_last_mip(
	int index,
	int32_t measured_kpa_x100,
	int32_t predicted_kpa_x100,
	uint16_t percent,
	bool repeatable,
	bool reference_valid
)
{
	if (index < 0 ||
	    index >= PROFILE_MAX ||
	    !database.profiles[index].valid ||
	    measured_kpa_x100 <= 0) {
		return -EINVAL;
	}

	database.profiles[index].last_mip_kpa_x100 =
		measured_kpa_x100;
	database.profiles[index].last_mip_predicted_x100 =
		predicted_kpa_x100;
	database.profiles[index].last_mip_percent = percent;
	database.profiles[index].last_mip_repeatable =
		repeatable ? 1 : 0;
	database.profiles[index].last_mip_reference_valid =
		reference_valid ? 1 : 0;

	return save_database();
}
