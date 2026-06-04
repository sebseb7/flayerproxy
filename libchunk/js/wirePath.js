'use strict';

const path = require('path');
const playPacketNames = require('./playPacketNames');

/** Packet names libchunk can decode (decode_wire.c + play_stream.c). */
const DECODED_PACKET_NAMES = new Set([
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
  'sound_effect',
  'entity_sound_effect',
  'entity_update_attributes',
  'entity_teleport',
  'entity_effect',
  'remove_entity_effect',
  'position',
  'c2s_position',
  'c2s_position_look',
  'c2s_look',
  'c2s_flying',
  'c2s_teleport_confirm',
  'respawn',
  'initialize_world_border',
  'registry_data',
  'custom_payload',
  'feature_flags',
  'select_known_packs',
  'finish_configuration',
  'bundle_delimiter',
  'step_tick',
  'success',
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
  'system_chat',
  'set_ticking_state',
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

/** Decodable + all protocol 773 play S2C / capture labels (path inference). */
const PACKET_NAMES = new Set([...DECODED_PACKET_NAMES, ...playPacketNames]);

const ARCHIVE_CATEGORIES = new Set(['player', 'config', 'misc', 'client']);

/** mc_reference_client: 0042_s2c_play_2c_map_chunk.wire (legacy: 0042_play_2c_map_chunk.wire) */
const RE_REFERENCE_CAPTURE =
  /^(\d{4})_(?:(s2c|c2s)_)?(login|config|play|hs)_([0-9a-f]{2})_([a-z0-9_]+)\.wire$/i;

/** Packet suffix in filenames (includes c2s_* and numeric wire names). */
const RE_PKT_SUFFIX = '[a-z0-9_]+';

const RE_COORD_XZ = new RegExp(`^x(-?\\d+)_z(-?\\d+)(?:\\.(${RE_PKT_SUFFIX}))?\\.wire$`);
const RE_COORD_XYZ = new RegExp(`^x(-?\\d+)_y(-?\\d+)_z(-?\\d+)(?:\\.(${RE_PKT_SUFFIX}))?\\.wire$`);
const RE_ENTITY_ID_PKT = new RegExp(`^(-?\\d+)\\.(${RE_PKT_SUFFIX})\\.wire$`);
const RE_PKT_ONLY = new RegExp(`^(${RE_PKT_SUFFIX})\\.wire$`);
const RE_FLAT = new RegExp(`^(${RE_PKT_SUFFIX})\\/(${RE_PKT_SUFFIX})\\.wire$`);
const RE_CATEGORY = new RegExp(`^(player|config|misc|client)\\/(${RE_PKT_SUFFIX})\\.wire$`);
const RE_CLIENT_FLAT = new RegExp(`^(${RE_PKT_SUFFIX})\\.(${RE_PKT_SUFFIX})\\.wire$`);
const RE_LEGACY = /^\d+-([a-z0-9_]+)(?:-\d+)?$/;

function parseReferenceCaptureBasename(base) {
  const m = base.match(RE_REFERENCE_CAPTURE);
  if (!m) return null;
  const label = m[5];
  return {
    captureSeq: Number(m[1]),
    captureDir: m[2] || 's2c',
    capturePhase: m[3],
    protocolId: parseInt(m[4], 16),
    packet: label === 'pkt' ? null : label,
  };
}

/**
 * Infer packet name from a sniffer wire file path.
 * @param {string} filePath
 * @returns {string|null}
 */
function packetNameFromPath(filePath) {
  const base = path.basename(filePath);
  const ref = parseReferenceCaptureBasename(base);
  if (ref?.packet) return ref.packet;

  const parts = filePath.split(/[/\\]/);

  let m = base.match(RE_CATEGORY);
  if (m && PACKET_NAMES.has(m[2])) return m[2];

  m = base.match(RE_FLAT);
  if (m && m[1] === m[2] && PACKET_NAMES.has(m[1])) return m[1];

  m = base.match(RE_CLIENT_FLAT);
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
        if (next === 'client' && i + 2 < parts.length) {
          const sub = parts[i + 2];
          if (PACKET_NAMES.has(sub)) return sub;
        }
      }
      if (next && PACKET_NAMES.has(next)) return next;
      break;
    }
    if (seg === 'client') {
      const pkt = parts[i + 1];
      if (pkt && PACKET_NAMES.has(pkt)) return pkt;
    }
    if (seg === 'teleport_confirm') return 'c2s_teleport_confirm';
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
 * @returns {'player'|'config'|'misc'|'client'|null}
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
 * @returns {{ packet: string|null, category?: string, captureSeq?: number, capturePhase?: string, protocolId?: number, worldX?: number, worldY?: number, worldZ?: number, entityId?: number, rx?: number, rz?: number, cx?: number, cz?: number }}
 */
function parseWirePath(filePath) {
  const base = path.basename(filePath);
  const parts = filePath.split(/[/\\]/);
  const ref = parseReferenceCaptureBasename(base);
  const packet = ref?.packet ?? packetNameFromPath(filePath);
  const out = {
    packet,
    category: archiveCategoryFromPath(filePath) ?? ref?.capturePhase ?? undefined,
    captureSeq: ref?.captureSeq,
    captureDir: ref?.captureDir,
    capturePhase: ref?.capturePhase,
    protocolId: ref?.protocolId,
  };

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
  DECODED_PACKET_NAMES,
  PACKET_NAMES,
  ARCHIVE_CATEGORIES,
  packetNameFromPath,
  parseWirePath,
  archiveCategoryFromPath,
  pngPathForWire,
  parseReferenceCaptureBasename,
};
