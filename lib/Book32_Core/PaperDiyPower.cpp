#include "PaperDiyPower.h"
#if defined(BOARD_M5_PAPERDIY)
#include <Wire.h>

namespace {

constexpr uint8_t ADDRESS = 0x6E;
constexpr uint32_t I2C_HZ = 100000;
constexpr uint16_t DEVICE_ID = 0x2050;

constexpr uint8_t REG_DEVICE_ID = 0x00;
constexpr uint8_t REG_POWER_SOURCE = 0x04;
constexpr uint8_t REG_POWER_CONFIG = 0x06;
constexpr uint8_t REG_I2C_CONFIG = 0x09;
constexpr uint8_t REG_WATCHDOG = 0x0A;
constexpr uint8_t REG_SYSTEM_COMMAND = 0x0C;
constexpr uint8_t REG_GPIO_MODE = 0x10;
constexpr uint8_t REG_GPIO_OUT = 0x11;
constexpr uint8_t REG_GPIO_IN = 0x12;
constexpr uint8_t REG_GPIO_DRIVE = 0x13;
constexpr uint8_t REG_GPIO_PULL0 = 0x14;
constexpr uint8_t REG_GPIO_FUNC0 = 0x16;
constexpr uint8_t REG_BATTERY_MV = 0x22;
constexpr uint8_t REG_BUTTON_STATUS = 0x48;
constexpr uint8_t REG_BUTTON_CONFIG1 = 0x49;

constexpr uint8_t CHARGE_ENABLE = 1 << 0;
constexpr uint8_t EPD_POWER_GPIO = 2;
constexpr uint8_t CHARGE_STATUS_GPIO = 3;
// PY_G0 on the FPC I2S header (pin 3): the audio module's enable (docs/audio.md).
constexpr uint8_t AUDIO_MODULE_GPIO = 0;
constexpr uint8_t SINGLE_CLICK_RESET_DISABLE = 1 << 0;
constexpr uint8_t COMMAND_SHUTDOWN = 0xA1;

bool started = false;
bool chipPresent = false;

bool writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(ADDRESS);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool readRegisters(uint8_t reg, uint8_t* out, size_t length) {
    Wire.beginTransmission(ADDRESS);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(ADDRESS, length) != length) return false;
    for (size_t i = 0; i < length; ++i) out[i] = uint8_t(Wire.read());
    return true;
}

bool updateRegister(uint8_t reg, uint8_t clearMask, uint8_t setMask) {
    uint8_t value = 0;
    if (!readRegisters(reg, &value, 1)) return false;
    return writeRegister(reg, uint8_t((value & ~clearMask) | setMask));
}

}  // namespace

namespace PaperDiyPower {

bool begin() {
    if (started) return chipPresent;
    started = true;
    Wire.begin(PAPERDIY_I2C_SDA, PAPERDIY_I2C_SCL, I2C_HZ);
    Wire.setTimeOut(20);
    uint8_t id[2] = {};
    chipPresent = readRegisters(REG_DEVICE_ID, id, sizeof(id)) && (id[0] | (id[1] << 8)) == DEVICE_ID;
    Serial.printf("PaperDIY: M5PM1 %s\n", chipPresent ? "found" : "missing");
    if (!chipPresent) return false;
    // As M5GFX: no I2C idle sleep (the first transaction after it fails) and no watchdog.
    writeRegister(REG_I2C_CONFIG, 0x00);
    writeRegister(REG_WATCHDOG, 0x00);
    // CHG_EN clears itself on reset and in download mode: set it every boot.
    updateRegister(REG_POWER_CONFIG, 0, CHARGE_ENABLE);
    // CHG_STAT: a plain input with pull-up.
    updateRegister(REG_GPIO_FUNC0, 0b11 << (2 * CHARGE_STATUS_GPIO), 0);
    updateRegister(REG_GPIO_MODE, 1 << CHARGE_STATUS_GPIO, 0);
    updateRegister(REG_GPIO_PULL0, 0b11 << (2 * CHARGE_STATUS_GPIO), 0b01 << (2 * CHARGE_STATUS_GPIO));
    // InkDeck reads the power button itself while it runs.
    setPowerButtonResetEnabled(false);
    return true;
}

bool present() { return chipPresent; }

namespace {
// GPIO function, push-pull output, then the level.
bool setOutput(uint8_t gpio, bool high) {
    return updateRegister(REG_GPIO_FUNC0, 0b11 << (2 * gpio), 0) &&
           updateRegister(REG_GPIO_MODE, 0, 1 << gpio) &&
           updateRegister(REG_GPIO_DRIVE, 1 << gpio, 0) &&
           updateRegister(REG_GPIO_OUT, high ? 0 : 1 << gpio, high ? 1 << gpio : 0);
}
}  // namespace

void setEpdPower(bool on) {
    if (!begin()) return;
    setOutput(EPD_POWER_GPIO, on);
}

bool setAudioModulePower(bool on) {
    return begin() && setOutput(AUDIO_MODULE_GPIO, on);
}

bool readBatteryMillivolts(uint16_t& millivolts) {
    uint8_t raw[2] = {};
    if (!chipPresent || !readRegisters(REG_BATTERY_MV, raw, sizeof(raw))) return false;
    millivolts = uint16_t(raw[0] | ((raw[1] & 0x0F) << 8));
    return millivolts > 0;
}

bool isCharging() {
    uint8_t in = 0;
    return chipPresent && readRegisters(REG_GPIO_IN, &in, 1) && (in & (1 << CHARGE_STATUS_GPIO)) == 0;
}

bool isExternalPower() {
    uint8_t source = 0;
    return chipPresent && readRegisters(REG_POWER_SOURCE, &source, 1) && (source & 0x07) == 0;
}

bool isPowerButtonPressed() {
    uint8_t status = 0;
    return chipPresent && readRegisters(REG_BUTTON_STATUS, &status, 1) && (status & 0x01) != 0;
}

void setPowerButtonResetEnabled(bool enabled) {
    if (!chipPresent) return;
    updateRegister(REG_BUTTON_CONFIG1, enabled ? SINGLE_CLICK_RESET_DISABLE : 0,
                   enabled ? 0 : SINGLE_CLICK_RESET_DISABLE);
}

void shutdown() {
    if (chipPresent) writeRegister(REG_SYSTEM_COMMAND, COMMAND_SHUTDOWN);
}

}  // namespace PaperDiyPower
#endif
