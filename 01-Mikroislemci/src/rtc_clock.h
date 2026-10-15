#ifndef RTC_CLOCK_H
#define RTC_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

struct rtc_datetime {
	uint16_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	uint8_t weekday;
	bool valid;
};

int rtc_clock_init(void);
int rtc_clock_get(struct rtc_datetime *time);
int rtc_clock_set(const struct rtc_datetime *time);

#endif
