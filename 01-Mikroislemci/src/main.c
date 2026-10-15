#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#include "ble_health.h"
#include "display_ui.h"
#include "environment_sensor.h"
#include "heart_rate.h"
#include "input_encoder.h"
#include "max30102.h"
#include "measurement_log.h"
#include "mep_test.h"
#include "pressure_sensor.h"
#include "profile_store.h"
#include "rtc_clock.h"
#include "storage_reset.h"

enum app_screen {
	SCREEN_PROFILE_SELECT,
	SCREEN_MAIN_MENU,
	SCREEN_NAME_EDIT,
	SCREEN_GENDER_EDIT,
	SCREEN_AGE_EDIT,
	SCREEN_HEIGHT_EDIT,
	SCREEN_WEIGHT_EDIT,
	SCREEN_HEART,
	SCREEN_MEP_READY,
	SCREEN_MEP_ACTIVE,
	SCREEN_MEP_ATTEMPT,
	SCREEN_MEP_RESULT,
	SCREEN_MIP_READY,
	SCREEN_MIP_ACTIVE,
	SCREEN_MIP_ATTEMPT,
	SCREEN_MIP_RESULT,
	SCREEN_ENVIRONMENT,
	SCREEN_PROFILE_INFO,
	SCREEN_RECORDS,
	SCREEN_BLUETOOTH,
	SCREEN_PRESSURE_SETTINGS,
	SCREEN_RTC_EDIT,
	SCREEN_PROFILE_DELETE_CONFIRM,
	SCREEN_PROFILE_DELETED,
	SCREEN_PRESSURE_CAL_ZERO,
	SCREEN_PRESSURE_CAL_REFERENCE,
	SCREEN_PRESSURE_CAL_APPLY,
	SCREEN_PRESSURE_CAL_RESULT,
	SCREEN_PRESSURE_SATURATION,
	SCREEN_SAVED,
	SCREEN_SENSOR_ERROR,
};

static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ<";

static const struct device *i2c_bus;

static enum app_screen screen = SCREEN_PROFILE_SELECT;
static enum ui_menu_item menu_item = UI_MENU_CLOCK;

static int profile_option;
static int active_profile = -1;
static int new_profile_slot = -1;
static int name_character;
static int settings_option;
static uint8_t rtc_edit_field;

static struct user_profile draft_profile;
static struct heart_rate_state heart;
static struct mep_test_state mep;
static struct mep_test_state mip;
static struct environment_reading environment;
static struct rtc_datetime current_time;
static struct rtc_datetime rtc_edit_time;

static bool heart_sensor_ready;
static bool pressure_gpio_ready;
static bool bluetooth_ready;
static bool environment_sensor_ready;
static bool rtc_ready;
static bool ui_dirty = true;
static size_t record_cursor;
static uint8_t environment_failures;

static int64_t beat_flash_until;
static int64_t saved_screen_until;
static int64_t last_pressure_sample_ms;
static int64_t last_clock_update_ms;
static uint16_t last_stable_bpm;

static int32_t result_mep_kpa_x100;
static int32_t result_predicted_kpa_x100;
static uint16_t result_percent;
static bool result_repeatable;
static bool result_reference_valid;

static int32_t result_mip_kpa_x100;
static int32_t result_mip_predicted_x100;
static uint16_t result_mip_percent;
static bool result_mip_repeatable;
static bool result_mip_reference_valid;

#define PRESSURE_CALIBRATION_KPA_X100 3000
#define PRESSURE_VALIDATION_KPA_X100 4000
#define PRESSURE_CALIBRATION_SAMPLES 40

static int pressure_calibration_result;

static int profile_count(void)
{
	int count = 0;

	for (int i = 0; i < PROFILE_MAX; i++) {
		if (profile_store_get(i) != NULL) {
			count++;
		}
	}

	return count;
}

static int option_count(void)
{
	int count = profile_count();

	return count + (count < PROFILE_MAX ? 1 : 0);
}

static int profile_from_option(int option)
{
	int current = 0;

	for (int i = 0; i < PROFILE_MAX; i++) {
		if (profile_store_get(i) == NULL) {
			continue;
		}

		if (current == option) {
			return i;
		}

		current++;
	}

	return -1;
}

static int wrap_value(int value, int count)
{
	if (count <= 0) {
		return 0;
	}

	if (value < 0) {
		return count - 1;
	}

	if (value >= count) {
		return 0;
	}

	return value;
}

static int wrap_range(int value, int minimum, int maximum)
{
	if (value < minimum) {
		return maximum;
	}
	if (value > maximum) {
		return minimum;
	}
	return value;
}

static uint8_t days_in_month(uint16_t year, uint8_t month)
{
	static const uint8_t days[] = {
		31, 28, 31, 30, 31, 30,
		31, 31, 30, 31, 30, 31
	};
	uint8_t result = days[month - 1U];

	if (month == 2U &&
	    ((year % 4U == 0U && year % 100U != 0U) ||
	     year % 400U == 0U)) {
		result = 29;
	}

	return result;
}

static bool rtc_read_with_retry(struct rtc_datetime *time,
				uint8_t attempts,
				uint32_t delay_ms)
{
	if (time == NULL || attempts == 0U) {
		return false;
	}

	for (uint8_t attempt = 0; attempt < attempts; attempt++) {
		if (rtc_clock_get(time) == 0) {
			return true;
		}

		if (attempt + 1U < attempts) {
			k_msleep(delay_ms);
		}
	}

	return false;
}

