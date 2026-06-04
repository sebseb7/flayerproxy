'use strict';

const states = require('minecraft-protocol/src/states');

/**
 * Configuration without minecraft-data dimensionCodec (breaks 1.21.10 clients).
 */
function installEmptyConfigJoin(client, server) {
  client.removeAllListeners('login_acknowledged');
  client.once('login_acknowledged', () => {
    client.state = states.CONFIGURATION;
    client.once('finish_configuration', () => {
      client.state = states.PLAY;
      server.emit('playerJoin', client);
    });
    client.write('finish_configuration', {});
  });
}

function buildPlayLoginPacket(version, client, server) {
  const mcData = require('minecraft-data')(version);
  const base = mcData.loginPacket ? { ...mcData.loginPacket } : {};
  delete base.dimensionCodec;
  return {
    ...base,
    entityId: client.id,
    maxPlayers: server.maxPlayers,
    worldState: {
      ...(base.worldState || {}),
      gamemode: 'spectator',
      isFlat: true,
    },
    enforcesSecureChat: false,
  };
}

module.exports = { installEmptyConfigJoin, buildPlayLoginPacket };
