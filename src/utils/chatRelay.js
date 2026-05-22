const { computeChatChecksum } = require('minecraft-protocol/src/datatypes/checksums');

const CHAT_C2S_PACKETS = new Set([
  'chat_message',
  'chat_command',
  'chat_command_signed',
]);

/** Inbound packets handled locally; never forward to upstream bot. */
const CHAT_SESSION_PACKETS = new Set([
  ...CHAT_C2S_PACKETS,
  'message_acknowledgement',
]);

/**
 * minecraft-protocol server chat plugin always listens for message_acknowledgement
 * even when enforceSecureProfile is false, and kicks with chat_validation_failed.
 */
function disableInboundChatValidation(client) {
  if (!client) return;
  client.removeAllListeners('message_acknowledgement');
}

/**
 * Plain text or command string from a proxy client chat packet.
 */
function extractChatText(name, data) {
  if (!data || typeof data !== 'object') return null;

  if (name === 'chat_message') {
    return typeof data.message === 'string' ? data.message : null;
  }

  if (name === 'chat_command' || name === 'chat_command_signed') {
    if (typeof data.command !== 'string' || !data.command.length) return null;
    return data.command.startsWith('/') ? data.command : `/${data.command}`;
  }

  return null;
}

function collectUpstreamAcknowledgements(rawClient) {
  const lsm = rawClient._lastSeenMessages;
  if (!lsm) return [];

  const acks = [];
  const cap = lsm.capacity ?? lsm.length;
  for (let i = 0; i < cap; i++) {
    const entry = lsm[i];
    if (Buffer.isBuffer(entry)) acks.push(entry);
    else if (entry?.signature && Buffer.isBuffer(entry.signature)) acks.push(entry.signature);
  }
  return acks;
}

function buildAcknowledgedBitset(rawClient) {
  const lsm = rawClient._lastSeenMessages;
  if (!lsm) return Buffer.alloc(3);

  let acc = 0;
  const cap = lsm.capacity ?? lsm.length;
  for (let i = 0; i < cap; i++) {
    if (lsm[i]) acc |= 1 << i;
  }
  const bitset = Buffer.allocUnsafe(3);
  bitset[0] = acc & 0xff;
  bitset[1] = (acc >> 8) & 0xff;
  bitset[2] = (acc >> 16) & 0xff;
  return bitset;
}

/** Fallback when mineflayer bot.chat is unavailable. */
function sendUpstreamSignedChat(rawClient, text, options) {
  const mcData = require('minecraft-data')(rawClient.version);
  const timestamp = options.timestamp ?? BigInt(Date.now());
  const salt = options.salt ?? 1n;

  if (!rawClient.profileKeys?.private) {
    throw new Error('Upstream bot has no chat signing keys');
  }
  if (mcData.supportFeature('useChatSessions') && !rawClient._session?.uuid) {
    throw new Error('Upstream chat session not initialized');
  }

  const acknowledgements = collectUpstreamAcknowledgements(rawClient);
  const acknowledged = buildAcknowledgedBitset(rawClient);
  const checksum = computeChatChecksum(rawClient._lastSeenMessages ?? []);

  if (text.startsWith('/')) {
    const command = text.slice(1);
    const canSign = mcData.supportFeature('useChatSessions') && rawClient._session;
    const packetName =
      mcData.supportFeature('seperateSignedChatCommandPacket') && canSign
        ? 'chat_command_signed'
        : 'chat_command';
    rawClient.write(packetName, {
      command,
      timestamp,
      salt,
      argumentSignatures: [],
      messageCount: rawClient._lastSeenMessages?.pending ?? 0,
      checksum,
      acknowledged,
    });
    if (rawClient._lastSeenMessages) rawClient._lastSeenMessages.pending = 0;
    return;
  }

  if (!mcData.supportFeature('useChatSessions')) {
    if (typeof rawClient._signedChat === 'function') {
      rawClient._signedChat(text, { timestamp, salt });
      return;
    }
    throw new Error('Unsupported chat protocol version');
  }

  const signature = rawClient.signMessage(text, timestamp, salt, undefined, acknowledgements);
  rawClient.write('chat_message', {
    message: text,
    timestamp,
    salt,
    signature,
    offset: rawClient._lastSeenMessages?.pending ?? 0,
    checksum,
    acknowledged,
  });
  if (rawClient._lastSeenMessages) rawClient._lastSeenMessages.pending = 0;
}

/**
 * Re-sign chat for the upstream bot session (FlayerBot), not the Java client's account.
 */
function relayClientChatAsUpstream(serverConn, name, data, log) {
  if (!CHAT_C2S_PACKETS.has(name)) return false;

  if (!serverConn.connected) {
    log?.warn?.('Cannot relay chat: upstream not connected');
    return true;
  }

  const text = extractChatText(name, data);
  if (text == null) {
    log?.warn?.(`Ignoring ${name} with no message/command`);
    return true;
  }

  try {
    if (serverConn.bot?.chat) {
      serverConn.bot.chat(text);
    } else if (serverConn.rawClient) {
      sendUpstreamSignedChat(serverConn.rawClient, text, {
        timestamp: data.timestamp,
        salt: data.salt,
      });
    } else {
      throw new Error('No upstream chat path');
    }
    log?.info?.(`Chat sent upstream as bot (${text.length > 80 ? `${text.slice(0, 77)}…` : text})`);
  } catch (err) {
    log?.error?.(`Chat re-sign failed: ${err.message}`);
  }
  return true;
}

module.exports = {
  CHAT_SESSION_PACKETS,
  CHAT_C2S_PACKETS,
  disableInboundChatValidation,
  extractChatText,
  relayClientChatAsUpstream,
};
