#include "mep_test.h"

#include <string.h>

#include <zephyr/sys/util.h>

#define BLOW_START_KPA_X100 50
#define BLOW_END_KPA_X100 40

#define BLOW_WAIT_TIMEOUT_MS 15000
#define MAX_BLOW_DURATION_MS 5000
#define END_HOLD_MS 500
#define MIN_VALID_DURATION_MS 2000

#define AVERAGE_WINDOW_MS 1000
#define MIN_FULL_WINDOW_MS 950

static void clear_current_attempt(
	struct mep_test_state *state,
	int64_t now_ms
)
{
	state->waiting_for_blow = true;
	state->measuring = false;

	state->attempt_started_ms = now_ms;
	state->blow_started_ms = 0;
	state->below_started_ms = 0;

	state->live_kpa_x100 = 0;
	state->peak_kpa_x100 = 0;
	state->best_average_kpa_x100 = 0;

	state->window_head = 0;
	state->window_count = 0;

	memset(
		state->window,
		0,
		sizeof(state->window)
	);
}

void mep_test_reset(
	struct mep_test_state *state
)
{
	memset(state, 0, sizeof(*state));
	state->attempt_index = 0;
}

void mep_test_begin_attempt(
	struct mep_test_state *state,
	int64_t now_ms,
	enum pressure_test_direction direction
)
{
	state->direction = direction;
	clear_current_attempt(state, now_ms);
}

static void add_window_sample(
	struct mep_test_state *state,
	int32_t pressure_kpa_x100,
	int64_t now_ms
)
{
	state->window[state->window_head].pressure_kpa_x100 =
		pressure_kpa_x100;

	state->window[state->window_head].time_ms = now_ms;

	state->window_head =
		(state->window_head + 1) % MEP_WINDOW_SAMPLES;

	state->window_count = MIN(
		state->window_count + 1,
		MEP_WINDOW_SAMPLES
	);

	int64_t sum = 0;
	uint8_t used = 0;
	int64_t oldest_time = now_ms;

	for (uint8_t i = 0;
	     i < state->window_count;
	     i++) {
		int index =
			(state->window_head +
			 MEP_WINDOW_SAMPLES - 1 - i) %
			MEP_WINDOW_SAMPLES;

		const struct mep_window_sample *sample =
			&state->window[index];

		if (now_ms - sample->time_ms >
		    AVERAGE_WINDOW_MS) {
			break;
		}

		sum += MAX(
			sample->pressure_kpa_x100,
			0
		);

		oldest_time = sample->time_ms;
		used++;
	}

	if (used > 0 &&
	    now_ms - oldest_time >=
	    MIN_FULL_WINDOW_MS) {
		int32_t average =
			(int32_t)(sum / used);

		state->best_average_kpa_x100 = MAX(
			state->best_average_kpa_x100,
			average
		);
	}
}

static enum mep_event finish_attempt(
	struct mep_test_state *state,
	int64_t now_ms
)
{
	int64_t duration =
		state->blow_started_ms > 0
			? now_ms - state->blow_started_ms
			: 0;

	bool valid =
		duration >= MIN_VALID_DURATION_MS &&
		state->best_average_kpa_x100 >=
		BLOW_START_KPA_X100;

	state->attempts_valid[
		state->attempt_index
	] = valid;

	state->attempts_kpa_x100[
		state->attempt_index
	] = valid
		? state->best_average_kpa_x100
		: 0;

	state->waiting_for_blow = false;
	state->measuring = false;

	return MEP_EVENT_ATTEMPT_FINISHED;
}

enum mep_event mep_test_process(
	struct mep_test_state *state,
	int32_t pressure_kpa_x100,
	int64_t now_ms
)
{
	/*
	 * MEP yalniz pozitif, MIP yalniz negatif basinci kabul eder.
	 * MIP ekranda pozitif buyukluk olarak gosterilir; rastgele mutlak
	 * deger alinmaz, bu nedenle ufleme MIP olarak sayilmaz.
	 */
	state->live_kpa_x100 =
		state->direction == PRESSURE_TEST_INSPIRATORY
			? -pressure_kpa_x100
			: pressure_kpa_x100;

	if (state->waiting_for_blow) {
		if (state->live_kpa_x100 >=
		    BLOW_START_KPA_X100) {
			state->waiting_for_blow = false;
			state->measuring = true;
			state->blow_started_ms = now_ms;
			state->below_started_ms = 0;
		}

		if (state->waiting_for_blow &&
		    now_ms - state->attempt_started_ms >=
		    BLOW_WAIT_TIMEOUT_MS) {
			return finish_attempt(state, now_ms);
		}
	}

	if (!state->measuring) {
		return MEP_EVENT_NONE;
	}

	state->peak_kpa_x100 = MAX(
		state->peak_kpa_x100,
		state->live_kpa_x100
	);

	add_window_sample(
		state,
		state->live_kpa_x100,
		now_ms
	);

	if (state->live_kpa_x100 <
	    BLOW_END_KPA_X100) {
		if (state->below_started_ms == 0) {
			state->below_started_ms = now_ms;
		}
	} else {
		state->below_started_ms = 0;
	}

	bool ended =
		state->below_started_ms > 0 &&
		now_ms - state->below_started_ms >=
		END_HOLD_MS;

	bool timed_out =
		now_ms - state->blow_started_ms >=
		MAX_BLOW_DURATION_MS;

	if (ended || timed_out) {
		return finish_attempt(state, now_ms);
	}

	return MEP_EVENT_NONE;
}

bool mep_test_has_more_attempts(
	const struct mep_test_state *state
)
{
	return state->attempt_index + 1 <
	       MEP_ATTEMPT_COUNT;
}

