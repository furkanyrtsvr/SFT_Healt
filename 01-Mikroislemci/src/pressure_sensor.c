#include "pressure_sensor.h"

#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/irq.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/util.h>

#define USER_NODE DT_PATH(zephyr_user)

#define PRESSURE_MAGIC 0x50524553UL
#define PRESSURE_VERSION 4
#define PRESSURE_SETTINGS_KEY "pulse/pressure"

#define HX711_CLOCK_DELAY_US 1
#define HX711_CHANNEL_A_GAIN_64_PULSES 27
#define HX711_SETTLING_SAMPLES 4
#define HX711_POSITIVE_SATURATION 0x7FFFFFU
#define HX711_NEGATIVE_SATURATION 0x800000U

/* Kalibrasyon/olcum sirasinda ADC raylarinin son %5'ini kullanma. */
#define HX711_SAFE_RAW_LIMIT 7969177

/* Bir saniyelik nokta ortalamasinda izin verilen oynama. */
#define MAX_ZERO_SPREAD_COUNTS 100000
#define MIN_POINT_SPREAD_COUNTS 20000
#define MAX_POINT_SPREAD_PERCENT 5

/* 0-30 ve 30-40 kPa egimleri en fazla %10 farkli olabilir. */
#define MAX_SLOPE_DIFFERENCE_PERCENT 10
#define SENSOR_PHYSICAL_LIMIT_KPA_X100 4000

/*
 * Kalibrasyonsuz prototip modu. Acik kaynak karsilastirmada HX710B
 * x128 icin yaklasik 710000 count/kPa bulunmustur. Bu karttaki HX711
 * Kanal A x64 kullanildigi icin nominal egim kazanc oraniyla yariya
 * indirilir. Bu birim bazli kalibrasyon degil, yalnizca tahmini olcektir.
 */
#define NOMINAL_COUNTS_PER_KPA 355000

struct pressure_calibration_data {
	uint32_t magic;
	uint8_t version;
	uint8_t reserved[3];

	int32_t span_raw;
	int32_t reference_kpa_x100;
	int32_t validated_limit_kpa_x100;
};

struct raw_statistics {
	int32_t average;
	int32_t minimum;
	int32_t maximum;
};

struct direct_value {
	void *destination;
	size_t capacity;
	size_t loaded_length;
	bool fetched;
};

static const struct gpio_dt_spec pressure_sck =
	GPIO_DT_SPEC_GET(USER_NODE, pressure_sck_gpios);

static const struct gpio_dt_spec pressure_out =
	GPIO_DT_SPEC_GET(USER_NODE, pressure_out_gpios);

static struct pressure_calibration_data calibration;

static int32_t runtime_zero_raw;
static bool runtime_tared;
static bool initialized;
static uint8_t settling_samples_remaining;
static int32_t last_counts_per_kpa;

/* 0 -> ilk referans noktasi; ikinci nokta gecmeden flash'a yazilmaz. */
static int32_t pending_reference_raw;
static int32_t pending_span_raw;
static int32_t pending_reference_kpa_x100;
static bool pending_calibration;

static int64_t absolute_i64(int64_t value)
{
	return value < 0 ? -value : value;
}

static bool raw_near_rail(int32_t raw)
{
	return raw >= HX711_SAFE_RAW_LIMIT ||
	       raw <= -HX711_SAFE_RAW_LIMIT;
}

static int direct_loader(
	const char *name,
	size_t len,
	settings_read_cb read_cb,
	void *cb_arg,
	void *param
)
{
	struct direct_value *value = param;

	if (name[0] != '\0') {
		return 0;
	}

	if (len > value->capacity) {
		return -E2BIG;
	}

	int result = read_cb(cb_arg, value->destination, len);

	if (result >= 0) {
		value->fetched = true;
		value->loaded_length = len;
		return 0;
	}

	return result;
}

static int save_calibration(void)
{
	return settings_save_one(
		PRESSURE_SETTINGS_KEY,
		&calibration,
		sizeof(calibration)
	);
}