static void advance_clock_second(struct rtc_datetime *time)
{
	if (time == NULL || !time->valid) {
		return;
	}

	if (++time->second < 60U) {
		return;
	}
	time->second = 0;
	if (++time->minute < 60U) {
		return;
	}
	time->minute = 0;
	if (++time->hour < 24U) {
		return;
	}
	time->hour = 0;
	time->weekday = (uint8_t)((time->weekday + 1U) % 7U);
	if (++time->day <= days_in_month(time->year, time->month)) {
		return;
	}
	time->day = 1;
	if (++time->month <= 12U) {
		return;
	}
	time->month = 1;
	time->year++;
}

static const struct user_profile *active_profile_data(void)
{
	return profile_store_get(active_profile);
}

static void show_screen(void)
{
	const struct user_profile *profile;

	switch (screen) {
	case SCREEN_PROFILE_SELECT: {
		int index = profile_from_option(profile_option);

		profile = profile_store_get(index);

		ui_show_profile_selector(
			profile != NULL ? profile->name : "",
			index < 0
		);
		break;
	}

	case SCREEN_MAIN_MENU:
		profile = active_profile_data();

		if (profile != NULL) {
			if (menu_item == UI_MENU_CLOCK) {
				ui_show_clock(&current_time);
			} else {
				ui_show_main_menu(profile->name, menu_item);
			}
		}
		break;

	case SCREEN_NAME_EDIT:
		ui_show_name_editor(
			draft_profile.name,
			alphabet[name_character]
		);
		break;

	case SCREEN_GENDER_EDIT:
		ui_show_gender(
			(enum profile_gender)draft_profile.gender
		);
		break;

	case SCREEN_AGE_EDIT:
		ui_show_age(draft_profile.age);
		break;

	case SCREEN_HEIGHT_EDIT:
		ui_show_height(draft_profile.height_cm);
		break;

	case SCREEN_WEIGHT_EDIT:
		ui_show_weight(draft_profile.weight_kg);
		break;

	case SCREEN_HEART:
		ui_show_heart(
			heart_sensor_ready,
			&heart,
			k_uptime_get() < beat_flash_until
		);
		break;

	case SCREEN_MEP_READY:
		ui_show_mep_ready(
			pressure_gpio_ready,
			true
		);
		break;

	case SCREEN_MEP_ACTIVE:
		ui_show_mep_active(
			mep.attempt_index + 1,
			mep.live_kpa_x100,
			mep.waiting_for_blow
		);
		break;

	case SCREEN_MEP_ATTEMPT:
		ui_show_mep_attempt(
			mep.attempt_index + 1,
			mep_test_current_value(&mep),
			mep_test_current_valid(&mep)
		);
		break;

	case SCREEN_MEP_RESULT:
		ui_show_mep_result(
			result_mep_kpa_x100,
			result_predicted_kpa_x100,
			result_percent,
			result_repeatable,
			result_reference_valid
		);
		break;

	case SCREEN_MIP_READY:
		ui_show_mip_ready(pressure_gpio_ready);
		break;

	case SCREEN_MIP_ACTIVE:
		ui_show_mip_active(
			mip.attempt_index + 1,
			mip.live_kpa_x100,
			mip.waiting_for_blow
		);
		break;

	case SCREEN_MIP_ATTEMPT:
		ui_show_mip_attempt(
			mip.attempt_index + 1,
			mep_test_current_value(&mip),
			mep_test_current_valid(&mip)
		);
		break;

	case SCREEN_MIP_RESULT:
		ui_show_mip_result(
			result_mip_kpa_x100,
			result_mip_predicted_x100,
			result_mip_percent,
			result_mip_repeatable,
			result_mip_reference_valid
		);
		break;

	case SCREEN_ENVIRONMENT:
		ui_show_environment(
			environment_sensor_ready && environment.valid,
			environment.temperature_c_x10,
			environment.humidity_percent_x10,
			environment_quality_code(environment.quality)
		);
		break;

	case SCREEN_PROFILE_INFO:
		profile = active_profile_data();

		if (profile != NULL) {
			ui_show_profile_info(profile);
		}
		break;

	case SCREEN_RECORDS: {
		profile = active_profile_data();
		if (profile != NULL) {
			struct measurement_record record;
			size_t count = measurement_log_count(active_profile);
			const struct measurement_record *shown = NULL;

			if (count > 0) {
				record_cursor %= count;
				if (measurement_log_get_newest(
					    active_profile,
					    record_cursor,
					    &record) == 0) {
					shown = &record;
				}
			}

			ui_show_record(
				profile->name,
				shown,
				record_cursor,
				count
			);
		}
		break;
	}

	case SCREEN_BLUETOOTH:
		ui_show_bluetooth(
			bluetooth_ready,
			ble_health_is_connected()
		);
		break;

	case SCREEN_PRESSURE_SETTINGS:
		ui_show_settings((uint8_t)settings_option);
		break;

	case SCREEN_RTC_EDIT:
		ui_show_rtc_editor(&rtc_edit_time, rtc_edit_field);
		break;

	case SCREEN_PROFILE_DELETE_CONFIRM:
		profile = active_profile_data();
		ui_show_profile_delete_confirm(
			profile != NULL ? profile->name : "PROFIL"
		);
		break;

	case SCREEN_PROFILE_DELETED:
		ui_show_profile_deleted();
		break;

	case SCREEN_PRESSURE_CAL_ZERO:
		ui_show_pressure_cal_zero();
		break;

	case SCREEN_PRESSURE_CAL_REFERENCE:
		ui_show_pressure_cal_reference(
			PRESSURE_CALIBRATION_KPA_X100
		);
		break;

	case SCREEN_PRESSURE_CAL_APPLY:
		ui_show_pressure_cal_apply(
			PRESSURE_VALIDATION_KPA_X100
		);
		break;

	case SCREEN_PRESSURE_CAL_RESULT:
		ui_show_pressure_cal_result(
			pressure_calibration_result,
			pressure_sensor_counts_per_kpa(),
			pressure_sensor_validated_limit_kpa_x100()
		);
		break;

	case SCREEN_PRESSURE_SATURATION:
		ui_show_pressure_saturation();
		break;

	case SCREEN_SAVED:
		ui_show_saved();
		break;

	case SCREEN_SENSOR_ERROR:
		ui_show_sensor_error();
		break;
	}
}

