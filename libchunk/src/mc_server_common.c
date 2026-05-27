#define _POSIX_C_SOURCE 200809L

#include "mc_server_common.h"

#include "internal.h"
#include "mc_log.h"
#include "mc_packet_ids.h"
#include "mc_wire.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
/* Good for: Wall-clock milliseconds for keepalive timing.
 * Callers: mc_server_common.c (same file).
 */

static int64_t mc_now_ms(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return (int64_t)time(NULL) * 1000;
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
/* Good for: Send full buffer on socket (handle partial writes).
 * Callers: mc_server_common.c (same file).
 */

ssize_t mc_send_all(int fd, const void *buf, size_t len) {
  const uint8_t *p = (const uint8_t *)buf;
  size_t off = 0;
  while (off < len) {
    ssize_t n = send(fd, p + off, len - off, MSG_NOSIGNAL);
    if (n < 0) return n;
    if (n == 0) return 0;
    off += (size_t)n;
  }
  return (ssize_t)len;
}
/* Good for: Send length-prefixed packet (varint id + payload).
 * Callers: mc_auth_offline.c, mc_reference_client.c, mc_server_common.c (same file), mc_spectator.c, mc_static_config.c, mc_static_registries.c, mc_static_server.c, mc_wire_templates.c.
 */

int mc_send_frame(int fd, int32_t pkt_id, const uint8_t *payload, size_t payload_len) {
  mc_buf frame;
  memset(&frame, 0, sizeof(frame));
  if (mc_buf_frame(&frame, pkt_id, payload, payload_len) != LC_OK) {
    mc_buf_free(&frame);
    return -1;
  }
  ssize_t rc = mc_send_all(fd, frame.data, frame.len);
  size_t want = frame.len;
  mc_buf_free(&frame);
  return rc == (ssize_t)want ? 0 : -1;
}
/* Good for: Send pre-framed wire blob unchanged.
 * Callers: mc_spectator.c.
 */

int mc_send_wire_framed(int fd, const uint8_t *wire, size_t wire_len) {
  mc_buf frame;
  memset(&frame, 0, sizeof frame);
  if (mc_buf_varint(&frame, (int32_t)wire_len) != LC_OK) return -1;
  if (mc_buf_write(&frame, wire, wire_len) != LC_OK) {
    mc_buf_free(&frame);
    return -1;
  }
  ssize_t rc = mc_send_all(fd, frame.data, frame.len);
  size_t flen = frame.len;
  mc_buf_free(&frame);
  return rc == (ssize_t)flen ? 0 : -1;
}
/* Good for: Read varint from lc_buf (server common helper).
 * Callers: mc_server_common.c (same file).
 */

static lc_status read_varint_buf(lc_buf *b, int32_t *out) { return lc_buf_read_varint(b, out); }
/* Good for: Read one packet from socket into malloc'd buffer.
 * Callers: mc_reference_client.c, mc_spectator.c, mc_static_server.c.
 */

int mc_read_packet(int fd, uint8_t **out, size_t *out_len, int32_t *pkt_id) {
  uint8_t scratch[256];
  size_t n = 0;
  int32_t plen = 0;
  size_t body_start = 0;
  while (n < sizeof scratch) {
    ssize_t r = recv(fd, scratch + n, 1, 0);
    if (r <= 0) return -1;
    n++;
    lc_buf b;
    lc_buf_init(&b, scratch, n);
    if (read_varint_buf(&b, &plen) == LC_OK) {
      body_start = b.off;
      break;
    }
  }
  if (plen < 0 || plen > (int32_t)MC_WIRE_MAX) return -1;
  size_t body_need = (size_t)plen;
  uint8_t *packet = (uint8_t *)malloc(body_need);
  if (!packet) return -1;
  size_t have = n - body_start;
  if (have) memcpy(packet, scratch + body_start, have);
  while (have < body_need) {
    ssize_t r = recv(fd, packet + have, body_need - have, 0);
    if (r <= 0) {
      free(packet);
      return -1;
    }
    have += (size_t)r;
  }
  lc_buf pb;
  lc_buf_init(&pb, packet, body_need);
  if (read_varint_buf(&pb, pkt_id) != LC_OK) {
    free(packet);
    return -1;
  }
  *out = packet;
  *out_len = body_need;
  return 0;
}
/* Good for: Parse Login Start; fill username on mc_client.
 * Callers: mc_auth_offline.c.
 */

int mc_parse_login_start(const uint8_t *payload, size_t len, mc_client *cli) {
  lc_buf b;
  lc_buf_init(&b, payload, len);
  char *name = NULL;
  lc_uuid u;
  if (lc_buf_read_string(&b, &name) != LC_OK) return -1;
  if (lc_buf_read_uuid(&b, &u) != LC_OK) {
    free(name);
    return -1;
  }
  snprintf(cli->username, sizeof cli->username, "%s", name ? name : "player");
  memcpy(cli->uuid, u.bytes, 16);
  free(name);
  return 0;
}
/* Good for: Send Set Compression threshold -1 (disable).
 * Callers: libchunk.h (public API, no .c callers in tree).
 */

int mc_send_compress_disable(mc_client *cli) {
  mc_buf p;
  memset(&p, 0, sizeof p);
  if (mc_buf_varint(&p, -1) != LC_OK) return -1;
  int rc = mc_send_frame(cli->fd, MC_PKT_LOGIN_COMPRESS, p.data, p.len);
  mc_buf_free(&p);
  return rc;
}
/* Good for: Send Finish Configuration (client → server).
 * Callers: mc_spectator.c.
 */

int mc_send_config_finish(mc_client *cli) {
  return mc_send_frame(cli->fd, MC_PKT_CFG_FINISH, NULL, 0);
}
/* Good for: Send Status Response for ping/list.
 * Callers: mc_static_server.c.
 */

int mc_send_status_response(int fd) {
  static const char json[] =
      "{\"version\":{\"name\":\"1.21.10\",\"protocol\":773},"
      "\"players\":{\"max\":420,\"online\":0,\"sample\":[]},"
      "\"description\":{\"text\":\"static_server\"}}";
  mc_buf p;
  memset(&p, 0, sizeof p);
  if (mc_buf_string(&p, json) != LC_OK) return -1;
  int rc = mc_send_frame(fd, MC_PKT_STATUS_RESPONSE, p.data, p.len);
  mc_buf_free(&p);
  return rc;
}
/* Good for: Send Pong for status ping.
 * Callers: mc_static_server.c.
 */

int mc_send_status_pong(int fd, int64_t ping_time) {
  mc_buf p;
  memset(&p, 0, sizeof p);
  if (mc_buf_i64_be(&p, ping_time) != LC_OK) return -1;
  int rc = mc_send_frame(fd, MC_PKT_STATUS_PONG, p.data, p.len);
  mc_buf_free(&p);
  return rc;
}
/* Good for: Send Keep Alive with id.
 * Callers: mc_server_common.c (same file).
 */

int mc_send_keep_alive(int fd, int32_t pkt_id, int64_t id) {
  mc_buf p;
  memset(&p, 0, sizeof p);
  if (mc_buf_i64_be(&p, id) != LC_OK) return -1;
  int rc = mc_send_frame(fd, pkt_id, p.data, p.len);
  mc_buf_free(&p);
  return rc;
}
/* Good for: Send keepalive if interval elapsed.
 * Callers: mc_server_common.c (same file), mc_static_server.c.
 */

int mc_client_send_keep_alive(mc_client *cli) {
  if (!cli || cli->fd < 0) return -1;
  if (cli->state != MC_CLI_CONFIG && cli->state != MC_CLI_PLAY) return 0;
  int32_t pkt_id =
      cli->state == MC_CLI_CONFIG ? MC_PKT_CFG_KEEP_ALIVE : MC_PKT_PLAY_KEEP_ALIVE;
  cli->keep_alive_challenge = mc_now_ms();
  cli->keep_alive_sent_ms = cli->keep_alive_challenge;
  cli->keep_alive_pending = 1;
  MC_LOGD("static_server", "keep_alive -> id=%lld (%s)", (long long)cli->keep_alive_challenge,
          cli->state == MC_CLI_CONFIG ? "config" : "play");
  return mc_send_keep_alive(cli->fd, pkt_id, cli->keep_alive_challenge);
}
/* Good for: Record client Keep Alive response id.
 * Callers: mc_spectator.c, mc_static_server.c.
 */

int mc_client_handle_keep_alive(mc_client *cli, const uint8_t *payload, size_t payload_len) {
  if (!cli || payload_len < 8) return -1;
  lc_buf b;
  lc_buf_init(&b, payload, payload_len);
  int64_t id;
  if (lc_buf_read_i64_be(&b, &id) != LC_OK) return -1;
  if (cli->keep_alive_pending && id == cli->keep_alive_challenge) {
    cli->keep_alive_pending = 0;
  }
  return 0;
}
/* Good for: Send keepalive when due in play loop.
 * Callers: mc_static_server.c.
 */

int mc_client_tick_keep_alive(mc_client *cli) {
  if (!cli) return -1;
  if (cli->state != MC_CLI_CONFIG && cli->state != MC_CLI_PLAY) return 0;
  if (cli->keep_alive_sent_ms == 0) return 0;

  int64_t now = mc_now_ms();
  if (now - cli->keep_alive_sent_ms < MC_KEEP_ALIVE_INTERVAL_MS) return 0;

  if (cli->keep_alive_pending) return -1;

  return mc_client_send_keep_alive(cli);
}
/* Good for: Derive offline-mode UUID from username.
 * Callers: mc_auth_offline.c, mc_reference_client.c.
 */

void mc_offline_uuid(const char *username, uint8_t uuid[16]) {
  char buf[320];
  snprintf(buf, sizeof buf, "OfflinePlayer:%s", username ? username : "player");
  size_t msg_len = strlen(buf);
  uint64_t bit_len = (uint64_t)msg_len * 8;

  static const uint32_t k[64] = {
      0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
      0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
      0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
      0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
      0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
      0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
      0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
      0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
  static const uint8_t s[64] = {
      7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,  14, 20,
      5, 9,  14, 20, 5, 9,  14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
      6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

  uint8_t msg[128];
  memcpy(msg, buf, msg_len);
  msg[msg_len++] = 0x80;
  size_t pad = (msg_len % 64 <= 56) ? (56 - msg_len % 64) : (120 - msg_len % 64);
  memset(msg + msg_len, 0, pad);
  msg_len += pad;
  msg[msg_len++] = (uint8_t)bit_len;
  msg[msg_len++] = (uint8_t)(bit_len >> 8);
  msg[msg_len++] = (uint8_t)(bit_len >> 16);
  msg[msg_len++] = (uint8_t)(bit_len >> 24);
  msg[msg_len++] = 0;
  msg[msg_len++] = 0;
  msg[msg_len++] = 0;
  msg[msg_len++] = 0;

  uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
  for (size_t off = 0; off < msg_len; off += 64) {
    uint32_t M[16];
    for (int i = 0; i < 16; i++) {
      M[i] = (uint32_t)msg[off + i * 4] | ((uint32_t)msg[off + i * 4 + 1] << 8) |
             ((uint32_t)msg[off + i * 4 + 2] << 16) | ((uint32_t)msg[off + i * 4 + 3] << 24);
    }
    uint32_t A = a0, B = b0, C = c0, D = d0;
    for (int i = 0; i < 64; i++) {
      uint32_t F, g;
      if (i < 16) {
        F = (B & C) | ((~B) & D);
        g = (uint32_t)i;
      } else if (i < 32) {
        F = (D & B) | ((~D) & C);
        g = (5 * (uint32_t)i + 1) % 16;
      } else if (i < 48) {
        F = B ^ C ^ D;
        g = (3 * (uint32_t)i + 5) % 16;
      } else {
        F = C ^ (B | (~D));
        g = (7 * (uint32_t)i) % 16;
      }
      uint32_t tmp = D;
      D = C;
      C = B;
      uint32_t sum = A + F + k[i] + M[g];
      B = B + ((sum << s[i]) | (sum >> (32 - s[i])));
      A = tmp;
    }
    a0 += A;
    b0 += B;
    c0 += C;
    d0 += D;
  }
  uint32_t digest[4] = {a0, b0, c0, d0};
  for (int i = 0; i < 4; i++) {
    uuid[i * 4 + 0] = (uint8_t)(digest[i] >> 24);
    uuid[i * 4 + 1] = (uint8_t)(digest[i] >> 16);
    uuid[i * 4 + 2] = (uint8_t)(digest[i] >> 8);
    uuid[i * 4 + 3] = (uint8_t)digest[i];
  }
  uuid[6] = (uuid[6] & 0x0f) | 0x30;
  uuid[8] = (uuid[8] & 0x3f) | 0x80;
}
