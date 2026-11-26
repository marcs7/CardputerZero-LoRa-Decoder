# LoRa Decoder for CardputerZero

By **MarcS7 - IS0LTD**

A receive-only LoRa scanner and packet decoder for the M5 CardputerZero (Raspberry Pi CM0),
written in C++17 with LVGL 9.5 and based on the CardputerZero app template. It drives an
**SX1262** radio over Linux `spidev` and the GPIO character device. There is no vendor SDK and
no libgpiod dependency.

| Page (key) | What it does |
| --- | --- |
| **Scan** (`4`) | RSSI sweep of the band with live trace, peak hold, cursor readout and zoom (full, 2 MHz, 500 kHz) |
| **Hunt** (`5`) | Hops LoRaWAN channels and Meshtastic preset slots. On each hop it runs Channel Activity Detection (CAD) for every SF, then receives and decodes whatever it catches |
| **RX** (`6`) | Continuous receive on one channel, with frequency, SF, BW, CR, sync word and IQ all adjustable |
| **Log** (`7`) | Packet log with a decoded detail view and a hex dump |
| **Cfg** (`8`) | Band plan, scan profile, hunt mode, spectrum step, theme and radio status |

### Decoding

- **Meshtastic**: reads the 16-byte header (from, to, id, hops, channel hash, relay). It
  decrypts the payload with AES-CTR using the default `AQ==` key on every modem-preset channel
  name, plus any channels you add in the config. Decoded port types: text, position, node info,
  telemetry (device and environment), traceroute and routing. Other ports are shown as hex.
- **LoRaWAN**: shows the MHDR type; for join requests, the JoinEUI, DevEUI and DevNonce; for
  data frames, the DevAddr with its NetID / NwkID (TTN is recognised), FCtrl flags, FCnt, FOpts,
  FPort and MIC. The FRMPayload stays encrypted because session keys are not available.
- **Raw**: anything else is shown as hex plus printable ASCII. A packet is marked `[CRC]` when
  its CRC failed.

### Band plans and scan profiles

Band plans: EU868, US915, AU915, AS923, KR920, IN865 and EU433. Each one includes the LoRaWAN
uplink and downlink channels and the Meshtastic region; preset frequencies use the same
djb2-slot rule as the Meshtastic firmware (EU LongFast 869.525 MHz, US LongFast 906.875 MHz).

Scan profiles:

| Profile | What it scans |
| --- | --- |
| All | Meshtastic presets and LoRaWAN uplinks |
| LoRaWAN up | LoRaWAN uplinks |
| LoRaWAN down | LoRaWAN downlinks (inverted IQ) |
| Meshtastic | Meshtastic presets (sync word 0x2B) |
| Private 0x12 | LoRaWAN channels with the private sync word |

## Hardware: M5Stack Cap LoRa-1262