static void begin_new_profile(void)
{
	new_profile_slot = profile_store_first_free();

	if (new_profile_slot < 0) {
		return;
	}

	memset(&draft_profile, 0, sizeof(draft_profile));

	draft_profile.gender = PROFILE_MAN;
	draft_profile.age = 18;
	draft_profile.height_cm =
		PROFILE_DEFAULT_HEIGHT_CM;
	draft_profile.weight_kg =
		PROFILE_DEFAULT_WEIGHT_KG;

	name_character = 0;
	screen = SCREEN_NAME_EDIT;
	ui_dirty = true;
}

static void begin_profile_edit(void)
{
	const struct user_profile *profile =
		active_profile_data();

	if (profile == NULL || active_profile < 0) {
		return;
	}

	draft_profile = *profile;
	new_profile_slot = active_profile;
	name_character = 0;
	screen = SCREEN_NAME_EDIT;
	ui_dirty = true;
}

static void start_heart_measurement(void)
{
	heart_rate_reset(&heart);
	last_stable_bpm = 0;

	heart_sensor_ready =
		max30102_init(i2c_bus) == 0;

	beat_flash_until = 0;
	screen = SCREEN_HEART;
	ui_dirty = true;
}

static void save_and_send_measurement(
	enum measurement_type type,
	int32_t value_x100,
	int32_t predicted_x100,
	uint16_t percent
)
{
	struct rtc_datetime time;
	struct measurement_record record;

	if (rtc_clock_get(&time) == 0) {
		rtc_ready = true;
		current_time = time;

		if (measurement_log_add(
		    type,
		    (uint8_t)active_profile,
		    value_x100,
		    predicted_x100,
		    percent,
		    &time,
		    &record) == 0) {
			ble_health_send_record(&record);
			return;
		}
	} else {
		rtc_ready = false;
	}

	/* RTC bagli degilse canli Bluetooth olcumu yine kaybolmasin. */
	if (type == MEASUREMENT_HEART_RATE) {
		ble_health_send_heart(
			active_profile,
			(uint16_t)(value_x100 / 100)
		);
	} else if (type == MEASUREMENT_MEP) {
		ble_health_send_mep(
			active_profile,
			value_x100,
			predicted_x100,
			percent
		);
	} else {
		ble_health_send_mip(
			active_profile,
			value_x100,
			predicted_x100,
			percent
		);
	}
}

static void finish_heart_measurement(void)
{
	if (last_stable_bpm > 0 &&
	    active_profile >= 0) {
		(void)profile_store_set_last_bpm(
			active_profile,
			last_stable_bpm
		);

		save_and_send_measurement(
			MEASUREMENT_HEART_RATE,
			(int32_t)last_stable_bpm * 100,
			0,
			0
		);
	}

	screen = SCREEN_MAIN_MENU;
	ui_dirty = true;
}

static void process_heart_sensor(void)
{
	int available = max30102_samples_available(i2c_bus);

	if (available < 0) {
		heart_sensor_ready = false;
		ui_dirty = true;
		return;
	}

	while (available-- > 0) {
		struct max30102_sample sample;

		if (max30102_read_sample(
			    i2c_bus,
			    &sample) != 0) {
			heart_sensor_ready = false;
			ui_dirty = true;
			return;
		}

		if (heart_rate_process(&heart, sample.ir)) {
			beat_flash_until =
				k_uptime_get() + 120;
		}

		if (heart_rate_is_stable(&heart)) {
			last_stable_bpm =
				(uint16_t)heart.bpm;
		}
	}
}

static bool start_mep_attempt(void)
{
	/* Kullanici bu anda uflememeli; sensor sifirlaniyor. */
	if (pressure_sensor_tare(40) != 0) {
		screen = SCREEN_SENSOR_ERROR;
		ui_dirty = true;
		return false;
	}

	int64_t now = k_uptime_get();

	mep_test_begin_attempt(
		&mep,
		now,
		PRESSURE_TEST_EXPIRATORY
	);
	last_pressure_sample_ms = now;

	screen = SCREEN_MEP_ACTIVE;
	ui_dirty = true;

	return true;
}

static bool start_mip_attempt(void)
{
	/* Kullanici bu anda cekmemeli; ortam basinci sifirlanir. */
	if (pressure_sensor_tare(40) != 0) {
		screen = SCREEN_SENSOR_ERROR;
		ui_dirty = true;
		return false;
	}

	int64_t now = k_uptime_get();

	mep_test_begin_attempt(
		&mip,
		now,
		PRESSURE_TEST_INSPIRATORY
	);
	last_pressure_sample_ms = now;
	screen = SCREEN_MIP_ACTIVE;
	ui_dirty = true;

	return true;
}

