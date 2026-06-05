import express from 'express';
import fs from 'node:fs/promises';
import path from 'node:path';
import chalk from 'chalk';
import { createRequire } from 'node:module';
import sharp from 'sharp';
import { formatEntityType } from './client/entityTypeNames.js';
import minecraftData from 'minecraft-data';

const require = createRequire(import.meta.url);
const lc = require('./libchunk/js/index.js');
const mcData = minecraftData('1.21.10');

function readVarInt(buf, offset) {
  let value = 0;
  let size = 0;
  let b;
  while (true) {
    if (offset + size >= buf.length) {
      return null;
    }
    b = buf[offset + size];
    value |= (b & 0x7f) << (size * 7);
    size++;
    if ((b & 0x80) === 0) {
      break;
    }
  }
  return { value, size };
}

function parseSlot(buf) {
  let offset = 0;
  const countRes = readVarInt(buf, offset);
  if (!countRes) return null;
  const count = countRes.value;
  offset += countRes.size;
  if (count === 0) return { count: 0 };
  
  const itemIdRes = readVarInt(buf, offset);
  if (!itemIdRes) return null;
  const itemId = itemIdRes.value;
  
  return { count, itemId };
}

function getSpawnEntityItemName(payload) {
  try {
    let rest = payload;
    let spawn = null;
    let metadata = null;
    
    while (rest && rest.length > 0) {
      const frame = lc.tryReadFrame(rest);
      if (!frame) break;
      
      const packetName = lc.playS2cById[frame.id];
      if (packetName === 'spawn_entity') {
        spawn = lc.parseSpawnEntity(frame.payload);
      } else if (packetName === 'entity_metadata') {
        metadata = lc.parseEntityMetadata(frame.payload);
      }
      
      rest = frame.rest;
    }
    
    if (spawn && typeof spawn.type === 'number') {
      const typeName = formatEntityType(spawn.type);
      if (typeName === 'item' || typeName === 'item_frame' || typeName === 'glow_item_frame') {
        let itemName = null;
        if (metadata && metadata.metadata) {
          const itemEntry = metadata.metadata.find(e => e.typeName === 'item_stack' || e.typeId === 7);
          if (itemEntry && itemEntry.value) {
            const slot = parseSlot(itemEntry.value);
            if (slot && slot.count > 0 && typeof slot.itemId === 'number') {
              const item = mcData.items[slot.itemId];
              if (item) {
                itemName = item.name;
              }
            }
          }
        }
        return { typeName, itemName, spawn, metadata };
      }
      return { typeName, spawn, metadata };
    }
  } catch (e) {
    // ignore
  }
  return null;
}

const app = express();
const PORT = process.env.PORT || 3000;

const CHUNK_PIXELS = 32;
const BIGCHUNK_CHUNKS_PER_SIDE = 16;
const BIGCHUNK_PIXELS = CHUNK_PIXELS * BIGCHUNK_CHUNKS_PER_SIDE;
const TILE_BIGCHUNKS_PER_SIDE = 16;
const TILE_PIXELS = BIGCHUNK_PIXELS;

// Map of dimension -> Set of "bigX,bigZ" strings, where bigX/bigZ are chunk-coordinate origins
const dirtyBigchunks = new Map();

// Map of dimension -> Set of "tileBigX,tileBigZ" strings, where tileBigX/tileBigZ are bigchunk-origin chunk coordinates
const dirtyTiles = new Map();

// SSE clients set
const sseClients = new Set();

function broadcastPlayerSse(data) {
  broadcastSse({ kind: 'player', ...data });
}

function parsePlayerMovementHeaders(req) {
  const numOrUndef = (v) => {
    if (v === undefined || v === null || v === '') return undefined;
    const n = Number(v);
    return Number.isFinite(n) ? n : undefined;
  };

  const x = numOrUndef(req.headers['x-player-x']);
  const y = numOrUndef(req.headers['x-player-y']);
  const z = numOrUndef(req.headers['x-player-z']);
  const yaw = numOrUndef(req.headers['x-player-yaw']);
  const pitch = numOrUndef(req.headers['x-player-pitch']);

  return { x, y, z, yaw, pitch };
}

// Helper for Java-style modulo
function mod16(n) {
  return n - Math.floor(n / 16) * 16;
}

function chunkL1(n) {
  return n - mod16(n);
}

function chunkL2(n) {
  const l1 = chunkL1(n);
  return l1 - mod16(l1);
}

