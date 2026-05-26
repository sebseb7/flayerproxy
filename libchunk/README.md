# libchunk

C library that parses Minecraft Java Edition **1.21.5+** play/configuration packet **payloads** (including **1.21.10**), aligned with `minecraft-data` wire definitions. Chunk decoding is implemented from the protocol spec only — **no Prismarine** dependency.

## Usage

```c
#include <libchunk.h>

/* Payload only — no leading packet-id varint */
lc_block_change bc;
if (lc_parse_block_change(payload, payload_len, &bc) == LC_OK) {
  char dbg[256];
  lc_block_change_to_string(&bc, dbg, sizeof dbg);
  puts(dbg);
}

/* If your buffer includes the packet id (e.g. sniffer `wire` field): */
const uint8_t *payload;
size_t payload_len;
lc_skip_packet_id(wire, wire_len, &payload, &payload_len);
```

## Packets

See **[docs/WIRE_PACKETS.md](docs/WIRE_PACKETS.md)** for the full **149** clientbound catalog, initialization state-machine diagrams, vanilla 1.21.10 class links, and libchunk decode depth.

| Parser | Struct | `toString` |
|--------|--------|------------|
| `lc_parse_map_chunk` | `lc_map_chunk` | `lc_map_chunk_to_string` |
| `lc_parse_update_light` | `lc_update_light` | `lc_update_light_to_string`, `lc_update_light_fprint` (full masks + light bytes) |
| `lc_parse_block_change` | `lc_block_change` | `lc_block_change_to_string` |
| `lc_parse_unload_chunk` | `lc_unload_chunk` | `lc_unload_chunk_to_string` |
| `lc_parse_multi_block_change` | `lc_multi_block_change` | `lc_multi_block_change_to_string` |
| `lc_parse_spawn_entity` | `lc_spawn_entity` | `lc_spawn_entity_to_string` |
| `lc_parse_entity_metadata` | `lc_entity_metadata` | `lc_entity_metadata_to_string` |
| `lc_parse_entity_equipment` | `lc_entity_equipment` | `lc_entity_equipment_to_string`, `lc_entity_equipment_fprint` (slots + components) |
| `lc_parse_entity_destroy` | `lc_entity_destroy` | `lc_entity_destroy_to_string` |
| `lc_parse_set_passengers` | `lc_set_passengers` | `lc_set_passengers_to_string` |
| `lc_parse_rel_entity_move` | `lc_rel_entity_move` | `lc_rel_entity_move_to_string` |
| `lc_parse_entity_move_look` | `lc_entity_move_look` | `lc_entity_move_look_to_string` |
| `lc_parse_sync_entity_position` | `lc_sync_entity_position` | `lc_sync_entity_position_to_string` |
| `lc_parse_entity_velocity` | `lc_entity_velocity` | `lc_entity_velocity_to_string` |
| `lc_parse_entity_head_rotation` | `lc_entity_head_rotation` | `lc_entity_head_rotation_to_string` |
| `lc_parse_position` | `lc_position` | `lc_position_to_string` |
| `lc_parse_respawn` | `lc_respawn` | `lc_respawn_to_string` |
| `lc_parse_initialize_world_border` | `lc_initialize_world_border` | `lc_initialize_world_border_to_string` |
| `lc_parse_registry_data` | `lc_registry_data` | `lc_registry_data_to_string` |

Call the matching `*_free()` for packets that allocate heap memory (`map_chunk`, `update_light`, metadata, equipment, registry, respawn, etc.).

## Build

```bash
cd libchunk && make && make test
```

Produces `build/libchunk.a`.

### Node.js / VS Code

```bash
cd libchunk/js && npm install   # builds native addon
cd ../../extensions/minecraft-wire-viewer && npm install && npm run compile
```

See [js/README.md](js/README.md) and [extensions/minecraft-wire-viewer](../extensions/minecraft-wire-viewer/README.md).

### Show `toString` on sniffer captures

```bash
make show_decode

# Single decoded dump (from logs/sniffer/.../decoded/*.json)
./build/show_decode ../logs/sniffer/chunks/session-*/decoded/000177-spawn_entity*.json

# First N supported packets in a decoded folder
./build/show_decode --dir ../logs/sniffer/chunks/session-*/decoded max=30
```

### Batch-decode raw wire files (chunkLog)

Sniffer writes binary wire to `logs/sniffer/chunks/<server>/<ms>-<packet_name>` (with `chunkLog` enabled; `<server>` is `host_port`, e.g. `constantiam.net_25565`, also `registry_data` and `session-info.json` for block state-id lookup). `registry_data`, `tile_entity_data`, and `entity_update_attributes` decode to multi-line `.txt` (full NBT or attribute tables). `map_chunk` JSON includes decoded sign lines (`sign.front` / `sign.back`, four lines each) when block-entity NBT is sign-like, and `typeName` for known protocol block-entity type ids (same partial table as `src/sniffer/anvilBlockEntity.js`; unknown ids show `typeName: null`). Other play packets use one-line summaries unless noted.

