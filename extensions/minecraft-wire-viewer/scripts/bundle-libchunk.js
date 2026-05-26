'use strict';

/** Copy libchunk/js into node_modules (real files, not symlink) for vsce packaging. */
const fs = require('fs');
const path = require('path');

const extRoot = path.resolve(__dirname, '..');
const srcRoot = path.resolve(extRoot, '../../libchunk/js');
const destRoot = path.join(extRoot, 'vendor', 'libchunk');

const COPY = [
  'index.js',
  'index.d.ts',
  'wirePath.js',
  'package.json',
  'build/Release/chunk.node',
];

function rmrf(p) {
  fs.rmSync(p, { recursive: true, force: true });
}

function copyFile(src, dest) {
  fs.mkdirSync(path.dirname(dest), { recursive: true });
  fs.copyFileSync(src, dest);
}

if (!fs.existsSync(path.join(srcRoot, 'build/Release/chunk.node'))) {
  console.error('Native addon missing. Run: cd libchunk/js && npm run build');
  process.exit(1);
}

rmrf(destRoot);
fs.mkdirSync(destRoot, { recursive: true });

for (const rel of COPY) {
  if (rel === 'package.json') continue;
  const src = path.join(srcRoot, rel);
  if (!fs.existsSync(src)) {
    console.error('Missing:', src);
    process.exit(1);
  }
  copyFile(src, path.join(destRoot, rel));
}

fs.writeFileSync(
  path.join(destRoot, 'package.json'),
  JSON.stringify(
    {
      name: '@flayerproxy/libchunk',
      version: '0.1.0',
      main: 'index.js',
      types: 'index.d.ts',
      description: 'libchunk native decoder (bundled for VS Code extension)',
    },
    null,
    2
  ) + '\n'
);

console.log('Bundled libchunk into', destRoot);
