import chalk from 'chalk';
import { HS_LOGIN, LOGIN } from '../client/constants.js';
import { readVarInt, readString, writeString } from '../client/wire.js';
import { hasActiveDownstream } from '../client/captureStore.js';

const HS_STATUS = 1;

/** @param {object} ctx session callbacks and methods */
export function createOnPacket(ctx) {
  const {
    getPhase,
    logger,
    send,
    setPhase,
    sendLoginSuccess,
    beginConfig,
    setUsername,
    handleConfigC2s,
    handlePlayJoinC2s,
    handlePlayC2s,
    registerDownstream,
    isRejected,
  } = ctx;

  return function onPacket(sock, id, payload) {
    const phase = getPhase();

    if (phase === 'handshake') {
      let o = 0;
      const next = readVarInt(payload, o);
      if (!next) return;
      o = next.next;
      const hostLen = readVarInt(payload, o);
      if (!hostLen) return;
      o = hostLen.next + hostLen.value + 2;
      const intention = readVarInt(payload, o);
      if (!intention) return;
      if (intention.value === HS_STATUS) {
        setPhase('status');
        return;
      }
      if (intention.value === HS_LOGIN) {
        if (!isRejected && registerDownstream) {
          registerDownstream();
        }
        setPhase('login');
        return;
      }
      throw new Error(`unsupported handshake intention ${intention.value}`);
    }

    if (phase === 'status') {
      logger.c2s(id, payload);
      if (id === 0x00) {
        const onlineCount = hasActiveDownstream() ? 1 : 0;
        const statusJson = JSON.stringify({
          version: { name: '1.21.10', protocol: 773 },
          players: { max: 1, online: onlineCount, sample: [] },
          description: { text: 'flayerproxy capture replay' },
        });
        send(sock, 0x00, writeString(statusJson));
        return;
      }
      if (id === 0x01 && payload.length >= 8) {
        send(sock, 0x01, payload.subarray(0, 8));
        sock.end();
        return;
      }
      return;
    }

    if (phase === 'login') {
      logger.c2s(id, payload);
      if (id === LOGIN.C2S_START) {
        if (isRejected) {
          const reason = JSON.stringify({ text: "Server is full (1/1 slots occupied)." });
          send(sock, LOGIN.DISCONNECT, writeString(reason));
          sock.end();
          logger.warn('login rejected: server occupied');
          return;
        }
        const name = readString(payload, 0);
        const username = name?.value ?? 'Player';
        setUsername(username);
        sendLoginSuccess(sock, username);
        logger.info('login success', chalk.dim(username));
        return;
      }
      if (id === LOGIN.C2S_ACK) {
        beginConfig(sock);
        return;
      }
      return;
    }

    if (phase === 'config') {
      const handled = handleConfigC2s(sock, id, payload);
      if (!handled) logger.warn('unhandled C2S', chalk.dim(`0x${id.toString(16)}`));
      return;
    }

    if (phase === 'play_join') {
      const handled = handlePlayJoinC2s(sock, id, payload);
      if (!handled) logger.warn('unhandled C2S', chalk.dim(`0x${id.toString(16)}`));
      return;
    }

    if (phase === 'play') {
      const handled = handlePlayC2s(sock, id, payload);
      if (!handled) logger.warn('unhandled C2S', chalk.dim(`0x${id.toString(16)}`));
    }
  };
}
