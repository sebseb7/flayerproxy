#!/usr/bin/env bash
# Summarize raw map_chunk captures with libchunk (blocks by type, block entities, signs).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
INPUT="${1:-$ROOT/logs/sniffer/chunks/png/raw/map_chunk}"
OUT="${2:-$INPUT/summary}"
make -C "$ROOT/libchunk" -s summarize_raw_dir
cd "$ROOT"
exec "$ROOT/libchunk/build/summarize_raw_dir" "$INPUT" "$OUT"
