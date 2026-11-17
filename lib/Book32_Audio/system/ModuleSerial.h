#pragma once
/**
 * @file ModuleSerial.h
 * @brief The byte link to the audio module, whichever way the board reaches it.
 *
 * AudioLink (the protocol) and AudioModule (power, link test) talk to the module through this.
 * - LILYGO T5 S3 Pro: the module's USB-to-UART bridge, with the T5 as USB host. ModuleSerial is
 *   then simply another name for UsbSerialHost.
 * - M5Stack PaperDIY (INX_AUDIO_LINK_UART): a UART on two pins of the FPC header, wired straight
 *   to the module's UART (system/UartModuleSerial.cpp). The same calls and types.
 */

#if INX_HAS_AUDIO_MODULE

#if INX_AUDIO_LINK_UART

#include <cstddef>
#include <cstdint>

namespace ModuleSerial {

enum class State : uint8_t {
  Stopped,    ///< UART driver not installed
  Waiting,    ///< Not used: a UART cannot tell whether anything is on the far end
  Connected,  ///< UART running (the module itself shows up as AudioLink's HELLO)
  Failed,     ///< start() failed; see the module log
};

struct DeviceInfo {
  uint16_t vid = 0;
  uint16_t pid = 0;
  const char* driver = "";  ///< "UART"
  char product[40] = {};    ///< "UART<n> TX <pin> RX <pin>"
};

struct Counters {
  uint32_t rxBytes = 0;
  uint32_t txBytes = 0;
  uint32_t rxDropped = 0;  ///< Receive buffer or FIFO overflows
  uint32_t connects = 0;
  uint32_t disconnects = 0;
  uint32_t lineErrors = 0;  ///< Framing and parity errors
};

/** Installs the UART driver on the configured pins. No-op if already running. */
bool start(uint32_t baud);
/** Uninstalls the driver and lets the pins float. */
void stop();
State state();
bool isConnected();
DeviceInfo device();
Counters counters();
/** Blocking write. Returns the bytes written: all or nothing, 0 when stopped or on timeout. */
size_t write(const uint8_t* data, size_t length, uint32_t timeoutMs);
/** Reads up to length received bytes, waiting up to timeoutMs for the first one. */
size_t read(uint8_t* data, size_t length, uint32_t timeoutMs);
/** Drops everything waiting in the receive buffer. */
void clearReceived();

}  // namespace ModuleSerial

#else

#include "UsbSerialHost.h"
namespace ModuleSerial = UsbSerialHost;

#endif  // INX_AUDIO_LINK_UART

#endif  // INX_HAS_AUDIO_MODULE
