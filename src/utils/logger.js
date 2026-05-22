const COLORS = {
  reset: '\x1b[0m',
  dim: '\x1b[2m',
  red: '\x1b[31m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  blue: '\x1b[34m',
  magenta: '\x1b[35m',
  cyan: '\x1b[36m',
  white: '\x1b[37m',
};

const LEVEL_COLORS = {
  DEBUG: COLORS.dim,
  INFO: COLORS.green,
  WARN: COLORS.yellow,
  ERROR: COLORS.red,
};

function timestamp() {
  return new Date().toISOString().replace('T', ' ').replace('Z', '');
}

function createLogger(module) {
  const tag = `[${module}]`;

  function log(level, ...args) {
    const color = LEVEL_COLORS[level] || COLORS.white;
    const ts = COLORS.dim + timestamp() + COLORS.reset;
    const lvl = color + level.padEnd(5) + COLORS.reset;
    const mod = COLORS.cyan + tag + COLORS.reset;
    console.log(`${ts} ${lvl} ${mod}`, ...args);
  }

  return {
    debug: (...args) => log('DEBUG', ...args),
    info: (...args) => log('INFO', ...args),
    warn: (...args) => log('WARN', ...args),
    error: (...args) => log('ERROR', ...args),
  };
}

module.exports = { createLogger };
