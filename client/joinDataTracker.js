import { createRequire } from 'node:module';
import { s2cPacketName } from './packetNames.js';
import { PLAY } from './constants.js';

const require = createRequire(import.meta.url);
const lc = require('../libchunk/js/index.js');

const SIMPLE_KEYS = {
  update_time: 'update_time',
  advancements: 'advancements',
  experience: 'experience',
  update_health: 'update_health',
  held_item_slot: 'held_item_slot',
  abilities: 'abilities',
  difficulty: 'difficulty',
  declare_commands: 'declare_commands',
  update_view_position: 'update_view_position',
};

const ENTITY_PARSERS = {
  entity_metadata: (payload) => lc.parseEntityMetadata(payload),
  entity_update_attributes: (payload) => lc.parseEntityUpdateAttributes(payload),
  entity_status: (payload) => lc.parseEntityStatus(payload),
  entity_velocity: (payload) => lc.parseEntityVelocity(payload),
  entity_equipment: (payload) => lc.parseEntityEquipment(payload),
  entity_effect: (payload) => lc.parseEntityEffect(payload),
  remove_entity_effect: (payload) => lc.parseRemoveEntityEffect(payload),
};

function writeVarInt(value) {
  const buf = [];
  let temp = value;
  while (true) {
    if ((temp & ~0x7F) === 0) {
      buf.push(temp);
      break;
    }
    buf.push((temp & 0x7F) | 0x80);
    temp >>>= 7;
  }
  return Buffer.from(buf);
}

function serializePositionPacket(teleportId, x, y, z, yaw, pitch, flags = 0) {
  const varintBuf = writeVarInt(teleportId);
  const fieldsBuf = Buffer.alloc(8 * 6 + 4 * 2 + 4);
  fieldsBuf.writeDoubleBE(x, 0);
  fieldsBuf.writeDoubleBE(y, 8);
  fieldsBuf.writeDoubleBE(z, 16);
  fieldsBuf.writeDoubleBE(0.0, 24); // dx
  fieldsBuf.writeDoubleBE(0.0, 32); // dy
  fieldsBuf.writeDoubleBE(0.0, 40); // dz
  fieldsBuf.writeFloatBE(yaw, 48);
  fieldsBuf.writeFloatBE(pitch, 52);
  fieldsBuf.writeInt32BE(flags, 56);
  return Buffer.concat([varintBuf, fieldsBuf]);
}

function serializeUpdateViewPosition(chunkX, chunkZ) {
  return Buffer.concat([writeVarInt(chunkX), writeVarInt(chunkZ)]);
}