bool pressure_sensor_data_ready(void)
{
	if (!initialized) {
		return false;
	}

	/* HX711 veri hazir oldugunda DOUT LOW olur. */
	return gpio_pin_get_dt(&pressure_out) == 0;
}

bool pressure_sensor_is_calibrated(void)
{
	/* Bu surum bilincli olarak kalibrasyonsuz tahmini modda calisir. */
	return false;
}

bool pressure_sensor_is_tared(void)
{
	return runtime_tared;
}

int32_t pressure_sensor_counts_per_kpa(void)
{
	return NOMINAL_COUNTS_PER_KPA;
}

int32_t pressure_sensor_validated_limit_kpa_x100(void)
{
	return pressure_sensor_is_calibrated()
		? calibration.validated_limit_kpa_x100
		: 0;
}

int pressure_sensor_init(void)
{
	if (!gpio_is_ready_dt(&pressure_sck) ||
	    !gpio_is_ready_dt(&pressure_out)) {
		return -ENODEV;
	}

	if (gpio_pin_configure_dt(
		    &pressure_sck,
		    GPIO_OUTPUT_INACTIVE) != 0 ||
	    gpio_pin_configure_dt(
		    &pressure_out,
		    GPIO_INPUT) != 0) {
		return -EIO;
	}

	(void)gpio_pin_set_dt(&pressure_sck, 0);
	memset(&calibration, 0, sizeof(calibration));

	struct direct_value value = {
		.destination = &calibration,
		.capacity = sizeof(calibration),
		.loaded_length = 0,
		.fetched = false,
	};

	int result = settings_load_subtree_direct(
		PRESSURE_SETTINGS_KEY,
		direct_loader,
		&value
	);

	if (result != 0) {
		return result;
	}

	if (!value.fetched ||
	    value.loaded_length != sizeof(calibration) ||
	    calibration.magic != PRESSURE_MAGIC ||
	    calibration.version != PRESSURE_VERSION) {
		memset(&calibration, 0, sizeof(calibration));
		calibration.magic = PRESSURE_MAGIC;
		calibration.version = PRESSURE_VERSION;
	}

	runtime_zero_raw = 0;
	runtime_tared = false;
	last_counts_per_kpa = 0;
	pending_calibration = false;
	settling_samples_remaining = HX711_SETTLING_SAMPLES;
	initialized = true;

	return 0;
}

static int read_one_conversion(
	int32_t *raw,
	k_timeout_t timeout
)
{
	if (!initialized) {
		return -ENODEV;
	}

	if (raw == NULL) {
		return -EINVAL;
	}

	int64_t started = k_uptime_get();
	int64_t timeout_ms = k_ticks_to_ms_ceil64(timeout.ticks);

	while (!pressure_sensor_data_ready()) {
		if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
			return -EAGAIN;
		}

		if (!K_TIMEOUT_EQ(timeout, K_FOREVER) &&
		    k_uptime_get() - started >= timeout_ms) {
			return -ETIMEDOUT;
		}

		k_sleep(K_MSEC(1));
	}

	uint32_t value = 0;

	/*
	 * PD_SCK HIGH 60 us'yi gecerse ADC uyur. Darbeleri kisa
	 * kritik bolgede tutarak bu riski ve zamanlama bozulmasini onle.
	 */
	unsigned int key = irq_lock();

	for (int i = 0; i < 24; i++) {
		gpio_pin_set_dt(&pressure_sck, 1);
		k_busy_wait(HX711_CLOCK_DELAY_US);

		value = (value << 1) |
			(gpio_pin_get_dt(&pressure_out) > 0 ? 1U : 0U);

		gpio_pin_set_dt(&pressure_sck, 0);
		k_busy_wait(HX711_CLOCK_DELAY_US);
	}

	/* Toplam 27 darbe: sonraki donusum HX711 Kanal A, kazanc x64. */
	for (int i = 24; i < HX711_CHANNEL_A_GAIN_64_PULSES; i++) {
		gpio_pin_set_dt(&pressure_sck, 1);
		k_busy_wait(HX711_CLOCK_DELAY_US);
		gpio_pin_set_dt(&pressure_sck, 0);
		k_busy_wait(HX711_CLOCK_DELAY_US);
	}

	irq_unlock(key);
	value &= 0xFFFFFFU;

	if (value == HX711_POSITIVE_SATURATION ||
	    value == HX711_NEGATIVE_SATURATION) {
		return -EOVERFLOW;
	}

	/* 24-bit two's-complement isaret genisletme. */
	if ((value & 0x800000U) != 0U) {
		value |= 0xFF000000U;
	}

	*raw = (int32_t)value;
	return 0;
}

