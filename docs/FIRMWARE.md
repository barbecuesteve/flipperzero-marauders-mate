# ESP hardware & firmware notes

What Marauder's Mate is actually talking to, which firmware is on it, and how to
(re)flash it. Pairs with `firmware/README.md` (the patches) and the CLAUDE.md
"ESP32 Marauder firmware (ground truth)" section.

> **Update 2026-09-18:** the C5 was flashed from v1.10.2 to a **patched
> v1.17.0** (`firmware/build/update.bin` = v1.17.0 + `marauder-mate.patch` +
> `wardrive-coexist.patch`) via `update -s` from its SD card (**Slot D**), over
> the Flipper bridge — no direct port needed. This resolved every version-drift
> issue at once (scan/beacon format, GPS labels, the missing `foxhunt` command)
> and connected the SD (29 GB). Details in "Reflash paths" below.

## Bench unit (2026-09)

A **dual-ESP** device:

- **Marauder v6** carrier board — 2.8" ILI9341 **touch** TFT, microSD, 18650
  charging, a dual-mode 2.4 GHz ESP32. Runs the on-device WiFi/Bluetooth/GPS/
  Device/Reboot menu. Flashed firmware **v1.14.1**.
- **ESP32-C5** radio daughterboard — **dual-band 2.4/5 GHz**, **headless**.
  Flashed firmware **v1.17.0** (patched; was v1.10.2 before 2026-09-18). Has its
  own SD on **Slot D** (of the device's two slots, C and D). Exposes the
  Bluetooth (`sniffbt`/`blespam`/`btwardrive`/`sniffskim`) and GPS
  (`gps`/`gpsdata`/`nmea`/`gpspoi`/`gpstracker`/`wardrive`/`foxhunt`) commands.

**The Flipper's USB-UART bridge (GPIO TX 13 / RX 14, 115200) is wired to the
C5**, so Marauder's Mate drives the **C5 radio**, not the v6 UI chip. To capture
raw serial with `tools/capture.py`, the Flipper must be in **GPIO → USB-UART
Bridge** mode (not running the app — the app owns the UART).

`info` from the C5 reports:

```
Firmware: Marauder
Version: v1.17.0              <- was v1.10.2 before the 2026-09-18 reflash
Hardware: ESP32-C5 DevKit
ESP-IDF: v5.5.1-710-g8410210c9a
SD Card: Connected           <- Slot D card (was "Not Connected" until a card went in)
SD Card Size: 29850MB
Bluetooth: Supported         <- these four are our marauder-mate.patch capability
GPS: Connected                  lines; a single info probe now gates the whole UI
Direct Upload: Supported
Dual Band: Supported
```

`Hardware: ESP32-C5 DevKit` means the flashed image was built with the
`MARAUDER_C5` config — **not** `MARAUDER_V8` (which is a different ESP32-C5
board). This distinction matters when reflashing (below).

## Firmware versions in play

| Thing | Version | Notes |
|---|---|---|
| C5 daughterboard (what we drive) | **v1.17.0** (patched) | `MARAUDER_C5`; was v1.10.2 until 2026-09-18 |
| v6 UI chip | v1.14.1 | not on our UART path |
| Source checkout (`~/Code/Flipper/ESP32Marauder`) | **v1.16.0** nightly | worktrees at v1.17.0 used for the build |
| Bundled C5 flasher bin (`C5_Py_Flasher_for_v8/bins/`) | v1.12.0, **v8** | wrong config for a devkit unit — see below |

**Ground-truth rule:** the source can diverge from the flashed firmware, so
confirm serial formats **on device**, not from source. The parsers were made
**format-agnostic** rather than version-pinned — they accept the label/format
variants seen across firmware (e.g. `gpsdata` `Accuracy:`/`Acc:`, the
`scanall`/`sniffbeacon` bare-vs-`BSSID:`-labeled AP line). See "Serial format
changes across firmware versions" below. The one genuine feature gap was Fox
Hunt: v1.10.2's `sigmon` had no targeted mode; the `foxhunt` command exists from
newer firmware, which the 2026-09-18 flash to v1.17.0 provides.

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

**Leading path — `update -s` (SD self-flash), no direct port needed. PROVEN
2026-09-18.** The firmware's `update` command:

- `update -w` (OTA over WiFi): **dead code** (commented out).
- `update -s`: reads **`/update.bin` from the SD card root**, streams it into the
  inactive OTA partition via the ESP-IDF OTA API (`esp_ota_*`), sets it as the
  boot partition, and reboots. A **self-flash from SD** — no USB, esptool, or
  BOOT/RESET lines. Just `stopscan` then `update -s` over the existing Flipper
  bridge.

It works because `MARAUDER_C5` compiles in SD (`HAS_SD`/`USE_SD`, `HAS_C5_SD`)
and its partition table is dual-OTA. Confirmed at flash time — the C5 printed:
`Written : 1856992 successfully / OTA done! / Currently running: app0 at 0x10000
/ Next OTA partition: app1 at 0x1F0000 / esp_ota_set_boot_partition result:
ESP_OK`, then rebooted into v1.17.0.

The proven procedure:

1. Build `MARAUDER_C5` v1.17.0 with both patches (`firmware/build_c5.sh`); the
   app image is `firmware/build/update.bin`.
2. Copy it to the root of the **C5's SD card, which is Slot D** (the two device
   slots are labeled C and D; `ls /` over the bridge showed the marker file, so
   Slot D is the C5). Reinsert.
