'use strict';

const assert = require('assert');
const lib = require('..');

const successWire = Buffer.from(
  '02c75004d6e5853d50802166019d8d456d09466c61796572426f7400',
  'hex'
);
const success = lib.decodeWire('success', successWire);
assert.strictEqual(success.ok, true, success.error);
assert.strictEqual(
  success.text,
  'success{uuid=c75004d6-e585-3d50-8021-66019d8d456d,username=FlayerBot,properties=0}'
);

const path = require('path');
const fs = require('fs');
const captureDir = path.resolve(__dirname, '../../../capture/1');
const advPath = path.join(captureDir, '0065_play_80_advancements.wire');
if (fs.existsSync(advPath)) {
  const adv = lib.decodeWireFile(advPath);
  assert.strictEqual(adv.ok, true, adv.error);
  assert.strictEqual(adv.packet, 'advancements');
  assert.ok(adv.text.startsWith('advancements{'), adv.text);
  assert.ok(!adv.text.includes('structure not fully decoded'), adv.text);
  console.log('advancements:', adv.text.slice(0, 120));
} else {
  console.log('skip advancements capture (no file at', advPath, ')');
}

console.log('loginAdvancements ok');
