#include "display_ui.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/util.h>

#define WIDTH 128
#define HEIGHT 64
#define SCREEN_BUFFER_SIZE (WIDTH * HEIGHT / 8)
#define WAVE_HEIGHT 24

static const struct device *oled;
static uint8_t screen_buffer[SCREEN_BUFFER_SIZE];

static void pixel(
	uint8_t *buffer,
	int width,
	int height,
	int x,
	int y,
	bool on
)
{
	if (x < 0 || x >= width ||
	    y < 0 || y >= height) {
		return;
	}

	size_t index =
		(size_t)(y / 8) * width + x;

	uint8_t mask = BIT(y % 8);

	if (on) {
		buffer[index] |= mask;
	} else {
		buffer[index] &= ~mask;
	}
}

static void line(
	uint8_t *buffer,
	int width,
	int height,
	int x0,
	int y0,
	int x1,
	int y1
)
{
	int dx = x1 >= x0 ? x1 - x0 : x0 - x1;
	int sx = x0 < x1 ? 1 : -1;

	int dy = -(y1 >= y0 ? y1 - y0 : y0 - y1);
	int sy = y0 < y1 ? 1 : -1;

	int error = dx + dy;

	while (true) {
		pixel(
			buffer,
			width,
			height,
			x0,
			y0,
			true
		);

		if (x0 == x1 && y0 == y1) {
			break;
		}

		int twice_error = 2 * error;

		if (twice_error >= dy) {
			error += dy;
			x0 += sx;
		}

		if (twice_error <= dx) {
			error += dx;
			y0 += sy;
		}
	}
}

static void thick_line(
	uint8_t *buffer,
	int x0,
	int y0,
	int x1,
	int y1,
	int thickness
)
{
	for (int offset = -thickness / 2;
	     offset <= thickness / 2;
	     offset++) {
		line(
			buffer,
			WIDTH,
			HEIGHT,
			x0 + offset,
			y0,
			x1 + offset,
			y1
		);

		line(
			buffer,
			WIDTH,
			HEIGHT,
			x0,
			y0 + offset,
			x1,
			y1 + offset
		);
	}
}

static void fill_rectangle(
	uint8_t *buffer,
	int x0,
	int y0,
	int x1,
	int y1
)
{
	for (int y = y0; y <= y1; y++) {
		for (int x = x0; x <= x1; x++) {
			pixel(
				buffer,
				WIDTH,
				HEIGHT,
				x,
				y,
				true
			);
		}
	}
}

static void fill_circle(
	uint8_t *buffer,
	int center_x,
	int center_y,
	int radius
)
{
	for (int y = -radius; y <= radius; y++) {
		for (int x = -radius;
		     x <= radius;
		     x++) {
			if (x * x + y * y <=
			    radius * radius) {
				pixel(
					buffer,
					WIDTH,
					HEIGHT,
					center_x + x,
					center_y + y,
					true
				);
			}
		}
	}
}

static void ring(
	uint8_t *buffer,
	int center_x,
	int center_y,
	int inner_radius,
	int outer_radius
)
{
	int inner_squared =
		inner_radius * inner_radius;
	int outer_squared =
		outer_radius * outer_radius;

	for (int y = -outer_radius;
	     y <= outer_radius;
	     y++) {
		for (int x = -outer_radius;
		     x <= outer_radius;
		     x++) {
			int distance = x * x + y * y;

			if (distance >= inner_squared &&
			    distance <= outer_squared) {
				pixel(
					buffer,
					WIDTH,
					HEIGHT,
					center_x + x,
					center_y + y,
					true
				);
			}
		}
	}
}

static void fill_triangle(
	uint8_t *buffer,
	int center_x,
	int top_y,
	int bottom_y,
	int top_half,
	int bottom_half
)
{
	int height = MAX(bottom_y - top_y, 1);

	for (int y = top_y; y <= bottom_y; y++) {
		int half =
			top_half +
			(bottom_half - top_half) *
			(y - top_y) / height;

		fill_rectangle(
			buffer,
			center_x - half,
			y,
			center_x + half,
			y
		);
	}
}

static void rectangle(
	uint8_t *buffer,
	int x0,
	int y0,
	int x1,
	int y1
)
{
	line(buffer, WIDTH, HEIGHT, x0, y0, x1, y0);
	line(buffer, WIDTH, HEIGHT, x1, y0, x1, y1);
	line(buffer, WIDTH, HEIGHT, x1, y1, x0, y1);
	line(buffer, WIDTH, HEIGHT, x0, y1, x0, y0);
}

