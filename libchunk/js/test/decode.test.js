'use strict';

const assert = require('assert');
const path = require('path');
const lib = require('..');

const repoRoot = path.resolve(__dirname, '../../..');
const rawRoot = path.join(repoRoot, 'logs/sniffer/chunks/png/raw');

function findWire(globPart) {
  const { execSync } = require('child_process');
  const out = execSync(`find "${rawRoot}" -path '*${globPart}*' -name '*.wire' 2>/dev/null | head -1`, {
    encoding: 'utf8',
  }).trim();
  return out || null;
}

const entityMeta = findWire('entity_metadata/eu117');
const blockChange = findWire('block_change/rx24/rz69/cx785');

if (entityMeta) {
  const r = lib.decodeWireFile(entityMeta);
  assert.strictEqual(r.ok, true, r.error);
  assert.ok(r.text && r.text.length > 0);
  assert.strictEqual(r.packet, 'entity_metadata');
  console.log('entity_metadata:', r.text);
}

if (blockChange) {
  const r = lib.decodeWireFile(blockChange);
  assert.strictEqual(r.ok, true, r.error);
  assert.strictEqual(r.packet, 'block_change');
  console.log('block_change:', r.text);
}

assert.ok(lib.supportedPackets().includes('map_chunk'));
console.log('ok', lib.supportedPackets().length, 'packets');
