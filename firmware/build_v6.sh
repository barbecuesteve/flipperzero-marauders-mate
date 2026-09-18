#!/usr/bin/env bash
# Build an ESP32 Marauder image for the Marauder v6 (MARAUDER_V6), emitting the
# app image as update.bin for the firmware's `update -s` SD flash.
#
# The v6 is the touch-UI ESP32 (ILI9341 240x320, landscape 320x240) -- a DIFFERENT
# chip from the C5. Never cross-flash: this image goes on the v6's SD (Slot C).
# It is the base to fork for a custom on-device UI.
#
# Usage: firmware/build_v6.sh <path-to-ESP32Marauder-src> [output-dir]
# Default output: firmware/build/v6/update.bin
# -> the v6's SD (Slot C), then `stopscan; update -s` over the v6's CH340 USB.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export MM_SRC="${1:?usage: build_v6.sh <esp32marauder-src> [outdir]}"
export MM_OUT="${2:-$HERE/build/v6}"
export MM_FLAG="MARAUDER_V6"
export MM_FQBN="esp32:esp32:d32:PartitionScheme=min_spiffs"  # ESP32; min_spiffs is OTA-capable
export MM_TFT_SETUP="User_Setup_og_marauder.h"               # ILI9341 TFT config
export MM_LABEL="v6"
exec "$HERE/_build_marauder.sh"
