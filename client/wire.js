import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const lc = require('../libchunk/js/index.js');

export const createFrameProcessor = lc.createFrameProcessor;
export const buildFrame = lc.buildFrame;
export const writeVarInt = lc.writeVarInt;
export const writeString = lc.writeString;

export function readVarInt(buf, off = 0) {
  return lc.readVarIntAt(buf, off);
}

export function readString(buf, off) {
  return lc.readStringAt(buf, off);
}
