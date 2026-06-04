import { createRequire } from 'node:module';
import { s2cPacketName } from './packetNames.js';

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

export function createJoinDataTracker() {
  let ownEntityId = null;
  // Map of key -> { id, payload }
  const tracked = new Map();

  function reset() {
    ownEntityId = null;
    tracked.clear();
  }

  function getOwnEntityId() {
    return ownEntityId;
  }

  function setOwnEntityId(id) {
    ownEntityId = id;
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

    // 2. Check if it's login to extract own entityId
    if (packetName === 'login') {
      try {
        const login = lc.parsePlayLogin(payload);
        if (login) {
          ownEntityId = login.entityId;
        }
      } catch (e) {
        // ignore parsing error
      }
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

    // 4. Check if it's an entity packet for our own entityId
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

  return {
    noteS2c,
    mergeWith,
    reset,
    getOwnEntityId,
    setOwnEntityId,
    tracked,
  };
}