export function createJoinDataTracker() {
  let ownEntityId = null;
  let teleportId = 1;
  let dimensionName = null;
  let loginData = null; // Store full login data for reconnection
  const currentPos = { x: 0, y: 0, z: 0, yaw: 0, pitch: 0 };
  // Map of key -> { id, payload }
  const tracked = new Map();

  function reset() {
    ownEntityId = null;
    teleportId = 1;
    currentPos.x = 0;
    currentPos.y = 0;
    currentPos.z = 0;
    currentPos.yaw = 0;
    currentPos.pitch = 0;
    tracked.clear();
  }

  function getOwnEntityId() {
    return ownEntityId;
  }

  function setOwnEntityId(id) {
    ownEntityId = id;
  }

  function updateCapturedPosition() {
    const newPayload = serializePositionPacket(
      teleportId,
      currentPos.x,
      currentPos.y,
      currentPos.z,
      currentPos.yaw,
      currentPos.pitch,
      0
    );
    tracked.set('position', { id: PLAY.POSITION, payload: newPayload });

    const cx = Math.floor(currentPos.x / 16);
    const cz = Math.floor(currentPos.z / 16);
    const uvpPayload = serializeUpdateViewPosition(cx, cz);
    tracked.set('update_view_position', { id: PLAY.UPDATE_VIEW_POSITION, payload: uvpPayload });
  }

  function noteS2c(id, payload) {
    if (!Buffer.isBuffer(payload)) return;

    // Determine packet name using the play phase context
    const packetName = s2cPacketName('play', id);
    if (!packetName) return;

    // 1. Check if it's a simple keyed packet
    if (SIMPLE_KEYS[packetName]) {
      tracked.set(SIMPLE_KEYS[packetName], { id, payload });
      return;
    }

    // 2. Check if it's login to extract own entityId and dimension
    if (packetName === 'login') {
      try {
        const login = lc.parsePlayLogin(payload);
        if (login) {
          ownEntityId = login.entityId;
          if (login.dimensionName) {
            dimensionName = login.dimensionName;
          }
          loginData = login; // Store full login data for reconnection
        }
      } catch (e) {
        // ignore parsing error
      }
      return;
    }

    // 2b. Check if it's respawn to update dimension
    if (packetName === 'respawn') {
      try {
        const respawn = lc.parseRespawn(payload);
        if (respawn && respawn.dimensionName) {
          dimensionName = respawn.dimensionName;
          if (loginData) {
            loginData.dimensionName = respawn.dimensionName;
          }
        }
      } catch (e) {
        // ignore parsing error
      }
      tracked.set('respawn', { id, payload });
      return;
    }


    // 3. Check if it's game_state_change
    if (packetName === 'game_state_change') {
      try {
        const ev = lc.parseGameEvent(payload);
        if (ev) {
          tracked.set(`game_state_change_${ev.event}`, { id, payload });
        }
      } catch (e) {
        // ignore parsing error
      }
      return;
    }

    // 4. Check if it's position S2C
    if (packetName === 'position') {
      try {
        const pos = lc.parsePosition(payload);
        if (pos) {
          teleportId = pos.teleportId;
          const flags = pos.flags || 0;
          if ((flags & 0x01) === 0) currentPos.x = pos.x;
          else currentPos.x += pos.x;

          if ((flags & 0x02) === 0) currentPos.y = pos.y;
          else currentPos.y += pos.y;

          if ((flags & 0x04) === 0) currentPos.z = pos.z;
          else currentPos.z += pos.z;

          if ((flags & 0x08) === 0) currentPos.yaw = pos.yaw;
          else currentPos.yaw += pos.yaw;

          if ((flags & 0x10) === 0) currentPos.pitch = pos.pitch;
          else currentPos.pitch += pos.pitch;

          updateCapturedPosition();
        }
      } catch (e) {
        // ignore parsing error
      }
      return;
    }

    // 5. Check if it's an entity packet for our own entityId
    if (ownEntityId !== null && ENTITY_PARSERS[packetName]) {
      try {
        const parser = ENTITY_PARSERS[packetName];
        const parsed = parser(payload);
        if (parsed && parsed.entityId === ownEntityId) {
          // For entity_effect/remove_entity_effect, we can key by effectId to support multiple effects
          if (packetName === 'entity_effect') {
            tracked.set(`entity_effect_${parsed.effectId}`, { id, payload });
          } else if (packetName === 'remove_entity_effect') {
            // Remove the active effect from tracked map
            tracked.delete(`entity_effect_${parsed.effectId}`);
            // Also store the remove packet itself so the client knows it was removed
            tracked.set(`remove_entity_effect_${parsed.effectId}`, { id, payload });
          } else {
            tracked.set(packetName, { id, payload });
          }
        }
      } catch (e) {
        // ignore parsing error
      }
    }
  }

  function noteC2s(id, payload) {
    if (!Buffer.isBuffer(payload)) return;

    if (id === PLAY.C2S_POSITION) {
      if (payload.length >= 24) {
        currentPos.x = payload.readDoubleBE(0);
        currentPos.y = payload.readDoubleBE(8);
        currentPos.z = payload.readDoubleBE(16);
        updateCapturedPosition();
      }
    } else if (id === PLAY.C2S_POSITION_LOOK) {
      if (payload.length >= 32) {
        currentPos.x = payload.readDoubleBE(0);
        currentPos.y = payload.readDoubleBE(8);
        currentPos.z = payload.readDoubleBE(16);
        currentPos.yaw = payload.readFloatBE(24);
        currentPos.pitch = payload.readFloatBE(28);
        updateCapturedPosition();
      }
    } else if (id === PLAY.C2S_MOVE_ROT) {
      if (payload.length >= 8) {
        currentPos.yaw = payload.readFloatBE(0);
        currentPos.pitch = payload.readFloatBE(4);
        updateCapturedPosition();
      }
    }
  }

  /**
   * Helper to determine the tracking key for a given packet from the original playJoin array.
   * This is used to replace the correct packet in-place during merge.
   */
  function getPacketKey(id, payload) {
    const packetName = s2cPacketName('play', id);
    if (!packetName) return null;

    if (SIMPLE_KEYS[packetName]) {
      return SIMPLE_KEYS[packetName];
    }

    if (packetName === 'game_state_change') {
      try {
        const ev = lc.parseGameEvent(payload);
        if (ev) return `game_state_change_${ev.event}`;
      } catch {}
    }

    if (packetName === 'position') {
      return 'position';
    }

    if (packetName === 'respawn') {
      return 'respawn';
    }

    if (ownEntityId !== null && ENTITY_PARSERS[packetName]) {
      try {
        const parser = ENTITY_PARSERS[packetName];
        const parsed = parser(payload);
        if (parsed && parsed.entityId === ownEntityId) {
          if (packetName === 'entity_effect') {
            return `entity_effect_${parsed.effectId}`;
          }
          if (packetName === 'remove_entity_effect') {
            return `remove_entity_effect_${parsed.effectId}`;
          }
          return packetName;
        }
      } catch {}
    }

    return null;
  }

  /**
   * Merges the tracked packets with the original playJoin packet list.
   * Replaces existing packets in-place and appends new ones.
   * @param {{ id: number, payload: Buffer }[]} originalPlayJoin
   * @returns {{ id: number, payload: Buffer }[]}
   */
  function mergeWith(originalPlayJoin) {
    const result = [];
    const usedKeys = new Set();

    for (const pkt of originalPlayJoin) {
      const key = getPacketKey(pkt.id, pkt.payload);
      if (key && tracked.has(key)) {
        result.push(tracked.get(key));
        usedKeys.add(key);
      } else {
        result.push(pkt);
      }
    }

    // Append any tracked packets that were not in the original list
    for (const [key, pkt] of tracked.entries()) {
      if (!usedKeys.has(key)) {
        result.push(pkt);
      }
    }

    return result;
  }

  function getDimensionName() {
    return dimensionName;
  }

  function setDimensionName(name) {
    dimensionName = name;
  }

  function getLoginData() {
    return loginData;
  }

  function setLoginData(data) {
    loginData = data;
    if (data) {
      if (data.entityId) ownEntityId = data.entityId;
      if (data.dimensionName) dimensionName = data.dimensionName;
    }
  }

  return {
    noteS2c,
    noteC2s,
    mergeWith,
    reset,
    getOwnEntityId,
    setOwnEntityId,
    getDimensionName,
    setDimensionName,
    getLoginData,
    setLoginData,
    tracked,
    currentPos,
  };
}
