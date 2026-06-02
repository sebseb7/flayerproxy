#define _DEFAULT_SOURCE
/**
 * Minimal Minecraft 1.21.10 client: handshake → login → configuration → play.
 * Saves every inbound packet (id varint + payload) for comparison with mc_static_server.
 *
 * Usage: mc_reference_client <host> <port> <outdir>
 * See libchunk/docs/REFERENCE_CAPTURE.md
 */
#include "decode_wire.h"
#include "libchunk.h"
#include "mc_conn.h"
#include "mc_dns.h"
#include "mc_online.h"
#include "mc_packet_ids.h"
#include "mc_play_s2c_names.h"
#include "mc_server_common.h"
#include "mc_wire.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MAX_PACKETS 2000
#define PLAY_TIMEOUT_SEC 60

#define PLAY_PKT_CHUNK_BATCH_FINISHED 0x0b
#define PLAY_PKT_LOGIN_SHIFTED 0x31

typedef enum { CAP_HS, CAP_LOGIN, CAP_CFG, CAP_PLAY } cap_phase;

typedef struct {
  FILE *idx;
  const char *outdir;
  int seq;
} cap_ctx;

/** 0 = protocol 773 (static server), 1 = +1 shifted ids (newer Java). */
static int g_play_id_shift = -1;

static int read_varint(const uint8_t *data, size_t len, size_t *off, int32_t *out);

static int32_t play_pkt(int32_t base) { return base + (g_play_id_shift > 0 ? g_play_id_shift : 0); }

static int is_play_position(int32_t id) {
  if (g_play_id_shift >= 0) return id == play_pkt(MC_PKT_PLAY_POSITION);
  return id == MC_PKT_PLAY_POSITION || id == MC_PKT_PLAY_POSITION + 1;
}

static int is_play_map_chunk(int32_t id) {
  if (g_play_id_shift >= 0) return id == play_pkt(MC_PKT_PLAY_MAP_CHUNK);
  return id == MC_PKT_PLAY_MAP_CHUNK || id == MC_PKT_PLAY_MAP_CHUNK + 1;
}

static int is_play_ping(int32_t id) {
  if (g_play_id_shift >= 0) return id == play_pkt(MC_PKT_PLAY_PING);
  return id == MC_PKT_PLAY_PING || id == MC_PKT_PLAY_PING + 1;
}

static int is_play_keep_alive(int32_t id) {
  if (g_play_id_shift >= 0) return id == play_pkt(MC_PKT_PLAY_KEEP_ALIVE);
  return id == MC_PKT_PLAY_KEEP_ALIVE || id == MC_PKT_PLAY_KEEP_ALIVE + 1;
}

static void detect_play_id_shift(int32_t pkt_id) {
  if (g_play_id_shift >= 0) return;
  if (pkt_id == MC_PKT_PLAY_LOGIN) g_play_id_shift = 0;
  else if (pkt_id == PLAY_PKT_LOGIN_SHIFTED) g_play_id_shift = 1;
}

static const char *phase_name(cap_phase p) {
  switch (p) {
  case CAP_HS:
    return "hs";
  case CAP_LOGIN:
    return "login";
  case CAP_CFG:
    return "config";
  case CAP_PLAY:
    return "play";
  default:
    return "?";
  }
}

static const char *c2s_pkt_label(cap_phase phase, int32_t id) {
  if (phase == CAP_HS) {
    if (id == MC_PKT_HS_SET_PROTOCOL) return "handshake";
  }
  if (phase == CAP_LOGIN) {
    if (id == MC_PKT_C2S_LOGIN_START) return "login_start";
    if (id == MC_PKT_C2S_LOGIN_ACKNOWLEDGED) return "login_acknowledged";
  }
  if (phase == CAP_CFG) {
    if (id == MC_PKT_C2S_CFG_SELECT_KNOWN_PACKS) return "select_known_packs";
    if (id == MC_PKT_C2S_CFG_KEEP_ALIVE) return "keep_alive";
    if (id == MC_PKT_C2S_CFG_PONG) return "pong";
    if (id == MC_PKT_C2S_CFG_FINISH) return "finish_configuration";
  }
  if (phase == CAP_PLAY) {
    if (id == MC_PKT_C2S_TELEPORT_CONFIRM) return "teleport_confirm";
    if (id == MC_PKT_C2S_PLAYER_LOADED) return "player_loaded";
    if (id == MC_PKT_C2S_KEEP_ALIVE) return "keep_alive";
    if (id == MC_PKT_C2S_PONG) return "pong";
    if (id == MC_PKT_C2S_CHUNK_BATCH_RECEIVED) return "chunk_batch_received";
    if (id == MC_PKT_C2S_TICK_END) return "tick_end";
  }
  return NULL;
}

