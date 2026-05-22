const crypto = require('crypto');
const uuid = require('minecraft-protocol/src/datatypes/uuid');
const { concat } = require('minecraft-protocol/src/transforms/binaryStream');
const { mojangPublicKeyPem } = require('minecraft-protocol/src/server/constants');

/**
 * Apply login_start fields without completing local login (no success packet).
 */
function applyLoginStartIdentity(client, packet, _server, options) {
  const mcData = require('minecraft-data')(client.version);
  client.supportFeature = mcData.supportFeature;
  client.username = packet.username;

  if (packet.playerUUID) {
    client.uuid = packet.playerUUID;
  }

  if (packet.signature && mcData.supportFeature('signatureEncryption')) {
    if (packet.signature.timestamp < BigInt(Date.now())) {
      throw new Error('expired_public_key');
    }
    const publicKey = crypto.createPublicKey({
      key: packet.signature.publicKey,
      format: 'der',
      type: 'spki',
    });
    const signable = mcData.supportFeature('profileKeySignatureV2')
      ? concat('UUID', packet.playerUUID, 'i64', packet.signature.timestamp, 'buffer', publicKey.export({ type: 'spki', format: 'der' }))
      : Buffer.from(`${packet.signature.timestamp}${publicKeyToPem(packet.signature.publicKey)}`, 'utf8');

    if (!crypto.verify('RSA-SHA1', signable, crypto.createPublicKey(mojangPublicKeyPem), packet.signature.signature)) {
      throw new Error('invalid_public_key_signature');
    }
    client.profileKeys = { public: publicKey };
  }

  if (options['online-mode'] !== true && !client.uuid) {
    client.uuid = uuid.nameToMcOfflineUUID(client.username);
  }
}

function publicKeyToPem(mcPubKeyBuffer) {
  let pem = '-----BEGIN RSA PUBLIC KEY-----\n';
  let base64PubKey = mcPubKeyBuffer.toString('base64');
  const maxLineLength = 64;
  while (base64PubKey.length > 0) {
    pem += `${base64PubKey.substring(0, maxLineLength)}\n`;
    base64PubKey = base64PubKey.substring(maxLineLength);
  }
  pem += '-----END RSA PUBLIC KEY-----\n';
  return pem;
}

module.exports = { applyLoginStartIdentity };
