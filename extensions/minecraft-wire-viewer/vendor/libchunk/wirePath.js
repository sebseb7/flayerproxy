'use strict';

const path = require('path');

/** Packet names libchunk can decode (decode_wire.c + play_stream.c). */
const PACKET_NAMES = new Set([
  'map_chunk',
  'unload_chunk',
  'update_light',
  'block_change',
  'tile_entity_data',
  'multi_block_change',
  'spawn_entity',
  'entity_metadata',
  'entity_equipment',
  'entity_destroy',
  'set_passengers',
  'rel_entity_move',
  'entity_move_look',
  'entity_look',
  'sync_entity_position',
  'entity_velocity',
  'entity_head_rotation',
  'entity_update_attributes',
  'entity_teleport',
  'entity_effect',
  'remove_entity_effect',
  'position',
  'respawn',
  'initialize_world_border',
  'registry_data',
  'login',
  'update_health',
  'experience',
  'abilities',
  'entity_status',
  'spawn_position',
  'difficulty',
  'game_state_change',
  'window_items',
  'set_slot',
  'held_item_slot',
  'set_player_inventory',
  'set_cursor_item',
  'update_time',
  'chunk_batch_start',
  'chunk_batch_finished',
  'world_border_center',
  'world_border_size',
  'world_border_lerp_size',
  'world_border_warning_delay',
  'world_border_warning_reach',
  'simulation_distance',
  'update_view_distance',
  'update_view_position',
  'declare_commands',
  'player_info',
  'player_remove',
  'playerlist_header',
  'scoreboard_objective',
  'scoreboard_display_objective',
  'scoreboard_score',
  'reset_score',
  'teams',
  'boss_bar',
  'tracked_waypoint',
  'tags',
  'server_data',
  'update_recipes',
  'declare_recipes',
  'advancements',
  'recipe_book_add',
  'recipe_book_settings',
]);

const ARCHIVE_CATEGORIES = new Set(['player', 'config', 'misc']);

const RE_COORD_XZ = /^x(-?\d+)_z(-?\d+)(?:\.([a-z_]+))?\.wire$/;
const RE_COORD_XYZ = /^x(-?\d+)_y(-?\d+)_z(-?\d+)(?:\.([a-z_]+))?\.wire$/;
const RE_ENTITY_ID_PKT = /^(-?\d+)\.([a-z_]+)\.wire$/;
const RE_PKT_ONLY = /^([a-z_]+)\.wire$/;
const RE_FLAT = /^([a-z_]+)\/([a-z_]+)\.wire$/;
const RE_CATEGORY = /^(player|config|misc)\/([a-z_]+)\.wire$/;
const RE_LEGACY = /^\d+-([a-z_]+)(?:-\d+)?$/;

/**
 * Infer packet name from a sniffer wire file path.
 * @param {string} filePath
 * @returns {string|null}
 */
function packetNameFromPath(filePath) {
  const base = path.basename(filePath);
  const parts = filePath.split(/[/\\]/);

  let m = base.match(RE_CATEGORY);
  if (m && PACKET_NAMES.has(m[2])) return m[2];

  m = base.match(RE_FLAT);
  if (m && m[1] === m[2] && PACKET_NAMES.has(m[1])) return m[1];

  m = base.match(RE_ENTITY_ID_PKT);
  if (m && PACKET_NAMES.has(m[2])) return m[2];

  m = base.match(RE_COORD_XZ);
  if (m) {
    if (m[3] && PACKET_NAMES.has(m[3])) return m[3];
    return packetFromParentDirs(parts);
  }

  m = base.match(RE_COORD_XYZ);
  if (m) {
    if (m[3] && PACKET_NAMES.has(m[3])) return m[3];
    return packetFromParentDirs(parts);
  }

  m = base.match(RE_PKT_ONLY);
  if (m && PACKET_NAMES.has(m[1])) {
    const parent = parts[parts.length - 2] || '';
    if (/^e\d+$/.test(parent) || /^eu\d+$/.test(parent)) return m[1];
    if (packetFromParentDirs(parts) === m[1]) return m[1];
  }

  m = base.replace(/\.wire$/, '').match(RE_LEGACY);
  if (m && PACKET_NAMES.has(m[1])) return m[1];

  return packetFromParentDirs(parts);
}

