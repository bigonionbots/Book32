#pragma once
#include "Config.h"
#if defined(BOARD_M5_PAPERDIY)
#include <Arduino.h>

// The M5Stack PaperDIY's M5PM1 power chip (I2C 0x6E on SDA 41 / SCL 42). It
// owns what the S3 cannot reach directly: the e-paper rail (its GPIO2), the
// charge-status line (its GPIO3), the battery ADC, the power button and
// power-off. Register map: m5stack/M5PM1 src/M5PM1.h; the panel rail sequence
// is M5GFX's PaperDIY setup. Ported from bigonionbots/inx-pro
// (lib/hal_paperdiy/PaperDiyPower.cpp, MIT).
//
// Every call is from the main loop (the bus is not shared with a task).
namespace PaperDiyPower {

// Starts Wire on the PM1 bus and configures the chip. Safe to call again.
bool begin();
bool present();

// The panel rail. Refreshes also need GPIO46 (BST_EN), which FastEPD drives
// as the PaperS3 panel's PWR pin.
void setEpdPower(bool on);

// PM1 GPIO0, PY_G0 on the FPC I2S header (pin 3), wired to the audio module's
// EN: high runs it, low holds it in reset. It keeps its level while the S3
// sleeps or is off, so InkDeck drives it low before either.
bool setAudioModulePower(bool on);

bool readBatteryMillivolts(uint16_t& millivolts);
// CHG_STAT (PM1 GPIO3) is LOW while the charger runs.
bool isCharging();
// 5VIN (USB-C) is the power source.
bool isExternalPower();

// The power button's level (true = held).
bool isPowerButtonPressed();
// The chip's own single-click reset. Off while InkDeck reads the button;
// back on before deep sleep, so a click still wakes the board.
void setPowerButtonResetEnabled(bool enabled);

// Cuts the S3's supply; only the PM1 stays powered (the power button starts it again).
void shutdown();

}  // namespace PaperDiyPower
#endif
