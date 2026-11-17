// InkDeck's side of the audio backend: the hooks the core calls
// (Book32_Core/AudioHooks.h), in the order Inx Pro's main.cpp ran them.
#if INX_HAS_AUDIO_MODULE
#include <esp_heap_caps.h>

#include "AudioHooks.h"
#include "abs/AbsClient.h"
#include "abs/AbsCovers.h"
#include "abs/AbsQueue.h"
#include "abs/AbsWifi.h"
#include "system/AudioLink.h"
#include "system/AudioModule.h"
#include "system/AudioPlayer.h"

namespace AudioHooks {

void begin() {
  // malloc() keeps blocks under 4 KB in internal RAM; a long audiobook's
  // strings and MP4 tables then crowd out the Wi-Fi driver's DMA buffers.
  // From 256 bytes up, PSRAM first (as in Inx Pro).
  heap_caps_malloc_extmem_enable(256);
  AudioModule::begin();
  AudioLink::begin();
  AudioPlayer::begin();
  Abs::begin();
  AbsQueue::begin();
  AbsWifi::prepare();
}

void service() {
  AudioModule::service();
  AudioPlayer::service();
  Abs::service();
  AbsCovers::service();
  AbsQueue::service();
}

bool keepAwake() { return AudioPlayer::keepAwake() || AbsQueue::keepAwake(); }

bool holdsRadio() { return AbsWifi::inUse(); }

bool needsFullCpu() { return AudioPlayer::keepAwake(); }

void shutdown() {
  AudioPlayer::shutdown();
  AbsQueue::shutdown();
  // The charger keeps boosting through deep sleep, so the module would drain the battery.
  AudioModule::shutdown();
}

}  // namespace AudioHooks
#endif
