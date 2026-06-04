import crypto from 'node:crypto';
import { readVarInt, readString } from './wire.js';

export { readVarInt } from './wire.js';

export function offlineUUID(name) {
  const hash = crypto.createHash('md5').update(`OfflinePlayer:${name}`).digest();
  hash[6] = (hash[6] & 0x0f) | 0x30;
  hash[8] = (hash[8] & 0x3f) | 0x80;
  return hash;
}

export function readI64BE(buf, off) {
  if (off + 8 > buf.length) return null;
  return { value: buf.readBigInt64BE(off), next: off + 8 };
}

export function readF64BE(buf, off) {
  if (off + 8 > buf.length) return null;
  return { value: buf.readDoubleBE(off), next: off + 8 };
}

/** ClientboundSetTimePacket: gameTime i64 BE, dayTime i64 BE, tickDayTime bool. */
export function parseUpdateTime(payload) {
  if (payload.length < 17) return null;
  return {
    gameTime: payload.readBigInt64BE(0),
    dayTime: payload.readBigInt64BE(8),
    tickDayTime: payload[16] !== 0,
  };
}

/** ClientboundGameEventPacket: u8 event id, f32 value. */
export function parseGameEvent(payload) {
  if (payload.length < 5) return null;
  return {
    event: payload[0],
    value: payload.readFloatBE(1),
  };
}

/** ClientboundTickingStatePacket: tickRate f32 BE, isFrozen bool. */
export function parseSetTickingState(payload) {
  if (payload.length < 5) return null;
  return {
    tickRate: payload.readFloatBE(0),
    isFrozen: payload[4] !== 0,
  };
}

/** ClientboundLoginPacket: viewDistance is the 2nd varint after maxPlayers. */
export function parseLoginViewDistance(payload) {
  let o = 0;
  if (o + 5 > payload.length) return null;
  o += 4; // entityId i32 BE
  o += 1; // hardcore bool
  const dimCount = readVarInt(payload, o);
  if (!dimCount) return null;
  o = dimCount.next;
  for (let i = 0; i < dimCount.value; i++) {
    const s = readString(payload, o);
    if (!s) return null;
    o = s.next;
  }
  const maxPlayers = readVarInt(payload, o);
  if (!maxPlayers) return null;
  const viewDistance = readVarInt(payload, maxPlayers.next);
  if (!viewDistance) return null;
  return viewDistance.value;
}

/** ClientboundSetChunkCacheCenter: chunkX i32 BE, chunkZ i32 BE. */
export function parseUpdateViewPosition(payload) {
  if (payload.length < 8) return null;
  return { chunkX: payload.readInt32BE(0), chunkZ: payload.readInt32BE(4) };
}

/** Matches libchunk mc_static_chunk_radius_from_view. */
export function chunkRadiusFromView(viewDistance) {
  if (viewDistance <= 0) return 0;
  return viewDistance - 1;
}

export function expectedChunkGridCount(viewDistance) {
  const r = chunkRadiusFromView(viewDistance);
  const side = 2 * r + 1;
  return side * side;
}

export function parsePosition(payload) {
  let o = 0;
  const tid = readVarInt(payload, o);
  if (!tid) return null;
  o = tid.next;
  const x = readF64BE(payload, o);
  if (!x) return null;
  o = x.next;
  const y = readF64BE(payload, o);
  if (!y) return null;
  o = y.next;
  const z = readF64BE(payload, o);
  if (!z) return null;
  o = z.next;
  if (o + 24 > payload.length) return null;
  o += 24;
  if (o + 8 > payload.length) return null;
  const yaw = payload.readFloatBE(o);
  const pitch = payload.readFloatBE(o + 4);
  return { teleportId: tid.value, x: x.value, y: y.value, z: z.value, yaw, pitch };
}
