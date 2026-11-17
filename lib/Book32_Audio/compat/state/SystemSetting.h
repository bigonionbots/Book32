#pragma once
// The two system settings the audio backend reads. InkDeck keeps them in NVS
// (AudioSettings); see AudioSettings.h.
#include <cstdint>

struct SystemSetting {
  uint8_t autoResumeDownloadsOnCharge = 1;
  int16_t timeZoneOffsetMinutes = 0;
  int getTimeZoneOffsetMinutes() const { return timeZoneOffsetMinutes; }
  static SystemSetting& getInstance();
  void load();
  void save() const;
};

#define SETTINGS SystemSetting::getInstance()
