'use strict';

const { createLogger } = require('../utils/logger');

const log = createLogger('Registry');

/**
 * Registry / anvil codec version for chunk load + save.
 * Must match the protocol version used during capture.
 * @param {string} mcVersion
 */
function registryVersionFor(mcVersion) {
  if (require('minecraft-data')(mcVersion)) return mcVersion;
  const major = mcVersion.match(/^(1\.\d+)/)?.[1];
  if (major) {
    const versions = Object.keys(require('minecraft-data').versions.pc);
    const match = versions
      .filter((v) => v.startsWith(major))
      .sort()
      .pop();
    if (match) {
      log.warn(`No minecraft-data for ${mcVersion}; falling back to ${match}`);
      return match;
    }
  }
  return '1.21.1';
}

module.exports = { registryVersionFor };
