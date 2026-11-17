#pragma once
// The saved Wi-Fi network for the audio backend's on-demand joins. InkDeck
// keeps one network in the ESP32 Wi-Fi driver's own storage (WiFi.begin() with
// no arguments); this exposes it in the shape the backend expects.
#include <string>
#include <vector>

struct WifiCredential {
  std::string ssid;
  std::string password;
};

class WifiCredentialStore {
 public:
  static WifiCredentialStore& getInstance();
  bool loadFromFile();
  const std::vector<WifiCredential>& getCredentials() const { return credentials; }
  bool hasSavedCredential(const std::string& ssid) const;

 private:
  std::vector<WifiCredential> credentials;
};

#define WIFI_STORE WifiCredentialStore::getInstance()
