#pragma once
// Local date and time for the listening log. Inx Pro reads the T5's RTC (kept
// in local time); InkDeck sets the system clock from NTP in UTC, so this adds
// the audio settings' time zone offset.
#include <cstdint>
#include <sys/time.h>
#include <time.h>
#include "state/SystemSetting.h"

class HalGPIO {
 public:
  struct DateTime {
    uint16_t year = 0;
    uint8_t month = 0;
    uint8_t day = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    uint8_t weekday = 0;  // 1 = Monday ... 7 = Sunday
  };

  bool readDateTime(DateTime& out) const {
    const time_t utc = time(nullptr);
    if (utc < 1577836800) return false;  // before 2020: NTP has not set the clock
    const time_t local = utc + static_cast<time_t>(SETTINGS.getTimeZoneOffsetMinutes()) * 60;
    struct tm t;
    gmtime_r(&local, &t);
    out.year = static_cast<uint16_t>(t.tm_year + 1900);
    out.month = static_cast<uint8_t>(t.tm_mon + 1);
    out.day = static_cast<uint8_t>(t.tm_mday);
    out.hour = static_cast<uint8_t>(t.tm_hour);
    out.minute = static_cast<uint8_t>(t.tm_min);
    out.second = static_cast<uint8_t>(t.tm_sec);
    out.weekday = static_cast<uint8_t>(t.tm_wday == 0 ? 7 : t.tm_wday);
    return true;
  }
};
