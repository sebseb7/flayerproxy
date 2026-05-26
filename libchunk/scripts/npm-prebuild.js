'use strict';

const { execSync } = require('child_process');
const path = require('path');

const libchunkDir = path.resolve(__dirname, '..');
execSync('make', { cwd: libchunkDir, stdio: 'inherit' });
