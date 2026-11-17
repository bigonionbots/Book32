# Audiobooks (audio builds)

The `lilygo_t5s3_pro_audio` build of InkDeck adds an **Audiobooks** app to the
LILYGO T5 E-Paper S3 Pro H752-01 / Pro Lite, and `m5_paperdiy_audio` adds it to
the M5Stack PaperDIY ([below](#m5stack-paperdiy)). It plays:

- books on the MicroSD card under `/Audiobooks`: `.m4b`, `.m4a`, `.mp4` or
  `.mp3` files, or a folder of files per book (played in name order);
- books and podcasts from an [Audiobookshelf](https://www.audiobookshelf.org/)
  server, streamed over Wi-Fi or downloaded to the card. Progress syncs with
  the server, and listening sessions count in its stats.

Sound comes out of the T5's USB-C port, through either:

- **USB headphones**: a USB Audio Class 1.0 adapter (tested: Sabrent AU-UCMA,
  C-Media 0D8C:0147). The T5 decodes MP3 and AAC itself. UAC 2.0-only adapters
  do not work.
- **The USB-C audio module**: an M5Stack ATOM Lite running
  [atom-lite-audio-module](https://github.com/bigonionbots/atom-lite-audio-module),
  which plays to Bluetooth (A2DP) headphones or a wired PCM5102A DAC.

The T5 powers either one from its charger's OTG boost.

## Using it

1. Open **Audiobooks** on the main menu.
2. **Server** lists your Audiobookshelf libraries. Sign in first on the device's
   web page: **Audiobooks** tab, then **Audiobookshelf**. InkDeck keeps a token,
   never your password.
3. On a book, **Stream** plays it now. **Download** queues it to download while
   the T5 is on its charger; **Download now** starts on battery.
4. **Output** shows what is on the USB-C port. For the module, choose Bluetooth
   or Wired, then **Search** with your headphones in pairing mode and tap them.
   For USB headphones there is a **Test tone**.

A book keeps playing after you leave the app, including while you read. It
holds off idle sleep, keeps Wi-Fi on while it streams, and is saved before the
device sleeps.

**USB console:** while the USB-C port is on, it belongs to the headphones or
the module, so the serial console and esptool's auto-reset stop working until
you restart. To flash anyway, hold BOOT, tap RST, then release BOOT.
**Output > Save log** writes `/audio-log.txt` to the card.

## Building

```sh
tools/fetch_arduino3.sh            # once: Arduino-ESP32 3.3.7 for this env
pio run -e lilygo_t5s3_pro_audio
pio run -e lilygo_t5s3_pro_audio -t upload
pio run -e lilygo_t5s3_pro_audio -t uploadfs
```

This env alone runs on Arduino-ESP32 3.3.7 (ESP-IDF 5.5), which the USB host
and UAC drivers need. The other envs stay on espressif32 6.12.0. The audio
build updates over the air from
[bigonionbots/Book32](https://github.com/bigonionbots/Book32/releases) using
the assets `inkdeck-lilygo-audio-firmware.bin` and
`inkdeck-lilygo-audio-littlefs.bin`.

The hardware notes (the BQ25896 boost, the port's wiring, the ATOM Lite's
USB bridge and its throughput) are in Inx Pro's `docs/audio-module.md`, where
this code comes from; see `lib/Book32_Audio/README.md`.

## M5Stack PaperDIY

Experimental, env `m5_paperdiy_audio`. The PaperDIY has no USB host port, so
there are no USB headphones. Sound comes from the same audio module firmware
running on an **ESP32-DevKitC with a WROVER module** (env `wrover` of
[atom-lite-audio-module](https://github.com/bigonionbots/atom-lite-audio-module)),
wired to the PaperDIY's 12-pin FPC I2S header, with a PCM5102A for wired
headphones and Bluetooth A2DP for wireless ones.

| FPC pin | PaperDIY | DevKitC | Purpose |
|---|---|---|---|
| 8 | G1 | GPIO32 | Link, PaperDIY TX -> module RX |
| 7 | G2 | GPIO26 | Link, module TX -> PaperDIY RX |
| 3 | PY_G0 (M5PM1 GPIO0) | EN | Module power: high runs it, low holds it in reset |
| 11 / 12 | 3V3_AU | 3V3 | Module supply |
| 2 / 6 | GND | GND | Ground |

Leave G0 (a boot strap), G45 (the panel's OE) and G41 / G42 (the M5PM1's I2C)
unconnected. The PCM5102A connects to the DevKitC: BCK GPIO22, LCK GPIO19,
DIN GPIO23, SCK / FMT / FLT / DEMP to GND, XSMT to 3V3, VIN to 3V3.

**Power.** M5Stack does not rate 3V3_AU's current. The module averages roughly
100-150 mA while it streams to Bluetooth, with short peaks above 250 mA
(typical ESP32 figures, not measured here). If the PaperDIY resets or the panel
glitches when Bluetooth connects, add a 470 uF capacitor across the DevKitC's
3V3 and GND, or power the DevKitC separately (share GND only). While the module
is off (EN low) it draws only the DevKitC's power LED and regulator leakage.

**Using it.** **Output** on the Audiobooks app has **Module on / off**. Playing a
book turns the module on by itself, and it goes off again 20 s after it stops
answering, and before the PaperDIY sleeps or powers down (after hanging up a
Bluetooth headset, so it reconnects at once next time). Without a touchscreen,
the keys move a frame over the screen's buttons: KEY2 next, KEY1 previous,
hold KEY2 to press, hold KEY1 for back.

Unlike the LILYGO, the USB console keeps working while the module runs.

```sh
tools/fetch_arduino3.sh
pio run -e m5_paperdiy_audio -t upload
pio run -e m5_paperdiy_audio -t uploadfs
```

OTA assets: `inkdeck-paperdiy-audio-firmware.bin` and
`inkdeck-paperdiy-audio-littlefs.bin`.