int pressure_sensor_read_raw(
	int32_t *raw,
	k_timeout_t timeout
)
{
	if (!initialized) {
		return -ENODEV;
	}

	if (raw == NULL) {
		return -EINVAL;
	}

	while (settling_samples_remaining > 0) {
		int32_t discarded;
		int result = read_one_conversion(&discarded, timeout);

		if (result != 0) {
			return result;
		}

		settling_samples_remaining--;
	}

	return read_one_conversion(raw, timeout);
}

static int read_statistics(
	uint8_t sample_count,
	struct raw_statistics *statistics
)
{
	if (sample_count == 0 || statistics == NULL) {
		return -EINVAL;
	}

	int64_t sum = 0;
	int32_t minimum = INT32_MAX;
	int32_t maximum = INT32_MIN;

	for (uint8_t i = 0; i < sample_count; i++) {
		int32_t raw;
		int result = pressure_sensor_read_raw(&raw, K_MSEC(150));

		if (result != 0) {
			return result;
		}

		sum += raw;
		minimum = MIN(minimum, raw);
		maximum = MAX(maximum, raw);
	}

	uint8_t used_count = sample_count;

	/* Tek bir ani alt ve ust sapmayi ortalamadan cikar. */
	if (sample_count >= 5) {
		sum -= minimum;
		sum -= maximum;
		used_count -= 2;
	}

	statistics->average = (int32_t)(sum / used_count);
	statistics->minimum = minimum;
	statistics->maximum = maximum;

	return 0;
}

static bool point_is_stable(
	const struct raw_statistics *statistics,
	int64_t span
)
{
	int64_t spread =
		(int64_t)statistics->maximum - statistics->minimum;
	int64_t allowed = MAX(
		absolute_i64(span) * MAX_POINT_SPREAD_PERCENT / 100,
		MIN_POINT_SPREAD_COUNTS
	);

	return spread <= allowed;
}

int pressure_sensor_tare(uint8_t sample_count)
{
	struct raw_statistics statistics;
	int result = read_statistics(sample_count, &statistics);

	if (result != 0) {
		return result;
	}

	if (raw_near_rail(statistics.average)) {
		return -EOVERFLOW;
	}

	if ((int64_t)statistics.maximum - statistics.minimum >
	    MAX_ZERO_SPREAD_COUNTS) {
		return -EAGAIN;
	}

	runtime_zero_raw = statistics.average;
	runtime_tared = true;
	pending_calibration = false;

	return 0;
}

int pressure_sensor_calibrate_span(
	int32_t reference_kpa_x100,
	uint8_t sample_count
)
{
	if (!runtime_tared) {
		return -EACCES;
	}

	if (reference_kpa_x100 <= 0 ||
	    reference_kpa_x100 >= SENSOR_PHYSICAL_LIMIT_KPA_X100) {
		return -EINVAL;
	}

	struct raw_statistics statistics;
	int result = read_statistics(sample_count, &statistics);

	if (result != 0) {
		return result;
	}

	if (raw_near_rail(runtime_zero_raw) ||
	    raw_near_rail(statistics.average)) {
		return -EOVERFLOW;
	}

	int32_t span = statistics.average - runtime_zero_raw;

	if (span > -1000 && span < 1000) {
		return -ERANGE;
	}

	if (!point_is_stable(&statistics, span)) {
		return -EAGAIN;
	}

	pending_reference_raw = statistics.average;
	pending_span_raw = span;
	pending_reference_kpa_x100 = reference_kpa_x100;
	pending_calibration = true;

	last_counts_per_kpa = (int32_t)(
		(absolute_i64(span) * 100) /
		reference_kpa_x100
	);

	return 0;
}

