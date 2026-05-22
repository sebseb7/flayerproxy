const { createLogger } = require('../utils/logger');
const {
  createHandoffUpstreamGate,
  installHandoffUpstreamRelay,
  removeHandoffUpstreamRelay,
  releaseHeldPlayerLoaded,
  waitForClientChunkBatchReceived,
  waitForClientPlayerLoaded,
  ackChunkBatchToServer,
  installHandoffLiveChunkForward,
  removeHandoffLiveChunkForward,
  sendPermissionStatusToClient,
} = require('../utils/handoffSync');
const { ClientBridge } = require('../proxy/ClientBridge');
const { logPhase, logProxyC2S } = require('../utils/handoffTrace');

const log = createLogger('Session');

/**
 * @param {object} opts
 * @param {() => boolean} opts.canContinue - false when client left or session left HANDOFF
 */
async function performHandoff({
  client,
  serverConn,
  worldState,
  replayer,
  primeChunks,
  canContinue,
}) {
  const handoffGate = createHandoffUpstreamGate();
  const upstreamRelay = installHandoffUpstreamRelay(client, serverConn, log, handoffGate);
  let liveChunkForward = null;

  try {
    logPhase(log, 'START', client.username);
    await primeChunks();
    if (!canContinue()) return null;
    logPhase(log, 'PRIME_DONE');

    let replayBatchAckedByClient = false;
    const onReplayBatchAck = (_data, meta) => {
      if (meta.state === 'play' && meta.name === 'chunk_batch_received') {
        replayBatchAckedByClient = true;
        log.info('[Handoff] java chunk_batch_received during REPLAY (before post-terrain)');
      }
    };
    client.on('packet', onReplayBatchAck);

    await replayer.replay(client, { deferPostTerrain: true });
    client.removeListener('packet', onReplayBatchAck);
    if (!canContinue()) return null;
    logPhase(log, 'REPLAY_DONE', replayBatchAckedByClient ? 'javaAckedBatch=yes' : 'javaAckedBatch=no');

    if (!replayBatchAckedByClient) {
      const acked = await waitForClientChunkBatchReceived(client, 15_000, log);
      logPhase(log, 'WAIT_JAVA_BATCH_ACK', acked ? 'ok' : 'TIMEOUT');
    }
    if (!canContinue()) return null;

    liveChunkForward = installHandoffLiveChunkForward(client, serverConn, worldState, log);
    ackChunkBatchToServer(serverConn, log);

    if (!serverConn.confirmServerPosition()) {
      log.warn('[Handoff] confirmServerPosition failed');
    } else {
      logPhase(log, 'SERVER_POSITION_CONFIRMED');
    }

    await replayer.replayPostTerrain(client);
    if (!canContinue()) return null;
    const releasedHeldLoaded = releaseHeldPlayerLoaded(handoffGate, serverConn, log);
    logPhase(
      log,
      'POST_TERRAIN_REPLAY_DONE',
      releasedHeldLoaded ? 'releasedHeldPlayerLoaded=yes' : 'releasedHeldPlayerLoaded=no',
    );

    if (
      !sendPermissionStatusToClient(
        client,
        worldState.player.permissionStatus,
        log,
      )
    ) {
      log.warn(
        'No OP permission cached for client — run /op FlayerBot on the server (not your launcher username), then reconnect',
      );
    }

    logProxyC2S(log, 'player_loaded', {}, 'SKIPPED — proxy does not send player_loaded');
    logPhase(log, 'PLAYER_LOADED', 'proxy will NOT send; waiting for java client');

    removeHandoffUpstreamRelay(client, upstreamRelay);

    if (!canContinue()) return null;

    const bridge = new ClientBridge(client, serverConn, worldState);
    bridge.start();
    bridge.enableMovement();
    logPhase(log, 'BRIDGE_STARTED');

    const javaLoaded =
      releasedHeldLoaded || (await waitForClientPlayerLoaded(client, 60_000, log));
    logPhase(
      log,
      'JAVA_PLAYER_LOADED',
      releasedHeldLoaded
        ? 'held player_loaded released after post-terrain'
        : javaLoaded
          ? 'java sent player_loaded'
          : 'TIMEOUT no java player_loaded',
    );

    log.info(`Session handed off to ${client.username}`);
    return { bridge };
  } catch (err) {
    log.error('Error during handoff:', err);
    removeHandoffUpstreamRelay(client, upstreamRelay);
    return null;
  } finally {
    removeHandoffLiveChunkForward(serverConn, liveChunkForward);
  }
}

module.exports = { performHandoff };