static int write_bitmap(
	int x,
	int y,
	int width,
	int height,
	const uint8_t *buffer
)
{
	struct display_buffer_descriptor descriptor = {
		.buf_size =
			(uint32_t)(width * height / 8),
		.width = (uint16_t)width,
		.height = (uint16_t)height,
		.pitch = (uint16_t)width,
	};

	return display_write(
		oled,
		x,
		y,
		&descriptor,
		buffer
	);
}

static int font_width(int font)
{
	static const int widths[] = {
		10,
		15,
		20
	};

	return widths[CLAMP(font, 0, 2)];
}

static void print_center(
	const char *text,
	int y,
	int font
)
{
	(void)cfb_framebuffer_set_font(
		oled,
		font
	);

	int x =
		(WIDTH -
		 (int)strlen(text) *
		 font_width(font)) / 2;

	(void)cfb_print(
		oled,
		text,
		MAX(x, 0),
		y
	);
}

static void text_begin(void)
{
	cfb_framebuffer_clear(oled, false);
}

static void text_end(void)
{
	(void)cfb_framebuffer_finalize(oled);
}

static void format_kpa(
	char *buffer,
	size_t buffer_size,
	int32_t kpa_x100,
	bool include_unit
)
{
	if (kpa_x100 < 0) {
		kpa_x100 = 0;
	}

	snprintf(
		buffer,
		buffer_size,
		include_unit
			? "%ld.%02ld KPA"
			: "%ld.%02ld",
		(long)(kpa_x100 / 100),
		(long)(kpa_x100 % 100)
	);
}

static bool heart_pixel(int x, int y)
{
	int left_x = x - 7;
	int right_x = x - 16;
	int circle_y = y - 7;

	bool circles =
		(left_x * left_x +
		 circle_y * circle_y <= 36) ||
		(right_x * right_x +
		 circle_y * circle_y <= 36);

	if (circles) {
		return true;
	}

	if (y < 7 || y > 22) {
		return false;
	}

	int half_width =
		(22 - y) * 11 / 15;

	return x >= 12 - half_width &&
	       x <= 12 + half_width;
}

static void draw_heart(
	int origin_x,
	int origin_y,
	bool filled
)
{
	uint8_t buffer[24 * 3];

	memset(buffer, 0, sizeof(buffer));

	for (int y = 0; y < 24; y++) {
		for (int x = 0; x < 24; x++) {
			bool inside = heart_pixel(x, y);

			bool edge =
				inside &&
				(!heart_pixel(x - 1, y) ||
				 !heart_pixel(x + 1, y) ||
				 !heart_pixel(x, y - 1) ||
				 !heart_pixel(x, y + 1));

			if (inside && (filled || edge)) {
				pixel(
					buffer,
					24,
					24,
					x,
					y,
					true
				);
			}
		}
	}

	(void)write_bitmap(
		origin_x,
		origin_y,
		24,
		24,
		buffer
	);
}

static void draw_person(bool woman)
{
	memset(
		screen_buffer,
		0,
		sizeof(screen_buffer)
	);

	fill_circle(screen_buffer, 64, 10, 7);

	if (!woman) {
		fill_rectangle(screen_buffer, 53, 20, 75, 39);
		thick_line(screen_buffer, 54, 24, 42, 43, 5);
		thick_line(screen_buffer, 74, 24, 86, 43, 5);
		fill_rectangle(screen_buffer, 54, 38, 62, 62);
		fill_rectangle(screen_buffer, 66, 38, 74, 62);
	} else {
		fill_rectangle(screen_buffer, 57, 20, 71, 30);
		fill_triangle(screen_buffer, 64, 28, 48, 7, 18);
		thick_line(screen_buffer, 58, 24, 45, 43, 5);
		thick_line(screen_buffer, 70, 24, 83, 43, 5);
		fill_rectangle(screen_buffer, 56, 47, 63, 62);
		fill_rectangle(screen_buffer, 66, 47, 73, 62);
	}

	(void)write_bitmap(
		0,
		0,
		WIDTH,
		HEIGHT,
		screen_buffer
	);
}

