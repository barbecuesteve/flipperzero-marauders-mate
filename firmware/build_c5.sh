#!/usr/bin/env bash
# Build a patched ESP32 Marauder firmware for the ESP32-C5-DevKitC-1 (MARAUDER_C5)
# and emit the app image as update.bin -- the file the firmware's `update -s`
# reads from an SD card to self-flash over the existing Flipper UART bridge.
#
# Recipe mirrors upstream CI (.github/workflows/build_parallel.yml, the
# ESP32-C5-DevKitC-1 matrix row) so it stays in sync with how JCMK ships C5 bins.
# Requires: arduino-cli, git, ~2 GB disk, network.
#
# Usage:
#   firmware/build_c5.sh <path-to-ESP32Marauder-source> [output-dir]
# The source dir should already have marauder-mate.patch + wardrive-coexist.patch
# applied (see firmware/README.md). Output: <output-dir>/update.bin
set -euo pipefail

SRC="${1:?usage: build_c5.sh <esp32marauder-src> [outdir]}"
OUT="${2:-$PWD/c5-build-out}"
CORE_VER="3.3.4"
CORE_URL="https://github.com/espressif/arduino-esp32/releases/download/${CORE_VER}/package_esp32_dev_index.json"
FQBN="esp32:esp32:esp32c5:FlashSize=8M,PartitionScheme=default_8MB,PSRAM=enabled"
LIBS="$(mktemp -d)/c5libs"; mkdir -p "$LIBS"

echo ">> installing esp32:esp32@${CORE_VER}"
arduino-cli core update-index --additional-urls "$CORE_URL" >/dev/null
arduino-cli core install "esp32:esp32@${CORE_VER}" --additional-urls "$CORE_URL"

echo ">> cloning pinned libraries"
clone() { git clone --depth 1 --branch "$2" "https://github.com/$1" "$LIBS/$3" >/dev/null 2>&1; }
clone marian-craciunescu/ESP32Ping           1.6      ESP32Ping
clone ESP32Async/AsyncTCP                     v3.4.8   AsyncTCP
clone stevemarple/MicroNMEA                   v2.0.6   MicroNMEA
clone ESP32Async/ESPAsyncWebServer           v3.8.1   ESPAsyncWebServer
clone Bodmer/TFT_eSPI                         V2.5.34  TFT_eSPI
clone PaulStoffregen/XPT2046_Touchscreen      v1.4     XPT2046_Touchscreen
clone lvgl/lv_arduino                         3.0.0    lv_arduino
clone Bodmer/JPEGDecoder                      1.8.0    JPEGDecoder
clone h2zero/NimBLE-Arduino                   2.3.8    NimBLE-Arduino
clone adafruit/Adafruit_NeoPixel             1.12.0   Adafruit_NeoPixel
clone bblanchon/ArduinoJson                   v6.18.2  ArduinoJson
clone ivanseidel/LinkedList                   v1.3.3   LinkedList
clone plerup/espsoftwareserial               8.1.0    EspSoftwareSerial
clone adafruit/Adafruit_BusIO                 1.15.0   Adafruit_BusIO
clone adafruit/Adafruit_MAX1704X             1.0.2    Adafruit_MAX1704X
cp -r "$SRC/libraries/Adafruit_TCA8418" "$LIBS/Adafruit_TCA8418"

echo ">> configuring TFT_eSPI (drop selector, copy sketch User*.h)"
rm -f "$LIBS/TFT_eSPI/User_Setup_Select.h"
cp "$SRC"/User*.h "$LIBS/TFT_eSPI/"

echo ">> patching platform.txt / cpp_flags for core ${CORE_VER} (zmuldefs, no-exceptions)"
for pt in $(find "$HOME/Library/Arduino15/packages/esp32/hardware/esp32/" -name platform.txt 2>/dev/null; \
            find "$HOME/.arduino15/packages/esp32/hardware/esp32/" -name platform.txt 2>/dev/null); do
  sed -i '' 's/compiler.c.elf.extra_flags=/compiler.c.elf.extra_flags=-Wl,-zmuldefs /' "$pt" 2>/dev/null \
    || sed -i 's/compiler.c.elf.extra_flags=/compiler.c.elf.extra_flags=-Wl,-zmuldefs /' "$pt"
done
for cf in $(find "$HOME/Library/Arduino15/packages/esp32/tools/esp32-arduino-libs/" -name cpp_flags 2>/dev/null; \
            find "$HOME/.arduino15/packages/esp32/tools/esp32-arduino-libs/" -name cpp_flags 2>/dev/null); do
  sed -i '' 's/-fexceptions/-fno-exceptions/g' "$cf" 2>/dev/null \
    || sed -i 's/-fexceptions/-fno-exceptions/g' "$cf"
done

echo ">> compiling MARAUDER_C5"
mkdir -p "$OUT"
arduino-cli compile \
  --fqbn "$FQBN" \
  --libraries "$LIBS" \
  --build-property "compiler.cpp.extra_flags=-DMARAUDER_C5" \
  --warnings none \
  --output-dir "$OUT" \
  "$SRC/esp32_marauder/esp32_marauder.ino"

# The app image (flashed at 0x10000) is what `update -s` needs.
cp "$OUT/esp32_marauder.ino.bin" "$OUT/update.bin"
echo ">> done: $OUT/update.bin"
ls -la "$OUT/update.bin"
