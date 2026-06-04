'use strict';

const assert = require('assert');
const lc = require('..');

// 1. parsePosition
{
  const posHex = '04' +
    '3ff0000000000000' + '4000000000000000' + '4008000000000000' +
    '0000000000000000' + '0000000000000000' + '0000000000000000' +
    '42340000' + '42b40000' + '00000007';
  const r = lc.parsePosition(Buffer.from(posHex, 'hex'));
  assert.ok(r);
  assert.strictEqual(r.teleportId, 4);
  assert.strictEqual(r.x, 1.0);
  assert.strictEqual(r.y, 2.0);
  assert.strictEqual(r.z, 3.0);
  assert.strictEqual(r.yaw, 45.0);
  assert.strictEqual(r.pitch, 90.0);
}

// 2. parseUpdateTime
{
  const timeHex = '00000000001234560000000000abcdef01';
  const r = lc.parseUpdateTime(Buffer.from(timeHex, 'hex'));
  assert.ok(r);
  assert.strictEqual(r.gameTime, 0x123456n);
  assert.strictEqual(r.dayTime, 0xabcdefn);
  assert.strictEqual(r.tickDayTime, true);
}

// 3. parseGameEvent
{
  const eventHex = '0340a00000';
  const r = lc.parseGameEvent(Buffer.from(eventHex, 'hex'));
  assert.ok(r);
  assert.strictEqual(r.event, 3);
  assert.strictEqual(r.value, 5.0);
}

// 4. parseSetTickingState
{
  const tickHex = '41a0000000';
  const r = lc.parseSetTickingState(Buffer.from(tickHex, 'hex'));
  assert.ok(r);
  assert.strictEqual(r.tickRate, 20.0);
  assert.strictEqual(r.isFrozen, false);
}

// 5. parseUpdateHealth
{
  const healthHex = '41a000001440a00000';
  const r = lc.parseUpdateHealth(Buffer.from(healthHex, 'hex'));
  assert.ok(r);
  assert.strictEqual(r.health, 20.0);
  assert.strictEqual(r.food, 20);
  assert.strictEqual(r.saturation, 5.0);
}

// 6. parseUpdateViewPosition
{
  const vpHex = '0b16';
  const r = lc.parseUpdateViewPosition(Buffer.from(vpHex, 'hex'));
  assert.ok(r);
  assert.strictEqual(r.chunkX, 11);
  assert.strictEqual(r.chunkZ, 22);
}

console.log('all parser tests passed!');
