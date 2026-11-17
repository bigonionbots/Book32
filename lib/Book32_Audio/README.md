# Book32_Audio

The audiobook engine of the LILYGO audio build (`env:lilygo_t5s3_pro_audio`),
ported from [bigonionbots/inx-pro](https://github.com/bigonionbots/inx-pro)
(MIT), branch `feat/usb-headphones` at `905f400`, plus that branch's then
uncommitted USB headphone changes: the 45 ms adapter buffer and the
1024-byte control transfer limit.

| Here | Inx Pro |
| --- | --- |
| `abs/` | `src/abs/`: Audiobookshelf client, covers, downloads, queue, on-demand Wi-Fi |
| `audio/` | `src/audio/`: MP3 / MP4 readers over SD or HTTP, decoder, resampler |
| `system/` | `src/system/`: `AudioPlayer`, `AudioModule`, `AudioLink`, `AudioOutput`, `Bq25896`, `UsbSerialHost`, `UsbAudioOut`, `UsbAudioSink`, `ListeningLog` |

These files are kept as close to the originals as possible so later fixes can be
carried over with a diff. The only changes are:

- `audio/PcmDecoder.cpp`: `third_party` include paths one level deeper;
- `system/UsbAudioOut.h`: `#include <cstddef>`;
- `abs/AbsWifi.{h,cpp}`: `inUse()`, which InkDeck's radio-off paths check;
- for the M5Stack PaperDIY (`m5_paperdiy_audio`), which reaches the module over a UART and
  switches it with an M5PM1 GPIO:
  - `system/AudioLink.cpp`, `system/AudioModule.{h,cpp}`: `ModuleSerial` (`system/ModuleSerial.h`)
    in place of `UsbSerialHost`, which it aliases on the LILYGO;
  - `system/AudioModule.cpp`: the `INX_AUDIO_PM1_POWER` power path, and a Bluetooth hang-up
    before the module's power goes off (both boards);
  - `system/{UsbSerialHost,UsbAudioOut,UsbAudioSink,Bq25896}.cpp`: built only with
    `INX_AUDIO_USB_HOST`; `system/UsbStubs.cpp` stands in otherwise;
  - `abs/AbsQueue.cpp`: "on the charger" from the M5PM1;
  - new: `system/ModuleSerial.h`, `system/UartModuleSerial.cpp`, `system/UsbStubs.cpp`.

The original `INX_*` switches are unchanged (`INX_HAS_AUDIO_MODULE`, `INX_AUDIO_MODULE_BAUD`).
New ones (defaults in `compat/InkDeckAudioCompat.h`): `INX_AUDIO_USB_HOST`,
`INX_AUDIO_LINK_UART` (+ `INX_AUDIO_UART_TX`, `_RX`, `_NUM`), `INX_AUDIO_PM1_POWER`.

## InkDeck's side

- `compat/` provides what the sources expect from Inx Pro, mapped onto InkDeck:
  - `SDCardManager.h`: SdFat-style `FsFile` / `SdMan` over `EbookFS` (the MicroSD card).
  - `util/SdIoMutex.h`: the SD mutex.
  - `state/SystemSetting.h`: two settings in NVS (`audio` namespace).
  - `state/NetworkCredential.h`: the network saved in the Wi-Fi driver.
  - `HalGPIO.h`: the date, from the NTP clock plus the time zone setting.
  - `BoardConfig.h`: the charger and gauge I2C addresses.
  - `InkDeckAudioCompat.h`: force-included into this library's sources only (`audio_config.py`).
- `InkDeckAudio.cpp` implements `Book32_Core/AudioHooks.h`, the calls InkDeck's
  core makes: boot, every loop, sleep, keep-awake, and radio and CPU holds.
- `AudioWeb.cpp` is the web page's `/api/audio/*`.
- The screens are `lib/Apps/AppAudiobooks`, written for InkDeck. Inx Pro's
  activities are not ported.

## Vendored elsewhere in the repository

All unmodified, as in Inx Pro:

- `third_party/usb_host/`: the ESP-IDF USB host library and the CDC/FTDI/CP210x/CH34x and UAC class drivers (Apache-2.0). See its README.
- `third_party/minimp3/`: CC0.
- `third_party/opencore_aacdec/`: Apache-2.0.
- `shared/audio_link/`: the module protocol, verbatim copies from atom-lite-audio-module. See its README.
