#!/usr/bin/env python3
"""Summarize mc_reference_client capture dirs for join/chunk comparison."""
from __future__ import annotations

import argparse
import os
import struct
import sys


def read_varint(data: bytes, off: int = 0) -> tuple[int, int]:
    result = shift = 0
    while off < len(data):
        b = data[off]
        off += 1
        result |= (b & 0x7F) << shift
        if not (b & 0x80):
            return result, off
        shift += 7
    raise ValueError("truncated varint")


def parse_wire(path: str) -> tuple[int, bytes]:
    raw = open(path, "rb").read()
    pid, off = read_varint(raw, 0)
    return pid, raw[off:]


def parse_login(payload: bytes) -> dict:
    off = 0
    entity_id = struct.unpack(">i", payload[off : off + 4])[0]
    off += 4
    off += 1  # hardcore
    wc, off = read_varint(payload, off)
    for _ in range(wc):
        ln, off = read_varint(payload, off)
        off += ln
    max_players, off = read_varint(payload, off)
    view_distance, off = read_varint(payload, off)
    sim_distance, off = read_varint(payload, off)
    return {
        "entity_id": entity_id,
        "max_players": max_players,
        "view_distance": view_distance,
        "sim_distance": sim_distance,
    }


def chunk_coords(payload: bytes) -> tuple[int, int]:
    x = struct.unpack(">i", payload[0:4])[0]
    z = struct.unpack(">i", payload[4:8])[0]
    return x, z


def summarize(capture_dir: str) -> None:
    print(f"\n=== {capture_dir} ===")
    if not os.path.isdir(capture_dir):
        print("missing")
        return

    play = []
    chunks: set[tuple[int, int]] = set()
    views: list[int] = []
    sims: list[int] = []

    for fn in sorted(os.listdir(capture_dir)):
        if not fn.endswith(".wire"):
            continue
        pid, payload = parse_wire(os.path.join(capture_dir, fn))
        if "_play_" not in fn:
            continue
        play.append((fn, pid, len(payload)))
        if fn.endswith("play_30_login.wire") or ("login" in fn and "_play_" in fn and fn.startswith("0027")):
            print("login:", parse_login(payload))
        if "update_view_distance" in fn:
            views.append(read_varint(payload, 0)[0])
        if "simulation_distance" in fn:
            sims.append(read_varint(payload, 0)[0])
        if "map_chunk" in fn:
            chunks.add(chunk_coords(payload))

    print(f"play packets: {len(play)}")
    if views:
        print(f"update_view_distance: {views}")
    if sims:
        print(f"simulation_distance: {sims}")
    if chunks:
        xs = [c[0] for c in chunks]
        zs = [c[1] for c in chunks]
        side_x = max(xs) - min(xs) + 1
        side_z = max(zs) - min(zs) + 1
        print(f"map_chunks: {len(chunks)} unique, grid {side_x}x{side_z}, x=[{min(xs)},{max(xs)}] z=[{min(zs)},{max(zs)}]")
    print("play join order (first 25 play packets):")
    for fn, pid, plen in play[:25]:
        label = fn.split("_", 3)[-1].replace(".wire", "")
        print(f"  {fn[:4]} 0x{pid:02x} {label:28s} {plen:5d}B")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("dirs", nargs="+", help="capture directories (e.g. capture/reference capture/static)")
    args = ap.parse_args()
    for d in args.dirs:
        summarize(d)
    return 0


if __name__ == "__main__":
    sys.exit(main())
