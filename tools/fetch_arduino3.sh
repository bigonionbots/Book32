#!/bin/sh
# Extracts Arduino-ESP32 3.3.7 to the folder env:lilygo_t5s3_pro_audio links.
# espressif32 6.12.0 (the other envs) and pioarduino both install into
# ~/.platformio/packages/framework-arduinoespressif32 and replace each other's
# copy, so the audio env keeps its own.
set -e
dest="$HOME/.platformio/frameworks/arduino-esp32-3.3.7"
[ -f "$dest/package.json" ] && { echo "Already there: $dest"; exit 0; }
mkdir -p "$dest"
curl -fsSL https://github.com/espressif/arduino-esp32/releases/download/3.3.7/esp32-core-3.3.7.tar.xz \
  | tar -xJ --strip-components=1 -C "$dest"
echo "Arduino-ESP32 3.3.7 in $dest"
