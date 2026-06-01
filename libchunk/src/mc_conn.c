#define _GNU_SOURCE
#include "mc_conn.h"

#include "internal.h"
#include "mc_server_common.h"
#include "mc_wire.h"

#include <openssl/evp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <zlib.h>

#define MC_CONN_MAX (4 * 1024 * 1024)

static lc_status read_varint_buf(lc_buf *b, int32_t *out) { return lc_buf_read_varint(b, out); }

static int plain_reserve(mc_conn *c, size_t need) {
  if (need <= c->plain_cap) return 0;
  size_t ncap = c->plain_cap ? c->plain_cap : 4096;
  while (ncap < need) {
    if (ncap > MC_CONN_MAX) return -1;
    ncap *= 2;
  }
  uint8_t *p = (uint8_t *)realloc(c->plain, ncap);
  if (!p) return -1;
  c->plain = p;
  c->plain_cap = ncap;
  return 0;
}

static int conn_fill_plain(mc_conn *c) {
  if (c->sock_have >= sizeof c->sock_buf) return 0;
  ssize_t n = recv(c->fd, c->sock_buf + c->sock_have, sizeof c->sock_buf - c->sock_have, 0);
  if (n < 0) return -1;
  if (n == 0) return (c->plain_len || c->sock_have) ? 0 : -1;
  c->sock_have += (size_t)n;
  return 0;
}

static int conn_decrypt_more(mc_conn *c) {
  if (!c->encrypted || !c->dec_ctx) {
    if (c->sock_have == 0 && conn_fill_plain(c) != 0) return -1;
    if (c->sock_have == 0) return -1;
    size_t add = c->sock_have;
    if (plain_reserve(c, c->plain_len + add) != 0) return -1;
    memcpy(c->plain + c->plain_len, c->sock_buf, add);
    c->plain_len += add;
    c->sock_have = 0;
    return 0;
  }

  EVP_CIPHER_CTX *dec = (EVP_CIPHER_CTX *)c->dec_ctx;
  while (c->sock_have > 0) {
    int outl = 0;
    size_t chunk = c->sock_have > 256 ? 256 : c->sock_have;
    if (plain_reserve(c, c->plain_len + chunk + 16) != 0) return -1;
    if (EVP_DecryptUpdate(dec, c->plain + c->plain_len, &outl, c->sock_buf, (int)chunk) != 1) return -1;
    c->plain_len += (size_t)outl;
    memmove(c->sock_buf, c->sock_buf + chunk, c->sock_have - chunk);
    c->sock_have -= chunk;
  }
  if (c->sock_have == 0 && conn_fill_plain(c) != 0) return c->plain_len ? 0 : -1;
  if (c->sock_have == 0) return 0;

  int outl = 0;
  if (plain_reserve(c, c->plain_len + c->sock_have + 16) != 0) return -1;
  if (EVP_DecryptUpdate(dec, c->plain + c->plain_len, &outl, c->sock_buf, (int)c->sock_have) != 1) return -1;
  c->plain_len += (size_t)outl;
  c->sock_have = 0;
  return 0;
}

static int conn_read_plain_byte(mc_conn *c, uint8_t *out) {
  for (;;) {
    if (c->plain_len > 0) {
      *out = c->plain[0];
      memmove(c->plain, c->plain + 1, c->plain_len - 1);
      c->plain_len--;
      return 0;
    }
    if (conn_decrypt_more(c) != 0) return -1;
    if (c->plain_len == 0) return -1;
  }
}

static int conn_read_plain(mc_conn *c, uint8_t *buf, size_t need) {
  while (need > 0) {
    if (c->plain_len >= need) {
      memcpy(buf, c->plain, need);
      memmove(c->plain, c->plain + need, c->plain_len - need);
      c->plain_len -= need;
      return 0;
    }
    size_t take = c->plain_len;
    if (take) {
      memcpy(buf, c->plain, take);
      memmove(c->plain, c->plain + take, c->plain_len - take);
      c->plain_len -= take;
      buf += take;
      need -= take;
      continue;
    }
    if (conn_decrypt_more(c) != 0) return -1;
    if (c->plain_len == 0) return -1;
  }
  return 0;
}

