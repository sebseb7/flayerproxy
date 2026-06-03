import fs from 'node:fs';

const ANSI_RE = /\u001b\[[0-9;]*m/g;

function stripAnsi(s) {
  return String(s).replace(ANSI_RE, '');
}

/** @type {import('node:fs').WriteStream | null} */
let logStream = null;

/** @param {string | null | undefined} logFile */
export function initLogSink(logFile) {
  closeLogSink();
  if (logFile) logStream = fs.createWriteStream(logFile, { flags: 'w' });
}

export function writeLogLine(line) {
  console.error(line);
  if (logStream) logStream.write(stripAnsi(line) + '\n');
}

export function closeLogSink() {
  logStream?.end();
  logStream = null;
}

export function isLogSinkOpen() {
  return logStream !== null;
}