static void draw_wave(
	const struct heart_rate_state *heart
)
{
	uint8_t buffer[
		WIDTH * (WAVE_HEIGHT / 8)
	];

	memset(buffer, 0, sizeof(buffer));

	int previous_x = 0;
	int previous_y =
		heart->wave[heart->wave_head];

	for (int i = 1;
	     i < HEART_WAVE_POINTS;
	     i++) {
		int index =
			(heart->wave_head + i) %
			HEART_WAVE_POINTS;

		int x = i * 2;
		int y = heart->wave[index];

		line(
			buffer,
			WIDTH,
			WAVE_HEIGHT,
			previous_x,
			previous_y,
			x,
			y
		);

		previous_x = x;
		previous_y = y;
	}

	(void)write_bitmap(
		0,
		40,
		WIDTH,
		WAVE_HEIGHT,
		buffer
	);
}

static void draw_pressure_gauge(void)
{
	/* Basit yari-daire manometre simgesi. */
	for (int x = 45; x <= 83; x++) {
		for (int y = 18; y <= 46; y++) {
			int dx = x - 64;
			int dy = y - 42;
			int distance = dx * dx + dy * dy;

			if (distance >= 18 * 18 &&
			    distance <= 20 * 20 &&
			    y <= 42) {
				pixel(
					screen_buffer,
					WIDTH,
					HEIGHT,
					x,
					y,
					true
				);
			}
		}
	}

	line(screen_buffer, WIDTH, HEIGHT, 44, 42, 84, 42);
	thick_line(screen_buffer, 64, 42, 76, 28, 2);
	fill_circle(screen_buffer, 64, 42, 3);
}

static void draw_menu_icon(
	enum ui_menu_item item
)
{
	memset(
		screen_buffer,
		0,
		sizeof(screen_buffer)
	);

	switch (item) {
	case UI_MENU_CLOCK:
		ring(screen_buffer, 64, 32, 13, 15);
		thick_line(screen_buffer, 64, 32, 64, 22, 2);
		thick_line(screen_buffer, 64, 32, 73, 36, 2);
		fill_circle(screen_buffer, 64, 32, 2);
		break;

	case UI_MENU_HEART:
		for (int y = 0; y < 24; y++) {
			for (int x = 0; x < 24; x++) {
				if (heart_pixel(x, y)) {
					pixel(
						screen_buffer,
						WIDTH,
						HEIGHT,
						52 + x,
						17 + y,
						true
					);
				}
			}
		}
		break;

	case UI_MENU_MEP:
		draw_pressure_gauge();
		break;

	case UI_MENU_MIP:
		draw_pressure_gauge();
		break;

	case UI_MENU_ENVIRONMENT:
		/* Termometre ve nem damlasi. */
		ring(screen_buffer, 57, 40, 4, 6);
		thick_line(screen_buffer, 57, 18, 57, 38, 3);
		ring(screen_buffer, 57, 18, 2, 4);
		thick_line(screen_buffer, 71, 20, 66, 31, 2);
		thick_line(screen_buffer, 71, 20, 76, 31, 2);
		ring(screen_buffer, 71, 34, 5, 7);
		break;

	case UI_MENU_PROFILE:
		fill_circle(screen_buffer, 64, 23, 7);
		fill_circle(screen_buffer, 64, 41, 12);
		fill_rectangle(screen_buffer, 50, 41, 78, 48);
		break;

	case UI_MENU_RECORDS:
		rectangle(screen_buffer, 49, 18, 79, 45);
		fill_rectangle(screen_buffer, 54, 34, 58, 41);
		fill_rectangle(screen_buffer, 62, 27, 66, 41);
		fill_rectangle(screen_buffer, 70, 22, 74, 41);
		break;

	case UI_MENU_BLUETOOTH:
		thick_line(screen_buffer, 64, 17, 64, 46, 2);
		line(screen_buffer, WIDTH, HEIGHT, 64, 17, 75, 27);
		line(screen_buffer, WIDTH, HEIGHT, 75, 27, 56, 39);
		line(screen_buffer, WIDTH, HEIGHT, 56, 24, 75, 38);
		line(screen_buffer, WIDTH, HEIGHT, 75, 38, 64, 46);
		break;

	case UI_MENU_SETTINGS:
		/* Ince disli cark: ici bos, dis hatti ve sekiz kisa dis. */
		ring(screen_buffer, 64, 32, 8, 10);
		ring(screen_buffer, 64, 32, 2, 4);
		thick_line(screen_buffer, 64, 17, 64, 21, 2);
		thick_line(screen_buffer, 64, 43, 64, 47, 2);
		thick_line(screen_buffer, 49, 32, 53, 32, 2);
		thick_line(screen_buffer, 75, 32, 79, 32, 2);
		thick_line(screen_buffer, 53, 21, 56, 24, 2);
		thick_line(screen_buffer, 72, 40, 75, 43, 2);
		thick_line(screen_buffer, 53, 43, 56, 40, 2);
		thick_line(screen_buffer, 72, 24, 75, 21, 2);
		break;

	default:
		break;
	}

	(void)write_bitmap(
		0,
		16,
		WIDTH,
		32,
		screen_buffer + WIDTH * 2
	);
}

