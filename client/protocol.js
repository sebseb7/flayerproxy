import crypto from 'node:crypto';

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