```bash
make decode_raw_dir

# input_dir = raw captures, output_dir = one .txt per file (libchunk toString)
./build/decode_raw_dir ../logs/sniffer/chunks/session-1779586311873 ../logs/sniffer/chunks/session-1779586311873/decoded

# optional third argument: per-chunk 32×32 top-surface PNGs (2 px/block)
./build/decode_raw_dir <input_dir> <output_dir> <png_dir>

Decoded files are written flat under `<output_dir>/` and duplicated under `<output_dir>/<packet_name>/` (temporary layout for browsing by type).

# or from repo root (JSON + per-chunk top-surface PNGs):
./scripts/decode-chunk-raw.sh logs/sniffer/chunks/session-1779586311873 \
  logs/sniffer/chunks/session-1779586311873-decoded \
  logs/sniffer/chunks/session-1779586311873-png
```

When `png_dir` is set, the first `map_chunk` per chunk corner writes a **32×32** top-surface PNG as `x{worldX}_z{worldZ}.png` (2 px per block; world block at chunk `(x,z)` corner); later packets at the same corner are skipped for PNG (JSON is still written).

### Summarize raw map_chunk captures

Counts every block in decoded sections by **global state id** (sorted by count), block entities by **type id**, and sign text with world coordinates. Reads the same raw wire files as `decode_raw_dir` (not JSON).

```bash
make summarize_raw_dir
./build/summarize_raw_dir <input_dir> <output_dir>

# from repo root:
./scripts/summarize-chunks-raw.sh logs/sniffer/chunks/session-… logs/sniffer/chunks/session-…/summary
```

Writes `<output_dir>/blocks-by-type.txt` (minecraft block type, count — state variants merged),
`block-entities-by-type.txt`,
`block-entities/<typeId-minecraft-name>/coordinates.txt` (file, worldX, worldY, worldZ per row), and `signs.txt`.

### Live chunk stream receiver (sniffer `chunkStream`)

Listens for framed play packets from the Node sniffer (`config.json` → `sniffer.chunkStream`): terrain/entity packets (coordinate- or entity-id–based paths), plus player/inventory/world packets archived under `player/`, `config/`, and `misc/` (see `chunkStream.js` allowlists). Frame format: `uint32` inner length, `uint16` name length, packet name UTF-8, wire bytes. Only `map_chunk` writes top-surface PNG; all forwarded packets archive raw wire.

```bash
make chunk_stream_receiver

# terminal 1 — receiver (default bind 0.0.0.0; add -v for per-packet stderr logs)
./build/chunk_stream_receiver 25570 logs/sniffer/chunks/png2 logs/sniffer/chunks/raw2

# config.json: "chunkStream": { "host": "127.0.0.1", "port": 25570 }

# terminal 2 — sniffer + Minecraft client
npm run sniffer
```

PNG and raw `map_chunk` use the same hierarchy: `<dir>/rx…/rz…/cx…/cz…/` with `x{worldX}_z{worldZ}.png` and `x{worldX}_z{worldZ}.map_chunk.wire` (latest packet overwrites). Chunk/block packets use `coord.<packet>.wire` (e.g. `x…_z….update_light.wire`, `x…_y…_z….block_change.wire`, `unload_chunk/…/x…_z….unload_chunk.wire`). `spawn_entity` is coordinate-based under `spawn_entity/rx…/rz…/cx…/cz…/x…_y…_z….spawn_entity.wire`. Other entity packets: `<raw_dir>/<packet>/eu…/eu…/e<id>/<entityId>.<packet>.wire` (`entity_teleport`, effects, `entity_look` flat under `<packet>/` until parsed). Player packets: `<raw_dir>/player/<packet>/<packet>.wire` (`position` uses `player/position/rx…/…/x…_y…_z….position.wire`). Registry/recipe sync: `<raw_dir>/config/…`. Tab list, scoreboard, border, time, view: `<raw_dir>/misc/…`. Without `-v`, stderr only shows connect/disconnect, errors, and periodic stats (every 60s when packets arrived: packets/s and handler CPU share; session end: total handler time and wall-time %). Pass `-v` / `--verbose` to log each processed packet.

Stitch those chunk PNGs into **512×512** megatile AVIFs (16×16 chunks, black gaps). Requires `libavif-dev` (links `-lavif`). `stitch_megatiles` walks `<png_dir>` recursively (`rx…/rz…/cx…/cz…/x*_z*.png`; skips `raw/` and `X16/`). Default output: `<png_dir>/X16/`; pass a second argument for another directory.

```bash
./scripts/stitch-map-megatiles.sh logs/sniffer/chunks/png logs/sniffer/chunks/pngX16
# open logs/sniffer/chunks/pngX16/index.html — pan/zoom map (focus: tile nearest 0,0)
```

Parse failures are written as `<basename>.err`; `map_chunk` → `<basename>.json` (decoded `params` + `sections` only — no duplicate `wire` / `chunkData`; raw bytes stay in the session capture file).

**Wire notes (1.21.x):** `map_chunk` chunk `x`/`z` are big-endian i32; heightmaps are a typed `i64[]` list (not root NBT); block-entity `y` is big-endian i16; NBT uses network format (`anonOptionalNbt`: `0` absent, `1` then payload, or payload starting on the tag byte; u16 BE strings, BE numerics).
