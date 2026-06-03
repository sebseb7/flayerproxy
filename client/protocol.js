import crypto from 'node:crypto';

export function writeVarInt(n) {
  const out = [];
  let v = n >>> 0;
  do {
    let b = v & 0x7f;
    v >>>= 7;
    if (v !== 0) b |= 0x80;
    out.push(b);
  } while (v !== 0);
  return Buffer.from(out);
}

export function readVarInt(buf, off = 0) {
  let num = 0;
  let shift = 0;
  let o = off;
  for (;;) {
    if (o >= buf.length) return null;
    const b = buf[o++];
    num |= (b & 0x7f) << shift;
    if ((b & 0x80) === 0) return { value: num, next: o };
    shift += 7;
    if (shift > 35) throw new Error('varint too long');
  }
}

export function writeString(s) {
  const b = Buffer.from(s, 'utf8');
  return Buffer.concat([writeVarInt(b.length), b]);
}

export function readString(buf, off) {
  const lenR = readVarInt(buf, off);
  if (!lenR) return null;
  const start = lenR.next;
  const end = start + lenR.value;
  if (end > buf.length) return null;
  return { value: buf.toString('utf8', start, end), next: end };
}

export function writePacket(id, payload = Buffer.alloc(0)) {
  const body = Buffer.concat([writeVarInt(id), payload]);
  return Buffer.concat([writeVarInt(body.length), body]);
}

export function tryReadFrame(buf) {
  const lenR = readVarInt(buf, 0);
  if (!lenR) return null;
  const total = lenR.next + lenR.value;
  if (buf.length < total) return null;
  const body = buf.subarray(lenR.next, total);
  const rest = buf.subarray(total);
  const idR = readVarInt(body, 0);
  if (!idR) return null;
  return { id: idR.value, payload: body.subarray(idR.next), rest };
}

export function offlineUUID(name) {
  const hash = crypto.createHash('md5').update(`OfflinePlayer:${name}`).digest();
  hash[6] = (hash[6] & 0x0f) | 0x30;
  hash[8] = (hash[8] & 0x3f) | 0x80;
  return hash;
}

export function readI64BE(buf, off) {
  if (off + 8 > buf.length) return null;
  return { value: buf.readBigInt64BE(off), next: off + 8 };
}

export function readF64BE(buf, off) {
  if (off + 8 > buf.length) return null;
  return { value: buf.readDoubleBE(off), next: off + 8 };
}

export function parsePosition(payload) {
  let o = 0;
  const tid = readVarInt(payload, o);
  if (!tid) return null;
  o = tid.next;
  const x = readF64BE(payload, o);
  if (!x) return null;
  o = x.next;
  const y = readF64BE(payload, o);
  if (!y) return null;
  o = y.next;
  const z = readF64BE(payload, o);
  if (!z) return null;
  return { teleportId: tid.value, x: x.value, y: y.value, z: z.value };
}
