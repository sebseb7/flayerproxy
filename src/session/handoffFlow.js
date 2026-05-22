const { createLogger } = require('../utils/logger');
const {
  installHandoffUpstreamRelay,
  removeHandoffUpstreamRelay,
  ackChunkBatchToServer,
  installHandoffLiveChunkForward,
  removeHandoffLiveChunkForward,
  sendPermissionStatusToClient,
} = require('../utils/handoffSync');
const { ClientBridge } = require('../proxy/ClientBridge');

const log = createLogger('Session');

/**
 * Execute the handoff sequence: prime chunks → replay → position sync → permissions → bridge.
 *
 * @param {object} opts
 * @param {object} opts.client - minecraft-protocol proxy client
 * @param {import('./ServerConnection').ServerConnection} opts.serverConn
 * @param {import('../state/WorldStateCache').WorldStateCache} opts.worldState
 * @param {import('../replay/StateReplayer').StateReplayer} opts.replayer
 * @param {import('../proxy/ProxyServer').ProxyServer} opts.proxyServer
 * @param {function(): Promise<void>} opts.primeChunks - primes chunks near bot
 * @param {function(): boolean} opts.isHandoffState - returns true if still in HANDOFF state
 * @returns {Promise<{ bridge: ClientBridge, upstreamRelay: object }|null>} null on failure
 */
async function performHandoff({ client, serverConn, worldState, replayer, proxyServer, primeChunks, isHandoffState }) {
  const upstreamRelay = installHandoffUpstreamRelay(client, serverConn, log);
  const liveChunkForward = installHandoffLiveChunkForward(client, serverConn, worldState, log);

  try {
    await primeChunks();
    // Replay cached state (placeNewPlayer order: teleport → level info → chunks)
    await replayer.replay(client);
    ackChunkBatchToServer(serverConn, log);

    if (!isHandoffState()) return null;

    await serverConn.syncProxyClientPosition(client);
    serverConn.confirmServerPosition();

    if (
      !sendPermissionStatusToClient(
        client,
        worldState.player.permissionStatus,
        log
      )
    ) {
      log.warn(
        'No OP permission cached for client — run /op FlayerBot on the server (not your launcher username), then reconnect'
      );
    }

    serverConn.writeToServer('player_loaded', {});
    log.info('Sent player_loaded to server (hasClientLoaded)');

    removeHandoffUpstreamRelay(client, upstreamRelay);

    const bridge = new ClientBridge(client, serverConn, worldState);
    bridge.start();
    bridge.enableMovement();

    log.info(`Session handed off to ${client.username}`);
    return { bridge, upstreamRelay: null };
  } catch (err) {
    log.error('Error during handoff:', err);
    removeHandoffUpstreamRelay(client, upstreamRelay);
    return null;
  } finally {
    removeHandoffLiveChunkForward(serverConn, liveChunkForward);
  }
}

module.exports = { performHandoff };
