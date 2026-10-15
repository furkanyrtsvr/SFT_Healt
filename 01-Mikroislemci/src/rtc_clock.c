#include "rtc_clock.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#define USER_NODE DT_PATH(zephyr_user)

#define DS1302_CONTROL_WRITE 0x8E
#define DS1302_TRICKLE_WRITE 0x90
#define DS1302_CLOCK_BURST_WRITE 0xBE
#define DS1302_CLOCK_BURST_READ 0xBF

#define DS1302_CLOCK_REGISTER_COUNT 8
#define DS1302_HALF_CYCLE_US 2
#define DS1302_CE_SETUP_US 4

enum ds1302_clock_register {
	DS1302_REG_SECONDS,
	DS1302_REG_MINUTES,
	DS1302_REG_HOURS,
	DS1302_REG_DATE,
	DS1302_REG_MONTH,
	DS1302_REG_WEEKDAY,
	DS1302_REG_YEAR,
	DS1302_REG_CONTROL,
};

static const struct gpio_dt_spec rtc_clk =
	GPIO_DT_SPEC_GET(USER_NODE, rtc_clk_gpios);
static const struct gpio_dt_spec rtc_data =
	GPIO_DT_SPEC_GET(USER_NODE, rtc_data_gpios);
static const struct gpio_dt_spec rtc_ce =
	GPIO_DT_SPEC_GET(USER_NODE, rtc_ce_gpios);

static bool rtc_initialized;

static uint8_t bin_to_bcd(uint8_t value)
{
	return (uint8_t)(((value / 10U) << 4) | (value % 10U));
}

static int decode_bcd(uint8_t value, uint8_t mask,
		      uint8_t maximum, uint8_t *decoded)
{
	uint8_t bcd = value & mask;
	uint8_t low = bcd & 0x0FU;
	uint8_t high = (bcd >> 4) & 0x0FU;
	uint8_t result;

	if (decoded == NULL || low > 9U || high > 9U) {
		return -ENODATA;
	}

	result = (uint8_t)(high * 10U + low);
	if (result > maximum) {
		return -ENODATA;
	}

	*decoded = result;
	return 0;
}

static bool is_leap_year(uint16_t year)
{
	return (year % 4U == 0U && year % 100U != 0U) ||
		year % 400U == 0U;
}

static uint8_t days_in_month(uint16_t year, uint8_t month)
{
	static const uint8_t days[] = {
		31, 28, 31, 30, 31, 30,
		31, 31, 30, 31, 30, 31
	};

	if (month < 1U || month > 12U) {
		return 0;
	}

	if (month == 2U && is_leap_year(year)) {
		return 29;
	}

	return days[month - 1U];
}

static bool valid_datetime(const struct rtc_datetime *time)
{
	return time != NULL &&
		time->year >= 2000U && time->year <= 2099U &&
		time->month >= 1U && time->month <= 12U &&
		time->day >= 1U &&
		time->day <= days_in_month(time->year, time->month) &&
		time->hour <= 23U && time->minute <= 59U &&
		time->second <= 59U && time->weekday <= 6U;
}

static int data_as_output(void)
{
	return gpio_pin_configure_dt(&rtc_data, GPIO_OUTPUT_INACTIVE);
}

static int data_as_input(void)
{
	return gpio_pin_configure_dt(&rtc_data, GPIO_INPUT);
}

static int begin_transfer(void)
{
	int result;

	result = gpio_pin_set_dt(&rtc_ce, 0);
	if (result != 0) {
		return result;
	}

	result = gpio_pin_set_dt(&rtc_clk, 0);
	if (result != 0) {
		return result;
	}

	result = data_as_output();
	if (result != 0) {
		return result;
	}

	k_busy_wait(DS1302_CE_SETUP_US);
	result = gpio_pin_set_dt(&rtc_ce, 1);
	k_busy_wait(DS1302_CE_SETUP_US);

	return result;
}

static void end_transfer(void)
{
	(void)gpio_pin_set_dt(&rtc_ce, 0);
	(void)gpio_pin_set_dt(&rtc_clk, 0);
	k_busy_wait(DS1302_CE_SETUP_US);
	(void)data_as_output();
	(void)gpio_pin_set_dt(&rtc_data, 0);
}