static int conn_send_raw(mc_conn *c, const uint8_t *data, size_t len) {
  if (!c->encrypted || !c->enc_ctx) return mc_send_all(c->fd, data, len) == (ssize_t)len ? 0 : -1;

  EVP_CIPHER_CTX *enc = (EVP_CIPHER_CTX *)c->enc_ctx;
  size_t off = 0;
  uint8_t outbuf[8192];
  while (off < len) {
    size_t chunk = len - off;
    if (chunk > sizeof outbuf / 2) chunk = sizeof outbuf / 2;
    int outl = 0;
    if (EVP_EncryptUpdate(enc, outbuf, &outl, data + off, (int)chunk) != 1) return -1;
    if (outl > 0 && mc_send_all(c->fd, outbuf, (size_t)outl) != outl) return -1;
    off += chunk;
  }
  return 0;
}

static int varint_size(int32_t v) {
  size_t n = 0;
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_varint(&b, v) != LC_OK) return -1;
  n = b.len;
  mc_buf_free(&b);
  return (int)n;
}

static int mc_compress_payload(const uint8_t *in, size_t in_len, int32_t threshold, mc_buf *out) {
  if (threshold < 0 || (int)in_len < threshold) {
    if (mc_buf_varint(out, 0) != LC_OK) return -1;
    return mc_buf_write(out, in, in_len) == LC_OK ? 0 : -1;
  }
  uLongf dest_len = compressBound((uLong)in_len);
  uint8_t *tmp = (uint8_t *)malloc(dest_len);
  if (!tmp) return -1;
  if (compress2(tmp, &dest_len, in, (uLong)in_len, Z_DEFAULT_COMPRESSION) != Z_OK) {
    free(tmp);
    return -1;
  }
  int rc = -1;
  if (mc_buf_varint(out, (int32_t)in_len) == LC_OK && mc_buf_write(out, tmp, dest_len) == LC_OK) rc = 0;
  free(tmp);
  return rc;
}

static int mc_decompress_payload(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len) {
  lc_buf b;
  lc_buf_init(&b, in, in_len);
  int32_t uncompressed_len = 0;
  if (read_varint_buf(&b, &uncompressed_len) != LC_OK) return -1;
  if (uncompressed_len == 0) {
    size_t n = in_len - b.off;
    uint8_t *p = (uint8_t *)malloc(n ? n : 1);
    if (!p) return -1;
    if (n) memcpy(p, in + b.off, n);
    *out = p;
    *out_len = n;
    return 0;
  }
  size_t comp_len = in_len - b.off;
  uLongf dest_len = (uLongf)uncompressed_len;
  uint8_t *p = (uint8_t *)malloc(dest_len ? dest_len : 1);
  if (!p) return -1;
  if (uncompress(p, &dest_len, in + b.off, (uLong)comp_len) != Z_OK) {
    free(p);
    return -1;
  }
  *out = p;
  *out_len = (size_t)dest_len;
  return 0;
}

void mc_conn_init(mc_conn *c, int fd) {
  memset(c, 0, sizeof *c);
  c->fd = fd;
  c->compress_threshold = -1;
}

void mc_conn_free(mc_conn *c) {
  if (!c) return;
  if (c->enc_ctx) EVP_CIPHER_CTX_free((EVP_CIPHER_CTX *)c->enc_ctx);
  if (c->dec_ctx) EVP_CIPHER_CTX_free((EVP_CIPHER_CTX *)c->dec_ctx);
  free(c->plain);
  memset(c, 0, sizeof *c);
  c->fd = -1;
  c->compress_threshold = -1;
}

void mc_conn_set_compress(mc_conn *c, int32_t threshold) {
  if (c) c->compress_threshold = threshold;
}

