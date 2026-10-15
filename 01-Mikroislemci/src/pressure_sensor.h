#ifndef PRESSURE_SENSOR_H
#define PRESSURE_SENSOR_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>

/*
 * Dort pinli MPS20 sinifi sensor + HX711 modulu.
 * OUT dijital DOUT, SCK ise HX711 PD_SCK pinidir.
 * Surucu Kanal A'yi x64 kazanc ile okur.
 * Basinc degerleri 0.01 kPa birimindedir (1234 = 12.34 kPa).
 */

int pressure_sensor_init(void);

bool pressure_sensor_data_ready(void);
bool pressure_sensor_is_calibrated(void);
bool pressure_sensor_is_tared(void);
int32_t pressure_sensor_counts_per_kpa(void);
int32_t pressure_sensor_validated_limit_kpa_x100(void);

int pressure_sensor_read_raw(
	int32_t *raw,
	k_timeout_t timeout
);

int pressure_sensor_read_kpa_x100(
	int32_t *kpa_x100,
	k_timeout_t timeout
);

/* Ortam basincinda sifir noktasi alir. */
int pressure_sensor_tare(uint8_t sample_count);

/*
 * Ilk bilinen basinc noktasini RAM'e alir; henuz kaydetmez.
 * Ornek: 30.00 kPa icin reference_kpa_x100 = 3000.
 */
int pressure_sensor_calibrate_span(
	int32_t reference_kpa_x100,
	uint8_t sample_count
);

/*
 * Daha yuksek ikinci noktada egim/doygunluk kontrolu yapar.
 * Iki bolgenin egimi uyusursa kalibrasyonu kalici kaydeder.
 */
int pressure_sensor_validate_span(
	int32_t validation_kpa_x100,
	uint8_t sample_count
);

#endif
