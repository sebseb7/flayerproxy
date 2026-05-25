#!/usr/bin/env bash
# List map-PNG surface columns for raw map_chunk captures (libchunk, no Node).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
make -C "$ROOT/libchunk" -s list_map_surface
cd "$ROOT"
exec "$ROOT/libchunk/build/list_map_surface" "$@"