static int build_packet_wire(int32_t pkt_id, const uint8_t *payload, size_t payload_len,
                             uint8_t **out, size_t *out_len) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_varint(&b, pkt_id) != LC_OK) return -1;
  if (payload_len > 0 && mc_buf_write(&b, payload, payload_len) != LC_OK) {
    mc_buf_free(&b);
    return -1;
  }
  *out = b.data;
  *out_len = b.len;
  b.data = NULL;
  b.len = 0;
  return 0;
}

static int save_wire(cap_ctx *ctx, const char *dir, cap_phase phase, int32_t pkt_id,
                     const char *label, const uint8_t *wire, size_t wire_len) {
  int seq = ctx->seq++;
  char id_hex[8];
  snprintf(id_hex, sizeof id_hex, "%02x", (unsigned)(pkt_id & 0xff));
  const char *name = label ? label : "pkt";
  char fname[280];
  snprintf(fname, sizeof fname, "%s/%04d_%s_%s_%s_%s.wire", ctx->outdir, seq, dir, phase_name(phase),
           id_hex, name);

  FILE *f = fopen(fname, "wb");
  if (!f) {
    perror(fname);
    return -1;
  }
  if (wire_len > 0 && fwrite(wire, 1, wire_len, f) != wire_len) {
    fclose(f);
    return -1;
  }
  fclose(f);

  fprintf(ctx->idx, "%04d %s %s 0x%s %s %zu %s\n", seq, dir, phase_name(phase), id_hex, name,
          wire_len, fname);
  fprintf(stderr, "[%04d] %s %s 0x%02x %s (%zu B)\n", seq, dir, phase_name(phase),
          (unsigned)(pkt_id & 0xff), name, wire_len);
  return 0;
}

static int save_c2s_payload(cap_ctx *ctx, cap_phase phase, int32_t pkt_id, const uint8_t *payload,
                          size_t len) {
  uint8_t *wire = NULL;
  size_t wire_len = 0;
  if (build_packet_wire(pkt_id, payload, len, &wire, &wire_len) != 0) return -1;
  int rc = save_wire(ctx, "c2s", phase, pkt_id, c2s_pkt_label(phase, pkt_id), wire, wire_len);
  free(wire);
  return rc;
}

static int send_c2s(mc_conn *conn, cap_ctx *ctx, cap_phase phase, int32_t pkt_id,
                    const uint8_t *payload, size_t len) {
  if (save_c2s_payload(ctx, phase, pkt_id, payload, len) != 0) return -1;
  return mc_conn_send_frame(conn, pkt_id, payload, len);
}

static const char *pkt_label(cap_phase phase, int32_t id) {
  if (phase == CAP_LOGIN) {
    if (id == MC_PKT_LOGIN_DISCONNECT) return "disconnect";
    if (id == MC_PKT_LOGIN_SUCCESS) return "success";
    if (id == MC_PKT_LOGIN_COMPRESS) return "compress";
    if (id == MC_PKT_LOGIN_ENCRYPTION_BEGIN) return "encryption_begin";
  }
  if (phase == CAP_CFG) {
    if (id == MC_PKT_CFG_FINISH) return "finish_configuration";
    if (id == MC_PKT_CFG_KEEP_ALIVE) return "keep_alive";
    if (id == MC_PKT_CFG_PING) return "ping";
    if (id == MC_PKT_CFG_REGISTRY_DATA) return "registry_data";
    if (id == MC_PKT_CFG_SELECT_KNOWN_PACKS) return "select_known_packs";
    if (id == MC_PKT_CFG_FEATURE_FLAGS) return "feature_flags";
    if (id == MC_PKT_CFG_RESET_CHAT) return "reset_chat";
    if (id == MC_PKT_COMMON_CUSTOM_PAYLOAD) return "custom_payload";
    if (id == MC_PKT_COMMON_UPDATE_TAGS) return "update_tags";
  }
  if (phase == CAP_PLAY) {
    const char *play_name = mc_play_s2c_name(id);
    if (play_name) return play_name;
    if (g_play_id_shift > 0) {
      play_name = mc_play_s2c_name(id - g_play_id_shift);
      if (play_name) return play_name;
    }
  }
  return NULL;
}

