#ifndef PROFILE_STORE_H
#define PROFILE_STORE_H

#include <stdbool.h>
#include <stdint.h>

#define PROFILE_MAX 5
#define PROFILE_NAME_MAX 8

#define PROFILE_MIN_HEIGHT_CM 100
#define PROFILE_MAX_HEIGHT_CM 230
#define PROFILE_MIN_WEIGHT_KG 25
#define PROFILE_MAX_WEIGHT_KG 250

#define PROFILE_DEFAULT_HEIGHT_CM 170
#define PROFILE_DEFAULT_WEIGHT_KG 70

enum profile_gender {
	PROFILE_MAN,
	PROFILE_WOMAN,
};

struct user_profile {
	uint8_t valid;

	char name[
		PROFILE_NAME_MAX + 1
	];

	uint8_t age;
	uint8_t gender;
	uint16_t height_cm;
	uint16_t weight_kg;

	uint16_t last_bpm;

	/* Degerler 0.01 kPa biriminde tutulur. */
	int32_t last_mep_kpa_x100;
	int32_t last_mep_predicted_x100;
	uint16_t last_mep_percent;

	uint8_t last_mep_repeatable;
	uint8_t last_mep_reference_valid;

	int32_t last_mip_kpa_x100;
	int32_t last_mip_predicted_x100;
	uint16_t last_mip_percent;
	uint8_t last_mip_repeatable;
	uint8_t last_mip_reference_valid;
	uint8_t reserved[2];
};

int profile_store_init(void);

const struct user_profile *
profile_store_get(int index);

int profile_store_first_free(void);

int profile_store_save(
	int index,
	const struct user_profile *profile
);

int profile_store_delete(int index);

int profile_store_set_last_bpm(
	int index,
	uint16_t bpm
);

int profile_store_set_last_mep(
	int index,
	int32_t measured_kpa_x100,
	int32_t predicted_kpa_x100,
	uint16_t percent,
	bool repeatable,
	bool reference_valid
);

int profile_store_set_last_mip(
	int index,
	int32_t measured_kpa_x100,
	int32_t predicted_kpa_x100,
	uint16_t percent,
	bool repeatable,
	bool reference_valid
);

#endif
