#!/usr/bin/env bash
# Shared core for the per-board Marauder build scripts (build_c5.sh, build_v6.sh).
# Not run directly -- the wrappers set the board parameters and source this.
#
# Mirrors upstream CI (.github/workflows/build_parallel.yml). Requires:
# arduino-cli, git, ~2 GB disk, network. Inputs (env vars set by the wrapper):
#   MM_SRC        path to an ESP32Marauder checkout (patches already applied)
#   MM_OUT        output dir (created); receives update.bin
#   MM_FLAG       board define, e.g. MARAUDER_C5 / MARAUDER_V6
#   MM_FQBN       arduino-cli FQBN for the board
#   MM_TFT_SETUP  TFT_eSPI User_Setup header to enable (empty for headless boards)
#   MM_LABEL      human label for logs
set -euo pipefail

: "${MM_SRC:?}"; : "${MM_OUT:?}"; : "${MM_FLAG:?}"; : "${MM_FQBN:?}"; : "${MM_LABEL:?}"
MM_TFT_SETUP="${MM_TFT_SETUP:-}"
CORE_VER="3.3.4"
CORE_URL="https://github.com/espressif/arduino-esp32/releases/download/${CORE_VER}/package_esp32_dev_index.json"
LIBS="$(mktemp -d)/libs"; mkdir -p "$LIBS"

echo ">> [$MM_LABEL] installing esp32:esp32@${CORE_VER} (idempotent)"
arduino-cli core update-index --additional-urls "$CORE_URL" >/dev/null
arduino-cli core install "esp32:esp32@${CORE_VER}" --additional-urls "$CORE_URL"

echo ">> [$MM_LABEL] cloning pinned libraries"
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
cp -r "$MM_SRC/libraries/Adafruit_TCA8418" "$LIBS/Adafruit_TCA8418"

echo ">> [$MM_LABEL] configuring TFT_eSPI"
rm -f "$LIBS/TFT_eSPI/User_Setup_Select.h"
cp "$MM_SRC"/User*.h "$LIBS/TFT_eSPI/"
if [ -n "$MM_TFT_SETUP" ]; then
  # Enable this board's User_Setup in the sketch's User_Setup_Select.h (TFT boards).
  sed -i '' "s|^//#include <${MM_TFT_SETUP}>|#include <${MM_TFT_SETUP}>|" \
    "$LIBS/TFT_eSPI/User_Setup_Select.h" 2>/dev/null || \
  sed -i "s|^//#include <${MM_TFT_SETUP}>|#include <${MM_TFT_SETUP}>|" \
    "$LIBS/TFT_eSPI/User_Setup_Select.h"
  echo "   enabled TFT User_Setup: ${MM_TFT_SETUP}"
fi

echo ">> [$MM_LABEL] patching platform.txt / cpp_flags for core ${CORE_VER} (guarded)"
for pt in $(find "$HOME/Library/Arduino15/packages/esp32/hardware/esp32/" -name platform.txt 2>/dev/null; \
            find "$HOME/.arduino15/packages/esp32/hardware/esp32/" -name platform.txt 2>/dev/null); do
  grep -q -- '-Wl,-zmuldefs' "$pt" && continue # already patched -- don't double-apply
  sed -i '' 's/compiler.c.elf.extra_flags=/compiler.c.elf.extra_flags=-Wl,-zmuldefs /' "$pt" 2>/dev/null \
    || sed -i 's/compiler.c.elf.extra_flags=/compiler.c.elf.extra_flags=-Wl,-zmuldefs /' "$pt"
done
for cf in $(find "$HOME/Library/Arduino15/packages/esp32/tools/esp32-arduino-libs/" -name cpp_flags 2>/dev/null; \
            find "$HOME/.arduino15/packages/esp32/tools/esp32-arduino-libs/" -name cpp_flags 2>/dev/null); do
  grep -q 'fexceptions' "$cf" || continue # already patched
  sed -i '' 's/-fexceptions/-fno-exceptions/g' "$cf" 2>/dev/null \
    || sed -i 's/-fexceptions/-fno-exceptions/g' "$cf"
done

echo ">> [$MM_LABEL] compiling ${MM_FLAG}"
mkdir -p "$MM_OUT"
arduino-cli compile \
  --fqbn "$MM_FQBN" \
  --libraries "$LIBS" \
  --build-property "compiler.cpp.extra_flags=-D${MM_FLAG}" \
  --warnings none \
  --output-dir "$MM_OUT" \
  "$MM_SRC/esp32_marauder/esp32_marauder.ino"

# The app image (flashed at the app offset) is what `update -s` streams from SD.
cp "$MM_OUT/esp32_marauder.ino.bin" "$MM_OUT/update.bin"
echo ">> [$MM_LABEL] done: $MM_OUT/update.bin"
ls -la "$MM_OUT/update.bin"
