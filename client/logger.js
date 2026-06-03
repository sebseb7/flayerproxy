'use strict';

const chalk = require('chalk');
const { PLAY, CFG, LOG_LEVELS } = require('./constants');
const { s2cPacketName, c2sPacketName, c2sDecodeName } = require('./packetNames');
const { decodePayload } = require('./decode');

const PHASE_STYLE = {
  connect: chalk.gray,
  login: chalk.yellow,
  config: chalk.cyan,
  play_join: chalk.magenta,
  play: chalk.green.bold,
};

function createLogger({ getPhase, logLevel, debug }) {
  return {
    _line(parts) {
      console.error(parts.filter(Boolean).join(' '));
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
      this._line([this._ts(), this._tag(), this._phaseLabel(), chalk.white(msg), extra || '']);
    },

    warn(msg, extra) {
      if (!this._can('warn')) return;
      this._line([
        this._ts(),
        this._tag(),
        this._phaseLabel(),
        chalk.yellow('warn'),
        chalk.yellow(msg),
        extra || '',
      ]);
    },

    error(msg, extra) {
      if (!this._can('error')) return;
      this._line([
        this._ts(),
        this._tag(),
        this._phaseLabel(),
        chalk.red.bold('error'),
        chalk.red(msg),
        extra || '',
      ]);
    },

    debug(msg, extra) {
      if (!this._can('debug')) return;
      this._line([
        this._ts(),
        this._tag(),
        this._phaseLabel(),
        chalk.gray('debug'),
        chalk.gray(msg),
        extra || '',
      ]);
    },

    phaseChange(from, to) {
      if (!this._can('info')) return;
      const fromS = PHASE_STYLE[from] || chalk.white;
      const toS = PHASE_STYLE[to] || chalk.white;
      this._line([
        this._ts(),
        this._tag(),
        chalk.white('phase'),
        fromS(from),
        chalk.dim('→'),
        toS(to),
      ]);
    },

    s2c(id, payload, detail) {
      if (!this._can('debug')) return;
      const ph = getPhase();
      const len = payload.length;
      const name = s2cPacketName(ph, id);
      if (id !== PLAY.KEEP_ALIVE && id !== CFG.KEEP_ALIVE && !name && len > 4096 && !debug) return;
      const nameStr = name ? chalk.white(name) : chalk.dim('?');
      const summary = name ? decodePayload(name, payload) : null;
      const summaryStr = summary ? chalk.cyan(` ${summary}`) : '';
      this._line([
        this._ts(),
        this._tag(),
        this._phaseLabel(),
        chalk.bgMagenta.black(' S2C '),
        this._pktId(id),
        nameStr,
        chalk.dim(`len=${len}`),
        summaryStr,
        detail && !summary ? detail : '',
      ]);
    },

    c2s(id, payload, detail) {
      if (!this._can('debug')) return;
      if (id === PLAY.C2S_TICK_END && !debug) return;
      const ph = getPhase();
      const len = payload.length;
      const name = c2sPacketName(ph, id);
      const decodeName = c2sDecodeName(ph, id);
      const nameStr = name ? chalk.white(name) : chalk.dim('?');
      const summary = decodeName ? decodePayload(decodeName, payload) : null;
      const summaryStr = summary ? chalk.cyan(` ${summary}`) : '';
      this._line([
        this._ts(),
        this._tag(),
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
      this._line([
        this._ts(),
        this._tag(),
        this._phaseLabel(),
        chalk.cyan.bold('●'),
        chalk.cyan(label),
        detail || '',
      ]);
    },
  };
}

module.exports = { createLogger, LOG_LEVELS };