int pressure_sensor_validate_span(
	int32_t validation_kpa_x100,
	uint8_t sample_count
)
{
	if (!runtime_tared || !pending_calibration) {
		return -EACCES;
	}

	if (validation_kpa_x100 <= pending_reference_kpa_x100 ||
	    validation_kpa_x100 > SENSOR_PHYSICAL_LIMIT_KPA_X100) {
		return -EINVAL;
	}

	struct raw_statistics statistics;
	int result = read_statistics(sample_count, &statistics);

	if (result != 0) {
		return result;
	}

	if (raw_near_rail(statistics.average)) {
		pending_calibration = false;
		return -EOVERFLOW;
	}

	int32_t full_span = statistics.average - runtime_zero_raw;
	int32_t second_span = statistics.average - pending_reference_raw;

	/* Basinc artarken iki bolgenin isareti ayni olmali. */
	if ((pending_span_raw > 0 && second_span <= 0) ||
	    (pending_span_raw < 0 && second_span >= 0)) {
		pending_calibration = false;
		return -EDOM;
	}

	if (absolute_i64(full_span) <= absolute_i64(pending_span_raw) ||
	    !point_is_stable(&statistics, full_span)) {
		pending_calibration = false;
		return -EAGAIN;
	}

	/*
	 * slope(0..P1) ile slope(P1..P2) bolmesiz karsilastirilir:
	 * |span1|*(P2-P1) == |span2|*P1.
	 */
	int64_t first_scaled =
		absolute_i64(pending_span_raw) *
		(validation_kpa_x100 - pending_reference_kpa_x100);
	int64_t second_scaled =
		absolute_i64(second_span) *
		pending_reference_kpa_x100;
	int64_t largest = MAX(first_scaled, second_scaled);
	int64_t difference = absolute_i64(first_scaled - second_scaled);

	if (largest == 0 ||
	    difference * 100 >
		largest * MAX_SLOPE_DIFFERENCE_PERCENT) {
		pending_calibration = false;
		return -EDOM;
	}

	calibration.magic = PRESSURE_MAGIC;
	calibration.version = PRESSURE_VERSION;
	calibration.span_raw = pending_span_raw;
	calibration.reference_kpa_x100 =
		pending_reference_kpa_x100;
	calibration.validated_limit_kpa_x100 =
		validation_kpa_x100;

	pending_calibration = false;
	return save_calibration();
}

int pressure_sensor_read_kpa_x100(
	int32_t *kpa_x100,
	k_timeout_t timeout
)
{
	if (kpa_x100 == NULL) {
		return -EINVAL;
	}

	if (!runtime_tared) {
		return -EACCES;
	}

	int32_t raw;
	int result = pressure_sensor_read_raw(&raw, timeout);

	if (result != 0) {
		return result;
	}

	if (raw_near_rail(raw)) {
		return -EOVERFLOW;
	}

	int64_t delta = (int64_t)raw - runtime_zero_raw;
	/*
	 * Ortam sifirindan sonraki ham farki nominal acik kaynak
	 * egimiyle tahmini kPa'ya cevir. Sonuc kalibre edilmis degildir.
	 */
	int32_t pressure = (int32_t)(
		(delta * 100) / NOMINAL_COUNTS_PER_KPA
	);

	/* Sensor elemaninin fiziksel etiketinin disini asla raporlama. */
	if (pressure > SENSOR_PHYSICAL_LIMIT_KPA_X100 ||
	    pressure < -SENSOR_PHYSICAL_LIMIT_KPA_X100) {
		return -ERANGE;
	}

	*kpa_x100 = pressure;
	return 0;
}
