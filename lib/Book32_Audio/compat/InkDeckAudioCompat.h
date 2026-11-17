#pragma once
// Force-included into every source of the audio build (see platformio.ini,
// env:lilygo_t5s3_pro_audio). The audio backend under lib/Book32_Audio is kept
// close to its origin (bigonionbots/inx-pro, src/abs, src/audio, src/system) so
// later fixes there can be carried over; this header maps the few macros it
// expects onto InkDeck's.
#ifdef __cplusplus
#include "Config.h"
#endif

#ifndef INX_SERIAL
#define INX_SERIAL Serial
#endif
#ifndef INX_VERSION
#define INX_VERSION SYSTEM_VERSION
#endif

// Which audio hardware the board has (platformio.ini sets them per env):
// - INX_AUDIO_USB_HOST: the USB-C port runs as a USB host (LILYGO: module over a USB bridge, USB
//   headphone adapters, BQ25896 boost). 0 builds stubs in system/UsbStubs.cpp instead.
// - INX_AUDIO_LINK_UART: the module is on a plain UART (system/UartModuleSerial.cpp) rather than
//   behind a USB bridge (UsbSerialHost).
// - INX_AUDIO_PM1_POWER: the module's power is switched by an M5PM1 GPIO (PaperDIY) rather than
//   the BQ25896 OTG boost.
#ifndef INX_AUDIO_USB_HOST
#define INX_AUDIO_USB_HOST 1
#endif
#ifndef INX_AUDIO_LINK_UART
#define INX_AUDIO_LINK_UART 0
#endif
#ifndef INX_AUDIO_PM1_POWER
#define INX_AUDIO_PM1_POWER 0
#endif
