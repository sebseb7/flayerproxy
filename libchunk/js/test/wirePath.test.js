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

const c2s = wirePath.packetNameFromPath('capture/1/0003_c2s_login_03_login_acknowledged.wire');
assert.strictEqual(c2s, 'login_acknowledged');
const c2sMeta = wirePath.parseWirePath('capture/1/0003_c2s_login_03_login_acknowledged.wire');
assert.strictEqual(c2sMeta.captureDir, 'c2s');
assert.strictEqual(c2sMeta.capturePhase, 'login');

const sniffer =
  'raw/client/c2s_position/rx0/rz0/cx0/cz0/x0_y0_z0.c2s_position.wire';
assert.strictEqual(wirePath.packetNameFromPath(sniffer), 'c2s_position');

assert.ok(wirePath.PACKET_NAMES.has('bundle_delimiter'));
assert.ok(wirePath.PACKET_NAMES.has('entity_head_rotation'));
assert.ok(wirePath.DECODED_PACKET_NAMES.has('map_chunk'));
assert.ok(wirePath.DECODED_PACKET_NAMES.has('custom_payload'));
assert.ok(wirePath.DECODED_PACKET_NAMES.has('select_known_packs'));
assert.ok(wirePath.DECODED_PACKET_NAMES.has('bundle_delimiter'));
assert.ok(wirePath.DECODED_PACKET_NAMES.has('success'));
assert.strictEqual(
  wirePath.packetNameFromPath('out2/0000_login_02_success.wire'),
  'success'
);

console.log('wirePath ok', wirePath.PACKET_NAMES.size, 'known names');