int ui_display_init(void)
{
	oled = DEVICE_DT_GET(
		DT_CHOSEN(zephyr_display)
	);

	if (!device_is_ready(oled)) {
		return -1;
	}

	if (display_set_pixel_format(
		    oled,
		    PIXEL_FORMAT_MONO01) != 0) {
		return -1;
	}

	if (cfb_framebuffer_init(oled) != 0) {
		return -1;
	}

	cfb_framebuffer_clear(oled, true);
	display_blanking_off(oled);

	return 0;
}

void ui_show_profile_selector(
	const char *name,
	bool is_new
)
{
	text_begin();
	print_center("KISI SEC", 0, 0);

	(void)cfb_framebuffer_set_font(oled, 0);
	(void)cfb_print(oled, "<", 2, 28);
	(void)cfb_print(oled, ">", 116, 28);

	print_center(is_new ? "+ YENI" : name, 24, 0);
	text_end();
}

void ui_show_main_menu(
	const char *profile_name,
	enum ui_menu_item item
)
{
	static const char *labels[] = {
		"SAAT",
		"NABIZ",
		"MEP TESTI",
		"MIP TESTI",
		"ISI / NEM",
		"PROFIL",
		"KAYIT",
		"BLUETOOTH",
		"AYARLAR"
	};

	text_begin();
	print_center(profile_name, 0, 0);
	print_center(labels[item], 48, 0);
	text_end();

	draw_menu_icon(item);
}

void ui_show_clock(const struct rtc_datetime *time)
{
	char clock[12];
	char date[16];

	text_begin();
	print_center("SAAT", 0, 0);

	if (time == NULL || !time->valid) {
		print_center("--:--:--", 20, 1);
		print_center("RTC BEKLIYOR", 50, 0);
	} else {
		snprintf(clock, sizeof(clock), "%02u:%02u:%02u",
			time->hour, time->minute, time->second);
		snprintf(date, sizeof(date), "%02u/%02u/%04u",
			time->day, time->month, time->year);
		print_center(clock, 18, 1);
		print_center(date, 50, 0);
	}

	text_end();
}

void ui_show_name_editor(
	const char *name,
	char selected
)
{
	char character[2] = {
		selected,
		'\0'
	};

	text_begin();
	print_center(name[0] == '\0' ? "_" : name, 0, 0);

	(void)cfb_framebuffer_set_font(oled, 0);
	(void)cfb_print(oled, "<", 16, 30);
	(void)cfb_print(oled, ">", 102, 30);

	print_center(character, 24, 2);
	text_end();
}

void ui_show_gender(
	enum profile_gender gender
)
{
	draw_person(gender == PROFILE_WOMAN);
}

void ui_show_age(int age)
{
	char text[4];

	snprintf(text, sizeof(text), "%d", age);

	text_begin();
	print_center("YAS", 0, 0);
	print_center(text, 24, 2);
	text_end();
}

void ui_show_height(uint16_t height_cm)
{
	char text[8];

	snprintf(text, sizeof(text), "%u", height_cm);

	text_begin();
	print_center("BOY CM", 0, 0);
	print_center(text, 24, 2);
	text_end();
}

void ui_show_weight(uint16_t weight_kg)
{
	char text[8];

	snprintf(text, sizeof(text), "%u", weight_kg);

	text_begin();
	print_center("KILO KG", 0, 0);
	print_center(text, 24, 2);
	text_end();
}