function floorToMultiple(n, multiple) {
  return Math.floor(n / multiple) * multiple;
}

function markDirty(map, dimension, x, z) {
  if (!map.has(dimension)) {
    map.set(dimension, new Set());
  }
  map.get(dimension).add(`${x},${z}`);
}

function broadcastSse(data) {
  const eventData = JSON.stringify(data);
  for (const client of sseClients) {
    client.write(`data: ${eventData}\n\n`);
  }
}

function getPngPath(outputDir, dimension, chunkX, chunkZ) {
  const l1x = chunkL1(chunkX);
  const l2x = chunkL2(chunkX);
  const l1z = chunkL1(chunkZ);
  const l2z = chunkL2(chunkZ);
  return path.join(
    outputDir,
    dimension,
    String(l1x),
    String(l2x),
    String(l1z),
    String(l2z),
    `${chunkX}_${chunkZ}.png`
  );
}

// Enable CORS
app.use((req, res, next) => {
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Headers', '*');
  res.setHeader('Access-Control-Allow-Methods', '*');
  next();
});

// Enable raw body parsing for octet-stream up to 16MB (chunks can be large)
app.use(express.raw({ type: 'application/octet-stream', limit: '16mb' }));

// Serve static assets
app.use('/received_bigchunks', express.static(path.join(process.cwd(), 'received_bigchunks')));
app.use('/received_tiles', express.static(path.join(process.cwd(), 'received_tiles')));
app.use(express.static(path.join(process.cwd(), 'map-viewer/dist')));

