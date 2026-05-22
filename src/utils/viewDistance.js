const { createLogger } = require('./logger');

const log = createLogger('viewDistance');

/**
 * Coordinate server / bot / java client view distances for chunk streaming.
 *
 * 1.21+ uses separate S2C packets:
 * - simulation_distance { distance } — how far the client simulates/loads (5×5 when distance=2)
 * - update_view_distance { viewDistance } — render cap from server
 *
 * Play-state C2S has no `settings` on 1.21.10; use config proxy.clientViewDistance as java target.
 */

function clampViewDistance(vd) {
  return Math.max(2, Math.min(32, Math.floor(vd)));
}

/**
 * @param {{ viewDistance?: number|null, simulationDistance?: number|null }} misc
 */
function serverDistancesFromMisc(misc) {
  const render =
    misc.viewDistance?.viewDistance != null
      ? clampViewDistance(misc.viewDistance.viewDistance)
      : null;
  const simulation =
    misc.simulationDistance?.distance != null
      ? clampViewDistance(misc.simulationDistance.distance)
      : null;
  const effective =
    render != null && simulation != null
      ? Math.min(render, simulation)
      : render ?? simulation ?? 10;

  return { render, simulation, effective };
}

/**
 * @param {{ serverVd?: number, serverSim?: number, botVd?: number, javaVd?: number|null, clientTargetVd?: number }} opts
 */
function resolveViewDistances(opts) {
  const render = opts.serverVd != null ? clampViewDistance(opts.serverVd) : 10;
  const sim = opts.serverSim != null ? clampViewDistance(opts.serverSim) : render;
  const bot = clampViewDistance(opts.botVd ?? 10);
  const target = clampViewDistance(opts.clientTargetVd ?? 10);
  const java =
    opts.javaVd != null && !Number.isNaN(opts.javaVd) ? clampViewDistance(opts.javaVd) : null;

  const serverEffective = Math.min(render, sim);
  const desired = java ?? target;
  const upstream = Math.min(render, Math.max(bot, desired));
  const clientRender = Math.min(render, desired);
  const clientSimulation = clientRender;

  return {
    serverRender: render,
    serverSimulation: sim,
    serverEffective,
    bot,
    java,
    clientTarget: target,
    upstream,
    clientRender,
    clientSimulation,
  };
}

/**
 * Read view distance from legacy serverbound settings (bot / old protocols).
 * @param {object} data
 */
function viewDistanceFromSettings(data) {
  if (!data || typeof data !== 'object') return null;
  const vd = data.viewDistance ?? data.renderDistance ?? data.clientRenderDistance;
  if (vd == null || Number.isNaN(vd)) return null;
  return clampViewDistance(vd);
}

/**
 * @param {object} template - last java settings packet
 * @param {number} upstreamVd
 */
function settingsWithViewDistance(template, upstreamVd) {
  return {
    ...template,
    viewDistance: upstreamVd,
    renderDistance: upstreamVd,
  };
}

/**
 * Push coordinated S2C chunk radii to the java client (after handoff / on bridge start).
 * @param {object} client
 * @param {ReturnType<typeof resolveViewDistances>} ctx
 */
function pushClientChunkDistances(client, ctx) {
  if (!client || client.ended || client.state !== 'play') return;
  try {
    client.write('simulation_distance', { distance: ctx.clientSimulation });
    client.write('update_view_distance', { viewDistance: ctx.clientRender });
  } catch (err) {
    log.warn(`pushClientChunkDistances failed: ${err.message}`);
  }
}

/**
 * @param {ReturnType<typeof resolveViewDistances>} ctx
 */
function logViewDistanceSummary(ctx, label) {
  const simChunks = 2 * ctx.serverSimulation + 1;
  const effChunks = 2 * ctx.serverEffective + 1;
  log.info(
    `[viewDistance] ${label}: upstream=${ctx.upstream} javaRender=${ctx.clientRender} ` +
      `(server render=${ctx.serverRender} sim=${ctx.serverSimulation} → ~${effChunks}×${effChunks} max, ` +
      `bot=${ctx.bot}, target=${ctx.clientTarget})`,
  );
  if (ctx.serverSimulation < ctx.serverRender) {
    log.warn(
      `[viewDistance] Server simulation_distance=${ctx.serverSimulation} (~${simChunks}×${simChunks}) is below render=${ctx.serverRender} — constantiam may only stream ${simChunks}×${simChunks} columns to the bot`,
    );
  }
}

module.exports = {
  resolveViewDistances,
  clampViewDistance,
  serverDistancesFromMisc,
  viewDistanceFromSettings,
  settingsWithViewDistance,
  pushClientChunkDistances,
  logViewDistanceSummary,
};