void ui_show_heart(
	bool sensor_ready,
	const struct heart_rate_state *heart,
	bool beat_flash
)
{
	char bpm[12];

	text_begin();

	if (!sensor_ready) {
		print_center("SENSOR YOK", 16, 0);
		text_end();
		return;
	}

	if (!heart->finger_present) {
		print_center("PARMAK KOY", 8, 0);
		print_center("SABIT TUT", 32, 0);
		text_end();
		return;
	}

	(void)cfb_framebuffer_set_font(oled, 0);
	(void)cfb_print(oled, "BPM", 48, 0);

	if (heart_rate_is_stable(heart)) {
		snprintf(bpm, sizeof(bpm), "%d", heart->bpm);
	} else {
		strcpy(bpm, "--");
	}

	print_center(bpm, 16, 2);
	text_end();

	draw_heart(4, 8, beat_flash);
	draw_wave(heart);
}

void ui_show_mep_ready(
	bool sensor_ready,
	bool calibrated
)
{
	text_begin();

	if (!sensor_ready) {
		print_center("BASINC", 0, 0);
		print_center("SENSORU YOK", 24, 0);
		print_center("BAGLANTI BAK", 48, 0);
	} else if (!calibrated) {
		print_center("KALIBRASYON", 0, 0);
		print_center("EKSIK", 24, 0);
		print_center("AYARLARA GIR", 48, 0);
	} else {
		print_center("MEP", 0, 0);
		print_center("TEK OLCUM", 24, 0);
		print_center("BAS VE BASLA", 48, 0);
	}

	text_end();
}

void ui_show_mep_active(
	uint8_t attempt_number,
	int32_t live_kpa_x100,
	bool waiting_for_blow
)
{
	char pressure[16];

	ARG_UNUSED(attempt_number);

	format_kpa(
		pressure,
		sizeof(pressure),
		live_kpa_x100,
		true
	);

	text_begin();
	print_center("MEP OLCUM", 0, 0);
		print_center(
			waiting_for_blow
				? "NEFES AL"
				: "EN AZ 2 SN UFLE",
		20,
		0
	);
	print_center(pressure, 44, 0);
	text_end();
}

void ui_show_mep_attempt(
	uint8_t attempt_number,
	int32_t value_kpa_x100,
	bool valid
)
{
	char title[16];
	char pressure[16];

	snprintf(
		title,
		sizeof(title),
		"DENEME %u",
		attempt_number
	);

	format_kpa(
		pressure,
		sizeof(pressure),
		value_kpa_x100,
		true
	);

	text_begin();
	print_center(title, 0, 0);

	if (valid) {
		print_center(pressure, 24, 0);
		print_center("BIRAK VE BAS", 48, 0);
	} else {
		print_center("GECERSIZ", 24, 0);
		print_center("TEKRAR ICIN BAS", 48, 0);
	}

	text_end();
}

void ui_show_mep_result(
	int32_t measured_kpa_x100,
	int32_t predicted_kpa_x100,
	uint16_t percent,
	bool repeatable,
	bool reference_valid
)
{
	char measured[24];
	char predicted[24];
	const char *comment;

	ARG_UNUSED(repeatable);
	ARG_UNUSED(reference_valid);

	if (measured_kpa_x100 <= 0) {
		text_begin();
		print_center("MEP SONUCU", 0, 0);
		print_center("GECERLI TEST", 24, 0);
		print_center("BULUNAMADI", 48, 0);
		text_end();
		return;
	}

	snprintf(
		measured,
		sizeof(measured),
		"MEP %ld.%02ldKPA",
		(long)(measured_kpa_x100 / 100),
		(long)(measured_kpa_x100 % 100)
	);

	text_begin();
	print_center(measured, 0, 0);

	if (predicted_kpa_x100 > 0) {
		snprintf(
			predicted,
			sizeof(predicted),
			"TAH %ld.%02ldKPA",
			(long)(predicted_kpa_x100 / 100),
			(long)(predicted_kpa_x100 % 100)
		);

		if (percent < 80) {
			comment = "DUSUK";
		} else if (percent <= 120) {
			comment = "NORMAL";
		} else {
			comment = "YUKSEK";
		}

		print_center(predicted, 18, 0);
		print_center(comment, 36, 0);
		print_center("BAS: CIK", 52, 0);
	} else {
		print_center("TAH YOK", 18, 0);
		print_center("SONUC YOK", 36, 0);
		print_center("BAS: CIK", 52, 0);
	}

	text_end();
}