app.post('/chunks', async (req, res) => {
  const filePath = req.headers['x-file-path'];
  const fileName = req.headers['x-file-name'];
  const dimension = req.headers['x-dimension'] || 'unknown';
  const packetDirection = req.headers['x-packet-direction'];

  if (!filePath) {
    console.error(chalk.red('[ERROR] Received POST request missing X-File-Path header'));
    return res.status(400).send('Missing X-File-Path header');
  }

  const payload = req.body;
  if (!payload || payload.length === 0) {
    console.error(chalk.red(`[ERROR] Received empty body for ${fileName}`));
    return res.status(400).send('Empty request body');
  }

  // Player movement stream: publish to SSE and do not persist to disk.
  if (packetDirection === 'C2S' && filePath.includes('/client/')) {
    const movement = parsePlayerMovementHeaders(req);
    console.log(
      chalk.green('[PLAYER]') +
      chalk.dim(' dim=') + chalk.cyan(dimension) +
      chalk.dim(' file=') + chalk.white(filePath) +
      chalk.dim(
        movement.x !== undefined && movement.y !== undefined && movement.z !== undefined
          ? ` (${movement.x.toFixed(2)},${movement.y.toFixed(2)},${movement.z.toFixed(2)})`
          : ''
      ) +
      chalk.dim(
        movement.yaw !== undefined && movement.pitch !== undefined
          ? ` yaw/pitch=${movement.yaw.toFixed(1)},${movement.pitch.toFixed(1)}`
          : ''
      )
    );

    broadcastPlayerSse({
      dimension,
      filePath,
      fileName,
      ...movement,
      timestamp: Date.now(),
    });

    return res.status(200).send('OK');
  }

  // Determine target path and format
  const isMapChunk = filePath.endsWith('_map.chunk');
  const relativePath = isMapChunk ? filePath.replace(/_map\.chunk$/, '.png') : filePath;
  const outputDir = 'received_chunks';
  const targetPath = path.join(outputDir, relativePath);

  try {
    // Ensure parent directories exist
    await fs.mkdir(path.dirname(targetPath), { recursive: true });

    if (isMapChunk) {
      // Decode chunk and save as top-down surface PNG
      const resPng = lc.writeMapChunkPng(payload, targetPath, dimension);
      if (!resPng.ok) {
        throw new Error(resPng.error || 'writeMapChunkPng failed');
      }

      console.log(
        chalk.green('[PNG]') +
        chalk.dim(' dim=') + chalk.cyan(dimension) +
        chalk.dim(' file=') + chalk.white(relativePath) +
        chalk.dim(` (saved PNG)`)
      );

      // Parse chunk coordinates
      let chunkX, chunkZ;
      const nameMatch = fileName.match(/^(-?\d+)_(-?\d+)/);
      if (nameMatch) {
        chunkX = parseInt(nameMatch[1], 10);
        chunkZ = parseInt(nameMatch[2], 10);
      } else {
        const coords = lc.peekMapChunkCoords(payload);
        if (coords && coords.ok) {
          chunkX = coords.x;
          chunkZ = coords.z;
        }
      }

      if (chunkX !== undefined && chunkZ !== undefined) {
        const bigX = floorToMultiple(chunkX, BIGCHUNK_CHUNKS_PER_SIDE);
        const bigZ = floorToMultiple(chunkZ, BIGCHUNK_CHUNKS_PER_SIDE);
        markDirty(dirtyBigchunks, dimension, bigX, bigZ);
      }
    } else {
      // Write raw payload to disk
      await fs.writeFile(targetPath, payload);

      let typeDetail = '';
      if (filePath.endsWith('.spawn_entity.wire')) {
        const res = getSpawnEntityItemName(payload);
        if (res) {
          if (res.typeName === 'item' || res.typeName === 'item_frame' || res.typeName === 'glow_item_frame') {
            if (res.itemName) {
              typeDetail = chalk.dim(' type=') + chalk.yellow(`${res.typeName} (${res.itemName})`);
            } else {
              if (res.typeName === 'item') {
                typeDetail = chalk.dim(' type=') + chalk.red('item (UNKNOWN)');
                console.warn(
                  chalk.bold.yellow(`[WARNING] Failed to detect item name for spawn_entity in ${relativePath}!`)
                );
                if (res.spawn) {
                  console.warn(
                    chalk.yellow(`  Entity ID: ${res.spawn.entityId}, Position: (${res.spawn.x.toFixed(2)}, ${res.spawn.y.toFixed(2)}, ${res.spawn.z.toFixed(2)})`)
                  );
                }
                console.warn(
                  chalk.yellow(`  Has metadata packet: ${!!res.metadata}`)
                );
                console.warn(
                  chalk.yellow(`  Full packet payload (${payload.length} bytes, hex): ${payload.toString('hex')}`)
                );
              } else {
                typeDetail = chalk.dim(' type=') + chalk.yellow(`${res.typeName} (empty)`);
              }
            }
          } else {
            typeDetail = chalk.dim(' type=') + chalk.yellow(res.typeName);
          }
        } else {
          console.warn(
            chalk.bold.red(`[WARNING] Failed to parse spawn_entity file: ${relativePath}`)
          );
          console.warn(
            chalk.yellow(`  Full packet payload (${payload.length} bytes, hex): ${payload.toString('hex')}`)
          );
        }
      }

      console.log(
        chalk.green('[CHUNK]') +
        chalk.dim(' dim=') + chalk.cyan(dimension) +
        chalk.dim(' file=') + chalk.white(relativePath) +
        typeDetail +
        chalk.dim(` (${payload.length} bytes)`)
      );
    }

    res.status(200).send('OK');
  } catch (error) {
    console.error(chalk.red(`[ERROR] Failed to write chunk ${filePath}: ${error.message}`));
    res.status(500).send('Internal Server Error');
  }
});

async function listImageTiles(rootDir, urlPrefix, kind) {
  const results = [];
  try {
    const dimensions = await fs.readdir(rootDir);
    for (const dim of dimensions) {
      const dimPath = path.join(rootDir, dim);
      const stat = await fs.stat(dimPath);
      if (!stat.isDirectory()) continue;

      const files = await fs.readdir(dimPath);
      for (const file of files) {
        const match = file.match(/^x(-?\d+)_z(-?\d+)\.jpg$/);
        if (!match) continue;

        const worldX = parseInt(match[1], 10);
        const worldZ = parseInt(match[2], 10);
        results.push({
          kind,
          dimension: dim,
          bigX: worldX / 16,
          bigZ: worldZ / 16,
          worldX,
          worldZ,
          url: `${urlPrefix}/${dim}/${file}`
        });
      }
    }
  } catch (e) {
    // Ignore if the output directory is empty or doesn't exist
  }
  return results;
}

// API endpoint to list all bigchunks
app.get('/api/bigchunks', async (req, res) => {
  res.json(await listImageTiles('received_bigchunks', '/received_bigchunks', 'bigchunk'));
});

// API endpoint to list all bigger tiles
app.get('/api/tiles', async (req, res) => {
  res.json(await listImageTiles('received_tiles', '/received_tiles', 'tile'));
});