static int send_handshake(mc_conn *conn, cap_ctx *ctx, const char *host, uint16_t port) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_varint(&b, MC_PROTOCOL_VERSION) != LC_OK) return -1;
  if (mc_buf_string(&b, host) != LC_OK) return -1;
  if (mc_buf_u8(&b, (uint8_t)(port & 0xff)) != LC_OK) return -1;
  if (mc_buf_u8(&b, (uint8_t)(port >> 8)) != LC_OK) return -1;
  if (mc_buf_varint(&b, MC_HS_LOGIN) != LC_OK) return -1;
  if (send_c2s(conn, ctx, CAP_HS, MC_PKT_HS_SET_PROTOCOL, b.data, b.len) != 0) {
    mc_buf_free(&b);
    return -1;
  }
  mc_buf_free(&b);
  return 0;
}

static int send_login_start(mc_conn *conn, cap_ctx *ctx, const char *username, const uint8_t uuid[16]) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_string(&b, username) != LC_OK) return -1;
  if (mc_buf_uuid(&b, uuid) != LC_OK) return -1;
  int rc = send_c2s(conn, ctx, CAP_LOGIN, MC_PKT_C2S_LOGIN_START, b.data, b.len);
  mc_buf_free(&b);
  return rc;
}

static int read_varint(const uint8_t *data, size_t len, size_t *off, int32_t *out) {
  int32_t v = 0;
  int shift = 0;
  for (size_t i = 0; i < len && i < 5; i++) {
    uint8_t b = data[i];
    v |= (int32_t)(b & 0x7f) << shift;
    if (!(b & 0x80)) {
      *off = i + 1;
      *out = v;
      return 0;
    }
    shift += 7;
  }
  return -1;
}

static int payload_after_id(const uint8_t *wire, size_t wire_len, const uint8_t **body, size_t *body_len) {
  const uint8_t *payload = NULL;
  size_t payload_len = 0;
  if (lc_skip_packet_id(wire, wire_len, &payload, &payload_len) != LC_OK) return -1;
  *body = payload;
  *body_len = payload_len;
  return 0;
}

static int echo_select_known_packs(mc_conn *conn, cap_ctx *ctx, const uint8_t *wire, size_t wire_len) {
  const uint8_t *body = NULL;
  size_t body_len = 0;
  if (payload_after_id(wire, wire_len, &body, &body_len) != 0) return -1;
  return send_c2s(conn, ctx, CAP_CFG, MC_PKT_C2S_CFG_SELECT_KNOWN_PACKS, body, body_len);
}

static int send_teleport_confirm(mc_conn *conn, cap_ctx *ctx, int32_t teleport_id) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_varint(&b, teleport_id) != LC_OK) return -1;
  int rc = send_c2s(conn, ctx, CAP_PLAY, MC_PKT_C2S_TELEPORT_CONFIRM, b.data, b.len);
  mc_buf_free(&b);
  return rc;
}

static int send_cfg_pong(mc_conn *conn, cap_ctx *ctx, int32_t id) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_varint(&b, id) != LC_OK) return -1;
  int rc = send_c2s(conn, ctx, CAP_CFG, MC_PKT_C2S_CFG_PONG, b.data, b.len);
  mc_buf_free(&b);
  return rc;
}

static int send_play_pong(mc_conn *conn, cap_ctx *ctx, int32_t id) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_i32_be(&b, id) != LC_OK) return -1;
  int rc = send_c2s(conn, ctx, CAP_PLAY, MC_PKT_C2S_PONG, b.data, b.len);
  mc_buf_free(&b);
  return rc;
}

static int send_keep_alive_reply(mc_conn *conn, cap_ctx *ctx, cap_phase phase, int32_t c2s_id,
                                 const uint8_t *body, size_t body_len) {
  if (body_len != 8) return 0;
  return send_c2s(conn, ctx, phase, c2s_id, body, 8);
}

