# ESP32 Marauder firmware patches

Marauder's Mate reads richer data out of the ESP than stock Marauder firmware
emits over serial. These optional, additive patches add it (and fix a wardrive
crash). **The app works on stock firmware** — so flashing them is optional
polish, deferred until you're set up to build the ESP firmware (e.g. at C5 time).

Both patches are **applied in the ESP working tree but not yet compiled or
flashed** (the C5 Arduino toolchain isn't set up). `marauder-mate.patch` is
additive and low-risk; `wardrive-coexist.patch` includes a radio-lifecycle
change (Option 1 below) that **must be verified on the bench** before relying on
it. They are independent and apply in either order on a clean tree.

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

## wardrive-coexist.patch

Fixes a deterministic crash: `wardrive` runs a WiFi channel sweep and, every
fourth channel, a BLE scan — **concurrently with WiFi still up**. On the
ESP32-C5 this exhausts NimBLE's NPL event pool and asserts
(`npl_freertos_event_init npl_os_freertos.c:103`), core-dumping and rebooting
the C5 a second or two into the BLE phase. Plain `sniffbt`/`btwardrive` (BLE
only, WiFi down) never hit it — the trigger is the WiFi+BLE coexistence unique
to `wardrive`. Two independent changes, in `WiFiScan.cpp` / `WiFiScan.h` /
`CommandLine.cpp`:

1. **Correctness — time-slice the radios** (`executeWarDrive`): fully
   `shutdownWiFi()` immediately before `pBLEScan->start()` so the BLE window runs
   alone, and re-init WiFi (`startWardriverWiFi()`) in the `!ble_scanning` branch
   before the next channel scan. Reuses the same shutdown/start pair the
   `WIFI_SCAN_FAILED` path already uses. **NOT yet compiled or bench-verified** —
   this changes radio lifecycle control flow; confirm on device before trusting.

2. **Configurability — `wardrive -w`** (WiFi-only): a new `wardrive_ble_enabled`
   flag (default true) gates the BLE phase; `wardrive -w` clears it for a
   BLE-free sweep. Small and correct-by-inspection; a guaranteed-safe wardrive
   even if the coexistence timing is still marginal on some board.

Note: the currently flashed v1.10.2 ignores unknown args, so `wardrive -w` only
takes effect after reflashing with this patch. The app still sends plain
`wardrive`; switching it to `-w` (or adding a "WiFi-only" option) is a follow-up
once the firmware is reflashed.

## Apply + build

```sh
cd /path/to/ESP32Marauder
git apply /path/to/Flipper/firmware/marauder-mate.patch
git apply /path/to/Flipper/firmware/wardrive-coexist.patch
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
clone at `~/Code/ESP32Marauder` also carries the change in its working tree. That
clone has a fork remote (`fork` = `barbecuesteve/ESP32Marauder`) for opening a PR:
branch from `master`, commit the change, push to `fork`, and PR against upstream.
`origin` there is upstream — don't push to it.
