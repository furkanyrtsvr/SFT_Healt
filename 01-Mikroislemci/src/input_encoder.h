#ifndef INPUT_ENCODER_H
#define INPUT_ENCODER_H

#include <zephyr/kernel.h>

enum input_event {
	INPUT_CLOCKWISE,
	INPUT_COUNTERCLOCKWISE,
	INPUT_SHORT_PRESS,
	INPUT_LONG_PRESS,
};

int input_encoder_init(void);

int input_encoder_get(
	enum input_event *event,
	k_timeout_t timeout
);

#endif