static void finish_mep_session(void)
{
	const struct user_profile *profile =
		active_profile_data();

	result_mep_kpa_x100 =
		mep_test_best_value(&mep);

	result_predicted_kpa_x100 =
		profile != NULL
			? mep_predicted_kpa_x100(profile)
			: 0;

	result_percent =
		mep_percent_of_predicted(
			result_mep_kpa_x100,
			result_predicted_kpa_x100
		);

	result_repeatable =
		mep_test_is_repeatable(&mep);
	result_reference_valid =
		profile != NULL &&
		mep_reference_is_applicable(profile);

	if (result_mep_kpa_x100 > 0 &&
	    active_profile >= 0) {
		(void)profile_store_set_last_mep(
			active_profile,
			result_mep_kpa_x100,
			result_predicted_kpa_x100,
			result_percent,
			result_repeatable,
			result_reference_valid
		);

		save_and_send_measurement(
			MEASUREMENT_MEP,
			result_mep_kpa_x100,
			result_predicted_kpa_x100,
			result_percent
		);
	}

	screen = SCREEN_MEP_RESULT;
	ui_dirty = true;
}

static void finish_mip_session(void)
{
	const struct user_profile *profile = active_profile_data();

	result_mip_kpa_x100 = mep_test_best_value(&mip);
	result_mip_predicted_x100 = profile != NULL
		? mip_predicted_kpa_x100(profile)
		: 0;
	result_mip_percent = mep_percent_of_predicted(
		result_mip_kpa_x100,
		result_mip_predicted_x100
	);
	result_mip_repeatable = mep_test_is_repeatable(&mip);
	result_mip_reference_valid =
		profile != NULL && mep_reference_is_applicable(profile);

	if (result_mip_kpa_x100 > 0 && active_profile >= 0) {
		(void)profile_store_set_last_mip(
			active_profile,
			result_mip_kpa_x100,
			result_mip_predicted_x100,
			result_mip_percent,
			result_mip_repeatable,
			result_mip_reference_valid
		);
		save_and_send_measurement(
			MEASUREMENT_MIP,
			result_mip_kpa_x100,
			result_mip_predicted_x100,
			result_mip_percent
		);
	}

	screen = SCREEN_MIP_RESULT;
	ui_dirty = true;
}

static void process_pressure_sensor(void)
{
	bool is_mip = screen == SCREEN_MIP_ACTIVE;
	struct mep_test_state *test = is_mip ? &mip : &mep;
	int64_t now = k_uptime_get();

	if (!pressure_sensor_data_ready()) {
		if (now - last_pressure_sample_ms > 500) {
			screen = SCREEN_SENSOR_ERROR;
			ui_dirty = true;
		}

		return;
	}

	int32_t pressure_kpa_x100;

	int result = pressure_sensor_read_kpa_x100(
		    &pressure_kpa_x100,
		    K_NO_WAIT);

	if (result != 0) {
		screen = (result == -ERANGE ||
			  result == -EOVERFLOW)
			? SCREEN_PRESSURE_SATURATION
			: SCREEN_SENSOR_ERROR;
		ui_dirty = true;
		return;
	}

	last_pressure_sample_ms = now;

	enum mep_event event = mep_test_process(
		test,
		pressure_kpa_x100,
		now
	);

	if (event == MEP_EVENT_ATTEMPT_FINISHED) {
		if (mep_test_has_more_attempts(test)) {
			screen = is_mip
				? SCREEN_MIP_ATTEMPT
				: SCREEN_MEP_ATTEMPT;
			ui_dirty = true;
		} else if (is_mip) {
			finish_mip_session();
		} else {
			finish_mep_session();
		}
	}
}

static void handle_profile_select(enum input_event event)
{
	if (event == INPUT_CLOCKWISE) {
		profile_option = wrap_value(
			profile_option + 1,
			option_count()
		);

		ui_dirty = true;
	} else if (event == INPUT_COUNTERCLOCKWISE) {
		profile_option = wrap_value(
			profile_option - 1,
			option_count()
		);

		ui_dirty = true;
	} else if (event == INPUT_SHORT_PRESS) {
		int index = profile_from_option(profile_option);

		if (index < 0) {
			begin_new_profile();
		} else {
			active_profile = index;
			menu_item = UI_MENU_CLOCK;
			screen = SCREEN_MAIN_MENU;
			ui_dirty = true;
		}
	}
}

static void handle_name_edit(enum input_event event)
{
	int alphabet_count = (int)strlen(alphabet);

	if (event == INPUT_CLOCKWISE) {
		name_character = wrap_value(
			name_character + 1,
			alphabet_count
		);

		ui_dirty = true;
	} else if (event == INPUT_COUNTERCLOCKWISE) {
		name_character = wrap_value(
			name_character - 1,
			alphabet_count
		);

		ui_dirty = true;
	} else if (event == INPUT_SHORT_PRESS) {
		size_t length = strlen(draft_profile.name);
		char selected = alphabet[name_character];

		if (selected == '<') {
			if (length > 0) {
				draft_profile.name[length - 1] = '\0';
			}
		} else if (length < PROFILE_NAME_MAX) {
			draft_profile.name[length] = selected;
			draft_profile.name[length + 1] = '\0';
		}

		ui_dirty = true;
	} else if (event == INPUT_LONG_PRESS &&
		   draft_profile.name[0] != '\0') {
		screen = SCREEN_GENDER_EDIT;
		ui_dirty = true;
	}
}

static void handle_gender_edit(enum input_event event)
{
	if (event == INPUT_CLOCKWISE ||
	    event == INPUT_COUNTERCLOCKWISE) {
		draft_profile.gender =
			draft_profile.gender == PROFILE_MAN
			? PROFILE_WOMAN
			: PROFILE_MAN;

		ui_dirty = true;
	} else if (event == INPUT_SHORT_PRESS) {
		screen = SCREEN_AGE_EDIT;
		ui_dirty = true;
	}
}

static void handle_age_edit(enum input_event event)
{
	if (event == INPUT_CLOCKWISE &&
	    draft_profile.age < 120) {
		draft_profile.age++;
		ui_dirty = true;
	} else if (event == INPUT_COUNTERCLOCKWISE &&
		   draft_profile.age > 1) {
		draft_profile.age--;
		ui_dirty = true;
	} else if (event == INPUT_SHORT_PRESS) {
		screen = SCREEN_HEIGHT_EDIT;
		ui_dirty = true;
	}
}