// SSE endpoint to stream new bigchunk and tile updates
app.get('/api/sse', (req, res) => {
  res.setHeader('Content-Type', 'text/event-stream');
  res.setHeader('Cache-Control', 'no-cache');
  res.setHeader('Connection', 'keep-alive');
  res.flushHeaders();

  sseClients.add(res);

  req.on('close', () => {
    sseClients.delete(res);
  });
});

async function processDirtyBigchunks() {
  if (dirtyBigchunks.size === 0) return;

  const outputDir = 'received_chunks';
  const bigchunksDir = 'received_bigchunks';

  for (const [dimension, bigchunkKeys] of dirtyBigchunks.entries()) {
    if (bigchunkKeys.size === 0) continue;

    // Copy keys and clear set to avoid race conditions during async processing
    const keysToProcess = Array.from(bigchunkKeys);
    bigchunkKeys.clear();

    for (const key of keysToProcess) {
      const [bigX, bigZ] = key.split(',').map(Number);
      const compositeList = [];
      const mask = Array(16).fill(0);
      const grid = Array.from({ length: 16 }, () => Array(16).fill(false));

      // Check all 16x16 normal chunks within this bigchunk
      for (let localZ = 0; localZ < BIGCHUNK_CHUNKS_PER_SIDE; localZ++) {
        for (let localX = 0; localX < BIGCHUNK_CHUNKS_PER_SIDE; localX++) {
          const chunkX = bigX + localX;
          const chunkZ = bigZ + localZ;
          const pngPath = getPngPath(outputDir, dimension, chunkX, chunkZ);

          try {
            await fs.stat(pngPath);
            compositeList.push({
              input: pngPath,
              top: localZ * CHUNK_PIXELS,
              left: localX * CHUNK_PIXELS
            });
            grid[localZ][localX] = true;
            mask[localZ] |= (1 << localX);
          } catch (e) {
            // File does not exist, skip it
          }
        }
      }

      if (compositeList.length === 0) continue;

      const targetDir = path.join(bigchunksDir, dimension);
      const targetFileName = `x${bigX * 16}_z${bigZ * 16}.jpg`;
      const targetPath = path.join(targetDir, targetFileName);
      const jsonFileName = `x${bigX * 16}_z${bigZ * 16}.json`;
      const jsonPath = path.join(targetDir, jsonFileName);

      try {
        await fs.mkdir(targetDir, { recursive: true });

        // Composite all chunks in the 16x16 grid onto a blank black canvas and save
        await sharp({
          create: {
            width: BIGCHUNK_PIXELS,
            height: BIGCHUNK_PIXELS,
            channels: 4,
            background: { r: 0, g: 0, b: 0, alpha: 1 } // Black background
          }
        })
          .composite(compositeList)
          .jpeg({ quality: 80 })
          .toFile(targetPath);

        // Write bitmask metadata file
        await fs.writeFile(jsonPath, JSON.stringify({
          kind: 'bigchunk',
          dimension,
          worldX: bigX * 16,
          worldZ: bigZ * 16,
          bigX,
          bigZ,
          mask,
          grid
        }, null, 2));

        console.log(
          chalk.blue('[BIGCHUNK]') +
          chalk.dim(' dim=') + chalk.cyan(dimension) +
          chalk.dim(' file=') + chalk.white(`${dimension}/${targetFileName}`) +
          chalk.dim(` (stitched ${compositeList.length} chunks)`)
        );

        const worldX = bigX * 16;
        const worldZ = bigZ * 16;
        broadcastSse({
          kind: 'bigchunk',
          dimension,
          bigX,
          bigZ,
          worldX,
          worldZ,
          url: `/received_bigchunks/${dimension}/${targetFileName}?t=${Date.now()}` // add cachebuster
        });

        const tileBigX = floorToMultiple(bigX, BIGCHUNK_CHUNKS_PER_SIDE * TILE_BIGCHUNKS_PER_SIDE);
        const tileBigZ = floorToMultiple(bigZ, BIGCHUNK_CHUNKS_PER_SIDE * TILE_BIGCHUNKS_PER_SIDE);
        markDirty(dirtyTiles, dimension, tileBigX, tileBigZ);
      } catch (error) {
        console.error(
          chalk.red(`[ERROR] Failed to stitch bigchunk ${dimension}/${targetFileName}: ${error.message}`)
        );
      }
    }
  }
}

