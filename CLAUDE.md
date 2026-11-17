# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

InkDeck: Arduino/PlatformIO firmware for ESP32-S3 e-paper devices (eReader + Todo, Klipper monitor, Inkade game emulator, web management UI, OTA from GitHub releases). The repo is `rolohaun/Book32`; "Book32" is both the project's old name and the DIY board. `pi/` is a separate, finished Python edition for Raspberry Pi; it does not track ESP32 releases.

## Build targets

Four PlatformIO environments in `platformio.ini`, each selected by a board define:

| env | define | device |
| --- | --- | --- |
| `seeed_xiao_esp32s3` (default) | `BOARD_SEEED_XIAO_ESP32S3` | Book32 DIY, 7.5" GxEPD2 panel, three buttons, no touch |
| `seeed_reterminal_sticky` | `BOARD_SEEED_STICKY` | Seeed Sticky, touch, SD, buzzer, 32MB flash |
| `lilygo_t5s3_pro` | `BOARD_LILYGO_T5S3_PRO` | LILYGO T5 S3 Pro H752-01 only (experimental), FastEPD, frontlight, `-O3` |
| `lilygo_t5s3_pro_audio` | `BOARD_LILYGO_T5S3_PRO` + `INKDECK_AUDIO` | This fork's audio build: the LILYGO plus Audiobooks (see below) |

```sh
pio run -e <env>                 # build firmware
pio run -e <env> -t buildfs      # build the LittleFS web image from data/
pio run -e <env> -t upload       # flash app;  -t uploadfs  flashes data/ to the 1MB "spiffs" partition
pio device monitor               # 115200 baud
```

A change to shared code should be built for all four envs; board `#if`s are everywhere.

## Tests

There is no single test runner. Tests are standalone host programs in `test/`, each run directly:

- **C/C++ host tests** compile headers from `lib/Book32_Core` etc. against stubs. The build line is in each file's header comment, e.g.
  `c++ -std=c++17 -I lib/Book32_Core test/pad_mapping_host/test.cpp -o /tmp/t && /tmp/t`
  Board-specific ones need the define (`-DBOARD_SEEED_STICKY`, `-DBOARD_LILYGO_T5S3_PRO`). Some (Arduboy) need `.pio/libdeps/...` populated by a prior `pio run`.
- **Node tests** (`test/*.cjs`): `node test/<name>.cjs`. Several are source-shape assertions that regex-match firmware C++ (e.g. `emulator_scheduler.cjs`, `sticky_command_allowlist.cjs`), so refactoring that code may require updating the matching test. `release_package.cjs` hardcodes the release version and checks `docs/` manifests/firmware; `tools/test_installer.cjs` exercises the browser installer offline.
- **Console cores** (`tools/test_console_cores.ps1`): builds `lib/InkNes` + Inkade cores with zig from `.pio/host-tools`; Windows/PowerShell oriented.
- `test/test_reader_position` is a Unity test (`pio test` style).
- Pi edition: `cd pi && python -m pytest tests`.

## Architecture

- **`src/main.cpp`**: board-specific GPIO hold release/power latching after deep sleep, then `DisplayMgr` init → `WebMgr::mountFilesystems()` → fonts → battery/input/gamepad → app registration → network startup on a pinned FreeRTOS task (core 0) so the menu is usable before Wi-Fi. `loop()` pumps `InputMgr`, `AppMgr`, `WebMgr`.
- **App framework** (`lib/Book32_Core`): `App` (`BaseApp.h`: `start/update/draw/stop/forceRedraw`) managed by singleton `AppMgr`. Index 0 is the main menu, 1 is the reader. Optional apps (Todo, Klipper, Inkade) are registered only if enabled in `AppSelection` (chosen in the web installer / web page); with none enabled, home becomes the reader's library. Nearly all managers are `getInstance()` singletons.
- **Hardware abstraction**: `include/Config.h` holds per-board pins, screen size, `SYSTEM_VERSION`, `SLEEP_CONFIG_VERSION`, and capability flags (`BOOK32_HAS_TOUCH`, `BOOK32_HAS_SD`, `BOOK32_HAS_BUZZER`, `BOOK32_HAS_INKBOY`). `DisplayMgr.h` typedefs `Book32Display` to `Book32Display`/`StickyDisplay`/`LilygoDisplay`. Prefer capability flags over board defines in shared code.
- **Apps** (`lib/Apps/*`): `AppReader` (EPUB parse/paginate, page books/CBZ, sleep cover), `AppPaperboy` = **Inkade** (GB/GBC via `crankboy`, NES via `lib/InkNes`, Arduboy; emulation runs on its own task and packs frames into video drivers like `InkBoyVideo`/FastEPD queued video), `AppKlipper`, `AppTodo`, `AppWifi` (touch boards only). `lib/Book32_Apps` holds the main menu/footer/`HomeServices`.
- **Web** (`lib/Book32_Web/WebMgr`): ESPAsyncWebServer. Handlers run on the async task and must not draw; they set `volatile` pending flags that `WebMgr::update()` applies on the main loop. Front end is `data/` (`index.html`, `script.js`, `pages.js` converts PDFs/CBZ to page books in the browser using the gzipped pdf.js) and ships as the LittleFS image.
- **Network concurrency**: `include/NetworkWork.h` (`Gate`/`Lease`) arbitrates network use between startup, HTTPS and socket workers; covered by `test/network_work.cpp` and `network_work_integration.cjs`.
- **Updates** (`lib/Book32_Update/GitHubMgr`): OTA from the latest GitHub release of `GITHUB_REPO`, comparing with `VersionUtils`.
- **Storage**: partitions CSVs per board. Book32 has `spiffs` (1MB web UI LittleFS) plus a 10MB `ebooks` partition mounted at runtime; Sticky/LILYGO use MicroSD for books (falling back to internal) and require SD for ROMs (`/roms`). Updates/installer must never format the ebooks partition or SD card.

