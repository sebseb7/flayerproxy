# Reference packet capture (fix join / map_chunk / light wire)

Use a **known-good Minecraft 1.21.10 server** as ground truth, capture the full login → configuration → play join burst with `mc_reference_client`, then diff against `mc_static_server`.

## Why not a Node `server.js` alone?

Prismarine/`minecraft-protocol` can **decode** vanilla packets but does not generate vanilla flat-world `map_chunk` + embedded light bytes. For byte-accurate reference wire, use the **official Java 1.21.10 dedicated server** (recommended below).

You can still add a Node `server.js` later as a thin wrapper, but point the capture client at Java for chunk/light truth.

## 1. Reference Java server (port 25571)

Download [Minecraft 1.21.10 server jar](https://www.minecraft.net/en-us/download/server), then in the server directory:

**`server.properties`** (important fields):

```properties
server-port=25571
online-mode=false
network-compression-threshold=-1
view-distance=2
simulation-distance=2
level-type=minecraft:flat
generator-settings={"layers":[{"block":"minecraft:stone","height":127},{"block":"minecraft:grass_block","height":1}],"biome":"minecraft:plains","structures":{"structures":{}}}
spawn-protection=0
max-players=20
enforce-secure-profile=false
```

**Terrain must match `mc_static_server`** (`mc_static_grass.c`):

| | Static server | Reference Java flat |
|--|---------------|---------------------|
| Surface block | grass at **y=63** | grass top layer (1 block) |
| Below surface | **stone** y=-64…62 | stone layer (127 blocks) |
| Player feet | **y=64** (air above grass) | `/setworldspawn 8 64 8` |
| Heightmap | uniform **64** | same (vanilla MOTION_BLOCKING) |
| Dirt / 3-layer preset | **no** | **no** — do not use the old `dirt height 3` preset |

Layers are listed **bottom → top**. `127 + 1 = 128` blocks fills overworld y=-64…63 (stone then grass), same layout as the C server.

Notes:

- `network-compression-threshold=-1` is required for the C capture client (no zlib yet).
- `view-distance=2` → 5×5 chunk grid (matches `MC_STATIC_VIEW_RADIUS`).

First start:

```bash
java -Xmx1G -jar server.jar nogui
# accept EULA in eula.txt, restart
```

In the server console after join once (optional, match static spawn):

```mcfunction
/setworldspawn 8 64 8
/gamerule doDaylightCycle false
/gamerule doWeatherCycle false
```

Keep this server running on **127.0.0.1:25571**.

## 2. Capture reference join (libchunk client)

```bash
make -C libchunk mc_reference_client compare_join_capture

# Capture from Java reference server (offline, no compression)
MC_OFFLINE=1 ./libchunk/build/mc_reference_client 127.0.0.1 25571 capture/reference

# Capture from Constantiam (SRV + online auth + compression) — run from repo root
./libchunk/build/mc_reference_client constantiam.net 25565 capture/reference2 FlayerBot

# Or set tokens from an existing session:
# export MC_ACCESS_TOKEN=... MC_PROFILE_ID=<uuid without dashes>

# Capture from our static server (run in another terminal first)
./libchunk/build/mc_static_server --port 25572
MC_OFFLINE=1 ./libchunk/build/mc_reference_client 127.0.0.1 25572 capture/static
```

`mc_reference_client` resolves `_minecraft._tcp.<host>` when port is 25565 (required for Constantiam; the apex domain points at Cloudflare, not the game server).

Output layout:

```
capture/reference/
  index.txt              # one line per packet (S2C and C2S, chronological)
  0000_c2s_hs_00_handshake.wire
  0001_c2s_login_00_login_start.wire
  0002_s2c_login_02_success.wire
  0003_c2s_login_03_login_acknowledged.wire
  0042_s2c_play_2c_map_chunk.wire
  ...
```

**Direction**

| Log / filename | Meaning |
|----------------|---------|
| `[0002] s2c login 0x02 success` | Server → client |
| `[0003] c2s login 0x03 login_acknowledged` | Client → server |

Both directions are saved as `.wire` files with `s2c` or `c2s` in the name (`NNNN_dir_phase_id_label.wire`). Legacy captures without `s2c_`/`c2s_` are treated as S2C.

`index.txt` format: `seq dir phase 0xid name bytes path` (header line starts with `#`).

Each `.wire` file is **packet id varint + payload** (same as sniffer / `decode_raw_dir` input after outer frame length).

## 3. Compare captures

```bash
./libchunk/build/compare_join_capture capture/reference capture/static
```

This prints:

- Side-by-side join sequence (packet name, id, length)
- First byte mismatch for `map_chunk` / `login` / packets that differ in size
- Whether libchunk can parse `map_chunk` / embedded light

## 4. Decode with libchunk

```bash
make -C libchunk decode_raw_dir
./libchunk/build/decode_raw_dir capture/reference capture/reference-decoded
./libchunk/build/decode_raw_dir capture/static capture/static-decoded
```

Rename or symlink `*.wire` into subdirs named by packet type if you use flat `decode_raw_dir` (see README).

## 5. Optional: Node sniffer (already in repo)

If you prefer capturing through the existing MITM sniffer instead of the C client:

```bash
# config.json: server.host/port → Java reference :25571, sniffer.port 25567
npm run sniffer
# Connect vanilla client to localhost:25567
# Raw wire in logs/sniffer/chunks/<session>/
```

Use `decode_raw_dir` on that folder. Prefer `mc_reference_client` for a minimal, reproducible join capture without a GUI client.

## Target parity checklist

| Item | Reference (Java) | Static server |
|------|------------------|---------------|
| Protocol | 773 | 773 |
| View radius | 2 | 2 (`MC_STATIC_VIEW_RADIUS`) |
| Spawn | 8, 64, 8 | 8, 64, 8 |
| Flat grass y=63, stone below | yes | yes (stone 127 + grass 1) |
| Heightmap / spawn Y | 64 / (8,64,8) | 64 / (8,64,8) |
| Light sections | `sections + 2`, minSection−1 | must match |
| `map_chunk` id | 0x2b | 0x2b |
| Embedded light | BitSet masks + 2048 B arrays | same layout |

Fix static server until `compare_join_capture` shows matching `map_chunk` sizes and libchunk parses both without error.
