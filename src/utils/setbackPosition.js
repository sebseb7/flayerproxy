/**
 * Grim setback ClientboundPlayerPositionPacket: absolute XYZ, relative yaw/pitch (_value 24).
 * @param {object} data
 * @returns {boolean}
 */
function isSetbackStylePosition(data) {
  const f = data?.flags;
  if (!f || typeof f !== 'object') return false;
  if (f._value === 24) return true;
  return (
    f.x === false &&
    f.y === false &&
    f.z === false &&
    f.yaw === true &&
    f.pitch === true
  );
}

module.exports = { isSetbackStylePosition };