static int send_chunk_batch_received(mc_conn *conn, cap_ctx *ctx) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_f32_be(&b, 6.0f) != LC_OK) return -1;
  int rc = send_c2s(conn, ctx, CAP_PLAY, MC_PKT_C2S_CHUNK_BATCH_RECEIVED, b.data, b.len);
  mc_buf_free(&b);
  return rc;
}

static int handle_position_packet(mc_conn *conn, cap_ctx *ctx, const uint8_t *wire, size_t wire_len,
                                 int *player_loaded_sent) {
  const uint8_t *body = NULL;
  size_t body_len = 0;
  if (payload_after_id(wire, wire_len, &body, &body_len) != 0) return -1;
  size_t off = 0;
  int32_t tid = 0;
  if (read_varint(body, body_len, &off, &tid) != 0) return -1;
  if (send_teleport_confirm(conn, ctx, tid) != 0) return -1;
  if (!*player_loaded_sent) {
    *player_loaded_sent = 1;
    if (send_c2s(conn, ctx, CAP_PLAY, MC_PKT_C2S_PLAYER_LOADED, NULL, 0) != 0) return -1;
  }
  return 0;
}

static int handle_play_response(mc_conn *conn, cap_ctx *ctx, int32_t pkt_id, const uint8_t *wire,
                                size_t wire_len, int *player_loaded_sent, int *map_chunks_seen) {
  if (pkt_id == PLAY_PKT_CHUNK_BATCH_FINISHED) {
    return send_chunk_batch_received(conn, ctx);
  }
  if (is_play_keep_alive(pkt_id)) {
    const uint8_t *body = NULL;
    size_t body_len = 0;
    if (payload_after_id(wire, wire_len, &body, &body_len) != 0) return -1;
    return send_keep_alive_reply(conn, ctx, CAP_PLAY, MC_PKT_C2S_KEEP_ALIVE, body, body_len);
  }
  if (is_play_ping(pkt_id)) {
    const uint8_t *body = NULL;
    size_t body_len = 0;
    if (payload_after_id(wire, wire_len, &body, &body_len) != 0 || body_len < 4) return -1;
    int32_t ping_id = (int32_t)((uint32_t)body[0] << 24 | (uint32_t)body[1] << 16 | (uint32_t)body[2] << 8 |
                                (uint32_t)body[3]);
    return send_play_pong(conn, ctx, ping_id);
  }
  if (is_play_position(pkt_id)) {
    return handle_position_packet(conn, ctx, wire, wire_len, player_loaded_sent);
  }
  if (is_play_map_chunk(pkt_id)) {
    (*map_chunks_seen)++;
    if (!*player_loaded_sent && *map_chunks_seen >= 3) {
      *player_loaded_sent = 1;
      if (send_c2s(conn, ctx, CAP_PLAY, MC_PKT_C2S_PLAYER_LOADED, NULL, 0) != 0) return -1;
    }
    if (send_c2s(conn, ctx, CAP_PLAY, MC_PKT_C2S_TICK_END, NULL, 0) != 0) return -1;
    return 0;
  }
  return 0;
}

static int try_decode_summary(const char *name, const uint8_t *wire, size_t wire_len) {
  if (!name) return 0;
  if (strcmp(name, "map_chunk") != 0 && strcmp(name, "light_update") != 0) return 0;
  if (strcmp(name, "map_chunk") == 0 && wire_len < 256) return 0;
  char buf[4096];
  int rc = lc_decode_wire_to_string(name, wire, wire_len, buf, sizeof buf);
  if (rc > 0) {
    size_t n = strlen(buf);
    if (n > 200) {
      buf[200] = '\0';
      strcat(buf, "...");
    }
    fprintf(stderr, "  decode %s: %s\n", name, buf);
  } else if (rc < 0) {
    fprintf(stderr, "  decode %s: FAILED\n", name);
  }
  return rc;
}