The default configuration targets the [Cap LoRa-1262](https://docs.m5stack.com/en/cap/Cap_LoRa-1262)
plugged into the CardputerZero EXT header. The Cap-Bus and the EXT header share the same pin
order, so the mapping is:

| Cap LoRa-1262 | Cap pin | CardputerZero | Linux |
| --- | --- | --- | --- |
| LoRa_NSS | 14 | GPIO7 (SPI0_CS1) | `/dev/spidev0.1` |
| LoRa_SCK / MOSI / MISO | 11 / 12 / 13 | GPIO11 / GPIO10 / GPIO9 | SPI0 |
| LoRa_BUSY | 10 | GPIO22 | `gpiochip0` line 22 |
| LoRa_IRQ (DIO1) | 9 | GPIO23 (HAT_P1) | not used, IRQs are polled over SPI |
| LoRa_RST | 8 | GPIO26 (HAT_P0) | `gpiochip0` line 26 |
| SDA / SCL | 4 / 3 | GPIO2 / GPIO3 (I2C1) | `/dev/i2c-1` |
| GPS TX / RX | 1 / 2 | GPIO14 / GPIO15 (UART) | not used by this app |

Three Cap-specific details are handled automatically:

- **Cap power**: the 5 V rail on the EXT header is off by default
  (`/sys/class/leds/ext_5v_out`). The app switches it on at startup and restores it on exit.
- **Antenna switch**: it is enabled by driving P0 of the PI4IOE5V6408 expander (I2C `0x43`)
  high. The app does this at startup; without it the radio receives nothing.
- **TCXO 3.0 V on DIO3 + LDO regulator**: these match M5Stack's example
  `radio.begin(..., 3.0, true)`.

The cap covers 868 to 923 MHz, so the EU433 plan is listed but does not work with this hardware.

Requirements on the device:

- `/dev/spidev0.1` must exist (SPI0 with CE1 enabled; the display uses CE0).
- HAT_P0 and HAT_P1 must stay in GPIO mode, which is the default (not USB).
- The user running the app needs access to `/dev/spidev0.1`, `/dev/gpiochip0` and `/dev/i2c-1`.
- **Only one program can own the radio.** Stop `meshtasticd`, or any other LoRa app, before
  launching LoRa Decoder. If you don't, the app reports "in use".

Other SX1262 modules work too: edit the `[radio]` section (`pin_*`, `tcxo_voltage`, `use_dcdc`,
`pin_rxen`/`pin_txen`, `antenna_switch_pin=-1`). At startup the driver checks the chip by reading
the sync word register (expects `0x1424`) and shows any failure on the Hunt and Cfg pages. The
radio is never put into TX.

## Keys

| Context | Keys |
| --- | --- |
| Everywhere | `4`–`8` jump to a page · `ESC` then `←/→` move along the tab bar (`Enter`/`↓`/`ESC` back to the page) · `Tab` next page · hold `ESC` to exit · `Fn+H` help · `Fn+J` screenshot |
| Scan | `←/→` move the cursor · `↑/↓` zoom · `Enter` listen at the cursor · `P` reset peaks |
| Hunt | `←/→` change profile · `↑/↓` change band · `D` CAD or dwell mode · `Enter` open the log |
| RX | `←/→` tune · `↑/↓` change SF · `B` bandwidth · `R` coding rate · `S` tuning step · `W` sync word · `I` invert IQ · `M` next Meshtastic preset |
| Log | `↑/↓` select or scroll · `Enter` open or close the detail view · `L` listen on that packet's channel · `E` clear |
| Cfg | `↑/↓` select a row · `←/→` change its value · `T` theme |

Arrows: inside the app the keys `F` (↑), `X` (↓), `Z` (←) and `C` (→) work as arrows
without Fn, matching the arrows printed on the CardputerZero keyboard. `Fn+F/X/Z/C` works too.

## Configuration (`config/lora-decoder.conf`)

The app rewrites `[application]` and `[lora]` whenever you change a setting. `[radio]` (wiring,
TCXO, backend) and `[meshtastic]` (extra channels, written as `Name=base64PSK`) are only read.
Set `backend=sim` to use the built-in traffic simulator. The desktop build uses the simulator by
default, and every page title shows `[SIM]` while it is active.

## Build

The template toolchain needs CMake ≥ 3.31, Ninja, SDL2, FreeType, libpng, libjpeg and fmt for
the desktop simulator, plus `aarch64-linux-gnu-g++` for the device. `docker/Containerfile`
contains all of them:

```sh
podman build -t lora-decoder-dev -f docker/Containerfile docker
alias czb='podman run --rm --userns=keep-id -v "$PWD":/src -w /src lora-decoder-dev'

# Desktop simulator + host tests
czb cmake --preset linux-x86-64
czb cmake --build --preset linux-x86-64-dbg
czb ctest --test-dir build/linux-x86-64 -C Debug --output-on-failure
./build/linux-x86-64/Debug/lora_decoder            # needs a host with SDL2

# Device: cross-compile and package (downloads the CM0 BSP sysroot)
czb cmake --workflow --preset cp0-cross-package    # -> dist/LoRaDecoder_<ver>_rev1_arm64.deb
```

Install the package on the device with `sudo dpkg -i LoRaDecoder_*.deb`, or use `./deploy.sh`.

### Tests

- `tests/decoder_tests.cpp` checks AES against the FIPS-197 and NIST SP800-38A vectors, the
  Meshtastic channel hash and preset frequencies, encrypt/decrypt round trips, LoRaWAN data and
  join frames, raw text, band plans and time-on-air.
- `tests/service_sim_test.cpp` runs the radio worker against the simulator. Hunt must catch
  packets, and Listen must decode Meshtastic, LoRaWAN and private traffic. The spectrum sweep
  must complete.

## Layout

```text
src/decoder/          pure C++: AES-CTR, protobuf reader, Meshtastic, LoRaWAN, band plans
src/platform/radio/   spidev, GPIO uAPI v2, SX1262 driver, simulator, RadioService worker thread
src/model             pages + LoRa settings
src/viewmodel         radio <-> UI bridge (packet log, spectrum peak hold, settings actions)
src/view/screens      Spectrum / Hunt / Listen / Packets / Settings
```

## Limitations

- Receive only. One radio can listen to only one channel and SF at a time, so Hunt is
  statistical: short, fast-SF packets can fall between hops. Use RX on a known channel when you
  need every packet.
- LoRaWAN payloads and join-accepts stay encrypted.
- Meshtastic packets on channels with unknown keys are shown as encrypted, with their header.
- Hardware support is limited to the SX1262. The Cap LoRa-1262 wiring comes from M5Stack's
  documentation; this build has not yet run on a physical CardputerZero.

## Credits and license

Copyright © 2026 **MarcS7 - IS0LTD**.

LoRa Decoder is free software, released under the **GNU General Public License v3.0 or later**
(see [`LICENSE`](LICENSE)). You may use, study, modify and redistribute it. If you redistribute
it, or a work derived from it, you must keep this credit and the copyright notices and publish
the full source code under the same license.

Third-party components:

- [CardputerZero Template](https://github.com/CardputerZero/Template) © M5Stack Technology,
  MIT. Files derived from the template keep their MIT header; see
  [`LICENSES/MIT-M5Stack-Template.txt`](LICENSES/MIT-M5Stack-Template.txt).
- [LVGL](https://lvgl.io) and [{fmt}](https://fmt.dev), both MIT, fetched at build time.
- Fonts in `assets/fonts`: see [`assets/fonts/LICENSE.txt`](assets/fonts/LICENSE.txt).
- Protocol references: Meshtastic (packet format, default channel key), the LoRa Alliance
  LoRaWAN specification, and the Semtech SX1261/2 datasheet. No code was copied from them.
