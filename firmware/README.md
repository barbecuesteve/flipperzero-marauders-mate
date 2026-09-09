# ESP32 Marauder firmware patch

Marauder's Mate reads board capabilities from the ESP's `info` reply
(see `mm-xdy` and `docs/MENU.md` → "Capability detection"). Stock Marauder
firmware doesn't report Bluetooth / GPS / Direct Upload / Dual Band in `info`,
so this patch adds those lines to `RunInfo`.

## marauder-capinfo.patch

Adds to `WiFiScan.cpp` `RunInfo()`:

```
Bluetooth: Supported | Not Supported     (from HAS_BT)
GPS: Connected | Not Connected | Not Supported  (HAS_GPS + getGpsModuleStatus)
Direct Upload: Supported | Not Supported (from HAS_DIRECT_UPLOAD)
Dual Band: Supported | Not Supported     (from HAS_DUAL_BAND)
```

With it, a single `info` probe covers presence + SD + BT + GPS + upload +
dual-band. **Without it the app still works**: presence and SD come from stock
`info`, Bluetooth from the `sniffbt -serial` fallback; GPS / Direct Upload /
Dual Band stay Unknown (ungreyed). So flashing this is optional polish.

## Apply + build (deferred — do at C5 toolchain-setup time)

```sh
cd /path/to/ESP32Marauder
git apply /path/to/Flipper/firmware/marauder-capinfo.patch
```

Then build for the target board and flash. For the Flipper Zero WiFi Dev Board:
ESP32-S2, build flag `MARAUDER_FLIPPER` (uncomment in `esp32_marauder/configs.h`),
`esptoolChip: esp32s2`. Build needs the Arduino-ESP32 core + Marauder's library
set per the upstream "Compiling and Flashing" wiki (the repo bundles only a
couple of libraries). Flash offsets: bootloader `0x1000`, partitions `0x8000`,
boot_app0 `0xE000`, app `0x10000` (or just let `arduino-cli upload` handle them).

The patch targets the upstream tree (`justcallmekoko/ESP32Marauder`); the local
clone at `~/Code/ESP32Marauder` also carries the change in its working tree.
