#include "heart_rate.h"

#include <limits.h>
#include <string.h>

#include <zephyr/sys/util.h>

#define SAMPLE_PERIOD_MS 10

#define FINGER_ON_THRESHOLD 12000U
#define FINGER_OFF_THRESHOLD 8000U

#define FINGER_ON_SAMPLES 30
#define FINGER_OFF_SAMPLES 80

#define WARMUP_SAMPLES 80

#define MIN_INTERVAL_MS 300
#define MAX_INTERVAL_MS 1500
#define REFRACTORY_MS 280

#define BPM_HOLD_MS 6000

#define INTERVAL_COUNT 7
#define MIN_INTERVALS_FOR_BPM 2
#define INITIAL_INTERVAL_TOLERANCE_PERCENT 25

#define WAVE_HEIGHT 24
#define WAVE_CENTER (WAVE_HEIGHT / 2)

static int32_t median(
	const int32_t *values,
	uint8_t count
)
{
	int32_t sorted[INTERVAL_COUNT];

	for (uint8_t i = 0; i < count; i++) {
		sorted[i] = values[i];
	}

	for (uint8_t i = 1; i < count; i++) {
		int32_t value = sorted[i];
		int j = i - 1;

		while (j >= 0 &&
		       sorted[j] > value) {
			sorted[j + 1] = sorted[j];
			j--;
		}

		sorted[j + 1] = value;
	}

	return sorted[count / 2];
}

static void clear_measurement(
	struct heart_rate_state *state
)
{
	state->dc_q8 = 0;
	state->lowpass = 0;
	state->filtered = 0;

	state->window_min = INT32_MAX;
	state->window_max = INT32_MIN;
	state->window_samples = 0;

	state->beat_threshold = 0;
	state->display_scale = 32;

	state->polarity = 1;
	state->polarity_locked = false;
	state->beat_armed = false;

	state->sample_time_ms = 0;
	state->last_beat_ms = 0;
	state->last_good_beat_ms = 0;

	memset(
		state->intervals,
		0,
		sizeof(state->intervals)
	);

	state->interval_index = 0;
	state->interval_count = 0;
	state->rejected_count = 0;

	state->warmup_samples = 0;

	state->bpm_q8 = 0;
	state->bpm = 0;
	state->bpm_valid = false;

	state->wave_head = 0;

	for (int i = 0;
	     i < HEART_WAVE_POINTS;
	     i++) {
		state->wave[i] = WAVE_CENTER;
	}
}

void heart_rate_reset(
	struct heart_rate_state *state
)
{
	memset(state, 0, sizeof(*state));
	clear_measurement(state);
}

static void add_wave_point(
	struct heart_rate_state *state,
	int32_t value
)
{
	int32_t oriented =
		value * state->polarity;

	int32_t magnitude =
		oriented >= 0
			? oriented
			: -oriented;

	if (magnitude > state->display_scale) {
		state->display_scale = magnitude;
	} else if (state->display_scale > 32) {
		state->display_scale -= MAX(
			state->display_scale / 50,
			1
		);
	}

	int32_t normalized = CLAMP(
		(oriented *
		 (WAVE_CENTER - 2)) /
		MAX(state->display_scale, 1),

		-(WAVE_CENTER - 2),
		WAVE_CENTER - 2
	);

	state->wave[state->wave_head] =
		(int8_t)(
			WAVE_CENTER -
			normalized
		);

	state->wave_head =
		(state->wave_head + 1) %
		HEART_WAVE_POINTS;
}

