const crypto = require('crypto');
const yggdrasil = require('yggdrasil');
const { concat } = require('minecraft-protocol/src/transforms/binaryStream');
const mc = require('minecraft-protocol');
const { traceTx } = require('./packetTrace');

const states = mc.states;

const JOIN_RETRY_ATTEMPTS = 3;
const JOIN_RETRY_BASE_MS = 400;

function formatJoinError(err) {
  const parts = [err?.message || String(err)];
  if (err?.code) parts.push(`code=${err.code}`);
  if (err?.cause?.message) parts.push(`cause=${err.cause.message}`);
  if (err?.cause?.code) parts.push(`causeCode=${err.cause.code}`);
  return parts.join(' ');
}

function joinServerWithRetry(yggdrasilServer, accessToken, profileId, packet, sharedSecret) {
  return new Promise((resolve, reject) => {
    let attempt = 0;

    const run = () => {
      attempt += 1;
      yggdrasilServer.join(
        accessToken,
        profileId,
        packet.serverId,
        sharedSecret,
        packet.publicKey,
        (joinErr) => {
          if (!joinErr) return resolve();
          if (attempt >= JOIN_RETRY_ATTEMPTS) {
            return reject(
              new Error(
                `Mojang join failed after ${JOIN_RETRY_ATTEMPTS} attempts: ${formatJoinError(joinErr)}`,
              ),
            );
          }
          const delay = JOIN_RETRY_BASE_MS * attempt;
          setTimeout(run, delay);
        },
      );
    };

    run();
  });
}

function mcPubKeyToPem(mcPubKeyBuffer) {
  let pem = '-----BEGIN PUBLIC KEY-----\n';
  let base64PubKey = mcPubKeyBuffer.toString('base64');
  const maxLineLength = 64;
  while (base64PubKey.length > 0) {
    pem += base64PubKey.substring(0, maxLineLength) + '\n';
    base64PubKey = base64PubKey.substring(maxLineLength);
  }
  pem += '-----END PUBLIC KEY-----\n';
  return pem;
}

/**
 * Answer the real server's encryption_begin — only after Java has answered the sniffer.
 * @param {object} session
 * @param {object} config
 */
async function completeUpstreamEncryption(session, config) {
  const client = session.upstream;
  const packet = session.upstreamEncryptRequest;
  if (!client || !packet || session.upstreamEncryptDone) {
    return;
  }

  const sniffer = config.sniffer || {};
  const options = {
    agent: sniffer.agent,
    sessionServer: sniffer.sessionServer,
    haveCredentials: !!client.session?.accessToken,
    accessToken: client.session?.accessToken,
  };
  const yggdrasilServer = yggdrasil.server({
    agent: options.agent,
    host: options.sessionServer || 'https://sessionserver.mojang.com',
  });

  const sharedSecret = await new Promise((resolve, reject) => {
    crypto.randomBytes(16, (err, buf) => (err ? reject(err) : resolve(buf)));
  });

  if (options.haveCredentials && client.session?.selectedProfile?.id) {
    await joinServerWithRetry(
      yggdrasilServer,
      options.accessToken,
      client.session.selectedProfile.id,
      packet,
      sharedSecret,
    );
  }

  sendEncryptionKeyResponse(client, packet, sharedSecret, options, yggdrasilServer);
  session.upstreamEncryptDone = true;
  traceTx(session.packetLog, 'backend', 'C2S', { state: states.LOGIN, name: 'encryption_begin' }, null, {
    action: 'upstream_encrypt',
    bridge: 'sniffer→backend',
    method: 'parsed',
    note: 'after Java answered sniffer encryption_begin',
  });
}

function sendEncryptionKeyResponse(client, packet, sharedSecret, options, yggdrasilServer) {
  void yggdrasilServer;
  const mcData = require('minecraft-data')(client.version);
  const pubKey = mcPubKeyToPem(packet.publicKey);
  const encryptedSharedSecretBuffer = crypto.publicEncrypt(
    { key: pubKey, padding: crypto.constants.RSA_PKCS1_PADDING },
    sharedSecret,
  );
  const encryptedVerifyTokenBuffer = crypto.publicEncrypt(
    { key: pubKey, padding: crypto.constants.RSA_PKCS1_PADDING },
    packet.verifyToken,
  );

  if (mcData.supportFeature('signatureEncryption')) {
    const salt = BigInt(Date.now());
    client.write('encryption_begin', {
      sharedSecret: encryptedSharedSecretBuffer,
      hasVerifyToken: client.profileKeys == null,
      crypto: client.profileKeys
        ? {
            salt,
            messageSignature: crypto.sign(
              'sha256WithRSAEncryption',
              concat('buffer', packet.verifyToken, 'i64', salt),
              client.profileKeys.private,
            ),
          }
        : { verifyToken: encryptedVerifyTokenBuffer },
    });
  } else {
    client.write('encryption_begin', {
      sharedSecret: encryptedSharedSecretBuffer,
      verifyToken: encryptedVerifyTokenBuffer,
    });
  }
  client.setEncryption(sharedSecret);
}

module.exports = { completeUpstreamEncryption };
