#!/usr/bin/env bash
# Stitch per-chunk map PNGs into 16×16 megatile AVIFs (512×512 px, 2 px/block). Requires libavif.
#
# Input layout (chunk_stream_receiver): nested dirs under chunk_png_dir, e.g.
#   rx24/rz69/cx776/cz2216/x12416_z35456.png
# Subdirs named raw/ (wire captures) and X16/ are skipped. Output defaults to <chunk_png_dir>/X16.
#
# Usage:
#   stitch-map-megatiles.sh [chunk_png_dir] [megatile_dir]
#   STITCH_JOBS=4 stitch-map-megatiles.sh logs/sniffer/chunks/png logs/sniffer/chunks/pngX16
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CHUNK_PNG_DIR="${1:-$ROOT/logs/sniffer/chunks/png}"
MEGATILE_DIR="${2:-$CHUNK_PNG_DIR/X16}"

if [[ ! -d "$CHUNK_PNG_DIR" ]]; then
  echo "error: chunk png dir not found: $CHUNK_PNG_DIR" >&2
  exit 1
fi

if [[ -z "${STITCH_JOBS:-}" ]]; then
  JOBS="$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)"
else
  JOBS="$STITCH_JOBS"
fi

make -C "$ROOT/libchunk" -s stitch_megatiles
echo "stitch: $CHUNK_PNG_DIR -> $MEGATILE_DIR (${JOBS} workers)" >&2
"$ROOT/libchunk/build/stitch_megatiles" -j "$JOBS" "$CHUNK_PNG_DIR" "$MEGATILE_DIR"
node "$ROOT/scripts/generate-megatile-index.js" "$MEGATILE_DIR"
