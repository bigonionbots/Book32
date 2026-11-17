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
