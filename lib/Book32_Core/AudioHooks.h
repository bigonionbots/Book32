#pragma once
// Where InkDeck's core calls into the audiobook player (lib/Book32_Audio).
// Only env:lilygo_t5s3_pro_audio defines INKDECK_AUDIO; everywhere else these
// are empty inline functions, so the other builds are unchanged.
#include "Config.h"

class AsyncWebServer;

#if defined(INKDECK_AUDIO) && INKDECK_AUDIO
namespace AudioHooks {
// Boot, after the filesystems are mounted.
void begin();
// Every main loop: SD reads into the play queue, downloads, token saves.
void service();
// A book is playing, or downloads run on the charger: the idle timer waits.
bool keepAwake();
// A stream, sync or download holds Wi-Fi: paths that power the radio down
// for reading or a game leave it on.
bool holdsRadio();
// A book is playing (USB headphones decode on the S3): stay at full CPU speed.
bool needsFullCpu();
// Before deep sleep: saves progress, pauses downloads, turns the boost off.
void shutdown();
// The web page's Audiobooks card (/api/audio/...).
void registerWeb(AsyncWebServer& server);
}  // namespace AudioHooks
#else
namespace AudioHooks {
inline void begin() {}
inline void service() {}
inline bool keepAwake() { return false; }
inline bool holdsRadio() { return false; }
inline bool needsFullCpu() { return false; }
inline void shutdown() {}
inline void registerWeb(AsyncWebServer&) {}
}  // namespace AudioHooks
#endif