static void handle_height_edit(enum input_event event)
{
	if (event == INPUT_CLOCKWISE &&
	    draft_profile.height_cm <
		    PROFILE_MAX_HEIGHT_CM) {
		draft_profile.height_cm++;
		ui_dirty = true;
	} else if (event == INPUT_COUNTERCLOCKWISE &&
		   draft_profile.height_cm >
			   PROFILE_MIN_HEIGHT_CM) {
		draft_profile.height_cm--;
		ui_dirty = true;
	} else if (event == INPUT_SHORT_PRESS) {
		screen = SCREEN_WEIGHT_EDIT;
		ui_dirty = true;
	}
}

static void handle_weight_edit(enum input_event event)
{
	if (event == INPUT_CLOCKWISE &&
	    draft_profile.weight_kg <
		    PROFILE_MAX_WEIGHT_KG) {
		draft_profile.weight_kg++;
		ui_dirty = true;
	} else if (event == INPUT_COUNTERCLOCKWISE &&
		   draft_profile.weight_kg >
			   PROFILE_MIN_WEIGHT_KG) {
		draft_profile.weight_kg--;
		ui_dirty = true;
	} else if (event == INPUT_SHORT_PRESS) {
		if (profile_store_save(
			    new_profile_slot,
			    &draft_profile) == 0) {
			active_profile = new_profile_slot;

			ble_health_send_profile(
				active_profile,
				profile_store_get(active_profile)
			);
			saved_screen_until =
				k_uptime_get() + 800;

			screen = SCREEN_SAVED;
			ui_dirty = true;
		}
	}
}

static void handle_main_menu(enum input_event event)
{
	if (event == INPUT_CLOCKWISE) {
		menu_item = (enum ui_menu_item)wrap_value(
			menu_item + 1,
			UI_MENU_COUNT
		);

		ui_dirty = true;
	} else if (event == INPUT_COUNTERCLOCKWISE) {
		menu_item = (enum ui_menu_item)wrap_value(
			menu_item - 1,
			UI_MENU_COUNT
		);

		ui_dirty = true;
	} else if (event == INPUT_LONG_PRESS) {
		profile_option = 0;
		screen = SCREEN_PROFILE_SELECT;
		ui_dirty = true;
	} else if (event == INPUT_SHORT_PRESS) {
		switch (menu_item) {
		case UI_MENU_HEART:
			start_heart_measurement();
			break;

		case UI_MENU_CLOCK:
			break;

		case UI_MENU_MEP:
			screen = SCREEN_MEP_READY;
			ui_dirty = true;
			break;

		case UI_MENU_MIP:
			screen = SCREEN_MIP_READY;
			ui_dirty = true;
			break;

		case UI_MENU_ENVIRONMENT:
			screen = SCREEN_ENVIRONMENT;
			ui_dirty = true;
			break;

		case UI_MENU_PROFILE:
			screen = SCREEN_PROFILE_INFO;
			ui_dirty = true;
			break;

		case UI_MENU_RECORDS:
			record_cursor = 0;
			screen = SCREEN_RECORDS;
			ui_dirty = true;
			break;

		case UI_MENU_BLUETOOTH:
			screen = SCREEN_BLUETOOTH;
			ui_dirty = true;
			break;

		case UI_MENU_SETTINGS:
			settings_option = 0;
			screen = SCREEN_PRESSURE_SETTINGS;
			ui_dirty = true;
			break;

		default:
			break;
		}
	}
}

static void handle_records(enum input_event event)
{
	size_t count = measurement_log_count(active_profile);

	if (event == INPUT_CLOCKWISE && count > 0) {
		record_cursor = (record_cursor + 1U) % count;
		ui_dirty = true;
	} else if (event == INPUT_COUNTERCLOCKWISE && count > 0) {
		record_cursor = record_cursor == 0
			? count - 1U
			: record_cursor - 1U;
		ui_dirty = true;
	} else if (event == INPUT_SHORT_PRESS ||
		   event == INPUT_LONG_PRESS) {
		screen = SCREEN_MAIN_MENU;
		ui_dirty = true;
	}
}

static void handle_pressure_settings(enum input_event event)
{
	if (event == INPUT_CLOCKWISE) {
		settings_option = wrap_value(settings_option + 1, 3);
		ui_dirty = true;
	} else if (event == INPUT_COUNTERCLOCKWISE) {
		settings_option = wrap_value(settings_option - 1, 3);
		ui_dirty = true;
	} else if (event == INPUT_SHORT_PRESS) {
		if (settings_option == 0) {
			if (current_time.valid) {
				rtc_edit_time = current_time;
			} else {
				rtc_edit_time = (struct rtc_datetime) {
					.year = 2026,
					.month = 1,
					.day = 1,
					.hour = 12,
					.minute = 0,
					.second = 0,
					.weekday = 1,
					.valid = true,
				};
			}
			rtc_edit_field = 0;
			screen = SCREEN_RTC_EDIT;
		} else if (settings_option == 1) {
			screen = SCREEN_PROFILE_DELETE_CONFIRM;
		} else {
			screen = SCREEN_MAIN_MENU;
		}
		ui_dirty = true;
	} else if (event == INPUT_LONG_PRESS) {
		screen = SCREEN_MAIN_MENU;
		ui_dirty = true;
	}
}

