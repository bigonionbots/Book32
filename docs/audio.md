# Audiobooks on the LILYGO (audio build)

The `lilygo_t5s3_pro_audio` build of InkDeck adds an **Audiobooks** app to the
LILYGO T5 E-Paper S3 Pro H752-01 / Pro Lite. It plays:

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
