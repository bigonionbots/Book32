#if INX_HAS_AUDIO_MODULE
#include <Preferences.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <WiFi.h>
#include "HalGPIO.h"
#include "SDCardManager.h"
#include "state/NetworkCredential.h"
#include "state/SystemSetting.h"
#include "util/SdIoMutex.h"

SDCardManager SdMan;
HalGPIO gpio;

bool SDCardManager::removeDir(const char* path) {
  fs::File dir = EbookFS.open(path);
  if (!dir || !dir.isDirectory()) return false;
  std::vector<std::string> files, folders;
  for (fs::File entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    std::string child = std::string(path) + "/" + (strrchr(entry.name(), '/') ? strrchr(entry.name(), '/') + 1 : entry.name());
    (entry.isDirectory() ? folders : files).push_back(child);
  }
  dir.close();
  for (const auto& f : files) EbookFS.remove(f.c_str());
  for (const auto& f : folders) removeDir(f.c_str());
  return EbookFS.rmdir(path);
}

namespace {
StaticSemaphore_t gSdStorage;
SemaphoreHandle_t gSdMutex = nullptr;
portMUX_TYPE gSdInit = portMUX_INITIALIZER_UNLOCKED;
}  // namespace

namespace SdIoMutex {
void lock() {
  if (gSdMutex == nullptr) {
    portENTER_CRITICAL(&gSdInit);
    if (gSdMutex == nullptr) gSdMutex = xSemaphoreCreateRecursiveMutexStatic(&gSdStorage);
    portEXIT_CRITICAL(&gSdInit);
  }
  xSemaphoreTakeRecursive(gSdMutex, portMAX_DELAY);
}
void unlock() {
  if (gSdMutex != nullptr) xSemaphoreGiveRecursive(gSdMutex);
}
}  // namespace SdIoMutex

SystemSetting& SystemSetting::getInstance() {
  static SystemSetting settings;
  static bool loaded = false;
  if (!loaded) {
    loaded = true;
    settings.load();
  }
  return settings;
}

void SystemSetting::load() {
  Preferences prefs;
  if (!prefs.begin("audio", true)) return;
  autoResumeDownloadsOnCharge = prefs.getUChar("dlCharge", 1);
  timeZoneOffsetMinutes = prefs.getShort("tzMin", 0);
  prefs.end();
}

void SystemSetting::save() const {
  Preferences prefs;
  if (!prefs.begin("audio", false)) return;
  prefs.putUChar("dlCharge", autoResumeDownloadsOnCharge);
  prefs.putShort("tzMin", timeZoneOffsetMinutes);
  prefs.end();
}

WifiCredentialStore& WifiCredentialStore::getInstance() {
  static WifiCredentialStore store;
  return store;
}

bool WifiCredentialStore::loadFromFile() {
  credentials.clear();
  // The driver must be initialised to read its stored station config.
  const bool wasOff = WiFi.getMode() == WIFI_OFF;
  if (wasOff) WiFi.mode(WIFI_STA);
  wifi_config_t config{};
  const bool read = esp_wifi_get_config(WIFI_IF_STA, &config) == ESP_OK;
  if (wasOff) WiFi.mode(WIFI_OFF);
  if (!read) return false;
  const char* ssid = reinterpret_cast<const char*>(config.sta.ssid);
  if (ssid[0] == '\0') return false;
  credentials.push_back({std::string(ssid, strnlen(ssid, sizeof(config.sta.ssid))),
                         std::string(reinterpret_cast<const char*>(config.sta.password),
                                     strnlen(reinterpret_cast<const char*>(config.sta.password), sizeof(config.sta.password)))});
  return true;
}

bool WifiCredentialStore::hasSavedCredential(const std::string& ssid) const {
  for (const auto& c : credentials) if (c.ssid == ssid) return true;
  return false;
}
#endif
