import crypto from 'node:crypto';
import { readVarInt } from './wire.js';

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

function readF64BE(buf, off) {
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


/** ClientboundSetHealthPacket: f32 health BE, varint food, f32 saturation BE. */
export function parseUpdateHealth(payload) {
  if (payload.length < 5) return null;
  const health = payload.readFloatBE(0);
  const food = readVarInt(payload, 4);
  if (!food) return null;
  if (food.next + 4 > payload.length) return null;
  const saturation = payload.readFloatBE(food.next);
  return { health, food: food.value, saturation };
}

/** ClientboundSetChunkCacheCenter: chunkX i32 BE, chunkZ i32 BE. */
/** ClientboundSetChunkCacheCenterPacket: chunkX, chunkZ as VarInts. */
export function parseUpdateViewPosition(payload) {
  const x = readVarInt(payload, 0);
  if (!x) return null;
  const z = readVarInt(payload, x.next);
  if (!z) return null;
  return { chunkX: x.value, chunkZ: z.value };
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
