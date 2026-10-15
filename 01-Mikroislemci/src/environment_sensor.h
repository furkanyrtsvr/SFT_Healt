#ifndef ENVIRONMENT_SENSOR_H
#define ENVIRONMENT_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

enum environment_quality {
	ENVIRONMENT_UNKNOWN,
	ENVIRONMENT_GOOD,
	ENVIRONMENT_DRY,
	ENVIRONMENT_HUMID,
	ENVIRONMENT_COLD,
	ENVIRONMENT_HOT,
};

struct environment_reading {
	int16_t temperature_c_x10;
	uint16_t humidity_percent_x10;
	enum environment_quality quality;
	bool valid;
};

int environment_sensor_init(void);
int environment_sensor_read(struct environment_reading *reading);
const char *environment_quality_code(enum environment_quality quality);

#endif
