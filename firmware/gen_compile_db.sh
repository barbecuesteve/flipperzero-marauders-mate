#!/usr/bin/env bash
# Generate a clangd compilation database (compile_commands.json) for the v6
# Marauder source so VSCode/clangd resolves the ESP32 core + library headers
# (FS.h, TFT_eSPI, SPI, ...) and stops flagging phantom "file not found" errors.
#
# Reuses the real v6 build core (build_v6.sh -> _build_marauder.sh) in DB-only
# mode, so the include paths and -D flags exactly match a production build.
# Libraries are cloned ONCE into a persistent, gitignored scratch dir (.ide/)
# inside the source tree -- NOT a throwaway /tmp dir -- so the paths baked into
# compile_commands.json stay valid and later builds reuse the cache.
#
# Usage: firmware/gen_compile_db.sh [path-to-ESP32Marauder-src]
#   default src: /Users/barbecuesteve/Code/Flipper/marauder-v7
# Re-run after adding a new source file or changing include structure.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$(cd "${1:-/Users/barbecuesteve/Code/Flipper/marauder-v7}" && pwd)"

SCRATCH="$SRC/.ide"                 # gitignored: lib cache + build path live here
export MM_LIBS="$SCRATCH/libs"      # persistent lib clones (build core skips existing)
export MM_BUILD_PATH="$SCRATCH/build"
export MM_ONLY_DB=1
mkdir -p "$MM_LIBS" "$MM_BUILD_PATH" "$SCRATCH/out"

# Drive the v6 build core in DB-only mode (sets FQBN / TFT setup / board flag).
bash "$HERE/build_v6.sh" "$SRC" "$SCRATCH/out"

DB="$MM_BUILD_PATH/compile_commands.json"
[ -f "$DB" ] || { echo "ERROR: $DB was not generated" >&2; exit 1; }

# clangd auto-discovers compile_commands.json at the source root.
cp "$DB" "$SRC/compile_commands.json"
echo ">> wrote $SRC/compile_commands.json ($(grep -c '"file"' "$SRC/compile_commands.json") entries)"
echo ">> reload the VSCode window (or restart clangd) to pick it up"