void ui_show_mip_ready(bool sensor_ready)
{
	text_begin();

	if (!sensor_ready) {
		print_center("BASINC", 0, 0);
		print_center("SENSORU YOK", 24, 0);
		print_center("BAGLANTI BAK", 48, 0);
	} else {
		print_center("MIP", 0, 0);
		print_center("AGIZLIK KAPALI", 20, 0);
		print_center("BAS VE BASLA", 44, 0);
	}

	text_end();
}

void ui_show_mip_active(
	uint8_t attempt_number,
	int32_t live_kpa_x100,
	bool waiting_for_inhale
)
{
	char pressure[16];

	ARG_UNUSED(attempt_number);
	format_kpa(pressure, sizeof(pressure), live_kpa_x100, true);

	text_begin();
	print_center("MIP OLCUM", 0, 0);
	print_center(
		waiting_for_inhale
			? "NEFES VER"
			: "EN AZ 2 SN CEK",
		20,
		0
	);
	print_center(pressure, 44, 0);
	text_end();
}

void ui_show_mip_attempt(
	uint8_t attempt_number,
	int32_t value_kpa_x100,
	bool valid
)
{
	char title[16];
	char pressure[16];

	snprintf(title, sizeof(title), "DENEME %u", attempt_number);
	format_kpa(pressure, sizeof(pressure), value_kpa_x100, true);

	text_begin();
	print_center(title, 0, 0);
	if (valid) {
		print_center(pressure, 24, 0);
		print_center("BIRAK VE BAS", 48, 0);
	} else {
		print_center("GECERSIZ", 24, 0);
		print_center("TEKRAR ICIN BAS", 48, 0);
	}
	text_end();
}

void ui_show_mip_result(
	int32_t measured_kpa_x100,
	int32_t predicted_kpa_x100,
	uint16_t percent,
	bool repeatable,
	bool reference_valid
)
{
	char measured[24];
	char predicted[24];
	const char *comment;

	ARG_UNUSED(repeatable);
	ARG_UNUSED(reference_valid);

	if (measured_kpa_x100 <= 0) {
		text_begin();
		print_center("MIP SONUCU", 0, 0);
		print_center("GECERLI TEST", 24, 0);
		print_center("BULUNAMADI", 48, 0);
		text_end();
		return;
	}

	snprintf(
		measured,
		sizeof(measured),
		"MIP %ld.%02ldKPA",
		(long)(measured_kpa_x100 / 100),
		(long)(measured_kpa_x100 % 100)
	);

	text_begin();
	print_center(measured, 0, 0);

	if (predicted_kpa_x100 > 0) {
		snprintf(
			predicted,
			sizeof(predicted),
			"TAH %ld.%02ldKPA",
			(long)(predicted_kpa_x100 / 100),
			(long)(predicted_kpa_x100 % 100)
		);
		comment = percent < 80
			? "DUSUK"
			: percent <= 120 ? "NORMAL" : "YUKSEK";
		print_center(predicted, 18, 0);
		print_center(comment, 36, 0);
		print_center("BAS: CIK", 52, 0);
	} else {
		print_center("TAH YOK", 18, 0);
		print_center("SONUC YOK", 36, 0);
		print_center("BAS: CIK", 52, 0);
	}

	text_end();
}

void ui_show_environment(
	bool sensor_ready,
	int16_t temperature_c_x10,
	uint16_t humidity_percent_x10,
	const char *quality
)
{
	char temperature[20];
	char humidity[20];
	const char *comment = "BEKLENIYOR";

	text_begin();
	print_center("ISI - NEM", 0, 0);

	if (!sensor_ready) {
		print_center("DHT11 HATA", 24, 0);
		print_center("D4 VE DIRENC", 48, 0);
		text_end();
		return;
	}

	snprintf(
		temperature,
		sizeof(temperature),
		"SIC %d.%d C",
		temperature_c_x10 / 10,
		(temperature_c_x10 < 0 ?
		 -(temperature_c_x10 % 10) :
		  (temperature_c_x10 % 10))
	);
	snprintf(
		humidity,
		sizeof(humidity),
		"NEM %u.%u %%",
		humidity_percent_x10 / 10,
		humidity_percent_x10 % 10
	);

	if (strcmp(quality, "GOOD") == 0) {
		comment = "ORTAM UYGUN";
	} else if (strcmp(quality, "DRY") == 0) {
		comment = "ORTAM KURU";
	} else if (strcmp(quality, "HUMID") == 0) {
		comment = "ORTAM NEMLI";
	} else if (strcmp(quality, "COLD") == 0) {
		comment = "ORTAM SOGUK";
	} else if (strcmp(quality, "HOT") == 0) {
		comment = "ORTAM SICAK";
	}

	print_center(temperature, 18, 0);
	print_center(humidity, 34, 0);
	print_center(comment, 50, 0);
	text_end();
}

