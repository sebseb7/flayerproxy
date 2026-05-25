#!/usr/bin/env bash
# Rename map_chunk PNGs to top-left world block coords (chunk x,z → x*16, z*16).
# Reads matching *-map_chunk.json in the decoded dir for chunk indices.
#
# Usage: rename-chunk-pngs.sh <png_dir> <decoded_dir>
set -euo pipefail

PNG_DIR="${1:?png dir}"
DECODED_DIR="${2:?decoded dir}"

if [[ ! -d "$PNG_DIR" ]]; then
  echo "error: png dir not found: $PNG_DIR" >&2
  exit 1
fi
if [[ ! -d "$DECODED_DIR" ]]; then
  echo "error: decoded dir not found: $DECODED_DIR" >&2
  exit 1
fi

python3 - "$PNG_DIR" "$DECODED_DIR" <<'PY'
import json
import os
import sys
from collections import defaultdict

png_dir, decoded_dir = sys.argv[1], sys.argv[2]

entries = []
missing_json = []

for name in sorted(os.listdir(png_dir)):
    if not name.endswith(".png"):
        continue
    base = name[:-4]
    json_path = os.path.join(decoded_dir, base + ".json")
    if not os.path.isfile(json_path):
        json_path = os.path.join(decoded_dir, base + "-map_chunk.json")
    if not os.path.isfile(json_path):
        missing_json.append(name)
        continue
    with open(json_path, encoding="utf-8") as f:
        j = json.load(f)
    cx = j["params"]["x"]
    cz = j["params"]["z"]
    wx = cx * 16
    wz = cz * 16
    entries.append(
        {
            "png": name,
            "base": base,
            "chunk_x": cx,
            "chunk_z": cz,
            "world_x": wx,
            "world_z": wz,
        }
    )

by_coord = defaultdict(list)
for e in entries:
    by_coord[(e["world_x"], e["world_z"])].append(e)

def target_name(wx, wz, dup_index=None):
    if dup_index is None:
        return f"x{wx}_z{wz}.png"
    return f"x{wx}_z{wz}_{dup_index}.png"

renamed = []
skipped_exists = []

for (wx, wz), group in sorted(by_coord.items()):
    group.sort(key=lambda e: e["base"])
    for i, e in enumerate(group):
        suffix = None if i == 0 else group[i]["base"]
        new_name = target_name(wx, wz, suffix)
        old_path = os.path.join(png_dir, e["png"])
        new_path = os.path.join(png_dir, new_name)
        if os.path.abspath(old_path) == os.path.abspath(new_path):
            continue
        if os.path.exists(new_path) and os.path.abspath(old_path) != os.path.abspath(new_path):
            skipped_exists.append((e["png"], new_name))
            continue
        os.rename(old_path, new_path)
        renamed.append((e["png"], new_name, e["chunk_x"], e["chunk_z"], wx, wz))

duplicates = {k: v for k, v in by_coord.items() if len(v) > 1}

print(f"png files: {len(entries) + len(missing_json)}")
print(f"renamed: {len(renamed)}")
if missing_json:
    print(f"missing decoded json: {len(missing_json)}")
    for n in missing_json[:10]:
        print(f"  {n}")
    if len(missing_json) > 10:
        print(f"  ... and {len(missing_json) - 10} more")

if duplicates:
    print(f"\nduplicate world corners ({len(duplicates)} coords, {sum(len(v) for v in duplicates.values())} files):")
    for (wx, wz), group in sorted(duplicates.items()):
        print(f"  world ({wx}, {wz}) chunk ({group[0]['chunk_x']}, {group[0]['chunk_z']}):")
        for e in group:
            new = target_name(wx, wz, None if e is group[0] else e["base"])
            print(f"    {e['png']} -> {new}")
else:
    print("\nno duplicate world coordinates")

if skipped_exists:
    print(f"\nskipped (target exists): {len(skipped_exists)}")
    for old, new in skipped_exists[:5]:
        print(f"  {old} -> {new}")
PY
