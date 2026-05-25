/** Play packets that must be forwarded with writeRaw to survive NBT/chunk re-serialization */
const RAW_FORWARD_PACKETS = new Set([
  'map_chunk',
  'update_light',
  'unload_chunk',
  'chunk_batch_start',
  'chunk_batch_finished',
  'update_view_position',
  /** Re-encoding corrupts PositionUpdateRelatives (Grim setbacks use relative yaw/pitch) */
  'position',
  /**
   * Container sync: tolerant NBT decode, but re-encode drops Paper/Via payloads — relay captured wire.
   * Do not raw-forward entity_equipment / set_player_inventory: parse size can include trailing
   * bytes and vanilla reports "set_equipment … N bytes extra".
   */
  'window_items',
  'set_slot',
]);

/**
 * Play packets: minecraft-protocol's captured wire buffer can be longer than the parsed
 * payload (topBitSetTerminatedArray / Slot). Forwarding that buffer causes vanilla
 * "set_equipment … N bytes extra". Re-encode from parsed params on the target serializer.
 */
const PARSE_RELAY_PACKETS = new Set(['entity_equipment', 'set_player_inventory']);

module.exports = { RAW_FORWARD_PACKETS, PARSE_RELAY_PACKETS };