void mep_test_advance_attempt(
	struct mep_test_state *state
)
{
	if (mep_test_has_more_attempts(state)) {
		state->attempt_index++;
	}
}

bool mep_test_current_valid(
	const struct mep_test_state *state
)
{
	return state->attempts_valid[
		state->attempt_index
	];
}

int32_t mep_test_current_value(
	const struct mep_test_state *state
)
{
	return state->attempts_kpa_x100[
		state->attempt_index
	];
}

int32_t mep_test_best_value(
	const struct mep_test_state *state
)
{
	int32_t best = 0;

	for (int i = 0;
	     i < MEP_ATTEMPT_COUNT;
	     i++) {
		if (state->attempts_valid[i]) {
			best = MAX(
				best,
				state->attempts_kpa_x100[i]
			);
		}
	}

	return best;
}

bool mep_test_is_repeatable(
	const struct mep_test_state *state
)
{
#if MEP_ATTEMPT_COUNT == 1
	/* Tek olcum modunda tekrar uyumu aranmaz. */
	return state->attempts_valid[0];
#else
	int32_t best = 0;
	int32_t second = 0;

	for (int i = 0;
	     i < MEP_ATTEMPT_COUNT;
	     i++) {
		if (!state->attempts_valid[i]) {
			continue;
		}

		int32_t value =
			state->attempts_kpa_x100[i];

		if (value >= best) {
			second = best;
			best = value;
		} else if (value > second) {
			second = value;
		}
	}

	if (best <= 0 || second <= 0) {
		return false;
	}

	return best - second <= best / 10;
#endif
}

uint16_t mep_profile_bmi_x10(
	const struct user_profile *profile
)
{
	if (profile == NULL ||
	    profile->height_cm == 0) {
		return 0;
	}

	int32_t height_squared =
		(int32_t)profile->height_cm *
		profile->height_cm;

	/* BMI x10 = kg * 100000 / boy(cm)^2. */
	return (uint16_t)(
		((int32_t)profile->weight_kg *
		 100000 + height_squared / 2) /
		height_squared
	);
}

bool mep_reference_is_applicable(
	const struct user_profile *profile
)
{
	if (profile == NULL ||
	    profile->age < 20 ||
	    profile->age > 60) {
		return false;
	}

	uint16_t bmi_x10 =
		mep_profile_bmi_x10(profile);

	return bmi_x10 >= 180 &&
	       bmi_x10 <= 295;
}

int32_t mep_predicted_kpa_x100(
	const struct user_profile *profile
)
{
	if (profile == NULL ||
	    profile->age == 0 ||
	    profile->height_cm == 0 ||
	    profile->weight_kg == 0) {
		return 0;
	}

	/*
	 * Pehlivan ve ark. (2024), Turkiye yetiskinleri. Sonuc yalnizca
	 * profil tabanli tahmindir; klinik tani veya cihaz kalibrasyonu degildir.
	 * MEP(cmH2O) = 157.165 - 35.522*Gender
	 *              - 0.271*Age - 42.036*Height(m)
	 *              + 0.787*Weight(kg)
	 * Gender: erkek=0, kadin=1.
	 * Hesap cmH2O x1000 sabit nokta ile yapilir.
	 */
	int64_t cmh2o_x1000 = 157165;

	if (profile->gender == PROFILE_WOMAN) {
		cmh2o_x1000 -= 35522;
	}

	cmh2o_x1000 -=
		(int64_t)271 * profile->age;

	cmh2o_x1000 -=
		((int64_t)42036 *
		 profile->height_cm + 50) / 100;

	cmh2o_x1000 +=
		(int64_t)787 * profile->weight_kg;

	if (cmh2o_x1000 <= 0) {
		return 0;
	}

	/* 1 cmH2O = 0.0980665 kPa. */
	return (int32_t)(
		(cmh2o_x1000 * 980665 +
		 50000000) /
		100000000
	);
}

int32_t mip_predicted_kpa_x100(
	const struct user_profile *profile
)
{
	if (profile == NULL ||
	    profile->age == 0 ||
	    profile->height_cm == 0 ||
	    profile->weight_kg == 0) {
		return 0;
	}

	/*
	 * Pehlivan ve ark. (2024), Turkiye yetiskinleri:
	 * MIP(cmH2O) = 82.583 - 23.218*Gender - 0.093*Age
	 *              + 9.534*Height(m) + 0.343*Weight(kg)
	 * Gender: erkek=0, kadin=1.
	 */
	int64_t cmh2o_x1000 = 82583;

	if (profile->gender == PROFILE_WOMAN) {
		cmh2o_x1000 -= 23218;
	}

	cmh2o_x1000 -=
		(int64_t)93 * profile->age;

	cmh2o_x1000 +=
		((int64_t)9534 *
		 profile->height_cm + 50) / 100;

	cmh2o_x1000 +=
		(int64_t)343 * profile->weight_kg;

	if (cmh2o_x1000 <= 0) {
		return 0;
	}

	return (int32_t)(
		(cmh2o_x1000 * 980665 +
		 50000000) /
		100000000
	);
}

uint16_t mep_percent_of_predicted(
	int32_t measured_kpa_x100,
	int32_t predicted_kpa_x100
)
{
	if (measured_kpa_x100 <= 0 ||
	    predicted_kpa_x100 <= 0) {
		return 0;
	}

	int32_t percent =
		(measured_kpa_x100 * 100) /
		predicted_kpa_x100;

	return (uint16_t)CLAMP(
		percent,
		0,
		999
	);
}