static void handle_rtc_edit(enum input_event event)
{
	int delta = event == INPUT_CLOCKWISE
		? 1
		: event == INPUT_COUNTERCLOCKWISE ? -1 : 0;

	if (event == INPUT_LONG_PRESS) {
		screen = SCREEN_PRESSURE_SETTINGS;
		ui_dirty = true;
		return;
	}

	if (delta != 0) {
		switch (rtc_edit_field) {
		case 0:
			rtc_edit_time.year = (uint16_t)wrap_range(
				rtc_edit_time.year + delta, 2020, 2099);
			break;
		case 1:
			rtc_edit_time.month = (uint8_t)wrap_range(
				rtc_edit_time.month + delta, 1, 12);
			break;
		case 2:
			rtc_edit_time.day = (uint8_t)wrap_range(
				rtc_edit_time.day + delta,
				1,
				days_in_month(rtc_edit_time.year,
					      rtc_edit_time.month));
			break;
		case 3:
			rtc_edit_time.hour = (uint8_t)wrap_range(
				rtc_edit_time.hour + delta, 0, 23);
			break;
		default:
			rtc_edit_time.minute = (uint8_t)wrap_range(
				rtc_edit_time.minute + delta, 0, 59);
			break;
		}

		uint8_t maximum_day = days_in_month(
			rtc_edit_time.year,
			rtc_edit_time.month);
		if (rtc_edit_time.day > maximum_day) {
			rtc_edit_time.day = maximum_day;
		}
		ui_dirty = true;
		return;
	}

	if (event != INPUT_SHORT_PRESS) {
		return;
	}

	if (rtc_edit_field < 4U) {
		rtc_edit_field++;
		ui_dirty = true;
		return;
	}

	rtc_edit_time.second = 0U;
	rtc_edit_time.valid = true;
	rtc_ready = false;

	if (rtc_clock_set(&rtc_edit_time) == 0) {
		struct rtc_datetime verify_time;

		k_msleep(150);
		if (rtc_read_with_retry(&verify_time, 8U, 100U)) {
			current_time = verify_time;
			rtc_ready = true;
		}
	}

	last_clock_update_ms = k_uptime_get();
	if (rtc_ready) {
		saved_screen_until = last_clock_update_ms + 900;
		screen = SCREEN_SAVED;
	} else {
		screen = SCREEN_SENSOR_ERROR;
	}
	ui_dirty = true;
}

static void handle_profile_delete_confirm(enum input_event event)
{
	if (event == INPUT_LONG_PRESS) {
		screen = SCREEN_PRESSURE_SETTINGS;
		ui_dirty = true;
		return;
	}

	if (event != INPUT_SHORT_PRESS || active_profile < 0) {
		return;
	}

	if (profile_store_delete(active_profile) == 0) {
		(void)measurement_log_delete_profile(active_profile);
		active_profile = -1;
		profile_option = 0;
		saved_screen_until = k_uptime_get() + 900;
		screen = SCREEN_PROFILE_DELETED;
		ui_dirty = true;
	}
}

static void handle_pressure_cal_zero(enum input_event event)
{
	if (event == INPUT_LONG_PRESS) {
		screen = SCREEN_PRESSURE_SETTINGS;
		ui_dirty = true;
		return;
	}

	if (event != INPUT_SHORT_PRESS) {
		return;
	}

	pressure_calibration_result =
		pressure_sensor_tare(
			PRESSURE_CALIBRATION_SAMPLES
		);

	if (pressure_calibration_result == 0) {
		screen = SCREEN_PRESSURE_CAL_REFERENCE;
	} else {
		screen = SCREEN_PRESSURE_CAL_RESULT;
	}

	ui_dirty = true;
}

static void handle_pressure_cal_reference(enum input_event event)
{
	if (event == INPUT_LONG_PRESS) {
		screen = SCREEN_PRESSURE_CAL_ZERO;
		ui_dirty = true;
		return;
	}

	if (event != INPUT_SHORT_PRESS) {
		return;
	}

	pressure_calibration_result =
		pressure_sensor_calibrate_span(
			PRESSURE_CALIBRATION_KPA_X100,
			PRESSURE_CALIBRATION_SAMPLES
		);

	screen = pressure_calibration_result == 0
		? SCREEN_PRESSURE_CAL_APPLY
		: SCREEN_PRESSURE_CAL_RESULT;
	ui_dirty = true;
}

static void handle_pressure_cal_apply(enum input_event event)
{
	if (event == INPUT_LONG_PRESS) {
		screen = SCREEN_PRESSURE_CAL_REFERENCE;
		ui_dirty = true;
		return;
	}

	if (event != INPUT_SHORT_PRESS) {
		return;
	}

	pressure_calibration_result =
		pressure_sensor_validate_span(
			PRESSURE_VALIDATION_KPA_X100,
			PRESSURE_CALIBRATION_SAMPLES
		);

	screen = SCREEN_PRESSURE_CAL_RESULT;
	ui_dirty = true;
}

static void handle_mep_ready(enum input_event event)
{
	if (event == INPUT_LONG_PRESS) {
		screen = SCREEN_MAIN_MENU;
		ui_dirty = true;
		return;
	}

	if (event != INPUT_SHORT_PRESS) {
		return;
	}

	if (!pressure_gpio_ready) {
		screen = SCREEN_SENSOR_ERROR;
		ui_dirty = true;
		return;
	}

	mep_test_reset(&mep);
	(void)start_mep_attempt();
}

static void handle_mep_attempt(enum input_event event)
{
	if (event == INPUT_SHORT_PRESS) {
		if (mep_test_has_more_attempts(&mep)) {
			mep_test_advance_attempt(&mep);
			(void)start_mep_attempt();
		} else {
			finish_mep_session();
		}
	} else if (event == INPUT_LONG_PRESS) {
		if (mep_test_best_value(&mep) > 0) {
			finish_mep_session();
		} else {
			screen = SCREEN_MAIN_MENU;
			ui_dirty = true;
		}
	}
}

