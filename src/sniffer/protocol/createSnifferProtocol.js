'use strict';

const { Serializer, FullPacketParser } = require('protodef');
const { ProtoDefCompiler } = require('protodef').Compiler;
const nbt = require('prismarine-nbt');
const merge = require('lodash.merge');
const minecraftData = require('minecraft-data');
const {
  readAnonymousNbt,
  writeAnonymousNbt,
  sizeOfAnonymousNbt,
} = require('./tolerantAnonymousNbt');

/** Separate cache so sniffer NBT patch never affects mineflayer / main proxy legs. */
const snifferProtocols = {};

function installTolerantAnonymousNbt(compiler) {
  nbt.addTypesToCompiler('big', compiler);
  const native = {
    Read: { anonymousNbt: ['native', readAnonymousNbt] },
    Write: { anonymousNbt: ['native', writeAnonymousNbt] },
    SizeOf: { anonymousNbt: ['native', sizeOfAnonymousNbt] },
  };
  compiler.addTypes(native);
}

function createSnifferProtocol(state, direction, version, customPackets) {
  const key = `${state};${direction};${version};sniffer`;
  if (snifferProtocols[key]) return snifferProtocols[key];

  const mcData = minecraftData(version);
  if (!mcData) throw new Error(`unsupported protocol version: ${version}`);

  const mergedProtocol = merge(mcData.protocol, customPackets?.[mcData.version.majorVersion] ?? {});

  const compiler = new ProtoDefCompiler();
  compiler.addTypes(require('minecraft-protocol/src/datatypes/compiler-minecraft'));
  compiler.addProtocol(mergedProtocol, [state, direction]);
  installTolerantAnonymousNbt(compiler);
  const proto = compiler.compileProtoDefSync();
  snifferProtocols[key] = proto;
  return proto;
}

function createSnifferSerializer(opts = {}) {
  const { state, isServer, version, customPackets } = opts;
  const direction = !isServer ? 'toServer' : 'toClient';
  return new Serializer(createSnifferProtocol(state, direction, version, customPackets), 'packet');
}

function createSnifferDeserializer(opts = {}) {
  const { state, isServer, version, customPackets, noErrorLogging = false } = opts;
  const direction = isServer ? 'toServer' : 'toClient';
  return new FullPacketParser(
    createSnifferProtocol(state, direction, version, customPackets),
    'packet',
    noErrorLogging,
  );
}

module.exports = {
  createSnifferProtocol,
  createSnifferSerializer,
  createSnifferDeserializer,
};
