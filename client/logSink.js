import fs from 'node:fs';
import path from 'node:path';

const ANSI_RE = /\u001b\[[0-9;]*m/g;

function stripAnsi(s) {
  return String(s).replace(ANSI_RE, '');
}

/** @type {import('node:fs').WriteStream | null} */
let logStream = null;

/** @param {string | null | undefined} logFile */
export function initLogSink(logFile) {
  closeLogSink();
  if (logFile) {
    const dir = path.dirname(logFile);
    if (dir && dir !== '.') {
      fs.mkdirSync(dir, { recursive: true });
    }
    logStream = fs.createWriteStream(logFile, { flags: 'w' });
  }
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
