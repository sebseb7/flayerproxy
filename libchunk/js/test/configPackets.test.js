'use strict';

const assert = require('assert');
const fs = require('fs');
const path = require('path');
const lib = require('..');

const captureDir = path.resolve(__dirname, '../../../capture/1');

const cases = [
  {
    file: '0001_config_01_custom_payload.wire',
    packet: 'custom_payload',
    expect: 'custom_payload{channel=minecraft:brand,brand="Paper"}',
  },
  {
    file: '0002_config_0c_feature_flags.wire',
    packet: 'feature_flags',
    expect: 'feature_flags{count=1,flags=[minecraft:vanilla]}',
  },
  {
    file: '0003_config_0e_select_known_packs.wire',
    packet: 'select_known_packs',
    expect: 'select_known_packs{count=1,packs=[minecraft:core:1.21.10]}',
  },
  {
    file: '0026_config_03_finish_configuration.wire',
    packet: 'finish_configuration',
    expect: 'finish_configuration{}',
  },
  {
    file: '0044_play_7e_step_tick.wire',
    packet: 'step_tick',
    expect: 'step_tick{skipTick=false}',
  },
  {
    file: '0073_play_00_bundle_delimiter.wire',
    packet: 'bundle_delimiter',
    expect: 'bundle_delimiter{}',
  },
];

for (const { file, packet, expect } of cases) {
  const wirePath = path.join(captureDir, file);
  assert.ok(fs.existsSync(wirePath), `missing capture: ${wirePath}`);
  assert.ok(lib.isPacketSupported(packet), packet);
  const r = lib.decodeWireFile(wirePath);
  assert.strictEqual(r.ok, true, `${file}: ${r.error}`);
  assert.strictEqual(r.packet, packet);
  assert.strictEqual(r.text, expect, file);
}

console.log('configPackets ok', cases.length);