function packetFromParentDirs(parts) {
  for (let i = parts.length - 2; i >= 0; i--) {
    const seg = parts[i];
    if (seg === 'raw') {
      const next = parts[i + 1];
      if (ARCHIVE_CATEGORIES.has(next)) {
        const pkt = parts[i + 2];
        if (pkt && PACKET_NAMES.has(pkt)) return pkt;
      }
      if (next && PACKET_NAMES.has(next)) return next;
      break;
    }
    if (ARCHIVE_CATEGORIES.has(seg)) {
      const pkt = parts[i + 1];
      if (pkt && PACKET_NAMES.has(pkt)) return pkt;
    }
    if (PACKET_NAMES.has(seg)) return seg;
  }
  return null;
}

/**
 * player/, config/, or misc/ when path uses chunk_stream_receiver category dirs.
 * @param {string} filePath
 * @returns {'player'|'config'|'misc'|null}
 */
function archiveCategoryFromPath(filePath) {
  const parts = filePath.split(/[/\\]/);
  for (let i = 0; i < parts.length; i++) {
    if (parts[i] === 'raw' && i + 1 < parts.length && ARCHIVE_CATEGORIES.has(parts[i + 1])) {
      return parts[i + 1];
    }
    if (ARCHIVE_CATEGORIES.has(parts[i])) return parts[i];
  }
  return null;
}

/**
 * Parse coordinates / entity id from path + basename.
 * @param {string} filePath
 * @returns {{ packet: string|null, category?: string, worldX?: number, worldY?: number, worldZ?: number, entityId?: number, rx?: number, rz?: number, cx?: number, cz?: number }}
 */
function parseWirePath(filePath) {
  const base = path.basename(filePath);
  const parts = filePath.split(/[/\\]/);
  const packet = packetNameFromPath(filePath);
  const out = { packet, category: archiveCategoryFromPath(filePath) ?? undefined };

  let m = base.match(RE_COORD_XZ);
  if (m) {
    out.worldX = Number(m[1]);
    out.worldZ = Number(m[2]);
    Object.assign(out, regionFromParts(parts));
    return out;
  }

  m = base.match(RE_COORD_XYZ);
  if (m) {
    out.worldX = Number(m[1]);
    out.worldY = Number(m[2]);
    out.worldZ = Number(m[3]);
    Object.assign(out, regionFromParts(parts));
    return out;
  }

  m = base.match(RE_ENTITY_ID_PKT);
  if (m) {
    out.entityId = Number(m[1]);
    Object.assign(out, entityShardFromParts(parts));
    return out;
  }

  m = base.match(RE_PKT_ONLY);
  if (m) {
    const parent = parts[parts.length - 2] || '';
    const em = parent.match(/^e(\d+)$/);
    if (em) out.entityId = Number(em[1]);
    Object.assign(out, entityShardFromParts(parts));
    return out;
  }

  Object.assign(out, regionFromParts(parts));
  Object.assign(out, entityShardFromParts(parts));
  return out;
}

function regionFromParts(parts) {
  const o = {};
  for (const p of parts) {
    let m = p.match(/^rx(-?\d+)$/);
    if (m) o.rx = Number(m[1]);
    m = p.match(/^rz(-?\d+)$/);
    if (m) o.rz = Number(m[1]);
    m = p.match(/^cx(-?\d+)$/);
    if (m) o.cx = Number(m[1]);
    m = p.match(/^cz(-?\d+)$/);
    if (m) o.cz = Number(m[1]);
  }
  return o;
}

function entityShardFromParts(parts) {
  const o = {};
  for (const p of parts) {
    const em = p.match(/^e(\d+)$/);
    if (em) o.entityId = Number(em[1]);
  }
  return o;
}

/** Path to paired chunk PNG (map_chunk layout). */
function pngPathForWire(filePath, pngRoot) {
  const info = parseWirePath(filePath);
  if (info.packet !== 'map_chunk' || info.worldX == null || info.worldZ == null) return null;
  if (info.rx == null || info.rz == null || info.cx == null || info.cz == null) return null;
  const root = pngRoot || filePath.split(/[/\\]raw[/\\]/)[0];
  if (!root) return null;
  return path.join(
    root,
    `rx${info.rx}`,
    `rz${info.rz}`,
    `cx${info.cx}`,
    `cz${info.cz}`,
    `x${info.worldX}_z${info.worldZ}.png`
  );
}

module.exports = {
  PACKET_NAMES,
  ARCHIVE_CATEGORIES,
  packetNameFromPath,
  parseWirePath,
  archiveCategoryFromPath,
  pngPathForWire,
};