## Audio build (this fork)

`env:lilygo_t5s3_pro_audio` is the only env on **Arduino-ESP32 3.3.7 / ESP-IDF 5.5** (pioarduino), which the USB host and UAC drivers need. The other envs use espressif32 6.12.0, and the two platforms overwrite each other's global `framework-arduinoespressif32` package. So this env links its own copy: run `tools/fetch_arduino3.sh` once. The env also has its own `build_flags` (not `${env.build_flags}`, whose SPI include path names the 2.0.17 package), uses ESP32Async's AsyncTCP/ESPAsyncWebServer and ArduinoJson 7, sets `lib_archive = yes`, and adds the Network/FS/ArduinoJson include paths. Shared code that differs between Arduino 2 and 3 is guarded with `ESP_ARDUINO_VERSION_MAJOR` / `ESP_IDF_VERSION_MAJOR`.

- `lib/Book32_Audio`: Inx Pro's audio engine (Audiobookshelf, player, USB-C module link, USB headphones), kept close to upstream for syncing; `compat/` maps its SD, settings, Wi-Fi and clock APIs onto InkDeck. Its README lists the provenance and the local changes. The backend is **main-loop only**, except `AbsWorker`'s jobs and its own tasks.
- `lib/Book32_Core/AudioHooks.h` is the only coupling from the core: begin, service, keep-awake, `holdsRadio()` (opening a book does not power Wi-Fi down mid-stream), `needsFullCpu()`, shutdown before sleep, and `registerWeb`. Outside the audio env these are inline no-ops. Keep it that way, so the stock builds stay unchanged and mergeable with upstream.
- `lib/Apps/AppAudiobooks`: the on-device UI. `lib/Book32_Audio/AudioWeb.cpp`: `/api/audio/*`, served from a snapshot the main loop refreshes. The web tab appears only when that endpoint answers.
- OTA for this build uses `bigonionbots/Book32` and the `inkdeck-lilygo-audio-*` assets (`Config.h`).
- The vendored code under `third_party/` and `shared/audio_link/` is unmodified; update it from its source instead of editing it.

## Releases

Release binaries are committed. A release:
1. Bump `SYSTEM_VERSION` in `include/Config.h` (three board branches) and the `version` in `test/release_package.cjs`.
2. Build all three envs (`pio run` + `buildfs`).
3. `tools/package_release.ps1 -Version X.Y.Z` copies `.pio/build/*` outputs into `docs/firmware/` (versioned and unversioned names) and the repo root, merges factory images with esptool, and writes `docs/manifest-*-{update,factory}[-vX.Y.Z].json` for the ESP Web Tools installer served from `docs/` (GitHub Pages).
4. Add `docs/release-X.Y.Z.md` and publish a GitHub release with the app/littlefs bins (devices OTA from it).

## Other tooling

- `tools/converter/`: Node EPUB pre-converter. `tools/generate_open_sans_gfx.py` regenerates reader bitmap fonts from `Open_Sans/`. `convert_icon(s).py` turns `icon_*.png` into C headers. `tools/import_crankboy.py` / `import_nofrendo.py` vendor emulator cores.
- Third-party code and its licenses are listed in `THIRD_PARTY_NOTICES.md`; each release bundles source and notices (`tools/package_source.ps1`, `package_licenses.ps1`).
