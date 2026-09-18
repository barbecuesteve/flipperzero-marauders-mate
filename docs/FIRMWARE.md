# ESP hardware & firmware notes

What Marauder's Mate is actually talking to, which firmware is on it, and how to
(re)flash it. Pairs with `firmware/README.md` (the patches) and the CLAUDE.md
"ESP32 Marauder firmware (ground truth)" section.

## Bench unit (2026-09)

A **dual-ESP** device:

- **Marauder v6** carrier board — 2.8" ILI9341 **touch** TFT, microSD, 18650
  charging, a dual-mode 2.4 GHz ESP32. Runs the on-device WiFi/Bluetooth/GPS/
  Device/Reboot menu. Flashed firmware **v1.14.1**.
- **ESP32-C5** radio daughterboard — **dual-band 2.4/5 GHz**, **headless** (the
  `MARAUDER_C5` config has `HAS_SCREEN` commented out). Flashed firmware
  **v1.10.2**. Exposes the Bluetooth (`sniffbt`/`blespam`/`btwardrive`/
  `sniffskim`) and GPS (`gps`/`gpsdata`/`nmea`/`gpspoi`/`gpstracker`/`wardrive`)
  commands.

**The Flipper's USB-UART bridge (GPIO TX 13 / RX 14, 115200) is wired to the
C5**, so Marauder's Mate drives the **C5 radio**, not the v6 UI chip. To capture
raw serial with `tools/capture.py`, the Flipper must be in **GPIO → USB-UART
Bridge** mode (not running the app — the app owns the UART).

`info` from the C5 reports:

```
Firmware: Marauder
Version: v1.10.2
Hardware: ESP32-C5 DevKit
ESP-IDF: v5.5.1-...
SD Card: Not Connected        <- the C5 has no SD wired; SD lives on the v6 board
RAM Free: ~8.5 MB             <- includes PSRAM; internal SRAM is far smaller
```

`Hardware: ESP32-C5 DevKit` means the flashed image was built with the
`MARAUDER_C5` config — **not** `MARAUDER_V8` (which is a different ESP32-C5
board). This distinction matters when reflashing (below).

## Firmware versions in play

| Thing | Version | Notes |
|---|---|---|
| C5 daughterboard (what we drive) | **v1.10.2** | `MARAUDER_C5` config |
| v6 UI chip | v1.14.1 | not on our UART path |
| Source checkout (`~/Code/Flipper/ESP32Marauder`) | **v1.16.0** nightly | ahead of both; close-but-not-exact reference |
| Bundled C5 flasher bin (`C5_Py_Flasher_for_v8/bins/`) | v1.12.0, **v8** | wrong config for a devkit unit — see below |

**Ground-truth rule:** the source has diverged from the flashed C5, so confirm
serial formats **on device**, not from source. All of this app's GPS/BT parsers
were built against live v1.10.2 captures, and are effectively **pinned to
v1.10.2**. Upgrading the C5 firmware can change serial formats and would require
re-verifying the parsers.

## The web installer (justcallmekoko.github.io/MarauderInstaller)

A browser (Web Serial, Chrome/Edge) flasher that writes **prebuilt stock release
binaries** per board. Its hardware list includes several ESP32-C5 targets:

- **`esp32-c5-devkitc-1 · ESP32-C5`** — **our target** (matches the current
  `MARAUDER_C5` / "ESP32-C5 DevKit" build).
- `marauder-v8 · ESP32-C5` — a *different* C5 board (`MARAUDER_V8` config,
  different pin/peripheral map). The repo's bundled `_v8` bin is this one.
- `dual-mini-c5`, `lilygo-t-dongle-c5`, `marauder-mini-v3`, `marauder-pancake` —
  other C5 boards.

### Two cautions

1. **Match the config to the hardware.** Per the v6 wiki, a mismatched binary
   can leave SD (and, for us, GPS/pin mapping) broken. GPS working right now is
   evidence `MARAUDER_C5` is correct for this unit — don't flash a `v8` image
   onto it. The bundled `C5_Py_Flasher_for_v8/bins/esp32_marauder_v1_12_0_v8.bin`
   is a v8 build; it is **not** the right image for the devkit-configured C5.
2. **The installer won't carry our patches.** It flashes stock release bins, so
   it includes neither `marauder-mate.patch` (capability lines) nor
   `wardrive-coexist.patch` (the crash fix). It would also bump the C5 off
   v1.10.2, changing serial formats (parser re-verification) and almost
   certainly keeping the wardrive crash (still present in v1.16.0 source).

The installer's real value here: confirming the C5 is a first-class build target,
and quick **stock recovery** if a custom build goes wrong.

## Reflash paths

### A. Web installer — stock, easy, no patches
Chrome/Edge → the installer → pick **ESP32-C5-DevKitC-1** → flash. Use this to
get back to a known-good stock image, or to try a newer stock release (accept the
format/parity caveats above).

