'use strict';

const mc = require('minecraft-protocol');
const states = mc.states;

/**
 * minecraft-protocol installServerHandlers loads server/login.js, which on login_start
 * immediately sends compress + success (offline UUID) and on login_acknowledged sends an
 * empty registry. That races the MITM relay and causes client LoginTimeout.
 */
function stripVanillaLoginHandlers(client) {
  client.removeAllListeners('login_start');
  client.removeAllListeners('encryption_begin');
  client.removeAllListeners('login_acknowledged');
  client.removeAllListeners('finish_configuration');
}

/**
 * @param {import('minecraft-protocol').Client} client
 */
function ensureClientConfigurationState(client) {
  if (client.state !== states.CONFIGURATION) {
    client.state = states.CONFIGURATION;
  }
}

module.exports = { stripVanillaLoginHandlers, ensureClientConfigurationState };