async function processDirtyTiles() {
  if (dirtyTiles.size === 0) return;

  const bigchunksDir = 'received_bigchunks';
  const tilesDir = 'received_tiles';

  for (const [dimension, tileKeys] of dirtyTiles.entries()) {
    if (tileKeys.size === 0) continue;

    // Copy keys and clear set to avoid race conditions during async processing
    const keysToProcess = Array.from(tileKeys);
    tileKeys.clear();

    for (const key of keysToProcess) {
      const [tileBigX, tileBigZ] = key.split(',').map(Number);
      const compositeList = [];
      const mask = Array(16).fill(0);
      const grid = Array.from({ length: 16 }, () => Array(16).fill(false));

      // Build a 16x16 image from already stitched bigchunks. Each input bigchunk is resized
      // down to 32x32 so the resulting bigger tile is still 512x512 for easy map viewing.
      for (let localZ = 0; localZ < TILE_BIGCHUNKS_PER_SIDE; localZ++) {
        for (let localX = 0; localX < TILE_BIGCHUNKS_PER_SIDE; localX++) {
          const bigX = tileBigX + localX * BIGCHUNK_CHUNKS_PER_SIDE;
          const bigZ = tileBigZ + localZ * BIGCHUNK_CHUNKS_PER_SIDE;
          const bigchunkFile = `x${bigX * 16}_z${bigZ * 16}.jpg`;
          const bigchunkPath = path.join(bigchunksDir, dimension, bigchunkFile);

          try {
            await fs.stat(bigchunkPath);
            const resizedInput = await sharp(bigchunkPath)
              .resize(CHUNK_PIXELS, CHUNK_PIXELS)
              .jpeg({ quality: 80 })
              .toBuffer();
            compositeList.push({
              input: resizedInput,
              top: localZ * CHUNK_PIXELS,
              left: localX * CHUNK_PIXELS
            });
            grid[localZ][localX] = true;
            mask[localZ] |= (1 << localX);
          } catch (e) {
            // Bigchunk does not exist yet, skip it
          }
        }
      }

      if (compositeList.length === 0) continue;

      const targetDir = path.join(tilesDir, dimension);
      const targetFileName = `x${tileBigX * 16}_z${tileBigZ * 16}.jpg`;
      const targetPath = path.join(targetDir, targetFileName);
      const jsonFileName = `x${tileBigX * 16}_z${tileBigZ * 16}.json`;
      const jsonPath = path.join(targetDir, jsonFileName);

      try {
        await fs.mkdir(targetDir, { recursive: true });

        await sharp({
          create: {
            width: TILE_PIXELS,
            height: TILE_PIXELS,
            channels: 4,
            background: { r: 0, g: 0, b: 0, alpha: 1 } // Black background
          }
        })
          .composite(compositeList)
          .jpeg({ quality: 80 })
          .toFile(targetPath);

        // Write bitmask metadata file
        await fs.writeFile(jsonPath, JSON.stringify({
          kind: 'tile',
          dimension,
          worldX: tileBigX * 16,
          worldZ: tileBigZ * 16,
          bigX: tileBigX,
          bigZ: tileBigZ,
          mask,
          grid
        }, null, 2));

        console.log(
          chalk.magenta('[TILE]') +
          chalk.dim(' dim=') + chalk.cyan(dimension) +
          chalk.dim(' file=') + chalk.white(`${dimension}/${targetFileName}`) +
          chalk.dim(` (stitched ${compositeList.length} bigchunks)`)
        );

        broadcastSse({
          kind: 'tile',
          dimension,
          bigX: tileBigX,
          bigZ: tileBigZ,
          worldX: tileBigX * 16,
          worldZ: tileBigZ * 16,
          url: `/received_tiles/${dimension}/${targetFileName}?t=${Date.now()}` // add cachebuster
        });
      } catch (error) {
        console.error(
          chalk.red(`[ERROR] Failed to stitch tile ${dimension}/${targetFileName}: ${error.message}`)
        );
      }
    }
  }
}

async function processDirtyImages() {
  await processDirtyBigchunks();
  await processDirtyTiles();
}

// Check for dirty bigchunks and tiles every 5 seconds
setInterval(processDirtyImages, 5000);

app.listen(PORT, () => {
  console.log(chalk.bold.green('=== Express Chunk Receiver Demo ==='));
  console.log(chalk.cyan(`Listening on port ${PORT}`));
  console.log(chalk.dim('POST chunks to: ') + chalk.yellow(`http://localhost:${PORT}/chunks`));
  console.log(chalk.dim('Saving streamed chunks under: ') + chalk.white('./received_chunks/'));
  console.log(chalk.bold.green('==================================='));
});
