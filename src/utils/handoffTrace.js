/**
 * Explicit handoff / chunk-load tracing — no guessing from log silence.
 * All lines use prefix [Handoff] at INFO unless noted.
 */

const C2S_TRACE = new Set([
  'player_loaded',
  'chunk_batch_received',
  'teleport_confirm',
]);

const JAVA_S2C_TRACE = new Set([
  'login',
  'position',
  'game_state_change',
  'update_view_position',
  'update_view_distance',
  'chunk_batch_start',
  'chunk_batch_finished',
  'map_chunk',
]);

function formatC2S(name, data) {
  if (name === 'teleport_confirm') return `teleportId=${data?.teleportId}`;
  if (name === 'chunk_batch_received') {
    return `chunksPerTick=${data?.chunksPerTick ?? '?'}`;
  }
  if (name === 'player_loaded') return '(empty)';
  return '';
}

function formatS2C(name, data) {
  if (name === 'game_state_change') {
    return `reason=${data?.reason} gameMode=${data?.gameMode}`;
  }
  if (name === 'position') {
    return `teleportId=${data?.teleportId} pos=(${data?.x?.toFixed?.(2)}, ${data?.y?.toFixed?.(2)}, ${data?.z?.toFixed?.(2)})`;
  }
  if (name === 'update_view_position') {
    return `chunk=(${data?.chunkX}, ${data?.chunkZ})`;
  }
  if (name === 'chunk_batch_finished') return `batchSize=${data?.batchSize}`;
  if (name === 'map_chunk') return `chunk=(${data?.x}, ${data?.z})`;
  if (name === 'login') return `entityId=${data?.entityId}`;
  return '';
}

/**
 * @param {import('./logger').Logger} log
 * @param {string} phase
 * @param {string} [detail]
 */
function logPhase(log, phase, detail = '') {
  if (!log) return;
  const extra = detail ? ` — ${detail}` : '';
  log.info(`[Handoff] phase ${phase}${extra}`);
}

/**
 * Proxy (or relay) wrote C2S to upstream bot connection.
 * @param {import('./logger').Logger} log
 * @param {string} packetName
 * @param {object} data
 * @param {string} source - e.g. handoffFlow.ackChunkBatch, ClientBridge.forward, mineflayer.flush
 */
function logProxyC2S(log, packetName, data, source) {
  if (!log || !C2S_TRACE.has(packetName)) return;
  log.info(
    `[Handoff] proxy C2S → upstream: ${packetName} source=${source} ${formatC2S(packetName, data)}`,
  );
}

/**
 * Java client sent C2S; about to forward upstream.
 */
function logJavaC2S(log, packetName, data, when) {
  if (!log || !C2S_TRACE.has(packetName)) return;
  log.info(
    `[Handoff] java C2S received: ${packetName} when=${when} ${formatC2S(packetName, data)} → forwarding upstream`,
  );
}

/**
 * Proxy sent S2C to java client (replay, live forward, or write).
 */
function logProxyS2C(log, packetName, data, source) {
  if (!log) return;
  if (packetName === 'map_chunk') {
    return;
  }
  if (!JAVA_S2C_TRACE.has(packetName)) return;
  log.info(
    `[Handoff] proxy S2C → java: ${packetName} source=${source} ${formatS2C(packetName, data)}`,
  );
}

let mapChunkReplayCount = 0;

function resetMapChunkReplayCount() {
  mapChunkReplayCount = 0;
}

function logMapChunkReplayed(log, x, z, mode) {
  mapChunkReplayCount++;
  if (mapChunkReplayCount === 1 || mapChunkReplayCount % 16 === 0) {
    log.info(`[Handoff] proxy S2C → java: map_chunk #${mapChunkReplayCount} chunk=(${x},${z}) mode=${mode}`);
  }
}

function logMapChunkReplayDone(log, total, summary) {
  log.info(`[Handoff] proxy S2C → java: map_chunk done total=${total} (${summary})`);
}

/**
 * Listen for critical C2S from java during handoff (before bridge owns relay).
 */
function installJavaC2SHandoffLog(client, when, log) {
  const handler = (data, meta) => {
    if (meta.state !== 'play') return;
    if (!C2S_TRACE.has(meta.name)) return;
    logJavaC2S(log, meta.name, data, when);
  };
  client.on('packet', handler);
  return handler;
}

function removeJavaC2SHandoffLog(client, handler) {
  if (handler) client.removeListener('packet', handler);
}

module.exports = {
  logPhase,
  logProxyC2S,
  logJavaC2S,
  logProxyS2C,
  logMapChunkReplayed,
  logMapChunkReplayDone,
  resetMapChunkReplayCount,
  installJavaC2SHandoffLog,
  removeJavaC2SHandoffLog,
  C2S_TRACE,
};
