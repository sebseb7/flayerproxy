'use strict';

/**
 * Stable world folder name for a Minecraft server (not per sniffer session).
 * Example: constantiam.net + 25565 → "constantiam.net_25565"
 *
 * @param {string} host
 * @param {number|string} port
 * @returns {string}
 */
function serverWorldDirName(host, port) {
  const h = String(host ?? 'unknown')
    .trim()
    .toLowerCase()
    .replace(/[^\w.\-]+/g, '_')
    .replace(/_+/g, '_')
    .replace(/^_|_$/g, '');
  const p = Number(port);
  const safePort = Number.isFinite(p) ? p : 25565;
  return `${h || 'server'}_${safePort}`;
}

/**
 * World directory name for SnifferWorldCapture.configureExport.
 * @param {object} sniffer - config.sniffer
 * @param {{ host: string, port: number }} server - config.server
 * @param {string} sessionId - packet log session id (per-session fallback)
 */
function resolveSaveLevelWorldName(sniffer, server, sessionId) {
  if (sniffer.saveLevelPerSession) return sessionId;
  if (sniffer.saveLevelName) return sniffer.saveLevelName;
  return serverWorldDirName(server.host, server.port);
}

module.exports = { serverWorldDirName, resolveSaveLevelWorldName };
