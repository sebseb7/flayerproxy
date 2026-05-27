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
#include "mc_packet_ids.h"
#include "mc_server_common.h"
#include "mc_wire.h"

#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_PACKETS 2000
#define PLAY_TIMEOUT_SEC 8

#define PLAY_PKT_CHUNK_BATCH_FINISHED 0x0b
#define PLAY_PKT_LOGIN_SHIFTED 0x31

typedef enum { CAP_HS, CAP_LOGIN, CAP_CFG, CAP_PLAY } cap_phase;

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
    if (id == MC_PKT_PLAY_LOGIN || id == PLAY_PKT_LOGIN_SHIFTED) return "login";
    if (is_play_map_chunk(id)) return "map_chunk";
    if (id == play_pkt(MC_PKT_PLAY_LIGHT_UPDATE)) return "light_update";
    if (is_play_keep_alive(id)) return "keep_alive";
    if (is_play_ping(id)) return "ping";
    if (is_play_position(id)) return "position";
    if (id == PLAY_PKT_CHUNK_BATCH_FINISHED) return "chunk_batch_finished";
    if (id == play_pkt(MC_PKT_PLAY_CHUNK_BATCH_START)) return "chunk_batch_start";
    if (id == play_pkt(MC_PKT_PLAY_UPDATE_VIEW_DISTANCE)) return "update_view_distance";
    if (id == play_pkt(MC_PKT_PLAY_UPDATE_VIEW_POSITION)) return "update_view_position";
    if (id == play_pkt(MC_PKT_PLAY_SIMULATION_DISTANCE)) return "simulation_distance";
    if (id == play_pkt(MC_PKT_PLAY_SPAWN_POSITION)) return "spawn_position";
    if (id == play_pkt(MC_PKT_PLAY_PLAYER_INFO)) return "player_info";
    if (id == play_pkt(MC_PKT_PLAY_ABILITIES)) return "abilities";
    if (id == play_pkt(MC_PKT_PLAY_DIFFICULTY)) return "difficulty";
    if (id == play_pkt(MC_PKT_PLAY_UPDATE_TIME)) return "update_time";
    if (id == play_pkt(MC_PKT_PLAY_INITIALIZE_WORLD_BORDER)) return "initialize_world_border";
    if (id == play_pkt(MC_PKT_PLAY_GAME_STATE_CHANGE)) return "game_state_change";
    if (id == play_pkt(MC_PKT_PLAY_EXPERIENCE)) return "experience";
    if (id == play_pkt(MC_PKT_PLAY_UPDATE_HEALTH)) return "update_health";
    if (id == play_pkt(MC_PKT_PLAY_SET_TICKING_STATE)) return "set_ticking_state";
    if (id == play_pkt(MC_PKT_PLAY_STEP_TICK)) return "step_tick";
  }
  return NULL;
}

static int tcp_connect(const char *host, const char *port_str) {
  struct addrinfo hints, *res = NULL, *ai;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  int gai = getaddrinfo(host, port_str, &hints, &res);
  if (gai != 0) {
    fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(gai));
    return -1;
  }
  int fd = -1;
  for (ai = res; ai; ai = ai->ai_next) {
    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) continue;
    if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
    close(fd);
    fd = -1;
  }
  freeaddrinfo(res);
  if (fd < 0) fprintf(stderr, "connect failed: %s\n", strerror(errno));
  return fd;
}

static int send_handshake(int fd, const char *host, uint16_t port) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_varint(&b, MC_PROTOCOL_VERSION) != LC_OK) return -1;
  if (mc_buf_string(&b, host) != LC_OK) return -1;
  if (mc_buf_u8(&b, (uint8_t)(port & 0xff)) != LC_OK) return -1;
  if (mc_buf_u8(&b, (uint8_t)(port >> 8)) != LC_OK) return -1;
  if (mc_buf_varint(&b, MC_HS_LOGIN) != LC_OK) return -1;
  if (mc_send_frame(fd, MC_PKT_HS_SET_PROTOCOL, b.data, b.len) != 0) {
    mc_buf_free(&b);
    return -1;
  }
  mc_buf_free(&b);
  return 0;
}

static int send_login_start(int fd, const char *username, const uint8_t uuid[16]) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_string(&b, username) != LC_OK) return -1;
  if (mc_buf_uuid(&b, uuid) != LC_OK) return -1;
  int rc = mc_send_frame(fd, MC_PKT_C2S_LOGIN_START, b.data, b.len);
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

static int echo_select_known_packs(int fd, const uint8_t *wire, size_t wire_len) {
  const uint8_t *body = NULL;
  size_t body_len = 0;
  if (payload_after_id(wire, wire_len, &body, &body_len) != 0) return -1;
  return mc_send_frame(fd, MC_PKT_C2S_CFG_SELECT_KNOWN_PACKS, body, body_len);
}

