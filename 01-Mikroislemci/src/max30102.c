#include "max30102.h"

#include <errno.h>

#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#define MAX30102_ADDRESS          0x57

#define REG_INT_STATUS_1          0x00
#define REG_INT_STATUS_2          0x01
#define REG_INT_ENABLE_1          0x02
#define REG_INT_ENABLE_2          0x03

#define REG_FIFO_WR_PTR           0x04
#define REG_OVF_COUNTER           0x05
#define REG_FIFO_RD_PTR           0x06
#define REG_FIFO_DATA             0x07
#define REG_FIFO_CONFIG           0x08

#define REG_MODE_CONFIG           0x09
#define REG_SPO2_CONFIG           0x0A
#define REG_LED1_PA               0x0C
#define REG_LED2_PA               0x0D

#define REG_PART_ID               0xFF
#define EXPECTED_PART_ID          0x15

static int write_register(
	const struct device *i2c,
	uint8_t reg,
	uint8_t value
)
{
	return i2c_reg_write_byte(
		i2c,
		MAX30102_ADDRESS,
		reg,
		value
	);
}

static int read_register(
	const struct device *i2c,
	uint8_t reg,
	uint8_t *value
)
{
	return i2c_reg_read_byte(
		i2c,
		MAX30102_ADDRESS,
		reg,
		value
	);
}

int max30102_init(
	const struct device *i2c
)
{
	uint8_t value;
	int err;

	if (!device_is_ready(i2c)) {
		return -ENODEV;
	}

	err = read_register(
		i2c,
		REG_PART_ID,
		&value
	);

	if (err != 0) {
		return err;
	}

	if (value != EXPECTED_PART_ID) {
		return -ENODEV;
	}

	/* Yazilimsal reset */
	err = write_register(
		i2c,
		REG_MODE_CONFIG,
		0x40
	);

	if (err != 0) {
		return err;
	}

	for (int i = 0; i < 100; i++) {
		err = read_register(
			i2c,
			REG_MODE_CONFIG,
			&value
		);

		if (err != 0) {
			return err;
		}

		if ((value & 0x40) == 0) {
			break;
		}

		k_sleep(K_MSEC(1));
	}

	/* Kesme kullanilmiyor */
	if (write_register(
		    i2c,
		    REG_INT_ENABLE_1,
		    0x00) != 0 ||
	    write_register(
		    i2c,
		    REG_INT_ENABLE_2,
		    0x00) != 0) {
		return -EIO;
	}

	/* FIFO temizligi */
	if (write_register(
		    i2c,
		    REG_FIFO_WR_PTR,
		    0x00) != 0 ||
	    write_register(
		    i2c,
		    REG_OVF_COUNTER,
		    0x00) != 0 ||
	    write_register(
		    i2c,
		    REG_FIFO_RD_PTR,
		    0x00) != 0) {
		return -EIO;
	}

	/*
	 * Ortalama 1
	 * FIFO rollover kapali
	 * Almost-full 15
	 */
	if (write_register(
		    i2c,
		    REG_FIFO_CONFIG,
		    0x0F) != 0) {
		return -EIO;
	}

	/*
	 * ADC araligi 4096 nA
	 * 100 sample/s
	 * 411 us pulse
	 * 18 bit
	 */
	if (write_register(
		    i2c,
		    REG_SPO2_CONFIG,
		    0x27) != 0) {
		return -EIO;
	}

	/*
	 * Nabiz icin IR kanali biraz daha guclu tutulur.
	 * Kirmizi: yaklasik 4.8 mA, IR: yaklasik 10 mA.
	 */
	if (write_register(
		    i2c,
		    REG_LED1_PA,
		    0x18) != 0 ||
	    write_register(
		    i2c,
		    REG_LED2_PA,
		    0x32) != 0) {
		return -EIO;
	}

	/*
	 * SpO2 modu
	 * FIFO'da RED ve IR kanallari
	 */
	if (write_register(
		    i2c,
		    REG_MODE_CONFIG,
		    0x03) != 0) {
		return -EIO;
	}

	/* Eski durum bayraklarini temizle */
	(void)read_register(
		i2c,
		REG_INT_STATUS_1,
		&value
	);

	(void)read_register(
		i2c,
		REG_INT_STATUS_2,
		&value
	);

	return 0;
}

int max30102_samples_available(
	const struct device *i2c
)
{
	uint8_t write_pointer;
	uint8_t read_pointer;
	int err;

	err = read_register(
		i2c,
		REG_FIFO_WR_PTR,
		&write_pointer
	);

	if (err != 0) {
		return err;
	}

	err = read_register(
		i2c,
		REG_FIFO_RD_PTR,
		&read_pointer
	);

	if (err != 0) {
		return err;
	}

	return (write_pointer - read_pointer) & 0x1F;
}

int max30102_read_sample(
	const struct device *i2c,
	struct max30102_sample *sample
)
{
	uint8_t data[6];
	int err;

	if (sample == NULL) {
		return -EINVAL;
	}

	err = i2c_burst_read(
		i2c,
		MAX30102_ADDRESS,
		REG_FIFO_DATA,
		data,
		sizeof(data)
	);

	if (err != 0) {
		return err;
	}

	sample->red =
		(((uint32_t)data[0] << 16) |
		 ((uint32_t)data[1] << 8) |
		 data[2]) & 0x3FFFF;

	sample->ir =
		(((uint32_t)data[3] << 16) |
		 ((uint32_t)data[4] << 8) |
		 data[5]) & 0x3FFFF;

	return 0;
}
