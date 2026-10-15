#include "environment_sensor.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>

static const struct device *const dht11 =
	DEVICE_DT_GET(DT_ALIAS(dht0));

static int32_t sensor_value_to_x10(const struct sensor_value *value)
{
	return value->val1 * 10 + value->val2 / 100000;
}

static enum environment_quality classify(
	int16_t temperature_c_x10,
	uint16_t humidity_percent_x10
)
{
	if (humidity_percent_x10 < 300) {
		return ENVIRONMENT_DRY;
	}
	if (humidity_percent_x10 > 600) {
		return ENVIRONMENT_HUMID;
	}
	if (temperature_c_x10 < 180) {
		return ENVIRONMENT_COLD;
	}
	if (temperature_c_x10 > 260) {
		return ENVIRONMENT_HOT;
	}
	return ENVIRONMENT_GOOD;
}

int environment_sensor_init(void)
{
	return device_is_ready(dht11) ? 0 : -ENODEV;
}

int environment_sensor_read(struct environment_reading *reading)
{
	if (reading == NULL) {
		return -EINVAL;
	}
	if (!device_is_ready(dht11)) {
		return -ENODEV;
	}

	int result = sensor_sample_fetch(dht11);

	if (result != 0) {
		return result;
	}

	struct sensor_value temperature;
	struct sensor_value humidity;

	result = sensor_channel_get(
		dht11,
		SENSOR_CHAN_AMBIENT_TEMP,
		&temperature
	);
	if (result == 0) {
		result = sensor_channel_get(
			dht11,
			SENSOR_CHAN_HUMIDITY,
			&humidity
		);
	}
	if (result != 0) {
		return result;
	}

	reading->temperature_c_x10 =
		(int16_t)sensor_value_to_x10(&temperature);
	reading->humidity_percent_x10 =
		(uint16_t)sensor_value_to_x10(&humidity);
	reading->quality = classify(
		reading->temperature_c_x10,
		reading->humidity_percent_x10
	);
	reading->valid = true;
	return 0;
}

const char *environment_quality_code(enum environment_quality quality)
{
	switch (quality) {
	case ENVIRONMENT_GOOD:
		return "GOOD";
	case ENVIRONMENT_DRY:
		return "DRY";
	case ENVIRONMENT_HUMID:
		return "HUMID";
	case ENVIRONMENT_COLD:
		return "COLD";
	case ENVIRONMENT_HOT:
		return "HOT";
	default:
		return "UNKNOWN";
	}
}
