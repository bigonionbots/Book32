#pragma once
// The two LILYGO T5 S3 Pro (H752-01) I2C addresses the audio backend reads from
// freeink-sdk's BoardConfig in its original tree. Both share the touch bus
// (SDA 39 / SCL 40) that InkDeck starts in StickyTouch / LilygoControls.
#include <cstdint>

namespace BoardConfig {
struct BatteryGauge {
    uint8_t gaugeAddr;    // BQ27220 fuel gauge
    uint8_t chargerAddr;  // BQ25896 charger (OTG boost for the audio module)
};
struct Board {
    BatteryGauge batteryGauge;
};
#if defined(BOARD_M5_PAPERDIY)
// No BQ27220 or BQ25896: the M5PM1 does both jobs (PaperDiyPower). 0 = absent.
inline constexpr Board ACTIVE{{0x00, 0x00}};
#else
inline constexpr Board ACTIVE{{0x55, 0x6B}};
#endif
}  // namespace BoardConfig