static void handle_mip_ready(enum input_event event)
{
	if (event == INPUT_LONG_PRESS) {
		screen = SCREEN_MAIN_MENU;
		ui_dirty = true;
		return;
	}

	if (event != INPUT_SHORT_PRESS) {
		return;
	}

	if (!pressure_gpio_ready) {
		screen = SCREEN_SENSOR_ERROR;
		ui_dirty = true;
		return;
	}

	mep_test_reset(&mip);
	(void)start_mip_attempt();
}

static void handle_mip_attempt(enum input_event event)
{
	if (event == INPUT_SHORT_PRESS) {
		if (mep_test_has_more_attempts(&mip)) {
			mep_test_advance_attempt(&mip);
			(void)start_mip_attempt();
		} else {
			finish_mip_session();
		}
	} else if (event == INPUT_LONG_PRESS) {
		if (mep_test_best_value(&mip) > 0) {
			finish_mip_session();
		} else {
			screen = SCREEN_MAIN_MENU;
			ui_dirty = true;
		}
	}
}

static void handle_input(enum input_event event)
{
	switch (screen) {
	case SCREEN_PROFILE_SELECT:
		handle_profile_select(event);
		break;

	case SCREEN_MAIN_MENU:
		handle_main_menu(event);
		break;

	case SCREEN_NAME_EDIT:
		handle_name_edit(event);
		break;

	case SCREEN_GENDER_EDIT:
		handle_gender_edit(event);
		break;

	case SCREEN_AGE_EDIT:
		handle_age_edit(event);
		break;

	case SCREEN_HEIGHT_EDIT:
		handle_height_edit(event);
		break;

	case SCREEN_WEIGHT_EDIT:
		handle_weight_edit(event);
		break;

	case SCREEN_HEART:
		if (event == INPUT_SHORT_PRESS ||
		    event == INPUT_LONG_PRESS) {
			finish_heart_measurement();
		}
		break;

	case SCREEN_MEP_READY:
		handle_mep_ready(event);
		break;

	case SCREEN_MEP_ACTIVE:
		if (event == INPUT_LONG_PRESS) {
			screen = SCREEN_MAIN_MENU;
			ui_dirty = true;
		}
		break;

	case SCREEN_MEP_ATTEMPT:
		handle_mep_attempt(event);
		break;

	case SCREEN_MIP_READY:
		handle_mip_ready(event);
		break;

	case SCREEN_MIP_ACTIVE:
		if (event == INPUT_LONG_PRESS) {
			screen = SCREEN_MAIN_MENU;
			ui_dirty = true;
		}
		break;

	case SCREEN_MIP_ATTEMPT:
		handle_mip_attempt(event);
		break;

	case SCREEN_MEP_RESULT:
	case SCREEN_MIP_RESULT:
	case SCREEN_ENVIRONMENT:
	case SCREEN_BLUETOOTH:
	case SCREEN_PRESSURE_CAL_RESULT:
	case SCREEN_PRESSURE_SATURATION:
	case SCREEN_SENSOR_ERROR:
		if (event == INPUT_SHORT_PRESS ||
		    event == INPUT_LONG_PRESS) {
			screen = SCREEN_MAIN_MENU;
			ui_dirty = true;
		}
		break;

	case SCREEN_RECORDS:
		handle_records(event);
		break;

	case SCREEN_PRESSURE_SETTINGS:
		handle_pressure_settings(event);
		break;

	case SCREEN_RTC_EDIT:
		handle_rtc_edit(event);
		break;

	case SCREEN_PROFILE_DELETE_CONFIRM:
		handle_profile_delete_confirm(event);
		break;

	case SCREEN_PROFILE_DELETED:
		break;

	case SCREEN_PRESSURE_CAL_ZERO:
		handle_pressure_cal_zero(event);
		break;

	case SCREEN_PRESSURE_CAL_REFERENCE:
		handle_pressure_cal_reference(event);
		break;

	case SCREEN_PRESSURE_CAL_APPLY:
		handle_pressure_cal_apply(event);
		break;

	case SCREEN_PROFILE_INFO:
		if (event == INPUT_SHORT_PRESS) {
			begin_profile_edit();
		} else if (event == INPUT_LONG_PRESS) {
			screen = SCREEN_MAIN_MENU;
			ui_dirty = true;
		}
		break;

	case SCREEN_SAVED:
		break;
	}
}

static void sync_bluetooth_data(void)
{
	for (int i = 0; i < PROFILE_MAX; i++) {
		const struct user_profile *profile =
			profile_store_get(i);

		if (profile != NULL) {
			ble_health_send_profile(i, profile);
		}
	}

	if (environment.valid) {
		ble_health_send_environment(
			environment.temperature_c_x10,
			environment.humidity_percent_x10,
			environment_quality_code(environment.quality)
		);
	}

	size_t record_count = measurement_log_count(-1);
	for (size_t i = record_count; i > 0; i--) {
		struct measurement_record record;

		if (measurement_log_get_newest(-1, i - 1U, &record) == 0) {
			ble_health_send_record(&record);
		}
	}

	ble_health_send_sync_done();
}

