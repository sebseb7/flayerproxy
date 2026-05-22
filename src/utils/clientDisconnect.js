/**
 * minecraft-protocol's default errorHandler calls client.end(err), which crashes
 * kick_disconnect serialization (reason must be string/JSON, not an Error).
 */

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

/**
 * Wrap client.end so Error objects are never passed to kick_disconnect.
 */
function wrapClientEnd(client) {
  const protoEnd = client.end.bind(client);
  client.end = function safeEnd(endReason, fullReason) {
    return protoEnd(disconnectReasonText(endReason), fullReason);
  };
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
      client._end(text);
    } catch {
      /* socket already gone */
    }
  }
}

module.exports = {
  disconnectReasonText,
  wrapClientEnd,
  safeEndClient,
};
