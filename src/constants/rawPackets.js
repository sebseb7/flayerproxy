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
]);

module.exports = { RAW_FORWARD_PACKETS };