static int write_byte_lsb(uint8_t value)
{
	for (uint8_t bit = 0; bit < 8U; bit++) {
		int result = gpio_pin_set_dt(
			&rtc_data,
			(value >> bit) & 1U
		);

		if (result != 0) {
			return result;
		}

		k_busy_wait(DS1302_HALF_CYCLE_US);
		result = gpio_pin_set_dt(&rtc_clk, 1);
		if (result != 0) {
			return result;
		}

		k_busy_wait(DS1302_HALF_CYCLE_US);
		result = gpio_pin_set_dt(&rtc_clk, 0);
		if (result != 0) {
			return result;
		}

		k_busy_wait(DS1302_HALF_CYCLE_US);
	}

	return 0;
}

/*
 * DS1302 okuma komutunun son yukselen kenarindan sonra MCU, I/O hattini
 * birakmalidir. Sonraki dusen kenarda DS1302 ilk veri bitini surer.
 */
static int write_read_command(uint8_t command)
{
	for (uint8_t bit = 0; bit < 8U; bit++) {
		int result = gpio_pin_set_dt(
			&rtc_data,
			(command >> bit) & 1U
		);

		if (result != 0) {
			return result;
		}

		k_busy_wait(DS1302_HALF_CYCLE_US);
		result = gpio_pin_set_dt(&rtc_clk, 1);
		if (result != 0) {
			return result;
		}

		k_busy_wait(DS1302_HALF_CYCLE_US);
		if (bit == 7U) {
			result = data_as_input();
			if (result != 0) {
				return result;
			}
		}

		result = gpio_pin_set_dt(&rtc_clk, 0);
		if (result != 0) {
			return result;
		}

		k_busy_wait(DS1302_HALF_CYCLE_US);
	}

	return 0;
}

static int read_bytes_lsb(uint8_t *values, size_t length)
{
	size_t total_bits;

	if (values == NULL || length == 0U) {
		return -EINVAL;
	}

	memset(values, 0, length);
	total_bits = length * 8U;

	for (size_t bit = 0; bit < total_bits; bit++) {
		int state = gpio_pin_get_dt(&rtc_data);

		if (state < 0) {
			return state;
		}

		if (state > 0) {
			values[bit / 8U] |=
				(uint8_t)(1U << (bit % 8U));
		}

		if (bit + 1U == total_bits) {
			break;
		}

		if (gpio_pin_set_dt(&rtc_clk, 1) != 0) {
			return -EIO;
		}
		k_busy_wait(DS1302_HALF_CYCLE_US);

		if (gpio_pin_set_dt(&rtc_clk, 0) != 0) {
			return -EIO;
		}
		k_busy_wait(DS1302_HALF_CYCLE_US);
	}

	return 0;
}

static int write_register(uint8_t command, uint8_t value)
{
	int result = begin_transfer();

	if (result == 0) {
		result = write_byte_lsb(command);
	}
	if (result == 0) {
		result = write_byte_lsb(value);
	}

	end_transfer();
	return result;
}

static int clock_burst_read(uint8_t values[DS1302_CLOCK_REGISTER_COUNT])
{
	int result = begin_transfer();

	if (result == 0) {
		result = write_read_command(DS1302_CLOCK_BURST_READ);
	}
	if (result == 0) {
		result = read_bytes_lsb(
			values,
			DS1302_CLOCK_REGISTER_COUNT
		);
	}

	end_transfer();
	return result;
}

static int clock_burst_write(
	const uint8_t values[DS1302_CLOCK_REGISTER_COUNT])
{
	int result = begin_transfer();

	if (result == 0) {
		result = write_byte_lsb(DS1302_CLOCK_BURST_WRITE);
	}

	for (size_t i = 0;
	     result == 0 && i < DS1302_CLOCK_REGISTER_COUNT;
	     i++) {
		result = write_byte_lsb(values[i]);
	}

	end_transfer();
	return result;
}

static int decode_hour(uint8_t value, uint8_t *hour)
{
	uint8_t decoded;
	int result;

	if ((value & 0x80U) == 0U) {
		return decode_bcd(value, 0x3FU, 23U, hour);
	}

	result = decode_bcd(value, 0x1FU, 12U, &decoded);
	if (result != 0 || decoded == 0U) {
		return -ENODATA;
	}

	*hour = (uint8_t)(decoded % 12U);
	if ((value & 0x20U) != 0U) {
		*hour = (uint8_t)(*hour + 12U);
	}

	return 0;
}

