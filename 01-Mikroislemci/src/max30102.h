#ifndef MAX30102_H
#define MAX30102_H

#include <stdint.h>

#include <zephyr/device.h>

struct max30102_sample {
	uint32_t red;
	uint32_t ir;
};

int max30102_init(
	const struct device *i2c
);

int max30102_samples_available(
	const struct device *i2c
);

int max30102_read_sample(
	const struct device *i2c,
	struct max30102_sample *sample
);

#endif