static int send_teleport_confirm(int fd, int32_t teleport_id) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_varint(&b, teleport_id) != LC_OK) return -1;
  int rc = mc_send_frame(fd, MC_PKT_C2S_TELEPORT_CONFIRM, b.data, b.len);
  mc_buf_free(&b);
  return rc;
}

static int send_cfg_pong(int fd, int32_t id) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_varint(&b, id) != LC_OK) return -1;
  int rc = mc_send_frame(fd, MC_PKT_C2S_CFG_PONG, b.data, b.len);
  mc_buf_free(&b);
  return rc;
}

static int send_play_pong(int fd, int32_t id) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_i32_be(&b, id) != LC_OK) return -1;
  int rc = mc_send_frame(fd, MC_PKT_C2S_PONG, b.data, b.len);
  mc_buf_free(&b);
  return rc;
}

static int save_packet(FILE *idx, const char *outdir, int seq, cap_phase phase, int32_t pkt_id,
                       const uint8_t *wire, size_t wire_len) {
  const char *label = pkt_label(phase, pkt_id);
  char id_hex[8];
  snprintf(id_hex, sizeof id_hex, "%02x", (unsigned)(pkt_id & 0xff));
  char fname[256];
  if (label)
    snprintf(fname, sizeof fname, "%s/%04d_%s_%s_%s.wire", outdir, seq, phase_name(phase), id_hex, label);
  else
    snprintf(fname, sizeof fname, "%s/%04d_%s_%s_pkt.wire", outdir, seq, phase_name(phase), id_hex);

  FILE *f = fopen(fname, "wb");
  if (!f) {
    perror(fname);
    return -1;
  }
  if (fwrite(wire, 1, wire_len, f) != wire_len) {
    fclose(f);
    return -1;
  }
  fclose(f);

  const char *name = label ? label : "unknown";
  fprintf(idx, "%04d %s 0x%s %s %zu %s\n", seq, phase_name(phase), id_hex, name, wire_len, fname);
  return 0;
}

static int send_keep_alive_reply(int fd, int32_t c2s_id, const uint8_t *body, size_t body_len) {
  if (body_len != 8) return 0;
  return mc_send_frame(fd, c2s_id, body, 8);
}

static int send_chunk_batch_received(int fd) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_f32_be(&b, 6.0f) != LC_OK) return -1;
  int rc = mc_send_frame(fd, MC_PKT_C2S_CHUNK_BATCH_RECEIVED, b.data, b.len);
  mc_buf_free(&b);
  return rc;
}

static int handle_position_packet(int fd, const uint8_t *wire, size_t wire_len, int *player_loaded_sent) {
  const uint8_t *body = NULL;
  size_t body_len = 0;
  if (payload_after_id(wire, wire_len, &body, &body_len) != 0) return -1;
  size_t off = 0;
  int32_t tid = 0;
  if (read_varint(body, body_len, &off, &tid) != 0) return -1;
  if (send_teleport_confirm(fd, tid) != 0) return -1;
  if (!*player_loaded_sent) {
    *player_loaded_sent = 1;
    if (mc_send_frame(fd, MC_PKT_C2S_PLAYER_LOADED, NULL, 0) != 0) return -1;
  }
  return 0;
}

