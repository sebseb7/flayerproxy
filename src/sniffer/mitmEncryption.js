const crypto = require('crypto');
const NodeRSA = require('node-rsa');

/**
 * Offer encryption_begin to the Java client using the sniffer's RSA key (MITM leg).
 * @param {import('minecraft-protocol').Client} client - server-side peer (Java)
 * @param {import('minecraft-protocol').Server} server
 * @param {object} options - createServer options
 * @returns {Promise<void>} resolves when Java leg encryption is active
 */
function enableJavaEncryption(client, server, options) {
  const onlineMode = options['online-mode'] === true;

  return new Promise((resolve, reject) => {
    const serverId = onlineMode ? crypto.randomBytes(4).toString('hex') : '-';
    client.verifyToken = crypto.randomBytes(4);
    const publicKeyStrArr = server.serverKey.exportKey('pkcs8-public-pem').split('\n');
    let publicKeyStr = '';
    for (let i = 1; i < publicKeyStrArr.length - 1; i++) {
      publicKeyStr += publicKeyStrArr[i];
    }
    client.publicKey = Buffer.from(publicKeyStr, 'base64');

    client.once('encryption_begin', (packet) => {
      try {
        const sharedSecret = decryptSharedSecret(server, client, packet);
        client.setEncryption(sharedSecret);
        resolve();
      } catch (err) {
        reject(err);
      }
    });

    client.write('encryption_begin', {
      serverId,
      publicKey: client.publicKey,
      verifyToken: client.verifyToken,
      shouldAuthenticate: onlineMode,
    });
  });
}

function decryptSharedSecret(server, client, packet) {
  const keyRsa = new NodeRSA(server.serverKey.exportKey('pkcs1'), 'private', {
    encryptionScheme: 'pkcs1',
  });
  keyRsa.setOptions({ environment: 'browser' });

  if (packet.hasVerifyToken === false && packet.crypto) {
    const { concat } = require('minecraft-protocol/src/transforms/binaryStream');
    const signable = concat('buffer', client.verifyToken, 'i64', packet.crypto.salt);
    if (
      client.profileKeys &&
      !crypto.verify('sha256WithRSAEncryption', signable, client.profileKeys.public, packet.crypto.messageSignature)
    ) {
      throw new Error('invalid_public_key_signature');
    }
  } else if (packet.verifyToken != null) {
    const encryptedToken = packet.hasVerifyToken ? packet.crypto?.verifyToken : packet.verifyToken;
    const decryptedToken = keyRsa.decrypt(encryptedToken);
    if (!client.verifyToken.equals(decryptedToken)) {
      throw new Error('invalid_verify_token');
    }
  }

  return keyRsa.decrypt(packet.sharedSecret);
}

module.exports = { enableJavaEncryption };