int rtc_clock_set(const struct rtc_datetime *time)
{
	uint8_t values[DS1302_CLOCK_REGISTER_COUNT];
	int result;

	if (!rtc_initialized) {
		return -ENODEV;
	}
	if (!valid_datetime(time)) {
		return -EINVAL;
	}

	/* WP kapatilir; trickle sarj CR2032 icin devre disi tutulur. */
	result = write_register(DS1302_CONTROL_WRITE, 0x00);
	if (result != 0) {
		return result;
	}

	result = write_register(DS1302_TRICKLE_WRITE, 0x00);
	if (result != 0) {
		return result;
	}

	values[DS1302_REG_SECONDS] = bin_to_bcd(time->second) & 0x7FU;
	values[DS1302_REG_MINUTES] = bin_to_bcd(time->minute);
	values[DS1302_REG_HOURS] = bin_to_bcd(time->hour); /* 24 saat */
	values[DS1302_REG_DATE] = bin_to_bcd(time->day);
	values[DS1302_REG_MONTH] = bin_to_bcd(time->month);
	values[DS1302_REG_WEEKDAY] = (uint8_t)(time->weekday + 1U);
	values[DS1302_REG_YEAR] =
		bin_to_bcd((uint8_t)(time->year - 2000U));
	values[DS1302_REG_CONTROL] = 0x00;

	result = clock_burst_write(values);
	if (result != 0) {
		return result;
	}

	/* Saat ayarlandiktan sonra clock/calendar registerlarini koru. */
	return write_register(DS1302_CONTROL_WRITE, 0x80);
}

int rtc_clock_get(struct rtc_datetime *time)
{
	uint8_t values[DS1302_CLOCK_REGISTER_COUNT];
	struct rtc_datetime decoded = { 0 };
	uint8_t year;
	int result;

	if (time == NULL) {
		return -EINVAL;
	}
	if (!rtc_initialized) {
		return -ENODEV;
	}

	result = clock_burst_read(values);
	if (result != 0) {
		return result;
	}

	/* CH=1 ise osilator durmustur; bu veri gecerli saat sayilmaz. */
	if ((values[DS1302_REG_SECONDS] & 0x80U) != 0U) {
		return -ENODATA;
	}

	if (decode_bcd(values[DS1302_REG_SECONDS], 0x7FU, 59U,
		       &decoded.second) != 0 ||
	    decode_bcd(values[DS1302_REG_MINUTES], 0x7FU, 59U,
		       &decoded.minute) != 0 ||
	    decode_hour(values[DS1302_REG_HOURS], &decoded.hour) != 0 ||
	    decode_bcd(values[DS1302_REG_DATE], 0x3FU, 31U,
		       &decoded.day) != 0 ||
	    decode_bcd(values[DS1302_REG_MONTH], 0x1FU, 12U,
		       &decoded.month) != 0 ||
	    decode_bcd(values[DS1302_REG_YEAR], 0xFFU, 99U, &year) != 0) {
		return -ENODATA;
	}

	if (decoded.day == 0U || decoded.month == 0U ||
	    values[DS1302_REG_WEEKDAY] < 1U ||
	    values[DS1302_REG_WEEKDAY] > 7U) {
		return -ENODATA;
	}

	decoded.year = (uint16_t)(2000U + year);
	decoded.weekday =
		(uint8_t)(values[DS1302_REG_WEEKDAY] - 1U);
	decoded.valid = valid_datetime(&decoded);
	if (!decoded.valid) {
		return -ENODATA;
	}

	*time = decoded;
	return 0;
}

int rtc_clock_init(void)
{
	rtc_initialized = false;

	if (!gpio_is_ready_dt(&rtc_clk) ||
	    !gpio_is_ready_dt(&rtc_data) ||
	    !gpio_is_ready_dt(&rtc_ce)) {
		return -ENODEV;
	}

	if (gpio_pin_configure_dt(&rtc_clk, GPIO_OUTPUT_INACTIVE) != 0 ||
	    gpio_pin_configure_dt(&rtc_data, GPIO_OUTPUT_INACTIVE) != 0 ||
	    gpio_pin_configure_dt(&rtc_ce, GPIO_OUTPUT_INACTIVE) != 0) {
		return -EIO;
	}

	(void)gpio_pin_set_dt(&rtc_ce, 0);
	(void)gpio_pin_set_dt(&rtc_clk, 0);
	(void)gpio_pin_set_dt(&rtc_data, 0);
	k_busy_wait(DS1302_CE_SETUP_US);

	/* Init mevcut RTC tarihini okumaz, sifirlamaz veya uzerine yazmaz. */
	rtc_initialized = true;
	return 0;
}
