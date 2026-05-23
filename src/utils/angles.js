/**
 * f32 degrees (sync_entity_position, etc.) → protocol i8 byte angle.
 */
function degreesToByteAngle(value) {
  if (typeof value !== 'number' || Number.isNaN(value)) return 0;
  let byte = Math.floor((value % 360) * 256 / 360);
  if (byte > 127) byte -= 256;
  if (byte < -128) byte += 256;
  return byte;
}

/**
 * Convert yaw/pitch to the i8 byte format used by spawn_entity and similar packets.
 * spawn_entity / rel_entity_move use i8 bytes; sync_entity_position uses f32 degrees.
 */
function toByteAngle(value) {
  if (typeof value !== 'number' || Number.isNaN(value)) return 0;

  // Outside i8 range → always degrees (e.g. 270.0 from sync)
  if (value < -128 || value > 127) {
    return degreesToByteAngle(value);
  }

  // Fractional values are degrees, not wire bytes
  if (Math.abs(value - Math.round(value)) >= 1e-6) {
    return degreesToByteAngle(value);
  }

  // Integer in i8 range: already a protocol byte (spawn_entity wire decode)
  return Math.round(value);
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

module.exports = { toByteAngle, degreesToByteAngle, sanitizeSpawnEntity };
