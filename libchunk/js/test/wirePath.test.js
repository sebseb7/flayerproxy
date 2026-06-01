'use strict';

const assert = require('assert');
const path = require('path');
const wirePath = require('../wirePath');

const ref =
  'out2/0290_play_51_entity_head_rotation.wire';
assert.strictEqual(wirePath.packetNameFromPath(ref), 'entity_head_rotation');

const meta = wirePath.parseWirePath(ref);
assert.strictEqual(meta.packet, 'entity_head_rotation');
assert.strictEqual(meta.captureSeq, 290);
assert.strictEqual(meta.capturePhase, 'play');
assert.strictEqual(meta.protocolId, 0x51);

const cfg = wirePath.packetNameFromPath('out2/0001_config_01_custom_payload.wire');
assert.strictEqual(cfg, 'custom_payload');

const sniffer =
  'raw/client/c2s_position/rx0/rz0/cx0/cz0/x0_y0_z0.c2s_position.wire';
assert.strictEqual(wirePath.packetNameFromPath(sniffer), 'c2s_position');

assert.ok(wirePath.PACKET_NAMES.has('bundle_delimiter'));
assert.ok(wirePath.PACKET_NAMES.has('entity_head_rotation'));
assert.ok(wirePath.DECODED_PACKET_NAMES.has('map_chunk'));

console.log('wirePath ok', wirePath.PACKET_NAMES.size, 'known names');
