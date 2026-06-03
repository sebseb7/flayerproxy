import chalk from 'chalk';
import { PLAY, CFG, LOG_LEVELS } from '../client/constants.js';
import { s2cPacketName, c2sPacketName, c2sDecodeName } from '../client/packetNames.js';
import { decodePayload } from '../client/decode.js';

const PHASE_STYLE = {
  handshake: chalk.gray,
  status: chalk.gray,
  login: chalk.yellow,
  config: chalk.cyan,
  play_join: chalk.magenta,
  play: chalk.green.bold,
};

const LEVEL_TAG = {
  debug: () => chalk.gray.bold('DBG'),
  info: () => chalk.white('INF'),
  warn: () => chalk.yellow.bold('WRN'),
  error: () => chalk.red.bold('ERR'),
};

export function createServerLogger({ getPhase, logLevel = LOG_LEVELS.info, debug = false }) {
  const showLevelTags = logLevel >= LOG_LEVELS.debug;

  function emit(level, parts) {
    console.error(
      [
        chalk.dim(new Date().toISOString().slice(11, 23)),
        chalk.bgGreen.black.bold(' mc-server '),
        showLevelTags ? LEVEL_TAG[level]?.() : null,
        ...parts,
      ]
        .filter(Boolean)
        .join(' '),
    );
  }

  function can(level) {
    return logLevel >= LOG_LEVELS[level];
  }

  function phaseLabel() {
    const p = getPhase();
    const style = PHASE_STYLE[p] || chalk.white;
    return style(`[${p}]`);
  }

  function pktId(id) {
    return chalk.hex('#ffaa00')('0x' + id.toString(16).padStart(2, '0'));
  }

  return {
    info(msg, extra) {
      if (!can('info')) return;
      emit('info', [phaseLabel(), chalk.white(msg), extra || '']);
    },
    warn(msg, extra) {
      if (!can('warn')) return;
      emit('warn', [phaseLabel(), chalk.yellow(msg), extra || '']);
    },
    error(msg, extra) {
      if (!can('error')) return;
      emit('error', [phaseLabel(), chalk.red(msg), extra || '']);
    },
    debug(msg, extra) {
      if (!can('debug')) return;
      emit('debug', [phaseLabel(), chalk.gray(msg), extra || '']);
    },
    event(label, detail) {
      if (!can('info')) return;
      emit('info', [phaseLabel(), chalk.cyan.bold('●'), chalk.cyan(label), detail || '']);
    },
    phaseChange(from, to) {
      if (!can('info')) return;
      const fromS = PHASE_STYLE[from] || chalk.white;
      const toS = PHASE_STYLE[to] || chalk.white;
      emit('info', [chalk.white('phase'), fromS(from), chalk.dim('→'), toS(to)]);
    },
    s2c(id, payload) {
      if (!can('debug')) return;
      const ph = getPhase();
      const len = payload.length;
      const name = s2cPacketName(ph, id);
      if (id !== PLAY.KEEP_ALIVE && id !== CFG.KEEP_ALIVE && !name && len > 4096 && !debug) return;
      const nameStr = name ? chalk.white(name) : chalk.dim('?');
      const summary = name ? decodePayload(name, payload) : null;
      emit('debug', [
        phaseLabel(),
        chalk.bgMagenta.black(' S2C '),
        pktId(id),
        nameStr,
        chalk.dim(`len=${len}`),
        summary ? chalk.cyan(` ${summary}`) : '',
      ]);
    },
    c2s(id, payload) {
      if (!can('debug')) return;
      if (id === PLAY.C2S_TICK_END && !debug) return;
      const ph = getPhase();
      const len = payload.length;
      const name = c2sPacketName(ph, id);
      const decodeName = c2sDecodeName(ph, id);
      const nameStr = name ? chalk.white(name) : chalk.dim('?');
      const summary = decodeName ? decodePayload(decodeName, payload) : null;
      emit('debug', [
        phaseLabel(),
        chalk.bgCyan.black(' C2S '),
        pktId(id),
        nameStr,
        chalk.dim(`len=${len}`),
        summary ? chalk.cyan(` ${summary}`) : '',
      ]);
    },
  };
}
