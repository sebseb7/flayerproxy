# @flayerproxy/libchunk

Node.js bindings for **libchunk** — parse Minecraft 1.21.x play packet wire bytes and return `toString` summaries (same as `decode_raw_dir` / `show_decode`). Includes terrain/entity packets plus **player**, **inventory**, **config**, and **misc** stream packets from `chunk_stream_receiver` (`player/login/login.wire`, `misc/update_time/update_time.wire`, etc.).

## Build

Requires libchunk built (`libavif` optional for PNG tools; decode-only needs the static library):

```bash
cd libchunk && make
cd js && npm install
```

## API

```javascript
const lc = require('@flayerproxy/libchunk');

// Explicit packet name + buffer (payload may include leading packet-id varint)
const buf = fs.readFileSync('raw/block_change/.../x1_y2_z3.wire');
lc.decodeWire('block_change', buf);
// => { ok: true, text: 'block_change location=(...) type=...' }

// Infer packet from sniffer path
lc.decodeWireFile(path.join('raw', 'entity_metadata', 'eu117', 'eu9', 'e7670224', 'entity_metadata.wire'));

lc.hexDump(buf);
lc.supportedPackets();
lc.isPacketSupported('map_chunk');

// Full map_chunk JSON
lc.decodeMapChunkJson('x12416_z35296.wire', buf);

// Path helpers
lc.packetNameFromPath(filePath);
lc.parseWirePath(filePath);  // { packet, category?, worldX?, entityId?, ... }
lc.archiveCategoryFromPath(filePath);  // 'player' | 'config' | 'misc' | null
lc.pngPathForWire(mapChunkWirePath);
```

## VS Code extension

See [extensions/minecraft-wire-viewer](../../extensions/minecraft-wire-viewer/README.md).
