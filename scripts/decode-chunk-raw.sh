#!/usr/bin/env bash
# Decode sniffer raw chunk captures with libchunk (JSON + optional top-surface PNGs).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
make -C "$ROOT/libchunk" -s decode_raw_dir
cd "$ROOT"
exec "$ROOT/libchunk/build/decode_raw_dir" "$@"
