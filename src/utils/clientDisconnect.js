/**
 * minecraft-protocol's default errorHandler calls client.end(err), which crashes
 * kick_disconnect serialization (reason must be string/JSON, not an Error).
 *
 * Calling client._end() immediately after write() often closes the serializer
 * before the kick packet is flushed, which makes Java clients report a crash.
 */

let nbt;

const DISCONNECT_FLUSH_MS = 200;
const DISCONNECT_WAIT_MS = 5000;

function disconnectReasonText(reason) {
  if (reason instanceof Error) return reason.message || 'Disconnected';
  if (reason == null) return 'Disconnected';
  if (typeof reason === 'string') return reason;
  try {
    return JSON.stringify(reason);
  } catch {
    return String(reason);
  }
}

function buildDisconnectPayload(client, text) {
  try {
    if (
      typeof client._supportFeature === 'function' &&
      client._supportFeature('chatPacketsUseNbtComponents')
    ) {
      if (!nbt) nbt = require('prismarine-nbt');
      return nbt.comp({ text: nbt.string(text) });
    }
  } catch {
    /* use JSON fallback */
  }
  return JSON.stringify({ text });
}

/**
 * Write the correct disconnect packet for the client's protocol state.
 */
function sendDisconnectPacket(client, text) {
  const state = client.state;
  if (state === 'play') {
    client.write('kick_disconnect', { reason: buildDisconnectPayload(client, text) });
    return;
  }
  if (state === 'login') {
    client.write('disconnect', { reason: JSON.stringify({ text }) });
    return;
  }
  if (state === 'configuration') {
    client.write('disconnect', { reason: buildDisconnectPayload(client, text) });
  }
}

/**
 * Wrap client.end so Error objects are never passed to kick_disconnect.
 */
function wrapClientEnd(client) {
  const serverEnd = client.end.bind(client);
  client.end = function safeEnd(endReason, fullReason) {
    const text = disconnectReasonText(endReason);
    if (fullReason == null && client.state === 'configuration') {
      try {
        sendDisconnectPacket(client, text);
        const endFn = client._end || serverEnd;
        return endFn(text);
      } catch {
        /* fall through to server end */
      }
    }
    return serverEnd(text, fullReason);
  };
}

function endClientAfterFlush(client, text) {
  const endNow = () => {
    try {
      if (!client.ended && typeof client._end === 'function') {
        client._end(text);
      }
    } catch {
      /* socket already gone */
    }
  };

  const sock = client.socket;
  if (!sock || sock.destroyed) {
    endNow();
    return;
  }

  const scheduleEnd = () => setTimeout(endNow, DISCONNECT_FLUSH_MS);
  if (sock.writableNeedDrain) {
    sock.once('drain', scheduleEnd);
    setTimeout(endNow, 500);
  } else {
    scheduleEnd();
  }
}

/**
 * End a proxy client without throwing if the socket is already closed.
 */
function safeEndClient(client, reason) {
  if (!client || client.ended) return;
  const text = disconnectReasonText(reason);
  try {
    client.end(text);
  } catch {
    try {
      if (typeof client._end === 'function') {
        client._end(text);
      }
    } catch {
      /* socket already gone */
    }
  }
}

/**
 * Write disconnect/kick, wait for flush, then close. Resolves when the client ends.
 */
function gracefulEndClient(client, reason) {
  if (!client || client.ended) return Promise.resolve();

  const text = disconnectReasonText(reason);

  return new Promise((resolve) => {
    let settled = false;
    const finish = () => {
      if (settled) return;
      settled = true;
      clearTimeout(maxWait);
      client.removeListener('end', onEnd);
      resolve();
    };
    const onEnd = () => finish();
    const maxWait = setTimeout(finish, DISCONNECT_WAIT_MS);

    client.once('end', onEnd);

    try {
      if (client.state === 'play' || client.state === 'login' || client.state === 'configuration') {
        sendDisconnectPacket(client, text);
        endClientAfterFlush(client, text);
      } else if (typeof client._end === 'function') {
        client._end(text);
      } else {
        client.end(text);
      }
    } catch {
      try {
        if (typeof client._end === 'function') client._end(text);
      } catch {
        finish();
      }
    }
  });
}

/**
 * Gracefully disconnect every client on a minecraft-protocol server.
 */
async function disconnectServerClients(server, reason) {
  if (!server?.clients) return;
  const clients = Object.values(server.clients);
  await Promise.all(clients.map((client) => gracefulEndClient(client, reason)));
}

function closeServerListenSocket(server) {
  if (!server?.socketServer) return;
  try {
    server.socketServer.close();
  } catch {
    /* already closed */
  }
}

function delay(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

module.exports = {
  disconnectReasonText,
  buildDisconnectPayload,
  sendDisconnectPacket,
  wrapClientEnd,
  safeEndClient,
  gracefulEndClient,
  disconnectServerClients,
  closeServerListenSocket,
  delay,
};
