#ifndef HEART_RATE_H
#define HEART_RATE_H

#include <stdbool.h>
#include <stdint.h>

#define HEART_WAVE_POINTS 64

struct heart_rate_state {
	int64_t dc_q8;
	int32_t lowpass;
	int32_t filtered;

	int32_t window_min;
	int32_t window_max;
	uint16_t window_samples;

	int32_t beat_threshold;
	int32_t display_scale;

	int8_t polarity;
	bool polarity_locked;
	bool beat_armed;

	int64_t sample_time_ms;
	int64_t last_beat_ms;
	int64_t last_good_beat_ms;

	int32_t intervals[7];
	uint8_t interval_index;
	uint8_t interval_count;
	uint8_t rejected_count;

	uint16_t warmup_samples;
	uint16_t finger_on_samples;
	uint16_t finger_off_samples;

	int32_t bpm_q8;
	int bpm;

	bool bpm_valid;
	bool finger_present;

	int8_t wave[HEART_WAVE_POINTS];
	uint8_t wave_head;
};

void heart_rate_reset(
	struct heart_rate_state *state
);

bool heart_rate_process(
	struct heart_rate_state *state,
	uint32_t ir
);

static inline bool heart_rate_is_stable(
	const struct heart_rate_state *state
)
{
	return state->finger_present &&
	       state->bpm_valid &&
	       state->bpm >= 40 &&
	       state->bpm <= 200;
}

#endif
