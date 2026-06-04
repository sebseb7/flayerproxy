import chalk from 'chalk';
import { writeLogLine } from './logSink.js';
import { createJoinDataTracker } from './joinDataTracker.js';

/** In-memory S2C capture from upstream (config + play_join). Shared by client and server. */

/** @typedef {{ id: number, payload: Buffer }} CapturedPacket */

const listeners = new Set();

/** @type {CapturedPacket[]} */
let config = [];
/** @type {CapturedPacket[]} */
let playJoin = [];
let ready = false;

const joinDataTracker = createJoinDataTracker();

export function resetCapture() {
  config = [];
  playJoin = [];
  ready = false;
  joinDataTracker.reset();
}

export function isCaptureReady() {
  return ready;
}

let upstreamSock = null;
let getUpstreamPhase = () => 'connect';
let upstreamSend = null;

export function setUpstreamClient(sock, getPhaseFn, sendFn) {
  upstreamSock = sock;
  getUpstreamPhase = getPhaseFn;
  upstreamSend = sendFn;
}

export function getUpstreamClient() {
  return {
    sock: upstreamSock,
    getPhase: getUpstreamPhase,
    send: upstreamSend,
  };
}

let downstreamSock = null;
let getDownstreamPhase = () => 'handshake';
let downstreamSend = null;

export function setDownstreamClient(sock, getPhaseFn, sendFn) {
  downstreamSock = sock;
  getDownstreamPhase = getPhaseFn;
  downstreamSend = sendFn;
}

export function getDownstreamClient() {
  return {
    sock: downstreamSock,
    getPhase: getDownstreamPhase,
    send: downstreamSend,
  };
}

export function recordConfigS2c(id, payload) {
  if (ready) return;
  config.push({ id, payload: Buffer.from(payload) });
}

export function recordPlayJoinS2c(id, payload) {
  if (ready) return;
  playJoin.push({ id, payload: Buffer.from(payload) });
}

export function trackPlayPacket(id, payload) {
  joinDataTracker.noteS2c(id, payload);
}

/** Drop pre-death join burst; post-respawn play_join is recorded fresh. */
export function clearPlayJoinCapture() {
  if (ready) return;
  playJoin = [];
  joinDataTracker.reset();
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
  const mergedPlayJoin = joinDataTracker.mergeWith(playJoin);
  return { config, playJoin: mergedPlayJoin, ready };
}