void ui_show_profile_info(
	const struct user_profile *profile
)
{
	char detail[24];
	char body[24];

	snprintf(
		detail,
		sizeof(detail),
		"%s YAS %u",
		profile->gender == PROFILE_MAN
			? "ERKEK"
			: "KADIN",
		profile->age
	);

	snprintf(
		body,
		sizeof(body),
		"%uCM %uKG",
		profile->height_cm,
		profile->weight_kg
	);

	text_begin();
	print_center(profile->name, 0, 0);
	print_center(detail, 16, 0);
	print_center(body, 32, 0);
	print_center("BAS: DUZENLE", 52, 0);
	text_end();
}

void ui_show_record(
	const char *profile_name,
	const struct measurement_record *record,
	size_t record_index,
	size_t record_count
)
{
	char value[24];
	char clock[16];
	char position[24];

	ARG_UNUSED(profile_name);

	text_begin();

	if (record == NULL || record_count == 0) {
		print_center("OLCUM GECMISI", 0, 0);
		print_center("KAYIT YOK", 26, 0);
		print_center("BAS: CIK", 48, 0);
		text_end();
		return;
	}

	snprintf(position, sizeof(position), "%u/%u",
		(unsigned int)(record_index + 1U),
		(unsigned int)record_count);
	print_center("OLCUM GECMISI", 0, 0);

	if (record->type == MEASUREMENT_HEART_RATE) {
		snprintf(value, sizeof(value), "NABIZ %ld BPM",
			(long)(record->value_x100 / 100));
	} else {
		const char *type = record->type == MEASUREMENT_MEP ? "MEP" : "MIP";
		snprintf(value, sizeof(value), "%s %ld.%02ld KPA", type,
			(long)(record->value_x100 / 100),
			(long)(record->value_x100 % 100));
	}
	print_center(value, 22, 0);
	snprintf(clock, sizeof(clock), "%02u:%02u",
		record->time.hour, record->time.minute);
	print_center(clock, 42, 0);
	print_center(position, 54, 0);

	text_end();
}

void ui_show_bluetooth(
	bool ready,
	bool connected
)
{
	text_begin();
	print_center("BLUETOOTH", 0, 0);

	if (!ready) {
		print_center("BASLATILAMADI", 24, 0);
	} else if (connected) {
		print_center("UYGULAMA BAGLI", 20, 0);
		print_center("VERI HAZIR", 40, 0);
	} else {
		print_center("nRF52840_fy", 20, 0);
		print_center("BAGLANTI BEKLIYOR", 40, 0);
	}

	text_end();
}

void ui_show_settings(uint8_t selected)
{
	static const char *items[] = {
		"SAAT AYARLA",
		"PROFIL SIL",
		"GERI"
	};

	text_begin();
	print_center("AYARLAR", 0, 0);
	print_center(items[selected % 3U], 24, 0);
	print_center("<  BAS: SEC  >", 48, 0);
	text_end();
}

void ui_show_rtc_editor(
	const struct rtc_datetime *time,
	uint8_t field
)
{
	static const char *labels[] = {
		"YIL", "AY", "GUN", "SAAT", "DAKIKA"
	};
	char value[16];

	text_begin();
	print_center(labels[field % 5U], 0, 0);

	switch (field % 5U) {
	case 0:
		snprintf(value, sizeof(value), "%04u", time->year);
		break;
	case 1:
		snprintf(value, sizeof(value), "%02u", time->month);
		break;
	case 2:
		snprintf(value, sizeof(value), "%02u", time->day);
		break;
	case 3:
		snprintf(value, sizeof(value), "%02u", time->hour);
		break;
	default:
		snprintf(value, sizeof(value), "%02u", time->minute);
		break;
	}

	print_center(value, 20, 1);
	print_center("<  DONDUR  >", 44, 0);
	print_center("BAS: SONRA", 54, 0);
	text_end();
}

