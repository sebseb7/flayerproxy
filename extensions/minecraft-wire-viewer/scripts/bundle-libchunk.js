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
  'playPacketNames.js',
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

// eslint-disable-next-line @typescript-eslint/no-require-imports
const wirePath = require(path.join(destRoot, 'wirePath.js'));
const probe = 'raw/client/c2s_position/rx0/rz0/cx0/cz0/x0_y0_z0.c2s_position.wire';
if (wirePath.packetNameFromPath(probe) !== 'c2s_position') {
  console.error('Bundled wirePath.js does not recognize c2s_position paths');
  process.exit(1);
}
const captureProbes = [
  ['0290_play_51_entity_head_rotation.wire', 'entity_head_rotation'],
  ['0062_play_10_declare_commands.wire', 'declare_commands'],
  ['0047_play_77_system_chat.wire', 'system_chat'],
  ['0058_play_7d_set_ticking_state.wire', 'set_ticking_state'],
  ['0031_play_83_update_recipes.wire', 'update_recipes'],
];
for (const [capturePath, packet] of captureProbes) {
  if (wirePath.packetNameFromPath(capturePath) !== packet) {
    console.error(`Bundled wirePath.js: expected ${packet} from ${capturePath}`);
    process.exit(1);
  }
}

// eslint-disable-next-line @typescript-eslint/no-require-imports
const native = require(path.join(destRoot, 'index.js'));
const requiredDecoders = [
  'c2s_position',
  'entity_head_rotation',
  'declare_commands',
  'system_chat',
  'set_ticking_state',
  'update_recipes',
  'custom_payload',
  'feature_flags',
  'select_known_packs',
  'finish_configuration',
  'bundle_delimiter',
  'step_tick',
  'success',
  'advancements',
];
for (const packet of requiredDecoders) {
  if (!native.isPacketSupported(packet)) {
    console.error('Bundled native addon missing decoder:', packet);
    process.exit(1);
  }
}

console.log('Bundled libchunk into', destRoot);
