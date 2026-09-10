# ESP32 Marauder firmware patch

Marauder's Mate reads richer data out of the ESP than stock Marauder firmware
emits over serial. This optional, additive patch adds it. **The app works on
stock firmware** — these just light up extra detail — so flashing it is optional
polish, deferred until you're set up to build the ESP firmware (e.g. at C5 time).

## marauder-mate.patch

All changes are in `esp32_marauder/WiFiScan.cpp`, additive (nothing removed):

1. **Capability lines in `info`** (`RunInfo`): adds
   `Bluetooth: Supported|Not Supported`, `GPS: Connected|Not Connected|Not
   Supported`, `Direct Upload: Supported|Not Supported`, and
   `Dual Band: Supported|Not Supported`. Lets a single `info` probe gate the
   whole UI (BT/GPS/upload/dual). Without it the app still detects the board and
   SD card, uses a `sniffbt` fallback for Bluetooth, and leaves the rest
   ungreyed.

2. **Pwnagotchi identity** (`processPwnagotchiBeacon`): emits `MAC:`, `Ver:`,
   `Uptime:`, and `Deauth:` (before the existing `Pwnd #:` line). Stock firmware
   prints only name + pwnd count; this gives the Pwnagotchi monitor a real MAC.
   The app's parser accepts both formats.

## Apply + build

```sh
cd /path/to/ESP32Marauder
git apply /path/to/Flipper/firmware/marauder-mate.patch
```

Then build for your board and flash. Flipper Zero WiFi Dev Board: ESP32-S2,
build flag `MARAUDER_FLIPPER` (uncomment in `esp32_marauder/configs.h`),
`esptoolChip: esp32s2`. The build needs the Arduino-ESP32 core plus Marauder's
library set per the upstream "Compiling and Flashing" wiki (this repo bundles
only a couple). Flash offsets: bootloader `0x1000`, partitions `0x8000`,
boot_app0 `0xE000`, app `0x10000` (or let `arduino-cli upload` handle them).

Still open as a separate item: making the `led` command drive the Flipper dev
board's 3-GPIO status LED (`HAS_FLIPPER_LED`) so the LED colour picker works on
that board — see bead `mm-82m`.

The patch targets the upstream tree (`justcallmekoko/ESP32Marauder`); the local
clone at `~/Code/ESP32Marauder` also carries the change in its working tree.
