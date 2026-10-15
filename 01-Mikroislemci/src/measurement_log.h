#ifndef MEASUREMENT_LOG_H
#define MEASUREMENT_LOG_H

#include <stddef.h>
#include <stdint.h>

#include "rtc_clock.h"

#define MEASUREMENT_LOG_CAPACITY 48

enum measurement_type {
	MEASUREMENT_HEART_RATE = 1,
	MEASUREMENT_MEP = 2,
	MEASUREMENT_MIP = 3,
};

struct measurement_record {
	uint32_t sequence;
	uint8_t valid;
	uint8_t type;
	uint8_t profile_index;
	uint8_t reserved;
	int32_t value_x100;
	int32_t predicted_x100;
	uint16_t percent;
	struct rtc_datetime time;
};

int measurement_log_init(void);
int measurement_log_add(
	enum measurement_type type,
	uint8_t profile_index,
	int32_t value_x100,
	int32_t predicted_x100,
	uint16_t percent,
	const struct rtc_datetime *time,
	struct measurement_record *saved
);
size_t measurement_log_count(int profile_index);
int measurement_log_get_newest(
	int profile_index,
	size_t newest_index,
	struct measurement_record *record
);
int measurement_log_delete_profile(int profile_index);

#endif