static int handle_play_response(int fd, int32_t pkt_id, const uint8_t *wire, size_t wire_len, int *player_loaded_sent,
                                int *map_chunks_seen) {
  if (pkt_id == PLAY_PKT_CHUNK_BATCH_FINISHED) {
    return send_chunk_batch_received(fd);
  }
  if (is_play_keep_alive(pkt_id)) {
    const uint8_t *body = NULL;
    size_t body_len = 0;
    if (payload_after_id(wire, wire_len, &body, &body_len) != 0) return -1;
    return send_keep_alive_reply(fd, MC_PKT_C2S_KEEP_ALIVE, body, body_len);
  }
  if (is_play_ping(pkt_id)) {
    const uint8_t *body = NULL;
    size_t body_len = 0;
    if (payload_after_id(wire, wire_len, &body, &body_len) != 0 || body_len < 4) return -1;
    int32_t ping_id = (int32_t)((uint32_t)body[0] << 24 | (uint32_t)body[1] << 16 | (uint32_t)body[2] << 8 |
                                (uint32_t)body[3]);
    return send_play_pong(fd, ping_id);
  }
  if (is_play_position(pkt_id)) {
    return handle_position_packet(fd, wire, wire_len, player_loaded_sent);
  }
  if (is_play_map_chunk(pkt_id)) {
    (*map_chunks_seen)++;
    if (!*player_loaded_sent && *map_chunks_seen >= 3) {
      *player_loaded_sent = 1;
      if (mc_send_frame(fd, MC_PKT_C2S_PLAYER_LOADED, NULL, 0) != 0) return -1;
    }
    if (mc_send_frame(fd, MC_PKT_C2S_TICK_END, NULL, 0) != 0) return -1;
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
  if (argc != 4) {
    fprintf(stderr, "Usage: %s <host> <port> <outdir>\n", argv[0]);
    return 1;
  }
  const char *host = argv[1];
  const char *port_str = argv[2];
  const char *outdir = argv[3];

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

  int fd = tcp_connect(host, port_str);
  if (fd < 0) {
    fclose(idx);
    return 1;
  }

  const char *username = "CaptureBot";
  uint8_t uuid[16];
  mc_offline_uuid(username, uuid);

  uint16_t port = (uint16_t)atoi(port_str);
  if (send_handshake(fd, host, port) != 0 || send_login_start(fd, username, uuid) != 0) {
    close(fd);
    fclose(idx);
    return 1;
  }

  cap_phase phase = CAP_LOGIN;
  int seq = 0;
  int login_ack_sent = 0;
  int config_finish_sent = 0;
  int player_loaded_sent = 0;
  int map_chunks_seen = 0;
  time_t play_enter = 0;

  fprintf(stderr, "connected to %s:%s, saving to %s\n", host, port_str, outdir);

  while (seq < MAX_PACKETS) {
    if (phase == CAP_PLAY && play_enter && time(NULL) - play_enter >= PLAY_TIMEOUT_SEC) break;

    uint8_t *wire = NULL;
    size_t wire_len = 0;
    int32_t pkt_id = 0;
    if (mc_read_packet(fd, &wire, &wire_len, &pkt_id) != 0) {
      fprintf(stderr, "read error or disconnect after %d packets\n", seq);
      free(wire);
      break;
    }

    if (phase == CAP_PLAY) detect_play_id_shift(pkt_id);

    if (save_packet(idx, outdir, seq, phase, pkt_id, wire, wire_len) != 0) {
      free(wire);
      break;
    }

    const char *label = pkt_label(phase, pkt_id);
    fprintf(stderr, "[%04d] %s 0x%02x %s (%zu B)\n", seq, phase_name(phase), (unsigned)(pkt_id & 0xff),
            label ? label : "?", wire_len);
    if (phase == CAP_PLAY) try_decode_summary(label, wire, wire_len);

    if (phase == CAP_LOGIN) {
      if (pkt_id == MC_PKT_LOGIN_COMPRESS) {
        const uint8_t *body = NULL;
        size_t body_len = 0;
        int32_t threshold = 0;
        if (payload_after_id(wire, wire_len, &body, &body_len) == 0 && body_len > 0) {
          size_t off = 0;
          read_varint(body, body_len, &off, &threshold);
        }
        if (threshold >= 0) {
          fprintf(stderr,
                  "server enabled compression (threshold=%d); set network-compression-threshold=-1 on reference server\n",
                  threshold);
          free(wire);
          fclose(idx);
          close(fd);
          return 1;
        }
      }
      if (pkt_id == MC_PKT_LOGIN_SUCCESS && !login_ack_sent) {
        login_ack_sent = 1;
        if (mc_send_frame(fd, MC_PKT_C2S_LOGIN_ACKNOWLEDGED, NULL, 0) != 0) {
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
        if (echo_select_known_packs(fd, wire, wire_len) != 0) {
          free(wire);
          break;
        }
      } else if (pkt_id == MC_PKT_CFG_KEEP_ALIVE) {
        const uint8_t *body = NULL;
        size_t body_len = 0;
        if (payload_after_id(wire, wire_len, &body, &body_len) != 0 ||
            send_keep_alive_reply(fd, MC_PKT_C2S_CFG_KEEP_ALIVE, body, body_len) != 0) {
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
        if (read_varint(body, body_len, &off, &ping_id) != 0 || send_cfg_pong(fd, ping_id) != 0) {
          free(wire);
          break;
        }
      } else if (pkt_id == MC_PKT_CFG_FINISH && !config_finish_sent) {
        config_finish_sent = 1;
        if (mc_send_frame(fd, MC_PKT_C2S_CFG_FINISH, NULL, 0) != 0) {
          free(wire);
          break;
        }
        phase = CAP_PLAY;
        play_enter = time(NULL);
        fprintf(stderr, "-> play (capture %ds)\n", PLAY_TIMEOUT_SEC);
      }
    } else if (phase == CAP_PLAY) {
      if (handle_play_response(fd, pkt_id, wire, wire_len, &player_loaded_sent, &map_chunks_seen) != 0) {
        fprintf(stderr, "play response failed for 0x%02x\n", (unsigned)(pkt_id & 0xff));
        free(wire);
        break;
      }
    }

    free(wire);
    seq++;
  }

  fclose(idx);
  close(fd);
  fprintf(stderr, "saved %d packets to %s/index.txt\n", seq, outdir);
  return 0;
}