static void update_threshold(
	struct heart_rate_state *state
)
{
	if (state->window_samples < 50) {
		return;
	}

	int32_t positive_peak =
		MAX(state->window_max, 0);

	int32_t negative_peak =
		MAX(-state->window_min, 0);

	int32_t amplitude =
		state->window_max -
		state->window_min;

	if (!state->polarity_locked) {
		state->polarity =
			positive_peak >= negative_peak
				? 1
				: -1;

		if (state->warmup_samples >=
		    WARMUP_SAMPLES) {
			state->polarity_locked = true;
		}
	}

	int32_t peak =
		state->polarity > 0
			? positive_peak
			: negative_peak;

	int32_t target = MAX(
		peak * 55 / 100,
		amplitude * 30 / 100
	);

	if (amplitude >= 40 &&
	    target > 0) {
		if (state->beat_threshold == 0) {
			state->beat_threshold =
				target;
		} else {
			state->beat_threshold +=
				(target -
				 state->beat_threshold) / 4;
		}
	}

	state->window_min = INT32_MAX;
	state->window_max = INT32_MIN;
	state->window_samples = 0;
}

static bool normalize_interval(
	struct heart_rate_state *state,
	int32_t *interval
)
{
	if (*interval < MIN_INTERVAL_MS) {
		return false;
	}

	if (state->interval_count < 3) {
		return *interval <=
		       MAX_INTERVAL_MS;
	}

	int32_t middle = median(
		state->intervals,
		state->interval_count
	);

	/*
	 * Bir kalp atimi kacirildiysa
	 * iki atimlik araligi ikiye bol.
	 */
	if (*interval >= middle * 165 / 100 &&
	    *interval <= middle * 235 / 100) {
		*interval /= 2;
	}

	int32_t difference =
		*interval >= middle
			? *interval - middle
			: middle - *interval;

	return
		*interval >= MIN_INTERVAL_MS &&
		*interval <= MAX_INTERVAL_MS &&
		difference <= middle * 35 / 100;
}

static void store_interval(
	struct heart_rate_state *state,
	int32_t interval
)
{
	state->intervals[
		state->interval_index
	] = interval;

	state->interval_index =
		(state->interval_index + 1) %
		INTERVAL_COUNT;

	state->interval_count = MIN(
		state->interval_count + 1,
		INTERVAL_COUNT
	);

	state->rejected_count = 0;

	state->last_good_beat_ms =
		state->sample_time_ms;

	if (state->interval_count <
	    MIN_INTERVALS_FOR_BPM) {
		return;
	}

	int32_t target_interval;

	if (state->interval_count == 2) {
		int32_t first = state->intervals[0];
		int32_t second = state->intervals[1];
		int32_t largest = MAX(first, second);
		int32_t difference = first >= second
			? first - second
			: second - first;

		if (difference * 100 >
		    largest * INITIAL_INTERVAL_TOLERANCE_PERCENT) {
			return;
		}

		target_interval = (first + second) / 2;
	} else {
		target_interval = median(
			state->intervals,
			state->interval_count
		);
	}

	int target_bpm = 60000 / target_interval;

	if (target_bpm < 40 ||
	    target_bpm > 200) {
		return;
	}

	if (!state->bpm_valid) {
		state->bpm_q8 =
			target_bpm << 8;

		state->bpm = target_bpm;
		state->bpm_valid = true;

		return;
	}

	/*
	 * 1/4 IIR filtre:
	 * Yeni olcume yumusak gecis.
	 */
	state->bpm_q8 +=
		(((target_bpm << 8) -
		  state->bpm_q8) / 4);

	int filtered_bpm =
		(state->bpm_q8 + 128) >> 8;

	/*
	 * Ekrandaki deger bir atimda
	 * en fazla 3 BPM degisebilir.
	 */
	state->bpm += CLAMP(
		filtered_bpm - state->bpm,
		-3,
		3
	);
}

static bool process_candidate_beat(
	struct heart_rate_state *state
)
{
	if (state->last_beat_ms == 0) {
		state->last_beat_ms =
			state->sample_time_ms;

		return true;
	}

	int32_t interval =
		(int32_t)(
			state->sample_time_ms -
			state->last_beat_ms
		);

	if (interval < REFRACTORY_MS) {
		return false;
	}

	if (normalize_interval(
		    state,
		    &interval)) {
		state->last_beat_ms =
			state->sample_time_ms;

		store_interval(
			state,
			interval
		);

		return true;
	}

	/*
	 * Tek kotu veya hatali tepe
	 * artik BPM degerini silmez.
	 */
	state->rejected_count++;

	if (interval > MAX_INTERVAL_MS) {
		state->last_beat_ms =
			state->sample_time_ms;
	}

	return false;
}

