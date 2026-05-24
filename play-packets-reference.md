# Play-state packet reference (Minecraft 1.21.10)

Reference for **server → client (S2C) play packets** observed in sniffer sessions. Wire layout is taken from [minecraft-data `1.21.9` proto](https://github.com/PrismarineJS/minecraft-data) (protocol **773** — same as **1.21.10**).

Field names match what the sniffer decodes into `params` in `logs/sniffer/chunks/.../decoded/*.json`.

---

## Shared types

### `position` (block coordinates)

Packed into 64 bits: **x** (26 signed), **z** (26 signed), **y** (12 signed). Decoded as `{ x, y, z }` in block space.

### `lpVec3` (low-precision velocity)

Used by `spawn_entity.velocity` and `entity_velocity.velocity`. Quantized 3-vector; decoded as `{ x, y, z }` floats (blocks/tick scale). Zero velocity encodes as a single `0` byte.

### Relative entity deltas (`dX`, `dY`, `dZ`)

`i16` values in **1/4096 block** units. Actual offset = `dX / 4096` blocks (same for Y/Z).

### Entity rotation bytes (`yaw`, `pitch`, `headYaw`)

`i8` angles: degrees = `byte * 360 / 256` (wraps at ±128).

### `PositionUpdateRelatives` (`play.position.flags`)

Bit flags indicating which fields are **relative to the current client position** instead of absolute:

| Flag | Meaning when set |
|------|------------------|
| `x`, `y`, `z` | Position component is a delta |
| `dx`, `dy`, `dz` | Velocity component is relative |
| `yaw`, `pitch` | Rotation is relative |
| `yawDelta` | Yaw delta mode |

When all flags are false, all position/rotation values are absolute (typical teleport).

### `Slot` (items)

```json
{
  "itemId": 894,
  "itemCount": 1,
  "addedComponentCount": 0,
  "removedComponentCount": 0,
  "components": [],
  "removeComponents": []
}
```

`itemId` is the blockReason block/item registry id. Empty slot: `itemCount: 0`.

### `ItemSoundHolder` (`sound` fields)

Either `{ "soundId": <registry id> }` or `{ "data": { "soundName": "minecraft:...", "fixedRange": <optional f32> } }`.

### `soundCategory`

Enum: `master`, `music`, `record`, `weather`, `block`, `hostile`, `neutral`, `player`, `ambient`, `voice`, `ui`.

### `anonymousNbt` / chat components

JSON-like NBT chat trees (`type` + `value`) or plain `{ "type": "string", "value": "..." }`.

---

## Entity motion & sync

### `play.entity_velocity`

**Vanilla:** `ClientboundSetEntityMotionPacket`  
**Direction:** S2C

Sets an entity's velocity (knockback, projectiles, etc.).

| Field | Type | Description |
|-------|------|-------------|
| `entityId` | varint | Entity network id |
| `velocity.x` | f32 (lpVec3) | X velocity (blocks/tick) |
| `velocity.y` | f32 | Y velocity |
| `velocity.z` | f32 | Z velocity |

**Example:** `{ "entityId": 14597015, "velocity": { "x": 0.992, "y": 0.994, "z": 0.859 } }`

---

### `play.sync_entity_position`

**Vanilla:** `ClientboundEntityPositionSyncPacket`  
**Direction:** S2C

Full position + rotation sync for an entity (teleport-style correction). Common on busy servers when relative moves drift.

| Field | Type | Description |
|-------|------|-------------|
| `entityId` | varint | Entity network id |
| `x`, `y`, `z` | f64 | Absolute position |
| `dx`, `dy`, `dz` | f64 | Velocity (often ~0; may show float noise) |
| `yaw`, `pitch` | f32 | Body rotation (degrees) |
| `onGround` | bool | Feet on ground |

**Example:** `{ "entityId": 14597009, "x": 12525.10, "y": 66, "z": 35495.50, "yaw": -47.82, "pitch": 0, "onGround": true }`

---

### `play.rel_entity_move`

**Vanilla:** `ClientboundMoveEntityPacket.Pos`  
**Direction:** S2C

Small relative position update.

| Field | Type | Description |
|-------|------|-------------|
| `entityId` | varint | Entity network id |
| `dX`, `dY`, `dZ` | i16 | Delta in 1/4096 blocks |
| `onGround` | bool | On ground flag |

**Example:** `{ "entityId": 14597220, "dX": -452, "dY": 633, "dZ": -401, "onGround": false }` → Δ ≈ (-0.110, 0.155, -0.098) blocks

---

### `play.entity_move_look`

**Vanilla:** `ClientboundMoveEntityPacket.PosRot`  
**Direction:** S2C

Relative move plus body rotation.

| Field | Type | Description |
|-------|------|-------------|
| `entityId` | varint | Entity network id |
| `dX`, `dY`, `dZ` | i16 | Delta in 1/4096 blocks |
| `yaw`, `pitch` | i8 | Body rotation bytes |
| `onGround` | bool | On ground flag |

---

### `play.entity_look`

**Vanilla:** `ClientboundMoveEntityPacket.Rot`  
**Direction:** S2C

Rotation only (no position change).

| Field | Type | Description |
|-------|------|-------------|
| `entityId` | varint | Entity network id |
| `yaw`, `pitch` | i8 | Body rotation bytes |
| `onGround` | bool | On ground flag |

---

### `play.entity_head_rotation`

**Vanilla:** `ClientboundRotateHeadPacket`  
**Direction:** S2C

Head yaw separate from body yaw.

| Field | Type | Description |
|-------|------|-------------|
| `entityId` | varint | Entity network id |
| `headYaw` | i8 | Head rotation byte |

---

## Entity lifecycle & state

### `play.spawn_entity`

**Vanilla:** `ClientboundAddEntityPacket`  
**Direction:** S2C

Creates a new entity in the world.

| Field | Type | Description |
|-------|------|-------------|
| `entityId` | varint | Network id assigned to this entity |
| `objectUUID` | UUID | Persistent entity UUID |
| `type` | varint | Entity type registry id (e.g. `105` = item entity) |
| `x`, `y`, `z` | f64 | Spawn position |
| `velocity` | lpVec3 | Initial velocity `{ x, y, z }` |
| `pitch`, `yaw`, `headPitch` | i8 | Rotation bytes |
| `objectData` | varint | Type-specific data (item id, projectile owner, etc.; `0` if none) |

---

### `play.entity_metadata`

**Vanilla:** `ClientboundSetEntityDataPacket`  
**Direction:** S2C

Updates entity data watcher fields. Array ends with sentinel key `0xFF` (not shown in decoded output).

| Field | Type | Description |
|-------|------|-------------|
| `entityId` | varint | Entity network id |
| `metadata[]` | array | List of changed entries |

Each metadata entry:

| Field | Type | Description |
|-------|------|-------------|
| `key` | u8 | Metadata index (entity-type-specific) |
| `type` | enum | Value type: `byte`, `int`, `long`, `float`, `string`, `component`, `optional_component`, `item_stack`, `boolean`, `rotations`, `block_pos`, `optional_block_pos`, `direction`, `optional_uuid`, `block_state`, `optional_block_state`, `particle`, `particles`, `villager_data`, `optional_unsigned_int`, `pose`, variant types (`cat_variant`, `cow_variant`, …), `vector3`, `quaternion`, `resolvable_profile`, etc. |
| `value` | varies | Typed payload matching `type` |

**Example:** `{ "key": 9, "type": "float", "value": 3 }`, `{ "key": 17, "type": "int", "value": 4 }`

---

### `play.entity_update_attributes`

**Vanilla:** `ClientboundUpdateAttributesPacket`  
**Direction:** S2C

Sets base attribute values and optional modifiers.

| Field | Type | Description |
|-------|------|-------------|
| `entityId` | varint | Entity network id |
| `properties[]` | array | Attribute list |

Each property:

| Field | Type | Description |
|-------|------|-------------|
| `key` | registry id | Attribute name, e.g. `generic.max_health`, `generic.movement_speed`, `generic.scale`, … |
| `value` | f64 | Base attribute value |
| `modifiers[]` | array | Optional modifiers |

Each modifier:

| Field | Type | Description |
|-------|------|-------------|
| `uuid` | string | Modifier UUID |
| `amount` | f64 | Modifier amount |
| `operation` | i8 | `0` = add, `1` = multiply base, `2` = multiply total |

---

### `play.entity_equipment`

**Vanilla:** `ClientboundSetEquipmentPacket`  
**Direction:** S2C

Updates one or more equipment slots. Terminated by a slot byte with high bit set.

| Field | Type | Description |
|-------|------|-------------|
| `entityId` | varint | Entity network id |
| `equipments[]` | array | Slot + item pairs |

Each equipment entry:

| Field | Type | Description |
|-------|------|-------------|
| `slot` | i8 | `0` main hand, `1` off hand, `2` feet, `3` legs, `4` chest, `5` head, `6` body, `7` saddle |
| `item` | Slot | Item in that slot |

---

### `play.entity_status`

**Vanilla:** `ClientboundEntityEventPacket`  
**Direction:** S2C

Triggers a client-side entity event (animation, effect).

| Field | Type | Description |
|-------|------|-------------|
| `entityId` | i32 | Entity network id |
| `entityStatus` | i8 | Event code |

Common status codes:

| Code | Effect |
|------|--------|
| 2 | Entity hurt (red flash) |
| 3 | Entity death |
| 6 | Wolf taming particles |
| 9 | Villager mating heart |
| 24 | Dolphin happy |
| 55 | Play totem animation |

---

### `play.set_passengers`

**Vanilla:** `ClientboundSetPassengersPacket`  
**Direction:** S2C

Defines which entities are riding a vehicle.

| Field | Type | Description |
|-------|------|-------------|
| `entityId` | varint | Vehicle entity id |
| `passengers` | varint[] | Ordered list of passenger entity ids (empty = dismount all) |

---

## World & chunks

### `play.map_chunk`

**Vanilla:** `ClientboundLevelChunkWithLightPacket`  
**Direction:** S2C

Sends a full chunk column with block data, heightmaps, block entities, and light.

| Field | Type | Description |
|-------|------|-------------|
| `x`, `z` | i32 | Chunk coordinates (block X = x×16) |
| `heightmaps[]` | array | `{ type, data }` pairs |
| `heightmaps[].type` | enum | `world_surface_wg`, `world_surface`, `ocean_floor_wg`, `ocean_floor`, `motion_blocking`, `motion_blocking_no_leaves` |
| `heightmaps[].data` | i64[] | Packed height values (36×64 bit longs per map) |
| `chunkData` | ByteArray | Section palette + block data (varint-length prefixed) |
| `blockEntities[]` | array | Block entities in this chunk |
| `skyLightMask` | i64[] | Sections with sky light present |
| `blockLightMask` | i64[] | Sections with block light present |
| `emptySkyLightMask` | i64[] | Sections explicitly without sky light |
| `emptyBlockLightMask` | i64[] | Sections explicitly without block light |
| `skyLight[]` | u8[][] | Per-section sky light arrays (2048 bytes each when present) |
| `blockLight[]` | u8[][] | Per-section block light arrays |

Each `blockEntities[]` entry:

| Field | Type | Description |
|-------|------|-------------|
| `x`, `z` | u4 each | Position within chunk (0–15) |
| `y` | i16 | Block Y |
| `type` | varint | Block entity type registry id |
| `nbtData` | optional NBT | Tile entity data |

---

### `play.block_change`

**Vanilla:** `ClientboundBlockUpdatePacket`  
**Direction:** S2C

Single block state change.

| Field | Type | Description |
|-------|------|-------------|
| `location` | position | Block `{ x, y, z }` |
| `type` | varint | Global block state id |

**Example:** `{ "location": { "x": 12461, "z": 35476, "y": 136 }, "type": 6661 }`

---

### `play.multi_block_change`

**Vanilla:** `ClientboundSectionBlocksUpdatePacket`  
**Direction:** S2C

Batch block updates within one 16×16×16 section.

| Field | Type | Description |
|-------|------|-------------|
| `chunkCoordinates.x` | i22 | Chunk X |
| `chunkCoordinates.z` | i22 | Chunk Z |
| `chunkCoordinates.y` | i20 | **Section index** (Y ÷ 16), not block Y |
| `records` | varint[] | Packed change records |

Each **record** is one varint packing local coords + state id:

```
stateId = record >> 12
blockY  = record & 0x0F        (0–15 within section)
blockX  = (record >> 8) & 0x0F
blockZ  = (record >> 4) & 0x0F
absolute Y = chunkCoordinates.y * 16 + blockY
```

**Example:** section `(781, 2216, 4)` with 6 records.

---

### `play.tile_entity_data`

**Vanilla:** `ClientboundBlockEntityDataPacket`  
**Direction:** S2C

Updates NBT for a block entity (chest, sign, spawner, etc.).

| Field | Type | Description |
|-------|------|-------------|
| `location` | position | Block `{ x, y, z }` |
| `action` | varint | Block entity type id (same registry as chunk block entities) |
| `nbtData` | optional NBT | Updated tile data; may be empty `{}` for "use cached" |

---

### `play.update_time`

**Vanilla:** `ClientboundSetTimePacket`  
**Direction:** S2C

World age and day time.

| Field | Type | Description |
|-------|------|-------------|
| `age` | i64 | Total world tick age |
| `time` | i64 | Day time (0–24000; negative = frozen) |
| `tickDayTime` | bool | If true, client advances `time` locally each tick |

Decoded i64 may appear as two-element array `[hi, lo]` in some log serializers.

---

## Player-facing

### `play.position`

**Vanilla:** `ClientboundPlayerPositionPacket`  
**Direction:** S2C

Teleports or adjusts the **local player**. Client must reply with `play.teleport_confirm` (same `teleportId`).

| Field | Type | Description |
|-------|------|-------------|
| `teleportId` | varint | Id for confirm packet |
| `x`, `y`, `z` | f64 | Position (absolute or delta per flags) |
| `dx`, `dy`, `dz` | f64 | Velocity |
| `yaw`, `pitch` | f32 | Look direction (degrees) |
| `flags` | bitfield | `PositionUpdateRelatives` — see shared types |

**Example (absolute spawn teleport):** `{ "teleportId": 1, "x": 12523.19, "y": 65, "z": 35480.43, "yaw": 7.77, "pitch": 25.35, "flags": { "_value": 0, ... } }`

---

### `play.player_info`

**Vanilla:** `ClientboundPlayerInfoUpdatePacket`  
**Direction:** S2C

Batch tab-list updates. Action flags determine which fields are present per player.

| Field | Type | Description |
|-------|------|-------------|
| `action` | bitfield | Which fields to update (see below) |
| `data[]` | array | One entry per affected player UUID |

Action flags:

| Flag | Adds field |
|------|------------|
| `add_player` | `player` (profile: name + properties) |
| `initialize_chat` | `chatSession` |
| `update_game_mode` | `gamemode` |
| `update_listed` | `listed` |
| `update_latency` | `latency` |
| `update_display_name` | `displayName` (NBT chat) |
| `update_hat` | `showHat` |
| `update_list_order` | `listPriority` |

Each `data[]` entry always includes `uuid`; other fields depend on `action`.

---

### `play.playerlist_header`

**Vanilla:** `ClientboundTabListPacket`  
**Direction:** S2C

Tab list header and footer text.

| Field | Type | Description |
|-------|------|-------------|
| `header` | NBT chat | Header (often empty string) |
| `footer` | NBT chat | Footer |

---

### `play.ping`

**Vanilla:** `ClientboundPingPacket`  
**Direction:** S2C

Latency measurement. Client responds with `play.pong` carrying the same `id`.

| Field | Type | Description |
|-------|------|-------------|
| `id` | i32 | Ping id (often negative in practice) |

---

## Effects & misc

### `play.sound_effect`

**Vanilla:** `ClientboundSoundPacket`  
**Direction:** S2C

Plays a sound at a world position.

| Field | Type | Description |
|-------|------|-------------|
| `sound` | ItemSoundHolder | Sound event |
| `soundCategory` | soundCategory | Volume category |
| `x`, `y`, `z` | i32 | Block coordinates (integer) |
| `volume` | f32 | Volume multiplier |
| `pitch` | f32 | Pitch multiplier |
| `seed` | i64 | Random seed for sound variation |

---

### `play.system_chat`

**Vanilla:** `ClientboundSystemChatPacket`  
**Direction:** S2C

Server system message (not signed player chat).

| Field | Type | Description |
|-------|------|-------------|
| `content` | NBT chat | Message text/component |
| `isActionBar` | bool | If true, show above hotbar instead of chat |

---

### `play.bundle_delimiter`

**Vanilla:** bundle wrapper (no payload)  
**Direction:** S2C

Marks start or end of a **packet bundle** (1.19.4+). No parameters — adjacent packets between delimiters are processed atomically on the client.

---

## Quick reference (session counts)

From `session-1779489992221` (~9 s in-world):

| Packet | Count |
|--------|------:|
| `entity_velocity` | 583 |
| `sync_entity_position` | 519 |
| `entity_head_rotation` | 401 |
| `rel_entity_move` | 305 |
| `map_chunk` | 265 |
| `entity_metadata` | 263 |
| `bundle_delimiter` | 248 |
| `entity_update_attributes` | 205 |
| `entity_move_look` | 188 |
| `spawn_entity` | 124 |
| `ping` | 92 |
| `player_info` | 77 |
| `entity_equipment` | 44 |
| `set_passengers` | 26 |
| `tile_entity_data` | 26 |
| `entity_status` | 23 |
| `system_chat` | 13 |
| `multi_block_change` | 9 |
| `entity_look` | 7 |
| `update_time` | 7 |
| `playerlist_header` | 6 |
| `sound_effect` | 5 |
| `block_change` | 2 |
| `position` | 2 |

---

## Sources

- Protocol definitions: `node_modules/minecraft-data/minecraft-data/data/pc/1.21.9/proto.yml`
- Protocol version **773** for Minecraft **1.21.9** and **1.21.10**
- Decoded examples: `logs/sniffer/chunks/session-*/decoded/`
- Multi-block record unpacking: `src/state/chunkMerge.js` (`applyMultiBlockChange`)
- lpVec3 codec: `node_modules/minecraft-protocol/src/datatypes/lpVec3.js`
