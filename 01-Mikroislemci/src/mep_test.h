#ifndef MEP_TEST_H
#define MEP_TEST_H

#include <stdbool.h>
#include <stdint.h>

#include "profile_store.h"

#define MEP_ATTEMPT_COUNT 1
#define MEP_WINDOW_SAMPLES 48

enum mep_event {
	MEP_EVENT_NONE,
	MEP_EVENT_ATTEMPT_FINISHED,
};

enum pressure_test_direction {
	PRESSURE_TEST_EXPIRATORY,
	PRESSURE_TEST_INSPIRATORY,
};

struct mep_window_sample {
	int32_t pressure_kpa_x100;
	int64_t time_ms;
};

struct mep_test_state {
	enum pressure_test_direction direction;
	uint8_t attempt_index;
	int32_t attempts_kpa_x100[MEP_ATTEMPT_COUNT];
	bool attempts_valid[MEP_ATTEMPT_COUNT];

	bool waiting_for_blow;
	bool measuring;

	int64_t attempt_started_ms;
	int64_t blow_started_ms;
	int64_t below_started_ms;

	int32_t live_kpa_x100;
	int32_t peak_kpa_x100;
	int32_t best_average_kpa_x100;

	struct mep_window_sample window[MEP_WINDOW_SAMPLES];
	uint8_t window_head;
	uint8_t window_count;
};

void mep_test_reset(
	struct mep_test_state *state
);

void mep_test_begin_attempt(
	struct mep_test_state *state,
	int64_t now_ms,
	enum pressure_test_direction direction
);

enum mep_event mep_test_process(
	struct mep_test_state *state,
	int32_t pressure_kpa_x100,
	int64_t now_ms
);

bool mep_test_has_more_attempts(
	const struct mep_test_state *state
);

void mep_test_advance_attempt(
	struct mep_test_state *state
);

bool mep_test_current_valid(
	const struct mep_test_state *state
);

int32_t mep_test_current_value(
	const struct mep_test_state *state
);

int32_t mep_test_best_value(
	const struct mep_test_state *state
);

bool mep_test_is_repeatable(
	const struct mep_test_state *state
);

uint16_t mep_profile_bmi_x10(
	const struct user_profile *profile
);

/*
 * Pehlivan ve ark. 2024 Turkiye yetiskin referansi:
 * 20-60 yas ve BMI 18.0-29.5 kg/m2.
 */
bool mep_reference_is_applicable(
	const struct user_profile *profile
);

int32_t mep_predicted_kpa_x100(
	const struct user_profile *profile
);

int32_t mip_predicted_kpa_x100(
	const struct user_profile *profile
);

uint16_t mep_percent_of_predicted(
	int32_t measured_kpa_x100,
	int32_t predicted_kpa_x100
);

#endif
