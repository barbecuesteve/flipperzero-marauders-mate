#!/usr/bin/env bash
# Build a patched ESP32 Marauder image for the ESP32-C5-DevKitC-1 (MARAUDER_C5),
# emitting the app image as update.bin for the firmware's `update -s` SD flash.
#
# Usage: firmware/build_c5.sh <path-to-ESP32Marauder-src> [output-dir]
# The source should already have marauder-mate.patch + wardrive-coexist.patch
# applied (see firmware/README.md). Default output: firmware/build/c5/update.bin
# -> the C5's SD (Slot D), then `stopscan; update -s` over the Flipper bridge.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export MM_SRC="${1:?usage: build_c5.sh <esp32marauder-src> [outdir]}"
export MM_OUT="${2:-$HERE/build/c5}"
export MM_FLAG="MARAUDER_C5"
export MM_FQBN="esp32:esp32:esp32c5:FlashSize=8M,PartitionScheme=default_8MB,PSRAM=enabled"
export MM_TFT_SETUP=""   # headless: no on-device screen
export MM_LABEL="C5"
exec "$HERE/_build_marauder.sh"
