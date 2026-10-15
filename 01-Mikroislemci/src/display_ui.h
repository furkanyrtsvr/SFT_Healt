#ifndef DISPLAY_UI_H
#define DISPLAY_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "heart_rate.h"
#include "measurement_log.h"
#include "profile_store.h"
#include "rtc_clock.h"

enum ui_menu_item {
	UI_MENU_CLOCK,
	UI_MENU_HEART,
	UI_MENU_MEP,
	UI_MENU_MIP,
	UI_MENU_ENVIRONMENT,
	UI_MENU_PROFILE,
	UI_MENU_RECORDS,
	UI_MENU_BLUETOOTH,
	UI_MENU_SETTINGS,
	UI_MENU_COUNT,
};

int ui_display_init(void);

void ui_show_profile_selector(
	const char *name,
	bool is_new
);

void ui_show_main_menu(
	const char *profile_name,
	enum ui_menu_item item
);

void ui_show_clock(
	const struct rtc_datetime *time
);

void ui_show_name_editor(
	const char *name,
	char selected
);

void ui_show_gender(
	enum profile_gender gender
);

void ui_show_age(int age);
void ui_show_height(uint16_t height_cm);
void ui_show_weight(uint16_t weight_kg);

void ui_show_heart(
	bool sensor_ready,
	const struct heart_rate_state *heart,
	bool beat_flash
);

void ui_show_mep_ready(
	bool sensor_ready,
	bool calibrated
);

void ui_show_mep_active(
	uint8_t attempt_number,
	int32_t live_kpa_x100,
	bool waiting_for_blow
);

void ui_show_mep_attempt(
	uint8_t attempt_number,
	int32_t value_kpa_x100,
	bool valid
);

void ui_show_mep_result(
	int32_t measured_kpa_x100,
	int32_t predicted_kpa_x100,
	uint16_t percent,
	bool repeatable,
	bool reference_valid
);

void ui_show_mip_ready(bool sensor_ready);
void ui_show_mip_active(
	uint8_t attempt_number,
	int32_t live_kpa_x100,
	bool waiting_for_inhale
);
void ui_show_mip_attempt(
	uint8_t attempt_number,
	int32_t value_kpa_x100,
	bool valid
);
void ui_show_mip_result(
	int32_t measured_kpa_x100,
	int32_t predicted_kpa_x100,
	uint16_t percent,
	bool repeatable,
	bool reference_valid
);

void ui_show_environment(
	bool sensor_ready,
	int16_t temperature_c_x10,
	uint16_t humidity_percent_x10,
	const char *quality
);

void ui_show_profile_info(
	const struct user_profile *profile
);

void ui_show_record(
	const char *profile_name,
	const struct measurement_record *record,
	size_t record_index,
	size_t record_count
);

void ui_show_bluetooth(
	bool ready,
	bool connected
);
void ui_show_settings(uint8_t selected);
void ui_show_rtc_editor(
	const struct rtc_datetime *time,
	uint8_t field
);
void ui_show_profile_delete_confirm(const char *name);
void ui_show_profile_deleted(void);
void ui_show_pressure_cal_zero(void);
void ui_show_pressure_cal_reference(
	int32_t reference_kpa_x100
);
void ui_show_pressure_cal_apply(
	int32_t validation_kpa_x100
);
void ui_show_pressure_cal_result(
	int result,
	int32_t counts_per_kpa,
	int32_t validated_limit_kpa_x100
);
void ui_show_pressure_saturation(void);
void ui_show_saved(void);
void ui_show_sensor_error(void);

#endif
