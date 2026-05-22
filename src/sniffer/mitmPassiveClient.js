'use strict';

const DefaultClientImpl = require('minecraft-protocol').Client;
const assert = require('assert');
const keepalive = require('minecraft-protocol/src/client/keepalive');
const compress = require('minecraft-protocol/src/client/compress');
const auth = require('minecraft-protocol/src/client/mojangAuth');
const microsoftAuth = require('minecraft-protocol/src/client/microsoftAuth');
const setProtocol = require('minecraft-protocol/src/client/setProtocol');
const tcpDns = require('minecraft-protocol/src/client/tcp_dns');
const autoVersion = require('minecraft-protocol/src/client/autoVersion');
const pluginChannels = require('minecraft-protocol/src/client/pluginChannels');
const versionChecking = require('minecraft-protocol/src/client/versionChecking');
const uuid = require('minecraft-protocol/src/datatypes/uuid');

/**
 * Upstream leg: Microsoft auth + keepalive only. No encrypt plugin (Java drives upstream encrypt).
 * No play.js — login_acknowledged, configuration, and play are relayed from Java only.
 */
function createPassiveClient(options) {
  assert.ok(options?.username, 'username is required');
  if (!options.version && !options.realms) options.version = false;

  const optVersion = options.version || require('minecraft-protocol/src/version').defaultVersion;
  const mcData = require('minecraft-data')(optVersion);
  if (!mcData) throw new Error(`unsupported protocol version: ${optVersion}`);
  const version = mcData.version;
  options.majorVersion = version.majorVersion;
  options.protocolVersion = version.version;
  const hideErrors = options.hideErrors || false;
  const Client = options.Client || DefaultClientImpl;

  const client = new Client(false, version.minecraftVersion, options.customPackets, hideErrors);

  tcpDns(client, options);

  const onReady = () => {
    if (options.version === false) autoVersion(client, options);
    setProtocol(client, options);
    keepalive(client, options);
    compress(client, options);
    pluginChannels(client, options);
    versionChecking(client, options);
  };

  if (typeof options.auth === 'function') {
    options.auth(client, options);
    onReady();
  } else {
    switch (options.auth) {
      case 'microsoft':
        if (options.realms) {
          microsoftAuth
            .realmAuthenticate(client, options)
            .then(() => microsoftAuth.authenticate(client, options))
            .catch((err) => client.emit('error', err))
            .then(onReady);
        } else {
          microsoftAuth.authenticate(client, options).catch((err) => client.emit('error', err));
          onReady();
        }
        break;
      case 'offline':
        client.username = options.username;
        client.uuid = uuid.nameToMcOfflineUUID(client.username);
        options.auth = 'offline';
        options.connect(client);
        onReady();
        break;
      default:
        throw new Error(`Unsupported auth: ${options.auth}`);
    }
  }

  return client;
}

module.exports = { createPassiveClient };
