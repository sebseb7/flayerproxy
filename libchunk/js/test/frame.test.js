'use strict';

const assert = require('assert');
const lc = require('../index.js');

const payload = Buffer.from([0xde, 0xad]);
const wire = lc.buildFrame(0x0e, payload);
const one = lc.tryReadFrame(wire);
assert.ok(one);
assert.strictEqual(one.id, 0x0e);
assert.ok(one.payload.equals(payload));

const frames = [];
const feed = lc.createFrameProcessor((id, pl) => frames.push({ id, pl: Buffer.from(pl) }));
feed(Buffer.concat([wire, lc.buildFrame(0x03, Buffer.alloc(0))]));
assert.strictEqual(frames.length, 2);
assert.ok(frames[0].pl.equals(payload));
assert.strictEqual(frames[1].id, 0x03);

console.log('frame.test.js ok');
