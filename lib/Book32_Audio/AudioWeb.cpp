// The web page's Audiobooks card: Audiobookshelf sign-in, the download queue,
// what is playing, and two settings. The audio backend is main-loop only and
// the web server runs on its own task, so handlers read a status snapshot
// that service() refreshes and leave requests for the main loop to act on.
#if INX_HAS_AUDIO_MODULE
#include <ArduinoJson.h>
#include <AsyncJson.h>
#include <ESPAsyncWebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <string>

#include "AudioHooks.h"
#include "abs/AbsClient.h"
#include "abs/AbsQueue.h"
#include "abs/AbsWifi.h"
#include "abs/AbsWorker.h"
#include "state/SystemSetting.h"
#include "system/AudioModule.h"
#include "system/AudioPlayer.h"
#include "system/UsbAudioOut.h"
#include "system/UsbAudioSink.h"

namespace AudioWeb {

namespace {
SemaphoreHandle_t lock = nullptr;
std::string statusJson = "{}";
unsigned long lastSnapshot = 0;

// Requests from the web task, taken by the main loop.
struct Pending {
  bool signIn = false;
  bool signOut = false;
  std::string server, user, password;
  std::string queueAction, queueItem;  // start, pause, remove
};
Pending pending;

// The last sign-in's outcome, for the page.
std::string signInState;  // "", "working", "ok", or an error

struct Guard {
  Guard() { xSemaphoreTake(lock, portMAX_DELAY); }
  ~Guard() { xSemaphoreGive(lock); }
};

const char* queueState(AbsQueue::State s) {
  switch (s) {
    case AbsQueue::State::Waiting: return "waiting";
    case AbsQueue::State::Starting: return "starting";
    case AbsQueue::State::Downloading: return "downloading";
    case AbsQueue::State::Failed: return "failed";
    case AbsQueue::State::Paused: return "paused";
  }
  return "";
}

void snapshot() {
  JsonDocument doc;
  doc["available"] = true;
  doc["signedIn"] = Abs::signedIn();
  doc["server"] = Abs::serverUrl();
  doc["user"] = Abs::username();
  doc["charging"] = AbsQueue::charging();
  doc["tzMinutes"] = SETTINGS.getTimeZoneOffsetMinutes();
  doc["autoDownloadOnCharger"] = SETTINGS.autoResumeDownloadsOnCharge != 0;
  JsonArray queue = doc["queue"].to<JsonArray>();
  for (const auto& e : AbsQueue::entries()) {
    JsonObject o = queue.add<JsonObject>();
    o["id"] = e.itemId;
    o["title"] = e.title;
    o["author"] = e.author;
    o["state"] = queueState(e.state);
    o["now"] = e.now;
    if (!e.error.empty()) o["error"] = e.error;
  }
  const AudioPlayer::View v = AudioPlayer::view();
  if (v.open) {
    JsonObject p = doc["playing"].to<JsonObject>();
    p["title"] = v.title;
    p["paused"] = v.paused;
    p["remote"] = v.remote;
    p["positionMs"] = v.bookPositionMs;
    p["durationMs"] = v.bookDurationMs;
    if (v.chapter >= 0) p["chapter"] = v.chapterTitle;
    p["output"] = v.output == UsbAudioSink::USB_HEADPHONES ? "headphones"
                  : v.waitingForModule                      ? "waiting"
                                                            : "module";
  }
  const auto snap = AudioModule::snapshot();
  doc["portOn"] = snap.linkOn;
#if INX_AUDIO_LINK_UART
  doc["portName"] = "Audio module";
#else
  doc["portName"] = "USB-C port";
#endif
  if (snap.headphonesState == UsbAudioOut::State::Ready || snap.headphonesState == UsbAudioOut::State::Playing)
    doc["headphones"] = snap.headphones.product;
  std::string out;
  serializeJson(doc, out);
  Guard g;
  statusJson = out;
}

}  // namespace

void service() {
  if (lock == nullptr) return;
  Pending work;
  {
    Guard g;
    work = pending;
    pending = Pending{};
  }
  if (work.signIn) {
    {
      Guard g;
      signInState = "working";
    }
    const std::string server = work.server, user = work.user, password = work.password;
    const bool posted = AbsWorker::post([server, user, password] {
      std::string error;
      bool ok = false;
      {
        AbsWifi::Use wifi(error);
        if (wifi.ok()) ok = Abs::signIn(server, user, password, error);
      }
      Guard g;
      signInState = ok ? "ok" : (error.empty() ? "Could not sign in" : error);
    });
    if (!posted) {
      Guard g;
      signInState = "Busy; try again in a moment";
    }
  }
  if (work.signOut) {
    AbsWorker::post([] {
      std::string ignored;
      AbsWifi::Use wifi(ignored);  // to tell the server, when it can
      Abs::signOut();
    });
  }
  if (!work.queueItem.empty()) {
    if (work.queueAction == "start") AbsQueue::startNow(work.queueItem);
    else if (work.queueAction == "pause") AbsQueue::pause(work.queueItem);
    else if (work.queueAction == "remove") AbsQueue::remove(work.queueItem);
  }
  const unsigned long now = millis();
  if (now - lastSnapshot >= 2000 || work.signIn || work.signOut || !work.queueItem.empty()) {
    lastSnapshot = now;
    snapshot();
  }
}

void registerEndpoints(AsyncWebServer& server) {
  if (lock == nullptr) lock = xSemaphoreCreateMutex();

  server.on("/api/audio/status", HTTP_GET, [](AsyncWebServerRequest* request) {
    std::string body;
    {
      Guard g;
      body = statusJson;
      // Splice the sign-in outcome into the snapshot.
      if (body.size() > 1 && !signInState.empty()) {
        JsonDocument extra;
        extra.set(signInState);
        std::string value;
        serializeJson(extra, value);
        body.insert(body.size() - 1, ",\"signIn\":" + value);
      }
    }
    request->send(200, "application/json", body.c_str());
  });

  auto* signIn = new AsyncCallbackJsonWebHandler("/api/audio/signin", [](AsyncWebServerRequest* request, JsonVariant& json) {
    std::string server = json["server"] | "";
    std::string user = json["user"] | "";
    std::string password = json["password"] | "";
    while (!server.empty() && server.back() == '/') server.pop_back();
    if (server.empty() || user.empty()) {
      request->send(400, "application/json", "{\"error\":\"Server address and user name are required\"}");
      return;
    }
    if (server.rfind("http://", 0) != 0 && server.rfind("https://", 0) != 0) server = "https://" + server;
    {
      Guard g;
      pending.signIn = true;
      pending.server = server;
      pending.user = user;
      pending.password = password;
      signInState = "working";
    }
    request->send(202, "application/json", "{\"status\":\"signing in\"}");
  });
  server.addHandler(signIn);

  server.on("/api/audio/signout", HTTP_POST, [](AsyncWebServerRequest* request) {
    {
      Guard g;
      pending.signOut = true;
      signInState.clear();
    }
    request->send(200, "application/json", "{\"status\":\"signed out\"}");
  });

  auto* queue = new AsyncCallbackJsonWebHandler("/api/audio/queue", [](AsyncWebServerRequest* request, JsonVariant& json) {
    const std::string action = json["action"] | "";
    const std::string id = json["id"] | "";
    if (id.empty() || (action != "start" && action != "pause" && action != "remove")) {
      request->send(400, "application/json", "{\"error\":\"action (start, pause, remove) and id are required\"}");
      return;
    }
    {
      Guard g;
      pending.queueAction = action;
      pending.queueItem = id;
    }
    request->send(200, "application/json", "{\"status\":\"ok\"}");
  });
  server.addHandler(queue);

  auto* settings = new AsyncCallbackJsonWebHandler("/api/audio/settings", [](AsyncWebServerRequest* request, JsonVariant& json) {
    // Two small fields: written straight to NVS (no SD) from the web task.
    SystemSetting& s = SETTINGS;
    if (json["tzMinutes"].is<int>()) {
      const int tz = json["tzMinutes"].as<int>();
      if (tz >= -12 * 60 && tz <= 14 * 60) s.timeZoneOffsetMinutes = static_cast<int16_t>(tz);
    }
    if (json["autoDownloadOnCharger"].is<bool>()) s.autoResumeDownloadsOnCharge = json["autoDownloadOnCharger"].as<bool>() ? 1 : 0;
    s.save();
    request->send(200, "application/json", "{\"status\":\"saved\"}");
  });
  server.addHandler(settings);
}

}  // namespace AudioWeb

namespace AudioHooks {
void registerWeb(AsyncWebServer& server) { AudioWeb::registerEndpoints(server); }
}  // namespace AudioHooks

#endif
