import { createRequire } from 'node:module';
import { LOGIN, CFG, PLAY } from './constants.js';

const require = createRequire(import.meta.url);
const playS2cById = require('../libchunk/js/playS2cById.js');

const S2C_NAMES = {
  [LOGIN.DISCONNECT]: 'login_disconnect',
  [CFG.CUSTOM_PAYLOAD]: 'custom_payload',
  [CFG.FINISH]: 'finish_configuration',
  [CFG.KEEP_ALIVE]: 'keep_alive',
  [CFG.PING]: 'ping',
  [CFG.REGISTRY_DATA]: 'registry_data',
  [CFG.FEATURE_FLAGS]: 'feature_flags',
  [CFG.UPDATE_TAGS]: 'tags',
  [CFG.SELECT_KNOWN_PACKS]: 'select_known_packs',
  [PLAY.CHUNK_BATCH_FINISHED]: 'chunk_batch_finished',
  [PLAY.KEEP_ALIVE]: 'keep_alive',
  [PLAY.POSITION]: 'position',
  [PLAY.PING]: 'ping',
};

/** Same protocol state as play (join burst, death screen, or steady play). */
function isPlayPhase(ph) {
  return ph === 'play' || ph === 'play_join' || ph === 'death';
}

export function s2cPacketName(ph, id) {
  if (ph === 'status') {
    if (id === 0x00) return 'status_response';
    if (id === 0x01) return 'pong';
  }
  if (ph === 'login' && id === LOGIN.SUCCESS) return 'success';
  if (ph === 'config' && id === CFG.DISCONNECT) return 'configuration_disconnect';
  if (isPlayPhase(ph)) {
    const n = playS2cById[id];
    if (n) return n;
  }
  return S2C_NAMES[id] || null;
}

export function c2sPacketName(ph, id) {
  if (ph === 'status') {
    if (id === 0x00) return 'status_request';
    if (id === 0x01) return 'ping';
  }
  if (ph === 'connect' && id === 0x00) return 'handshake';
  if (ph === 'login') {
    if (id === LOGIN.C2S_START) return 'login_start';
    if (id === LOGIN.C2S_ACK) return 'login_acknowledged';
    return null;
  }
  if (ph === 'config') {
    if (id === CFG.C2S_SETTINGS) return 'client_information';
    if (id === CFG.C2S_CUSTOM_PAYLOAD) return 'custom_payload';
    if (id === CFG.C2S_FINISH) return 'finish_configuration';
    if (id === CFG.C2S_KEEP_ALIVE) return 'keep_alive';
    if (id === CFG.C2S_PONG) return 'pong';
    if (id === CFG.C2S_SELECT_KNOWN_PACKS) return 'select_known_packs';
    return null;
  }
  if (isPlayPhase(ph)) {
    if (id === PLAY.C2S_TELEPORT_CONFIRM) return 'teleport_confirm';
    if (id === PLAY.C2S_CHUNK_BATCH_RECEIVED) return 'chunk_batch_received';
    if (id === PLAY.C2S_CLIENT_COMMAND) return 'client_command';
    if (id === PLAY.C2S_TICK_END) return 'tick_end';
    if (id === PLAY.C2S_KEEP_ALIVE) return 'keep_alive';
    if (id === PLAY.C2S_PLAYER_LOADED) return 'player_loaded';
    if (id === PLAY.C2S_PONG) return 'pong';
    if (id === PLAY.C2S_CUSTOM_PAYLOAD) return 'custom_payload';
    if (id === PLAY.C2S_POSITION) return 'position';
    if (id === PLAY.C2S_POSITION_LOOK) return 'position_look';
    if (id === PLAY.C2S_MOVE_ROT) return 'move_rot';
    if (id === PLAY.C2S_MOVE_STATUS) return 'move_status';
  }
  return null;
}

export function c2sDecodeName(ph, id) {
  if (ph === 'status') {
    if (id === 0x00) return 'status_request';
    if (id === 0x01) return 'ping';
  }
  if (ph === 'config' && id === CFG.C2S_SELECT_KNOWN_PACKS) return 'select_known_packs';
  if (isPlayPhase(ph)) {
    if (id === PLAY.C2S_TELEPORT_CONFIRM) return 'c2s_teleport_confirm';
    if (id === PLAY.C2S_POSITION) return 'c2s_position';
    if (id === PLAY.C2S_POSITION_LOOK) return 'c2s_position_look';
    if (id === PLAY.C2S_MOVE_ROT) return 'c2s_look';
    if (id === PLAY.C2S_MOVE_STATUS) return 'c2s_flying';
  }
  return null;
}
