import chalk from 'chalk';
import { createRequire } from 'node:module';
import { readVarInt } from './wire.js';
import { formatEntityType } from './entityTypeNames.js';

const require = createRequire(import.meta.url);
const lc = require('../libchunk/js/index.js');

/** @typedef {{ id: number, type: number, uuid: string, x: number, y: number, z: number, posKnown: boolean, vel: { x: number, y: number, z: number }, rot: { pitch: number, yaw: number, headPitch: number, headYaw?: number }, data: number, spawnTime: number, metadata?: Record<number, any>, equipment?: Record<number, any> }} TrackedEntity */

const SPAWN_RE =
  /^spawn_entity\{id=(-?\d+),uuid=([^,]+),type=(-?\d+),pos=\(([-\d.]+),([-\d.]+),([-\d.]+)\),vel=\(([-\d.]+),([-\d.]+),([-\d.]+)\),rot=\((-?\d+),(-?\d+),(-?\d+)\),data=(-?\d+)\}$/;

const SYNC_POS_RE =
  /^sync_entity_position\{id=(-?\d+),pos=\(([-\d.]+),([-\d.]+),([-\d.]+)\),/;

class Buf {
  /** @param {Buffer} buf */
  constructor(buf) {
    this.buf = buf;
    this.off = 0;
  }

  need(n) {
    return this.off + n <= this.buf.length;
  }

  readVarInt() {
    const r = readVarInt(this.buf, this.off);
    if (!r) return null;
    this.off = r.next;
    return r.value;
  }

  readI16LE() {
    if (!this.need(2)) return null;
    const v = this.buf.readInt16LE(this.off);
    this.off += 2;
    return v;
  }

  readI8() {
    if (!this.need(1)) return null;
    return this.buf.readInt8(this.off++);
  }

  readF64LE() {
    if (!this.need(8)) return null;
    const v = this.buf.readDoubleLE(this.off);
    this.off += 8;
    return v;
  }

  readBool() {
    if (!this.need(1)) return null;
    return this.buf.readUInt8(this.off++) !== 0;
  }

  readI32LE() {
    if (!this.need(4)) return null;
    const v = this.buf.readInt32LE(this.off);
    this.off += 4;
    return v;
  }
}

/** @param {number} x @param {number} y @param {number} z */
function isValidCoord(x, y, z) {
  return (
    Number.isFinite(x) &&
    Number.isFinite(y) &&
    Number.isFinite(z) &&
    Math.abs(x) < 3e7 &&
    Math.abs(y) < 3e7 &&
    Math.abs(z) < 3e7
  );
}

export function createEntityTracker() {
  /** @type {Map<number, TrackedEntity>} */
  const entities = new Map();

  function get(id) {
    return entities.get(id);
  }

  function set(id, ent) {
    entities.set(id, ent);
  }

  function remove(id) {
    entities.delete(id);
  }

  /** @param {TrackedEntity} e */
  function updatePos(e, x, y, z) {
    if (!isValidCoord(x, y, z)) return;
    e.x = x;
    e.y = y;
    e.z = z;
    e.posKnown = true;
  }

  function isFinitePos(e) {
    return e.posKnown && isValidCoord(e.x, e.y, e.z);
  }

  /** @param {TrackedEntity} e */
  function formatEntity(e) {
    const label = formatEntityType(e.type);
    if (!isFinitePos(e)) return label;
    return `${label} pos=(${e.x.toFixed(3)},${e.y.toFixed(3)},${e.z.toFixed(3)})`;
  }

  /** @param {number[]} ids @returns {string | null} */
  function suffixForIds(ids) {
    const parts = [];
    for (const id of ids) {
      const e = get(id);
      if (!e) continue;
      parts.push(formatEntity(e));
    }
    if (parts.length === 0) return null;
    return chalk.dim(` entity[${parts.join('; ')}]`);
  }

  function touchIds(payload) {
    const id = readFirstEntityId(payload);
    return id != null ? suffixForIds([id]) : null;
  }

  function readFirstEntityId(payload) {
    const id = new Buf(payload).readVarInt();
    return id == null ? null : id;
  }

  function handleSpawn(payload) {
    const r = lc.decodePayload('spawn_entity', payload);
    if (!r.ok || !r.text) return null;
    const m = r.text.match(SPAWN_RE);
    if (!m) return null;
    const id = Number(m[1]);
    const ent = {
      id,
      type: Number(m[3]),
      uuid: m[2],
      x: 0,
      y: 0,
      z: 0,
      posKnown: false,
      vel: { x: Number(m[7]), y: Number(m[8]), z: Number(m[9]) },
      rot: { pitch: Number(m[10]), yaw: Number(m[11]), headPitch: Number(m[12]) },
      data: Number(m[13]),
      spawnTime: Date.now(),
    };
    updatePos(ent, Number(m[4]), Number(m[5]), Number(m[6]));
    set(id, ent);
    return null;
  }

  function handleDestroy(payload) {
    const b = new Buf(payload);
    const count = b.readVarInt();
    if (count == null || count < 0) return null;
    for (let i = 0; i < count; i++) {
      const id = b.readVarInt();
      if (id == null) break;
      remove(id);
    }
    return null;
  }

  function applyDelta(id, dx, dy, dz) {
    const e = get(id);
    if (!e || !e.posKnown) return;
    updatePos(e, e.x + dx / 4096, e.y + dy / 4096, e.z + dz / 4096);
  }

  function handleRelMove(payload) {
    const p = lc.parseRelEntityMove(payload);
    if (!p) return null;
    applyDelta(p.entityId, p.dx, p.dy, p.dz);
    return suffixForIds([p.entityId]);
  }

  function handleMoveLook(payload) {
    const p = lc.parseEntityMoveLook(payload);
    if (!p) return null;
    applyDelta(p.entityId, p.dx, p.dy, p.dz);
    const e = get(p.entityId);
    if (e) {
      e.rot.yaw = p.yaw;
      e.rot.pitch = p.pitch;
    }
    return suffixForIds([p.entityId]);
  }

  function handleSyncPosition(payload) {
    const p = lc.parseSyncEntityPosition(payload);
    if (!p) return null;
    const e = get(p.entityId);
    if (e) {
      updatePos(e, p.x, p.y, p.z);
      e.rot.yaw = p.yaw;
      e.rot.pitch = p.pitch;
    }
    return suffixForIds([p.entityId]);
  }

  function handleTeleport(payload) {
    const r = lc.decodePayload('entity_teleport', payload);
    if (r.ok && r.text) {
      const m = r.text.match(
        /^entity_teleport\{entityId=(-?\d+),pos=\(([-\d.]+),([-\d.]+),([-\d.]+)\)/,
      );
      if (m) {
        const id = Number(m[1]);
        const e = get(id);
        if (e) updatePos(e, Number(m[2]), Number(m[3]), Number(m[4]));
        return suffixForIds([id]);
      }
    }
    const b = new Buf(payload);
    const id = b.readVarInt();
    const x = b.readF64LE();
    const y = b.readF64LE();
    const z = b.readF64LE();
    if (id == null || x == null || y == null || z == null) return null;
    const e = get(id);
    if (e) updatePos(e, x, y, z);
    return suffixForIds([id]);
  }

  function handleVelocity(payload) {
    const p = lc.parseEntityVelocity(payload);
    if (!p) return null;
    const e = get(p.entityId);
    if (e) {
      e.vel = p.velocity;
    }
    return suffixForIds([p.entityId]);
  }

  function handleHeadRotation(payload) {
    const p = lc.parseEntityHeadRotation(payload);
    if (!p) return null;
    const e = get(p.entityId);
    if (e) {
      e.rot.headYaw = p.headYaw;
    }
    return suffixForIds([p.entityId]);
  }

  function handleLook(payload) {
    const p = lc.parseEntityLook(payload);
    if (!p) return null;
    const e = get(p.entityId);
    if (e) {
      e.rot.yaw = p.yaw;
      e.rot.pitch = p.pitch;
    }
    return suffixForIds([p.entityId]);
  }

  function handleMetadata(payload) {
    const p = lc.parseEntityMetadata(payload);
    if (!p) return null;
    const e = get(p.entityId);
    if (e) {
      if (!e.metadata) e.metadata = {};
      for (const entry of p.metadata) {
        e.metadata[entry.key] = entry.value;
      }
    }
    return suffixForIds([p.entityId]);
  }

  function handleEquipment(payload) {
    const p = lc.parseEntityEquipment(payload);
    if (!p) return null;
    const e = get(p.entityId);
    if (e) {
      if (!e.equipment) e.equipment = {};
      for (const eq of p.equipments) {
        e.equipment[eq.slot] = { itemId: eq.itemId, itemCount: eq.itemCount };
      }
    }
    return suffixForIds([p.entityId]);
  }

  function handleEntityStatus(payload) {
    const b = new Buf(payload);
    const id = b.readI32LE();
    if (id == null) return null;
    return suffixForIds([id]);
  }

  function handleSetPassengers(payload) {
    const b = new Buf(payload);
    const vehicle = b.readVarInt();
    const count = b.readVarInt();
    if (vehicle == null || count == null || count < 0) return null;
    const ids = [vehicle];
    for (let i = 0; i < count; i++) {
      const p = b.readVarInt();
      if (p == null) break;
      ids.push(p);
    }
    return suffixForIds(ids);
  }

  function handleAttachEntity(payload) {
    const b = new Buf(payload);
    const attached = b.readI32LE();
    const holding = b.readI32LE();
    if (attached == null || holding == null) return null;
    return suffixForIds([attached, holding]);
  }

  /** @type {Record<string, (payload: Buffer) => string | null>} */
  const handlers = {
    spawn_entity: handleSpawn,
    entity_destroy: handleDestroy,
    rel_entity_move: handleRelMove,
    entity_move_look: handleMoveLook,
    sync_entity_position: handleSyncPosition,
    entity_teleport: handleTeleport,
    entity_velocity: handleVelocity,
    entity_metadata: handleMetadata,
    entity_update_attributes: touchIds,
    entity_equipment: handleEquipment,
    entity_head_rotation: handleHeadRotation,
    entity_look: handleLook,
    entity_effect: touchIds,
    remove_entity_effect: touchIds,
    entity_status: handleEntityStatus,
    set_passengers: handleSetPassengers,
    attach_entity: handleAttachEntity,
  };

  function noteS2c(packetName, payload) {
    if (!packetName || !Buffer.isBuffer(payload)) return null;
    const fn = handlers[packetName];
    if (!fn) return null;
    try {
      return fn(payload);
    } catch {
      return null;
    }
  }

  return { noteS2c, get, entities };
}
