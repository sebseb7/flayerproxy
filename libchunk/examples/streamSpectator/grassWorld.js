'use strict';

/**
 * Grass placeholder world for streamSpectator.
 * Terrain is built by the C static server / libchunk (mc_static_build_grass_chunk).
 * This module is intentionally empty — do not use prismarine or minecraft-protocol here.
 */
function sendGrassPlaceholderWorld(_client, _version) {
  throw new Error(
    'grassWorld.js: use mc_static_server or chunk_stream_receiver --spectator-port for flat grass terrain',
  );
}

module.exports = { sendGrassPlaceholderWorld };
