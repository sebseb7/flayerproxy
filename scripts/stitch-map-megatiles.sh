#!/usr/bin/env bash
# Stitch per-chunk map PNGs into 16x16 megatile AVIFs (512x512 px, 2 px/block). Requires libavif.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CHUNK_PNG_DIR="${1:-$ROOT/logs/sniffer/chunks/png}"
MEGATILE_DIR="${2:-$CHUNK_PNG_DIR/X16}"
if [[ -z "${STITCH_JOBS:-}" ]]; then
  JOBS="$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)"
else
  JOBS="$STITCH_JOBS"
fi

make -C "$ROOT/libchunk" -s stitch_megatiles
"$ROOT/libchunk/build/stitch_megatiles" -j "$JOBS" "$CHUNK_PNG_DIR" "$MEGATILE_DIR"
node "$ROOT/scripts/generate-megatile-index.js" "$MEGATILE_DIR"