3. Bridge mode → `stopscan`, then `ls /` to confirm `update.bin` is present at
   1856992 bytes, then `update -s`. It flashes and reboots.

Recovery if an OTA image is bad: re-do `update -s` with a known-good `update.bin`
(the other OTA slot still holds the previous app until overwritten), or fall back
to a direct-port flash.

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
- **`wardrive`**: on stock firmware it ran WiFi + BLE concurrently and crashed
  the C5 (`npl_freertos_event_init` assert, NimBLE NPL event-pool exhaustion) ~1-2 s
  into the BLE phase. `wardrive-coexist.patch` (time-slice the radios + a
  `wardrive -w` WiFi-only flag) is **in the flashed v1.17.0 build** — needs a
  fresh on-device confirmation that the crash is gone. The WiFi wardrive line
  parser (`mm_wardrive_parse_line`) is done and tested; a live wardrive scene can
  now be built (point it at `wardrive -w` for the WiFi-only sweep).

## Serial format changes across firmware versions

Comparing the `Serial.print*` output of every parsed command between the flashed
**v1.10.2** and the latest **v1.17.0** (diff of `WiFiScan.cpp` / `CommandLine.cpp`
/ `GpsInterface.cpp`). The bet held: changes are overwhelmingly **additive** —
**one** breaking rename, in the `gpsdata` block.

**Breaking (handled): `gpsdata` block relabel.** v1.17.0 shortened the block's
field labels. The parser now accepts both spellings (`mm_gps_fix_update`), so it
is version-agnostic — no version flag needed:

| field | v1.10.2 | v1.17.0 | parser accepts |
|---|---|---|---|
| fix | `Good Fix:` | `Fix:` | both |
| sats | `Satellites:` | `Sats:` | both |
| accuracy | `Accuracy:` | `Acc:` | both |
| lat/lon/alt | `Latitude:`/`Longitude:`/`Altitude:` | `Lat:`/`Lon:`/`Alt:` | both |
| datetime | `Datetime:` | `D/T:` | both (+ terse `Date/Time:`) |

**Additive in v1.17.0 (safe to ignore, or parse later):**
- `gpsdata` gains tracker-stat lines: ` Dist:`, ` Speed:`, ` Elapsed:`, ` Points:`.
- New BT sniff mode prints `Meta Device: ` (Ray-Ban/Meta glasses).
- New commands/output: SPIFFS backup/restore, geofence list, brightness.

**Unchanged (verified as diff context, not edits):** the scan/sniff line formats
— `list -a` (`[i][CH:x] essid rssi`), scanall/`sniffbeacon` (`<rssi> Ch: <ch>
<bssid> ESSID: ...`), `sniffprobe` (`Client:`/`Requesting:`), `sniffdeauth`
(`<rssi> Ch: <ch> <src> -> <dst>`), pinescan (`DET:`), multissid (`SSIDs:`),
pwnagotchi (`Name:`/`Pwnd #:`), `info`, foxhunt (`<name> RSSI: <n>`), host/port
scans, and both wardrive CSVs (`,WIFI` / `,,[BLE],`). NMEA is emitted by the GPS
module, so it's firmware-version-independent.

**Design note for public release:** prefer **label-synonym tolerance** (accept the
union of spellings, as the GPS parser now does) over a firmware-version switch.
It needs no version detection and degrades gracefully on both older and newer
firmware. Reserve a real version gate only for a format that is genuinely
ambiguous between versions (none so far).