### B. Source build — required for our patches
The repo's ESP firmware builds with the **Arduino-ESP32** core (arduino-cli /
Arduino IDE), **not** PlatformIO — `platformio.ini` only defines a `native` env
for host unit tests. Steps (deferred "C5 time" — toolchain not yet set up):

1. In `esp32_marauder/configs.h`, select the `MARAUDER_C5` build.
2. `git apply` both `firmware/marauder-mate.patch` and
   `firmware/wardrive-coexist.patch` (they apply cleanly, in either order).
3. Build with the Arduino-ESP32 core + Marauder's library set (upstream
   "Compiling and Flashing" wiki).
4. Flash the C5 with `C5_Py_Flasher_for_v8/c5_flasher.py` — esptool
   `--chip esp32c5`, offsets **0x2000** (bootloader) / **0x8000** (partitions) /
   **0x10000** (app), 921600 baud. (Point it at *your* freshly built bins, not
   the bundled v8 bin.)

### How do we physically flash the C5?

There is **no confirmed direct USB/UART port to the C5** yet — `c5_flasher.py`
and the web installer both assume one (esptool auto-reset via DTR/RTS → EN/IO0),
and we haven't found it. Known bench ports:

- `/dev/cu.usbmodemflip_*` — the **Flipper**; its GPIO USB-UART bridge reaches
  the C5's **UART only** (TX/RX, no DTR/RTS). Good for CLI/capture, not esptool
  auto-reset.
- `/dev/cu.usbserial-*` (CH340) — the **v6 board's display ESP32** (fw ~v1.14),
  **not** the C5.

**Leading path — `update -s` (SD self-flash), no direct port needed.** The
firmware's `update` command:

- `update -w` (OTA over WiFi): **dead code** (commented out in v1.10.2).
- `update -s`: reads **`/update.bin` from the SD card root**, streams it into the
  inactive OTA partition via the ESP-IDF OTA API (`esp_ota_*`), sets it as the
  boot partition, and reboots. A **self-flash from SD** — no USB, esptool, or
  BOOT/RESET lines. Just `stopscan` then `update -s` over the existing Flipper
  bridge.

This works for the C5 because `MARAUDER_C5` compiles in SD (`HAS_SD`/`USE_SD`,
and a dedicated `HAS_C5_SD`/`HAS_SEPARATE_SD` path), and the C5-family partition
table is dual-OTA (`otadata`/`app0`/`app1`, confirmed in the bundled
`partitions.bin`). The device has **two SD slots** (one per ESP); the C5's slot
just needs a card. To flash a patched build:

1. Build `MARAUDER_C5` v1.17.0 with both patches (section B above); name the app
   image `update.bin`.
2. Copy `update.bin` to the root of the **C5's** SD card; insert it.
3. Over the Flipper bridge: `stopscan`, then `update -s`. It flashes and reboots.

Verify before relying on it: (a) which SD slot actually feeds the C5, (b) that the
flashed C5's partition table has OTA slots (very likely — the family uses
app0/app1/otadata), and (c) keep a recovery path in case the OTA image is bad.

**Fallback paths** if SD self-flash is unavailable:

- **Direct C5 USB / UART0 + EN/IO0** (castellated pads or header) → normal
  `c5_flasher.py`.
- **Flipper bridge + manual bootloader entry** — hold BOOT (IO0) low, tap RESET,
  esptool `--before no_reset`. Untested; needs BOOT/RESET access.
- **Whatever originally flashed v1.10.2** — document it here once known.

## What this means for the app

- **GPS** (`GPS Data`, `GPS Sats` scenes): fully working on v1.10.2. `gpsdata`
  and `nmea` stream cleanly; parsers verified on device.
- **Bluetooth** parsers: `sniffbt` (all/flipper/airtag), `btwardrive`,
  `sniffskim` verified on device. Note the headless C5 prints `sniffbt` "all"
  records **run together on one line** (no `HAS_SCREEN` → no per-record newline)
  — the parser is buffer-level, not line-level.
- **SD-dependent GPS features are hobbled** on this unit (no SD on the C5):
  `gpspoi`, `gpstracker`, `upload` emit little/no serial and can't save.
- **`wardrive` crashes the C5** deterministically: it runs WiFi + BLE
  concurrently, exhausting NimBLE's NPL event pool (`npl_freertos_event_init`
  assert) ~1-2 s into the BLE phase. Diagnosis and the fix (time-slice the
  radios + a `wardrive -w` WiFi-only flag) are in `firmware/wardrive-coexist.patch`
  / `firmware/README.md`. The WiFi wardrive line parser
  (`mm_wardrive_parse_line`) is done and tested; the **live wardrive scene is on
  hold** until the C5 is reflashed with the fix (then point it at `wardrive -w`).