int mc_conn_set_encryption(mc_conn *c, const uint8_t shared_secret[16]) {
  if (!c || !shared_secret) return -1;
  EVP_CIPHER_CTX *enc = EVP_CIPHER_CTX_new();
  EVP_CIPHER_CTX *dec = EVP_CIPHER_CTX_new();
  if (!enc || !dec) {
    EVP_CIPHER_CTX_free(enc);
    EVP_CIPHER_CTX_free(dec);
    return -1;
  }
  if (EVP_EncryptInit_ex(enc, EVP_aes_128_cfb8(), NULL, shared_secret, shared_secret) != 1 ||
      EVP_DecryptInit_ex(dec, EVP_aes_128_cfb8(), NULL, shared_secret, shared_secret) != 1) {
    EVP_CIPHER_CTX_free(enc);
    EVP_CIPHER_CTX_free(dec);
    return -1;
  }
  EVP_CIPHER_CTX_set_padding(enc, 0);
  EVP_CIPHER_CTX_set_padding(dec, 0);
  c->enc_ctx = enc;
  c->dec_ctx = dec;
  c->encrypted = 1;
  return 0;
}

int mc_conn_send_frame(mc_conn *c, int32_t pkt_id, const uint8_t *payload, size_t payload_len) {
  mc_buf body;
  memset(&body, 0, sizeof body);
  if (mc_buf_varint(&body, pkt_id) != LC_OK) return -1;
  if (payload_len && mc_buf_write(&body, payload, payload_len) != LC_OK) {
    mc_buf_free(&body);
    return -1;
  }

  mc_buf wire;
  memset(&wire, 0, sizeof wire);
  int rc = -1;
  if (c->compress_threshold >= 0) {
    if (mc_compress_payload(body.data, body.len, c->compress_threshold, &wire) != 0) goto done;
  } else {
    wire = body;
    memset(&body, 0, sizeof body);
  }

  mc_buf frame;
  memset(&frame, 0, sizeof frame);
  if (mc_buf_varint(&frame, (int32_t)wire.len) != LC_OK) goto done;
  if (mc_buf_write(&frame, wire.data, wire.len) != LC_OK) goto done;
  rc = conn_send_raw(c, frame.data, frame.len);

done:
  mc_buf_free(&body);
  mc_buf_free(&wire);
  mc_buf_free(&frame);
  return rc;
}

int mc_conn_read_packet(mc_conn *c, uint8_t **out, size_t *out_len, int32_t *pkt_id) {
  uint8_t scratch[16];
  size_t n = 0;
  int32_t frame_len = 0;
  while (n < sizeof scratch) {
    uint8_t b;
    if (conn_read_plain_byte(c, &b) != 0) return -1;
    scratch[n++] = b;
    lc_buf lb;
    lc_buf_init(&lb, scratch, n);
    if (read_varint_buf(&lb, &frame_len) == LC_OK) break;
  }
  if (frame_len < 0 || frame_len > (int32_t)MC_CONN_MAX) return -1;

  uint8_t *frame = (uint8_t *)malloc((size_t)frame_len);
  if (!frame) return -1;
  if (conn_read_plain(c, frame, (size_t)frame_len) != 0) {
    free(frame);
    return -1;
  }

  uint8_t *body = frame;
  size_t body_len = (size_t)frame_len;
  uint8_t *decomp = NULL;
  if (c->compress_threshold >= 0) {
    if (mc_decompress_payload(frame, body_len, &decomp, &body_len) != 0) {
      free(frame);
      return -1;
    }
    free(frame);
    body = decomp;
  }

  lc_buf pb;
  lc_buf_init(&pb, body, body_len);
  if (read_varint_buf(&pb, pkt_id) != LC_OK) {
    free(body);
    return -1;
  }
  size_t payload_len = body_len - pb.off;
  uint8_t *packet = (uint8_t *)malloc(payload_len + (size_t)varint_size(*pkt_id));
  if (!packet) {
    free(body);
    return -1;
  }
  size_t off = 0;
  mc_buf idb;
  memset(&idb, 0, sizeof idb);
  mc_buf_varint(&idb, *pkt_id);
  memcpy(packet, idb.data, idb.len);
  off = idb.len;
  mc_buf_free(&idb);
  if (payload_len) memcpy(packet + off, body + pb.off, payload_len);
  free(body);
  *out = packet;
  *out_len = off + payload_len;
  return 0;
}
