import chalk from 'chalk';
import { PLAY, CFG, LOG_LEVELS } from './constants.js';
import { s2cPacketName, c2sPacketName, c2sDecodeName } from './packetNames.js';
import { decodePayload } from './decode.js';
import { writeLogLine, closeLogSink } from './logSink.js';
import { isNoisyC2sPacket, isNoisyS2cPacket } from './logNoise.js';
import { createEntityTracker } from './entityTracker.js';

const PHASE_STYLE = {
  connect: chalk.gray,
  login: chalk.yellow,
  config: chalk.cyan,
  play_join: chalk.magenta,
  death: chalk.red.bold,
  play: chalk.green.bold,
};

const LEVEL_TAG = {
  debug: () => chalk.gray.bold('DBG'),
  info: () => chalk.white('INF'),
  warn: () => chalk.yellow.bold('WRN'),
  error: () => chalk.red.bold('ERR'),
};

export function createLogger({ getPhase, logLevel, debug, logPingTick = false }) {
  const entityTracker = createEntityTracker();
  const showLevelTags = logLevel >= LOG_LEVELS.debug;

  function levelTag(level) {
    if (!showLevelTags) return null;
    const fn = LEVEL_TAG[level];
    return fn ? fn() : null;
  }

  return {
    _line(parts) {
      writeLogLine(parts.filter(Boolean).join(' '));
    },

    _emit(level, parts) {
      this._line([this._ts(), this._tag(), levelTag(level), ...parts]);
    },

    _ts() {
      return chalk.dim(new Date().toISOString().slice(11, 23));
    },

    _tag() {
      return chalk.bgBlue.black.bold(' mc-client ');
    },

    _phaseLabel() {
      const p = getPhase();
      const style = PHASE_STYLE[p] || chalk.white;
      return style(`[${p}]`);
    },

    _pktId(id) {
      return chalk.hex('#ffaa00')('0x' + id.toString(16).padStart(2, '0'));
    },

    _can(level) {
      return logLevel >= LOG_LEVELS[level];
    },

    info(msg, extra) {
      if (!this._can('info')) return;
      this._emit('info', [this._phaseLabel(), chalk.white(msg), extra || '']);
    },

    warn(msg, extra) {
      if (!this._can('warn')) return;
      this._emit('warn', [this._phaseLabel(), chalk.yellow(msg), extra || '']);
    },

    error(msg, extra) {
      if (!this._can('error')) return;
      this._emit('error', [this._phaseLabel(), chalk.red(msg), extra || '']);
    },

    debug(msg, extra) {
      if (!this._can('debug')) return;
      this._emit('debug', [this._phaseLabel(), chalk.gray(msg), extra || '']);
    },

    phaseChange(from, to) {
      if (!this._can('info')) return;
      const fromS = PHASE_STYLE[from] || chalk.white;
      const toS = PHASE_STYLE[to] || chalk.white;
      this._emit('info', [chalk.white('phase'), fromS(from), chalk.dim('→'), toS(to)]);
    },

    s2c(id, payload, detail) {
      if (!this._can('debug')) return;
      const ph = getPhase();
      const len = payload.length;
      const name = s2cPacketName(ph, id);
      if (!logPingTick && isNoisyS2cPacket(id)) return;
      if (id !== PLAY.KEEP_ALIVE && id !== CFG.KEEP_ALIVE && !name && len > 4096 && !debug) return;
      const nameStr = name ? chalk.white(name) : chalk.dim('?');
      const summary = name ? decodePayload(name, payload) : null;
      const summaryStr = summary ? chalk.cyan(` ${summary}`) : '';
      const entityNote = name ? entityTracker.noteS2c(name, payload) : null;
      this._emit('debug', [
        this._phaseLabel(),
        chalk.bgMagenta.black(' S2C '),
        this._pktId(id),
        nameStr,
        chalk.dim(`len=${len}`),
        summaryStr,
        detail && !summary ? detail : '',
        entityNote || '',
      ]);
    },

    c2s(id, payload, detail) {
      if (!this._can('debug')) return;
      if (!logPingTick && isNoisyC2sPacket(id)) return;
      const ph = getPhase();
      const len = payload.length;
      const name = c2sPacketName(ph, id);
      const decodeName = c2sDecodeName(ph, id);
      const nameStr = name ? chalk.white(name) : chalk.dim('?');
      const summary = decodeName ? decodePayload(decodeName, payload) : null;
      const summaryStr = summary ? chalk.cyan(` ${summary}`) : '';
      this._emit('debug', [
        this._phaseLabel(),
        chalk.bgGreen.black(' C2S '),
        this._pktId(id),
        nameStr,
        chalk.dim(`len=${len}`),
        summaryStr,
        detail && !summary ? detail : '',
      ]);
    },

    event(label, detail) {
      if (!this._can('info')) return;
      this._emit('info', [
        this._phaseLabel(),
        chalk.cyan.bold('●'),
        chalk.cyan(label),
        detail || '',
      ]);
    },

    close() {
      closeLogSink();
    },
  };
}