bool heart_rate_process(
	struct heart_rate_state *state,
	uint32_t ir
)
{
	bool beat_detected = false;

	/*
	 * Parmagin takildigini tek bir
	 * ornekle degil, 300 ms sonunda
	 * kabul et.
	 */
	if (!state->finger_present) {
		if (ir >= FINGER_ON_THRESHOLD) {
			state->finger_on_samples++;
		} else {
			state->finger_on_samples = 0;
		}

		if (state->finger_on_samples <
		    FINGER_ON_SAMPLES) {
			return false;
		}

		uint16_t on_samples =
			state->finger_on_samples;

		clear_measurement(state);

		state->finger_present = true;
		state->finger_on_samples =
			on_samples;

		state->dc_q8 =
			(int64_t)ir << 8;

		return false;
	}

	/*
	 * Parmagin kaldirildigini ancak
	 * 800 ms dusuk sinyalden sonra
	 * kabul et.
	 */
	if (ir <= FINGER_OFF_THRESHOLD) {
		state->finger_off_samples++;
	} else {
		state->finger_off_samples = 0;
	}

	if (state->finger_off_samples >=
	    FINGER_OFF_SAMPLES) {
		heart_rate_reset(state);
		return false;
	}

	state->sample_time_ms +=
		SAMPLE_PERIOD_MS;

	/*
	 * DC kaldirma ve hafif alcak
	 * geciren filtre.
	 */
	state->dc_q8 +=
		(((int64_t)ir << 8) -
		 state->dc_q8) / 32;

	int32_t ac =
		(int32_t)ir -
		(int32_t)(state->dc_q8 >> 8);

	state->lowpass +=
		(ac - state->lowpass) / 4;

	state->filtered =
		state->lowpass;

	state->window_min = MIN(
		state->window_min,
		state->filtered
	);

	state->window_max = MAX(
		state->window_max,
		state->filtered
	);

	state->window_samples++;

	state->warmup_samples = MIN(
		state->warmup_samples + 1,
		UINT16_MAX
	);

	add_wave_point(
		state,
		state->filtered
	);

	update_threshold(state);

	int32_t pulse =
		state->filtered *
		state->polarity;

	/*
	 * Yeni tepe kabul edilebilmesi
	 * icin sinyal once esigin altina
	 * inmelidir.
	 */
	if (pulse <
	    state->beat_threshold *
	    45 / 100) {
		state->beat_armed = true;
	}

	if (state->warmup_samples >=
	    WARMUP_SAMPLES &&
	    state->beat_threshold > 0 &&
	    state->beat_armed &&
	    pulse >= state->beat_threshold &&
	    (state->last_beat_ms == 0 ||
	     state->sample_time_ms -
	     state->last_beat_ms >=
	     REFRACTORY_MS)) {
		state->beat_armed = false;

		beat_detected =
			process_candidate_beat(
				state
			);
	}

	/*
	 * Kisa bir bozulmada son iyi
	 * BPM korunur. Gercek sinyal
	 * kaybi 6 saniye surerse silinir.
	 */
	if (state->bpm_valid &&
	    state->last_good_beat_ms > 0 &&
	    state->sample_time_ms -
	    state->last_good_beat_ms >
	    BPM_HOLD_MS) {
		state->bpm_valid = false;
		state->bpm = 0;
		state->bpm_q8 = 0;

		state->interval_count = 0;
		state->interval_index = 0;

		state->last_beat_ms = 0;
		state->polarity_locked = false;
	}

	return beat_detected;
}