static int mkdir_p(const char *path) {
  char buf[512];
  snprintf(buf, sizeof buf, "%s", path);
  size_t len = strlen(buf);
  if (len == 0) return -1;
  if (buf[len - 1] == '/') buf[len - 1] = '\0';
  for (char *p = buf + 1; *p; p++) {
    if (*p != '/') continue;
    *p = '\0';
    if (mkdir(buf, 0755) != 0 && errno != EEXIST) return -1;
    *p = '/';
  }
  if (mkdir(buf, 0755) != 0 && errno != EEXIST) return -1;
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 4 || argc > 5) {
    fprintf(stderr, "Usage: %s <host> <port> <outdir> [username]\n", argv[0]);
    fprintf(stderr, "  Auth: MC_ACCESS_TOKEN + MC_PROFILE_ID, or MSA via libchunk/scripts/msa_token.js\n");
    fprintf(stderr, "  Set FLAYERPROXY_ROOT to repo root (default: cwd) for SRV + MSA script.\n");
    return 1;
  }
  const char *host = argv[1];
  const char *port_str = argv[2];
  const char *outdir = argv[3];
  const char *username = argc >= 5 ? argv[4] : (getenv("MC_USERNAME") ? getenv("MC_USERNAME") : "FlayerBot");

  if (!getenv("FLAYERPROXY_ROOT")) {
    char cwd[512];
    if (getcwd(cwd, sizeof cwd)) setenv("FLAYERPROXY_ROOT", cwd, 0);
  }

  if (mkdir_p(outdir) != 0) {
    perror(outdir);
    return 1;
  }

  char idx_path[512];
  snprintf(idx_path, sizeof idx_path, "%s/index.txt", outdir);
  FILE *idx = fopen(idx_path, "w");
  if (!idx) {
    perror(idx_path);
    return 1;
  }

  fprintf(stderr, "connecting to %s:%s...\n", host, port_str);
  int fd = mc_tcp_connect(host, port_str, 15);
  if (fd < 0) {
    fclose(idx);
    return 1;
  }

  mc_conn conn;
  mc_conn_init(&conn, fd);

  mc_online_creds creds = {0};
  char *owned_token = NULL;
  char *owned_profile = NULL;
  int want_online_auth = strcmp(host, "127.0.0.1") != 0 && strcmp(host, "localhost") != 0 && !getenv("MC_OFFLINE");
  if (want_online_auth && mc_online_creds_from_env(&creds) != 0) {
    fprintf(stderr, "MSA login for %s (browser/device code may appear)...\n", username);
    if (mc_online_creds_from_msa(username, &owned_token, &owned_profile) == 0) {
      creds.access_token = owned_token;
      creds.profile_id = owned_profile;
    } else {
      fprintf(stderr, "warning: no credentials; online-mode servers will reject login\n");
    }
  }

  uint8_t uuid[16];
  mc_offline_uuid(username, uuid);

  cap_ctx ctx = {.idx = idx, .outdir = outdir, .seq = 0};

  uint16_t port = (uint16_t)atoi(port_str);
  if (send_handshake(&conn, &ctx, host, port) != 0 ||
      send_login_start(&conn, &ctx, username, uuid) != 0) {
    mc_conn_free(&conn);
    close(fd);
    fclose(idx);
    free(owned_token);
    free(owned_profile);
    return 1;
  }

  cap_phase phase = CAP_LOGIN;
  int login_ack_sent = 0;
  int config_finish_sent = 0;
  int player_loaded_sent = 0;
  int map_chunks_seen = 0;
  time_t play_enter = 0;

  fprintf(stderr, "connected to %s:%s as %s, saving to %s\n", host, port_str, username, outdir);
  fprintf(stderr, "  S2C/C2S: chronological .wire files (seq_dir_phase_id_name.wire)\n");
  fprintf(idx, "# seq dir phase id name bytes file\n");

  while (ctx.seq < MAX_PACKETS) {
    if (phase == CAP_PLAY && play_enter && time(NULL) - play_enter >= PLAY_TIMEOUT_SEC) break;

    uint8_t *wire = NULL;
    size_t wire_len = 0;
    int32_t pkt_id = 0;
    if (mc_conn_read_packet(&conn, &wire, &wire_len, &pkt_id) != 0) {
      fprintf(stderr, "read error or disconnect after %d packets\n", ctx.seq);
      free(wire);
      break;
    }

    if (phase == CAP_PLAY) detect_play_id_shift(pkt_id);

    const char *label = pkt_label(phase, pkt_id);
    if (save_wire(&ctx, "s2c", phase, pkt_id, label, wire, wire_len) != 0) {
      free(wire);
      break;
    }
    if (phase == CAP_PLAY) try_decode_summary(label, wire, wire_len);

    if (phase == CAP_LOGIN) {
      if (pkt_id == MC_PKT_LOGIN_ENCRYPTION_BEGIN) {
        const uint8_t *body = NULL;
        size_t body_len = 0;
        mc_encryption_begin enc;
        memset(&enc, 0, sizeof enc);
        if (payload_after_id(wire, wire_len, &body, &body_len) != 0 ||
            mc_parse_encryption_begin(body, body_len, &enc) != 0 ||
            mc_online_handle_encryption_begin(&conn, &enc, creds.access_token ? &creds : NULL) != 0) {
          fprintf(stderr, "encryption_begin failed\n");
          mc_encryption_begin_free(&enc);
          free(wire);
          break;
        }
        mc_encryption_begin_free(&enc);
      }
      if (pkt_id == MC_PKT_LOGIN_COMPRESS) {
        const uint8_t *body = NULL;
        size_t body_len = 0;
        int32_t threshold = 256;
        if (payload_after_id(wire, wire_len, &body, &body_len) == 0 && body_len > 0) {
          size_t off = 0;
          read_varint(body, body_len, &off, &threshold);
        }
        mc_conn_set_compress(&conn, threshold);
        fprintf(stderr, "compression enabled (threshold=%d)\n", threshold);
      }
      if (pkt_id == MC_PKT_LOGIN_SUCCESS && !login_ack_sent) {
        login_ack_sent = 1;
        if (send_c2s(&conn, &ctx, CAP_LOGIN, MC_PKT_C2S_LOGIN_ACKNOWLEDGED, NULL, 0) != 0) {
          free(wire);
          break;
        }
        phase = CAP_CFG;
        fprintf(stderr, "-> configuration\n");
      }
      if (pkt_id == MC_PKT_LOGIN_DISCONNECT) {
        fprintf(stderr, "login disconnect\n");
        free(wire);
        break;
      }
    } else if (phase == CAP_CFG) {
      if (pkt_id == MC_PKT_CFG_SELECT_KNOWN_PACKS) {
        if (echo_select_known_packs(&conn, &ctx, wire, wire_len) != 0) {
          free(wire);
          break;
        }
      } else if (pkt_id == MC_PKT_CFG_KEEP_ALIVE) {
        const uint8_t *body = NULL;
        size_t body_len = 0;
        if (payload_after_id(wire, wire_len, &body, &body_len) != 0 ||
            send_keep_alive_reply(&conn, &ctx, CAP_CFG, MC_PKT_C2S_CFG_KEEP_ALIVE, body, body_len) != 0) {
          free(wire);
          break;
        }
      } else if (pkt_id == MC_PKT_CFG_PING) {
        const uint8_t *body = NULL;
        size_t body_len = 0;
        if (payload_after_id(wire, wire_len, &body, &body_len) != 0) {
          free(wire);
          break;
        }
        size_t off = 0;
        int32_t ping_id = 0;
        if (read_varint(body, body_len, &off, &ping_id) != 0 || send_cfg_pong(&conn, &ctx, ping_id) != 0) {
          free(wire);
          break;
        }
      } else if (pkt_id == MC_PKT_CFG_FINISH && !config_finish_sent) {
        config_finish_sent = 1;
        if (send_c2s(&conn, &ctx, CAP_CFG, MC_PKT_C2S_CFG_FINISH, NULL, 0) != 0) {
          free(wire);
          break;
        }
        phase = CAP_PLAY;
        play_enter = time(NULL);
        fprintf(stderr, "-> play (capture %ds)\n", PLAY_TIMEOUT_SEC);
      }
    } else if (phase == CAP_PLAY) {
      if (handle_play_response(&conn, &ctx, pkt_id, wire, wire_len, &player_loaded_sent, &map_chunks_seen) !=
          0) {
        fprintf(stderr, "play response failed for 0x%02x\n", (unsigned)(pkt_id & 0xff));
        free(wire);
        break;
      }
    }

    free(wire);
  }

  fclose(idx);
  mc_conn_free(&conn);
  close(fd);
  free(owned_token);
  free(owned_profile);
  fprintf(stderr, "saved %d packets to %s/index.txt\n", ctx.seq, outdir);
  return 0;
}