int main(void)
{
	i2c_bus = DEVICE_DT_GET(DT_NODELABEL(i2c0));

	if (!device_is_ready(i2c_bus) ||
	    ui_display_init() != 0 ||
	    input_encoder_init() != 0 ||
	    storage_reset_once() != 0 ||
	    profile_store_init() != 0 ||
	    measurement_log_init() != 0) {
		return 0;
	}

	pressure_gpio_ready =
		pressure_sensor_init() == 0;

	bluetooth_ready =
		ble_health_init() == 0;
	environment_sensor_ready =
		environment_sensor_init() == 0;
	rtc_ready = false;
	if (rtc_clock_init() == 0) {
		/* Reset sonrasinda DS1302'nin hatlari birakmasini bekle. */
		k_msleep(250);
		rtc_ready = rtc_read_with_retry(
			&current_time,
			10U,
			100U
		);
	}
	last_clock_update_ms = k_uptime_get();

	heart_rate_reset(&heart);
	mep_test_reset(&mep);
	mep_test_reset(&mip);

	int64_t last_heart_draw = 0;
	int64_t last_mep_draw = 0;
	int64_t last_environment_read = 0;
	bool last_ble_connection = false;

	while (true) {
		enum input_event event;

		while (input_encoder_get(
			       &event,
			       K_NO_WAIT) == 0) {
			handle_input(event);
		}

		int64_t now = k_uptime_get();
		bool ble_connection =
			bluetooth_ready &&
			ble_health_is_connected();

		if (ble_connection != last_ble_connection) {
			last_ble_connection = ble_connection;

			if (screen == SCREEN_BLUETOOTH) {
				ui_dirty = true;
			}
		}

		if (bluetooth_ready &&
		    ble_health_take_sync_request()) {
			sync_bluetooth_data();
		}

		struct ble_profile_update profile_update;
		if (bluetooth_ready &&
		    ble_health_take_profile_update(&profile_update)) {
			if (profile_store_save(profile_update.index,
					       &profile_update.profile) == 0) {
				ble_health_send_profile(
					profile_update.index,
					profile_store_get(profile_update.index)
				);
				ble_health_send_status("PROFILE_SAVED");
				ui_dirty = true;
			} else {
				ble_health_send_status("PROFILE_ERROR");
			}
		}

		int remote_index;
		if (bluetooth_ready &&
		    ble_health_take_profile_delete(&remote_index)) {
			if (profile_store_delete(remote_index) == 0) {
				(void)measurement_log_delete_profile(remote_index);
				if (active_profile == remote_index) {
					active_profile = -1;
					profile_option = 0;
					screen = SCREEN_PROFILE_SELECT;
				}
				ble_health_send_status("PROFILE_DELETED");
				ui_dirty = true;
			} else {
				ble_health_send_status("PROFILE_ERROR");
			}
		}

		if (bluetooth_ready &&
		    ble_health_take_profile_select(&remote_index)) {
			if (profile_store_get(remote_index) != NULL) {
				active_profile = remote_index;
				menu_item = UI_MENU_CLOCK;
				screen = SCREEN_MAIN_MENU;
				ble_health_send_status("PROFILE_ACTIVE");
				ui_dirty = true;
			} else {
				ble_health_send_status("PROFILE_MISSING");
			}
		}

		if (bluetooth_ready && ble_health_take_session_end()) {
			active_profile = -1;
			profile_option = 0;
			screen = SCREEN_PROFILE_SELECT;
			ble_health_send_status("SESSION_ENDED");
			ui_dirty = true;
		}

		if ((screen == SCREEN_SAVED ||
		     screen == SCREEN_PROFILE_DELETED) &&
		    now >= saved_screen_until) {
			screen = active_profile >= 0
				? SCREEN_MAIN_MENU
				: SCREEN_PROFILE_SELECT;
			ui_dirty = true;
		}

		if (now - last_clock_update_ms >= 1000) {
			int64_t elapsed_seconds =
				(now - last_clock_update_ms) / 1000;
			last_clock_update_ms += elapsed_seconds * 1000;

			struct rtc_datetime rtc_time;

			/* rtc_ready false olsa da donanim her saniye yeniden denenir. */
			if (rtc_clock_get(&rtc_time) == 0) {
				current_time = rtc_time;
				rtc_ready = true;
			} else {
				rtc_ready = false;
				for (int64_t i = 0;
				     i < elapsed_seconds;
				     i++) {
					advance_clock_second(&current_time);
				}
			}

			if (screen == SCREEN_MAIN_MENU &&
			    menu_item == UI_MENU_CLOCK &&
			    current_time.valid) {
				ui_dirty = true;
			}
		}

		if (screen == SCREEN_HEART &&
		    heart_sensor_ready) {
			process_heart_sensor();

			if (now - last_heart_draw >= 100) {
				last_heart_draw = now;
				ui_dirty = true;
			}
		}

		if (screen == SCREEN_MEP_ACTIVE ||
		    screen == SCREEN_MIP_ACTIVE) {
			process_pressure_sensor();

			if (now - last_mep_draw >= 100) {
				last_mep_draw = now;
				ui_dirty = true;
			}
		}

		if (environment_sensor_ready &&
		    now - last_environment_read >= 3000 &&
		    screen != SCREEN_MEP_ACTIVE &&
		    screen != SCREEN_MIP_ACTIVE) {
			last_environment_read = now;

			if (environment_sensor_read(&environment) == 0) {
				environment_failures = 0;
				if (bluetooth_ready && ble_connection) {
					ble_health_send_environment(
						environment.temperature_c_x10,
						environment.humidity_percent_x10,
						environment_quality_code(
							environment.quality
						)
					);
				}
			} else {
				if (environment_failures < UINT8_MAX) {
					environment_failures++;
				}
				if (environment_failures >= 3) {
					environment.valid = false;
				}
			}

			if (screen == SCREEN_ENVIRONMENT) {
				ui_dirty = true;
			}
		}

		if (ui_dirty) {
			ui_dirty = false;
			show_screen();
		}

		k_sleep(K_MSEC(2));
	}

	return 0;
}
