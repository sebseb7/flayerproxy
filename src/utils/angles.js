/**
 * Convert yaw/pitch to the i8 byte format used by spawn_entity and similar packets.
 * Movement packets may send f32 degrees (e.g. sync_entity_position); spawn uses i8.
 */
function toByteAngle(value) {
  if (typeof value !== 'number' || Number.isNaN(value)) return 0;

  // Already a protocol byte angle (-128..127, integer)
  if (value >= -128 && value <= 127 && Math.abs(value - Math.round(value)) < 1e-6) {
    return Math.round(value);
  }

  // Notchian degrees (f32) -> byte: floor(angle * 256 / 360)
  let byte = Math.floor((value % 360) * 256 / 360);
  if (byte > 127) byte -= 256;
  if (byte < -128) byte += 256;
  return byte;
}

/**
 * Prepare spawn_entity packet data for serialization.
 */
function sanitizeSpawnEntity(spawnData) {
  if (!spawnData) return spawnData;
  return {
    ...spawnData,
    yaw: toByteAngle(spawnData.yaw),
    pitch: toByteAngle(spawnData.pitch),
    headPitch: toByteAngle(spawnData.headPitch),
  };
}

module.exports = { toByteAngle, sanitizeSpawnEntity };
