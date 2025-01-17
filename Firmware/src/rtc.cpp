#include "global.h"

#define EPOCH_2000 946684800
#define DAYS_IN_4_YEARS 1461

u8 rtcTimeQuality = TIME_QUALITY_NONE;
i16 rtcTimezoneOffset = 0;
const u16 days[4][12] =
	{
		{0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335},
		{366, 397, 425, 456, 486, 517, 547, 578, 609, 639, 670, 700},
		{731, 762, 790, 821, 851, 882, 912, 943, 974, 1004, 1035, 1065},
		{1096, 1127, 1155, 1186, 1216, 1247, 1277, 1308, 1339, 1369, 1400, 1430},
};

void rtcInit() {
	datetime_t t;
	rtcConvertToDatetime(EPOCH_2000, &t);
	rtc_init();
	rtcSetDatetime(&t, TIME_QUALITY_NONE);
}

bool rtcSetDatetime(datetime_t *t, u8 quality, bool hasDotw) {
	if (quality < rtcTimeQuality) return false;
	rtcTimeQuality = quality;
	if (!hasDotw) setDotwInDatetime(t);
	return rtc_set_datetime(t);
}

void rtcConvertToDatetime(u32 timestamp, datetime_t *t) {
	if (timestamp < EPOCH_2000) {
		t->year = 2000;
		t->month = 1;
		t->day = 1;
		t->dotw = 5;
		t->hour = 0;
		t->min = 0;
		t->sec = 0;
		return;
	}
	timestamp -= EPOCH_2000;
	t->sec = timestamp % 60;
	timestamp /= 60;
	t->min = timestamp % 60;
	timestamp /= 60;
	t->hour = timestamp % 24;
	timestamp /= 24;
	t->dotw = (timestamp + 5) % 7; // 2000-01-01 was a Saturday (5)
	u16 year = 2000;

	const u16 *daysOfThisYear = days[0];
	while (timestamp >= DAYS_IN_4_YEARS) {
		year += 4;
		timestamp -= DAYS_IN_4_YEARS;
	}
	for (u8 i = 3; i >= 0; i--) {
		if (timestamp >= days[i][0]) {
			year += i;
			daysOfThisYear = days[i];
			break;
		}
	}
	t->year = year;
	for (u8 i = 11; i >= 0; i--) {
		if (timestamp >= daysOfThisYear[i]) {
			t->month = i + 1;
			t->day = timestamp - daysOfThisYear[i] + 1;
			break;
		}
	}
}

time_t rtcGetUnixTimestamp() {
	datetime_t d;
	rtcGetDatetime(&d);
	time_t t = EPOCH_2000;
	t += (DAYS_IN_4_YEARS * 24 * 60 * 60) * ((d.year - 2000) / 4); // complete 4 years
	t += 24 * 60 * 60 * days[d.year % 4][d.month - 1]; // until the beginning of this month
	t += (d.day - 1) * 24 * 60 * 60; // until the beginning of the day
	t += (d.hour) * 60 * 60;
	t += (d.min) * 60;
	t += d.sec;
	return t;
}

void setDotwInDatetime(datetime_t *t) {
	u32 daysSince2000 = 5;
	u16 years = t->year - 2000;
	daysSince2000 += (years / 4) * 1461;
	daysSince2000 += days[years % 4][t->month - 1] + t->day - 1;
	t->dotw = daysSince2000 % 7;
}