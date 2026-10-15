#ifndef BLE_HEALTH_H
#define BLE_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

#include "profile_store.h"
#include "measurement_log.h"
#include "rtc_clock.h"

int ble_health_init(void);
bool ble_health_is_connected(void);
bool ble_health_take_sync_request(void);
bool ble_health_take_time_update(struct rtc_datetime *time);

struct ble_profile_update {
	int index;
	struct user_profile profile;
};

bool ble_health_take_profile_update(
	struct ble_profile_update *update
);
bool ble_health_take_profile_delete(int *index);
bool ble_health_take_profile_select(int *index);
bool ble_health_take_session_end(void);

void ble_health_send_profile(
	int index,
	const struct user_profile *profile
);

void ble_health_send_heart(
	int profile_index,
	uint16_t bpm
);

void ble_health_send_mep(
	int profile_index,
	int32_t measured_kpa_x100,
	int32_t predicted_kpa_x100,
	uint16_t percent
);

void ble_health_send_mip(
	int profile_index,
	int32_t measured_kpa_x100,
	int32_t predicted_kpa_x100,
	uint16_t percent
);

void ble_health_send_environment(
	int16_t temperature_c_x10,
	uint16_t humidity_percent_x10,
	const char *quality
);

void ble_health_send_record(
	const struct measurement_record *record
);

void ble_health_send_sync_done(void);
void ble_health_send_status(const char *status);

#endif
