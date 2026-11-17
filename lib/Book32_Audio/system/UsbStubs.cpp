/**
 * @file UsbStubs.cpp
 * @brief The USB-host parts of the audio backend, for boards without them (INX_AUDIO_USB_HOST 0).
 *
 * The M5Stack PaperDIY has no USB host port and no BQ25896: the module is on a UART and its power
 * is an M5PM1 GPIO. These stand-ins keep the callers (player, app, web page) unchanged: no
 * charger, never any USB headphones, and no USB bridge.
 */

#include "Bq25896.h"
#include "UsbAudioOut.h"
#include "UsbAudioSink.h"
#include "UsbSerialHost.h"

#if INX_HAS_AUDIO_MODULE && !INX_AUDIO_USB_HOST

namespace Bq25896 {
bool present() { return false; }
bool readStatus(Status&) { return false; }
bool startAdcConversion() { return false; }
bool externalVbusPresent(const Status&) { return false; }
bool disableWatchdog() { return false; }
bool setOtgConfig(bool) { return false; }
const char* vbusStatName(uint8_t) { return "n/a"; }
}  // namespace Bq25896

namespace UsbAudioOut {
bool install() { return false; }
void uninstall() {}
State state() { return State::Off; }
bool isConnected() { return false; }
DeviceInfo device() { return {}; }
Counters counters() { return {}; }
bool playTestTone() { return false; }
uint32_t connectCounter() { return 0; }
uint32_t bestRate(uint32_t sourceRate) { return sourceRate; }
bool streamStart(uint32_t) { return false; }
size_t streamWrite(const int16_t*, size_t, uint32_t) { return 0; }
void streamStop() {}
uint32_t streamRate() { return 0; }
uint32_t streamBufferedMs() { return 0; }
bool setVolume(uint8_t) { return false; }
}  // namespace UsbAudioOut

namespace UsbAudioSink {
void begin() {}
bool ready() { return false; }
uint32_t sessionCounter() { return 0; }
bool send(audiolink::Type, const void*, size_t, uint32_t) { return false; }
bool sendAudio(uint8_t, const uint8_t*, size_t, uint32_t) { return false; }
audiolink::StatusPayload status() { return {}; }
void service() {}
}  // namespace UsbAudioSink

#if !INX_AUDIO_LINK_UART
// Neither USB nor a UART: nothing reaches the module. Builds, but never connects.
namespace UsbSerialHost {
bool start(uint32_t) { return false; }
void stop() {}
State state() { return State::Stopped; }
bool isConnected() { return false; }
DeviceInfo device() { return {}; }
Counters counters() { return {}; }
size_t write(const uint8_t*, size_t, uint32_t) { return 0; }
size_t read(uint8_t*, size_t, uint32_t) { return 0; }
void clearReceived() {}
}  // namespace UsbSerialHost
#endif

#endif  // INX_HAS_AUDIO_MODULE && !INX_AUDIO_USB_HOST