void ui_show_profile_delete_confirm(const char *name)
{
	text_begin();
	print_center("PROFIL SIL?", 0, 0);
	print_center(name != NULL ? name : "PROFIL", 20, 0);
	print_center("BAS: EVET", 38, 0);
	print_center("UZUN: IPTAL", 52, 0);
	text_end();
}

void ui_show_profile_deleted(void)
{
	text_begin();
	print_center("PROFIL", 12, 0);
	print_center("SILINDI", 34, 1);
	text_end();
}

void ui_show_pressure_cal_zero(void)
{
	text_begin();
	print_center("SENSOR SIFIR", 0, 0);
	print_center("HORTUM BOS OLSUN", 16, 0);
	print_center("BASINC UYGULAMA", 32, 0);
	print_center("BAS: SIFIRLA", 48, 0);
	text_end();
}

void ui_show_pressure_cal_reference(
	int32_t reference_kpa_x100
)
{
	char kpa[32];

	snprintf(
		kpa,
		sizeof(kpa),
		"%ld.%02ld KPA UYGULA",
		(long)(reference_kpa_x100 / 100),
		(long)(reference_kpa_x100 % 100)
	);

	text_begin();
	print_center("1. REFERANS", 0, 0);
	print_center(kpa, 16, 0);
	print_center("KAPALI T BAGLANTI", 32, 0);
	print_center("SABITLE VE BAS", 48, 0);
	text_end();
}

void ui_show_pressure_cal_apply(
	int32_t validation_kpa_x100
)
{
	char kpa[32];

	snprintf(
		kpa,
		sizeof(kpa),
		"%ld.%02ld KPA UYGULA",
		(long)(validation_kpa_x100 / 100),
		(long)(validation_kpa_x100 % 100)
	);

	text_begin();
	print_center("2. DOGRULAMA", 0, 0);
	print_center(kpa, 16, 0);
	print_center("30DAN 40A CIKAR", 32, 0);
	print_center("SABITLE VE BAS", 48, 0);
	text_end();
}

void ui_show_pressure_cal_result(
	int result,
	int32_t counts_per_kpa,
	int32_t validated_limit_kpa_x100
)
{
	char slope[24];
	char limit[24];

	text_begin();

	if (result == 0) {
		snprintf(
			limit,
			sizeof(limit),
			"LINEER 0-%ld KPA",
			(long)(validated_limit_kpa_x100 / 100)
		);

		print_center("KALIBRE DOGRULANDI", 0, 0);
		print_center(limit, 18, 0);

		snprintf(
			slope,
			sizeof(slope),
			"CPK %ld",
			(long)counts_per_kpa
		);

		print_center(slope, 36, 0);
		print_center("BAS: CIK", 54, 0);
	} else if (result == -EDOM) {
		print_center("EGIM LINEER DEGIL", 0, 0);
		print_center("30-40 UYUSMADI", 20, 0);
		print_center("ACIK JET KULLANMA", 36, 0);
		print_center("KAPALI T HAT KUR", 52, 0);
	} else if (result == -EOVERFLOW) {
		print_center("HX711 DOYDU", 0, 0);
		print_center("30/40 KPA", 20, 0);
		print_center("BU KARTTA OKUNMUYOR", 36, 0);
		print_center("DUSUK REF GEREK", 52, 0);
	} else if (result == -EAGAIN) {
		print_center("BASINC KARARSIZ", 8, 0);
		print_center("SABITLE", 28, 0);
		print_center("VE TEKRARLA", 48, 0);
	} else if (result == -ERANGE) {
		print_center("BASINC ALGILANMADI", 8, 0);
		print_center("HORTUM VE REFERANS", 28, 0);
		print_center("BAGLANTISINA BAK", 48, 0);
	} else {
		print_center("KALIBRE OLMADI", 8, 0);
		print_center("SENSORU KONTROL", 32, 0);
	}

	text_end();
}

void ui_show_pressure_saturation(void)
{
	text_begin();
	print_center("HX711 SAT", 8, 0);
	print_center("BASINC ARALIK", 28, 0);
	print_center("DISINDA", 48, 0);
	text_end();
}

void ui_show_saved(void)
{
	text_begin();
	print_center("KAYDEDILDI", 24, 0);
	text_end();
}

void ui_show_sensor_error(void)
{
	text_begin();
	print_center("SENSOR HATASI", 12, 0);
	print_center("BAGLANTIYI BAK", 40, 0);
	text_end();
}
