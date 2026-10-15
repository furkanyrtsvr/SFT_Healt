#include "input_encoder.h"

#include <stdbool.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>

#define USER_NODE DT_PATH(zephyr_user)

#define ENCODER_STACK_SIZE 1024
#define ENCODER_PRIORITY 0

/*
 * Saat yonu ters calisirsa sadece bunu -1 yap.
 */
#define ENCODER_DIRECTION 1

#define LONG_PRESS_MS 900

static const struct gpio_dt_spec encoder_a =
	GPIO_DT_SPEC_GET(
		USER_NODE,
		encoder_a_gpios
	);

static const struct gpio_dt_spec encoder_b =
	GPIO_DT_SPEC_GET(
		USER_NODE,
		encoder_b_gpios
	);

static const struct gpio_dt_spec encoder_button =
	GPIO_DT_SPEC_GET(
		USER_NODE,
		encoder_sw_gpios
	);

static const int8_t transition_table[16] = {
	 0,  1, -1,  0,
	-1,  0,  0,  1,
	 1,  0,  0, -1,
	 0, -1,  1,  0
};

K_SEM_DEFINE(thread_start, 0, 1);

K_MSGQ_DEFINE(
	event_queue,
	sizeof(enum input_event),
	12,
	4
);

static int read_encoder_state(void)
{
	int a = gpio_pin_get_dt(&encoder_a);
	int b = gpio_pin_get_dt(&encoder_b);

	return (a << 1) | b;
}

static void send_event(enum input_event event)
{
	(void)k_msgq_put(
		&event_queue,
		&event,
		K_NO_WAIT
	);
}

static void encoder_thread(
	void *arg1,
	void *arg2,
	void *arg3
)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	k_sem_take(&thread_start, K_FOREVER);

	int previous_state = read_encoder_state();
	int movement = 0;

	bool last_raw_button = false;
	bool stable_button = false;

	int debounce_count = 0;
	int64_t press_started = 0;

	bool long_sent = false;

	while (true) {
		int current_state =
			read_encoder_state();

		if (current_state != previous_state) {
			int index =
				(previous_state << 2) |
				current_state;

			int direction =
				transition_table[index];

			previous_state = current_state;

			if (direction == 0) {
				movement = 0;
			} else {
				movement +=
					direction *
					ENCODER_DIRECTION;
			}

			if (movement >= 4) {
				send_event(
					INPUT_CLOCKWISE
				);

				movement = 0;
			} else if (movement <= -4) {
				send_event(
					INPUT_COUNTERCLOCKWISE
				);

				movement = 0;
			}
		}

		bool raw_button =
			gpio_pin_get_raw(
				encoder_button.port,
				encoder_button.pin
			) == 0;

		if (raw_button == last_raw_button) {
			debounce_count =
				MIN(
					debounce_count + 1,
					10
				);
		} else {
			last_raw_button =
				raw_button;

			debounce_count = 0;
		}

		if (debounce_count >= 10 &&
		    raw_button != stable_button) {
			stable_button = raw_button;

			if (stable_button) {
				press_started =
					k_uptime_get();

				long_sent = false;
			} else if (!long_sent) {
				send_event(
					INPUT_SHORT_PRESS
				);
			}
		}

		if (stable_button &&
		    !long_sent &&
		    k_uptime_get() -
		    press_started >= LONG_PRESS_MS) {
			send_event(INPUT_LONG_PRESS);
			long_sent = true;
		}

		k_sleep(K_MSEC(1));
	}
}

K_THREAD_DEFINE(
	encoder_thread_id,
	ENCODER_STACK_SIZE,
	encoder_thread,
	NULL,
	NULL,
	NULL,
	ENCODER_PRIORITY,
	0,
	0
);

int input_encoder_init(void)
{
	if (!gpio_is_ready_dt(&encoder_a) ||
	    !gpio_is_ready_dt(&encoder_b) ||
	    !gpio_is_ready_dt(&encoder_button)) {
		return -1;
	}

	if (gpio_pin_configure_dt(
		    &encoder_a,
		    GPIO_INPUT) != 0 ||
	    gpio_pin_configure_dt(
		    &encoder_b,
		    GPIO_INPUT) != 0 ||
	    gpio_pin_configure_dt(
		    &encoder_button,
		    GPIO_INPUT) != 0) {
		return -1;
	}

	k_sem_give(&thread_start);

	return 0;
}

int input_encoder_get(
	enum input_event *event,
	k_timeout_t timeout
)
{
	return k_msgq_get(
		&event_queue,
		event,
		timeout
	);
}
