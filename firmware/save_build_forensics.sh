#!/usr/bin/env bash
# Archive memory-map forensics for ONE firmware build so symbol/layout shifts
# can be cross-referenced against the empirical SD pass/fail result (the
# layout-sensitive "Heisenbug": a tiny code change shifts .text and flips SD
# 100% on/off, while .bss/.data/IRAM stay byte-identical).
#
# Saves the DERIVED symbol map (nm -S, ~150KB gz) + section sizes + the source
# diff that reproduces the build -- NOT the 33MB .elf. That is the lowest-storage
# artifact set that still lets a script spot which symbols moved between builds.
#
# Usage: save_build_forensics.sh <elf> <archive-dir> [label] [result]
#   result: WORKS | FAILS | UNKNOWN   (physical SD test outcome; default UNKNOWN)
# Non-fatal: never aborts a build (exits 0 even if the toolchain is missing).
set -uo pipefail
ELF="${1:-}"; ARCHIVE="${2:-}"; LABEL="${3:-build}"; RESULT="${4:-UNKNOWN}"
[ -n "$ELF" ] && [ -f "$ELF" ] || { echo "save_build_forensics: no elf '$ELF'" >&2; exit 0; }
[ -n "$ARCHIVE" ] || { echo "save_build_forensics: no archive dir" >&2; exit 0; }

BIN="$HOME/Library/Arduino15/packages/esp32/tools/esp-x32/2507/bin"
NM="$BIN/xtensa-esp32-elf-nm"; SIZE="$BIN/xtensa-esp32-elf-size"
[ -x "$NM" ] || { echo "save_build_forensics: no nm at $NM" >&2; exit 0; }

STAMP="$(date +%Y%m%d-%H%M%S)"
SRC="${MM_SRC:-.}"
SHA="$(shasum -a256 "$ELF" | cut -d' ' -f1)"
DIR="$ARCHIVE/${STAMP}_${LABEL}_${RESULT}"
mkdir -p "$DIR"

"$NM" -S -n --defined-only "$ELF" | gzip > "$DIR/symbols.nm.txt.gz"
"$SIZE" -A "$ELF" > "$DIR/sections.txt" 2>/dev/null || true
{
  echo "stamp:      $STAMP"
  echo "label:      $LABEL"
  echo "result:     $RESULT"     # WORKS | FAILS | UNKNOWN  (edit after device test)
  echo "elf_sha256: $SHA"
  echo "src:        $SRC"
  echo "git_head:   $(git -C "$SRC" rev-parse HEAD 2>/dev/null || echo '?')"
  echo "git_dirty:  $([ -n "$(git -C "$SRC" status --porcelain 2>/dev/null)" ] && echo yes || echo no)"
} > "$DIR/meta.txt"
# Exact source variant vs HEAD -- reproduces this build (apply on git_head).
git -C "$SRC" diff > "$DIR/source.diff" 2>/dev/null || true

echo ">> forensics: $DIR  (result=$RESULT, $(du -h "$DIR/symbols.nm.txt.gz" | cut -f1) map)"
