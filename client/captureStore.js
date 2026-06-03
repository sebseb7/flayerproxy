import chalk from 'chalk';
import { writeLogLine } from './logSink.js';

/** In-memory S2C capture from upstream (config + play_join). Shared by client and server. */

/** @typedef {{ id: number, payload: Buffer }} CapturedPacket */

const listeners = new Set();

/** @type {CapturedPacket[]} */
let config = [];
/** @type {CapturedPacket[]} */
let playJoin = [];
let ready = false;

export function resetCapture() {
  config = [];
  playJoin = [];
  ready = false;
}

export function isCaptureReady() {
  return ready;
}

export function recordConfigS2c(id, payload) {
  if (ready) return;
  config.push({ id, payload: Buffer.from(payload) });
}

export function recordPlayJoinS2c(id, payload) {
  if (ready) return;
  playJoin.push({ id, payload: Buffer.from(payload) });
}

export function markCaptureReady() {
  if (ready) return;
  ready = true;
  const snap = getCapture();
  for (const fn of listeners) {
    try {
      fn(snap);
    } catch (e) {
      writeLogLine(
        `${chalk.bgBlue.black.bold(' mc-client ')} ${chalk.red('captureStore listener:')} ${e?.message || e}`,
      );
    }
  }
  listeners.clear();
}

/** @param {(snap: ReturnType<typeof getCapture>) => void} fn */
export function onCaptureReady(fn) {
  if (ready) fn(getCapture());
  else listeners.add(fn);
}

export function getCapture() {
  return { config, playJoin, ready };
}
