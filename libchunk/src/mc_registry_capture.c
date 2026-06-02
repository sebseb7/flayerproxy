#define _DEFAULT_SOURCE
/**
 * Minimal client: login → configuration, collect registry_data + update_tags
 * in server send order (same payloads mc_static_send_registry_sync replays).
 */
#include "mc_registry_capture.h"

#include "internal.h"
#include "libchunk.h"
#include "mc_conn.h"
#include "mc_dns.h"
#include "mc_log.h"
#include "mc_online.h"
#include "mc_packet_ids.h"
#include "mc_server_common.h"
#include "mc_wire.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_REGISTRY_PACKETS 64
#define MAX_SYNC_STEPS 96

typedef struct {
  int select_known_packs;
  int login;
  int position;
  int update_recipes;
  int recipe_book_settings;
  int recipe_book_add;
  int world_border;
  int update_time;
} play_capture_flags;

static int play_capture_complete(const play_capture_flags *flags) {
  return flags->update_recipes && flags->recipe_book_settings && flags->recipe_book_add &&
         flags->world_border && flags->update_time;
}

static int capture_complete(int registries, int tags_seen, const play_capture_flags *flags) {
  return registries > 0 && tags_seen && flags->select_known_packs && play_capture_complete(flags);
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

static int send_handshake(mc_conn *conn, const char *host, uint16_t port) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_varint(&b, MC_PROTOCOL_VERSION) != LC_OK) return -1;
  if (mc_buf_string(&b, host) != LC_OK) return -1;
  if (mc_buf_u8(&b, (uint8_t)(port & 0xff)) != LC_OK) return -1;
  if (mc_buf_u8(&b, (uint8_t)(port >> 8)) != LC_OK) return -1;
  if (mc_buf_varint(&b, MC_HS_LOGIN) != LC_OK) return -1;
  int rc = mc_conn_send_frame(conn, MC_PKT_HS_SET_PROTOCOL, b.data, b.len);
  mc_buf_free(&b);
  return rc;
}

static int send_login_start(mc_conn *conn, const char *username, const uint8_t uuid[16]) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_string(&b, username) != LC_OK) return -1;
  if (mc_buf_uuid(&b, uuid) != LC_OK) return -1;
  int rc = mc_conn_send_frame(conn, MC_PKT_C2S_LOGIN_START, b.data, b.len);
  mc_buf_free(&b);
  return rc;
}

static int echo_select_known_packs(mc_conn *conn, const uint8_t *wire, size_t wire_len) {
  const uint8_t *body = NULL;
  size_t body_len = 0;
  if (payload_after_id(wire, wire_len, &body, &body_len) != 0) return -1;
  return mc_conn_send_frame(conn, MC_PKT_C2S_CFG_SELECT_KNOWN_PACKS, body, body_len);
}

static int send_keep_alive_reply(mc_conn *conn, int32_t c2s_id, const uint8_t *body, size_t body_len) {
  if (body_len != 8) return 0;
  return mc_conn_send_frame(conn, c2s_id, body, 8);
}

static int send_cfg_pong(mc_conn *conn, int32_t id) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_varint(&b, id) != LC_OK) return -1;
  int rc = mc_conn_send_frame(conn, MC_PKT_C2S_CFG_PONG, b.data, b.len);
  mc_buf_free(&b);
  return rc;
}

static uint8_t *dup_bytes(const uint8_t *src, size_t len) {
  uint8_t *p = (uint8_t *)malloc(len ? len : 1);
  if (!p) return NULL;
  if (len) memcpy(p, src, len);
  return p;
}

static int append_step(mc_registry_capture_result *out, mc_reg_sync_kind kind, const char *label,
                       const uint8_t *data, size_t len) {
  if (out->step_count >= MAX_SYNC_STEPS) return -1;
  uint8_t *owned = dup_bytes(data, len);
  if (!owned && len) return -1;
  mc_reg_sync_step *step = &out->steps[out->step_count++];
  step->kind = kind;
  step->pkt_id = 0;
  step->data = owned;
  step->len = len;
  snprintf(step->label, sizeof step->label, "%s", label ? label : "");
  return 0;
}

static int append_play_step(mc_registry_capture_result *out, int32_t pkt_id, const char *label,
                            const uint8_t *data, size_t len) {
  if (out->step_count >= MAX_SYNC_STEPS) return -1;
  uint8_t *owned = dup_bytes(data, len);
  if (!owned && len) return -1;
  mc_reg_sync_step *step = &out->steps[out->step_count++];
  step->kind = MC_REG_SYNC_PLAY;
  step->pkt_id = pkt_id;
  step->data = owned;
  step->len = len;
  snprintf(step->label, sizeof step->label, "%s", label ? label : "");
  return 0;
}

static int send_teleport_confirm(mc_conn *conn, int32_t teleport_id) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_varint(&b, teleport_id) != LC_OK) return -1;
  int rc = mc_conn_send_frame(conn, MC_PKT_C2S_TELEPORT_CONFIRM, b.data, b.len);
  mc_buf_free(&b);
  return rc;
}

static int send_play_pong(mc_conn *conn, int32_t id) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_i32_be(&b, id) != LC_OK) return -1;
  int rc = mc_conn_send_frame(conn, MC_PKT_C2S_PONG, b.data, b.len);
  mc_buf_free(&b);
  return rc;
}

static int handle_play_position(mc_conn *conn, const uint8_t *body, size_t body_len) {
  size_t off = 0;
  int32_t tid = 0;
  if (read_varint(body, body_len, &off, &tid) != 0) return -1;
  return send_teleport_confirm(conn, tid);
}

static int handle_play_response(mc_conn *conn, int32_t pkt_id, const uint8_t *body, size_t body_len) {
  if (pkt_id == MC_PKT_PLAY_KEEP_ALIVE) {
    if (body_len != 8) return 0;
    return mc_conn_send_frame(conn, MC_PKT_C2S_KEEP_ALIVE, body, 8);
  }
  if (pkt_id == MC_PKT_PLAY_PING) {
    if (body_len < 4) return -1;
    int32_t ping_id = (int32_t)((uint32_t)body[0] << 24 | (uint32_t)body[1] << 16 | (uint32_t)body[2] << 8 |
                                (uint32_t)body[3]);
    return send_play_pong(conn, ping_id);
  }
  if (pkt_id == MC_PKT_PLAY_POSITION) return handle_play_position(conn, body, body_len);
  if (pkt_id == MC_PKT_PLAY_CHUNK_BATCH_FINISHED) {
    mc_buf b;
    memset(&b, 0, sizeof b);
    if (mc_buf_f32_be(&b, 6.0f) != LC_OK) return -1;
    int rc = mc_conn_send_frame(conn, MC_PKT_C2S_CHUNK_BATCH_RECEIVED, b.data, b.len);
    mc_buf_free(&b);
    return rc;
  }
  return 0;
}

static const char *step_label_for_play(int32_t pkt_id) {
  switch (pkt_id) {
  case MC_PKT_PLAY_LOGIN:
    return "login";
  case MC_PKT_PLAY_POSITION:
    return "position";
  case MC_PKT_PLAY_UPDATE_RECIPES:
    return "update_recipes";
  case MC_PKT_PLAY_RECIPE_BOOK_SETTINGS:
    return "recipe_book_settings";
  case MC_PKT_PLAY_RECIPE_BOOK_ADD:
    return "recipe_book_add";
  case MC_PKT_PLAY_INITIALIZE_WORLD_BORDER:
    return "initialize_world_border";
  case MC_PKT_PLAY_UPDATE_TIME:
    return "update_time";
  default:
    return "play";
  }
}

static int capture_play_packet(mc_registry_capture_result *out, play_capture_flags *flags, int32_t pkt_id,
                               const char *label, const uint8_t *body, size_t body_len) {
  int *seen = NULL;
  if (pkt_id == MC_PKT_PLAY_LOGIN) seen = &flags->login;
  else if (pkt_id == MC_PKT_PLAY_POSITION) seen = &flags->position;
  else if (pkt_id == MC_PKT_PLAY_UPDATE_RECIPES) seen = &flags->update_recipes;
  else if (pkt_id == MC_PKT_PLAY_RECIPE_BOOK_SETTINGS) seen = &flags->recipe_book_settings;
  else if (pkt_id == MC_PKT_PLAY_RECIPE_BOOK_ADD) seen = &flags->recipe_book_add;
  else if (pkt_id == MC_PKT_PLAY_INITIALIZE_WORLD_BORDER) seen = &flags->world_border;
  else if (pkt_id == MC_PKT_PLAY_UPDATE_TIME) seen = &flags->update_time;
  if (!seen || *seen) return 0;
  if (append_play_step(out, pkt_id, label, body, body_len) != 0) return -1;
  *seen = 1;
  MC_LOGEV("static_server", "registry fetch: %s (0x%02x, %zu B)", label, (unsigned)(pkt_id & 0xff), body_len);
  return 0;
}

static const char *registry_label_from_payload(const uint8_t *payload, size_t len, char *buf, size_t buflen) {
  lc_buf b;
  lc_buf_init(&b, payload, len);
  char *id = NULL;
  if (lc_buf_read_string(&b, &id) != LC_OK || !id) {
    snprintf(buf, buflen, "registry");
    return buf;
  }
  snprintf(buf, buflen, "%s", id);
  free(id);
  return buf;
}

void mc_registry_capture_result_free(mc_registry_capture_result *out) {
  if (!out) return;
  for (size_t i = 0; i < out->step_count; i++) free(out->steps[i].data);
  free(out->steps);
  memset(out, 0, sizeof *out);
}

int mc_registry_capture_configuration(const mc_registry_capture_config *cfg, mc_registry_capture_result *out,
                                      mc_registry_config_ready_fn on_config_ready, void *ctx) {
  if (!cfg || !cfg->host || cfg->port <= 0 || !out) return -1;
  memset(out, 0, sizeof *out);

  const char *username = cfg->username ? cfg->username : "FlayerBot";
  char port_str[16];
  snprintf(port_str, sizeof port_str, "%d", cfg->port);

  int fd = mc_tcp_connect(cfg->host, port_str, 15);
  if (fd < 0) {
    MC_LOGE("static_server", "registry fetch: connect failed");
    return -1;
  }

  mc_conn conn;
  mc_conn_init(&conn, fd);

  mc_online_creds creds = {0};
  char *owned_token = NULL;
  char *owned_profile = NULL;
  int want_online =
      strcmp(cfg->host, "127.0.0.1") != 0 && strcmp(cfg->host, "localhost") != 0 && !getenv("MC_OFFLINE");
  if (want_online && mc_online_creds_from_env(&creds) != 0) {
    if (mc_online_creds_from_msa(username, &owned_token, &owned_profile) == 0) {
      creds.access_token = owned_token;
      creds.profile_id = owned_profile;
    }
  }

  uint8_t uuid[16];
  mc_offline_uuid(username, uuid);

  if (send_handshake(&conn, cfg->host, (uint16_t)cfg->port) != 0 ||
      send_login_start(&conn, username, uuid) != 0) {
    MC_LOGE("static_server", "registry fetch: handshake/login failed");
    mc_conn_free(&conn);
    close(fd);
    free(owned_token);
    free(owned_profile);
    return -1;
  }

  out->steps = (mc_reg_sync_step *)calloc(MAX_SYNC_STEPS, sizeof(mc_reg_sync_step));
  if (!out->steps) {
    mc_conn_free(&conn);
    close(fd);
    free(owned_token);
    free(owned_profile);
    return -1;
  }

  int phase_login = 1;
  int phase_play = 0;
  int login_ack_sent = 0;
  int tags_seen = 0;
  int registries = 0;
  play_capture_flags play_flags;
  memset(&play_flags, 0, sizeof play_flags);
  int rc = -1;

  for (int seq = 0; seq < 1024; seq++) {
    uint8_t *wire = NULL;
    size_t wire_len = 0;
    int32_t pkt_id = 0;
    if (mc_conn_read_packet(&conn, &wire, &wire_len, &pkt_id) != 0) {
      MC_LOGE("static_server", "registry fetch: read failed after %d packets", seq);
      free(wire);
      break;
    }

    if (phase_login) {
      if (pkt_id == MC_PKT_LOGIN_ENCRYPTION_BEGIN) {
        const uint8_t *body = NULL;
        size_t body_len = 0;
        mc_encryption_begin enc;
        memset(&enc, 0, sizeof enc);
        if (payload_after_id(wire, wire_len, &body, &body_len) != 0 ||
            mc_parse_encryption_begin(body, body_len, &enc) != 0 ||
            mc_online_handle_encryption_begin(&conn, &enc, creds.access_token ? &creds : NULL) != 0) {
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
      }
      if (pkt_id == MC_PKT_LOGIN_SUCCESS && !login_ack_sent) {
        login_ack_sent = 1;
        if (mc_conn_send_frame(&conn, MC_PKT_C2S_LOGIN_ACKNOWLEDGED, NULL, 0) != 0) {
          free(wire);
          break;
        }
        phase_login = 0;
      }
      if (pkt_id == MC_PKT_LOGIN_DISCONNECT) {
        MC_LOGE("static_server", "registry fetch: login disconnect");
        free(wire);
        break;
      }
      free(wire);
      if (!phase_login) continue;
      continue;
    }

    const uint8_t *body = NULL;
    size_t body_len = 0;
    if (payload_after_id(wire, wire_len, &body, &body_len) != 0) {
      free(wire);
      break;
    }

    if (pkt_id == MC_PKT_CFG_SELECT_KNOWN_PACKS) {
      if (!play_flags.select_known_packs &&
          append_step(out, MC_REG_SYNC_SELECT_KNOWN_PACKS, "select_known_packs", body, body_len) != 0) {
        free(wire);
        break;
      }
      play_flags.select_known_packs = 1;
      MC_LOGEV("static_server", "registry fetch: select_known_packs (%zu B)", body_len);
      int send_rc;
      if (cfg->client_known_packs && cfg->client_known_packs_len > 0) {
        send_rc = mc_conn_send_frame(&conn, MC_PKT_C2S_CFG_SELECT_KNOWN_PACKS, cfg->client_known_packs,
                                     cfg->client_known_packs_len);
      } else {
        send_rc = echo_select_known_packs(&conn, wire, wire_len);
      }
      if (send_rc != 0) {
        free(wire);
        break;
      }
    } else if (pkt_id == MC_PKT_CFG_KEEP_ALIVE) {
      if (send_keep_alive_reply(&conn, MC_PKT_C2S_CFG_KEEP_ALIVE, body, body_len) != 0) {
        free(wire);
        break;
      }
    } else if (pkt_id == MC_PKT_CFG_PING) {
      size_t off = 0;
      int32_t ping_id = 0;
      if (read_varint(body, body_len, &off, &ping_id) != 0 || send_cfg_pong(&conn, ping_id) != 0) {
        free(wire);
        break;
      }
    } else if (pkt_id == MC_PKT_CFG_REGISTRY_DATA) {
      if (registries >= MAX_REGISTRY_PACKETS) {
        free(wire);
        break;
      }
      char label[64];
      registry_label_from_payload(body, body_len, label, sizeof label);
      if (append_step(out, MC_REG_SYNC_REGISTRY, label, body, body_len) != 0) {
        free(wire);
        break;
      }
      registries++;
      MC_LOGEV("static_server", "registry fetch: registry_data %s (%zu B)", label, body_len);
    } else if (pkt_id == MC_PKT_COMMON_UPDATE_TAGS) {
      if (append_step(out, MC_REG_SYNC_TAGS, "update_tags", body, body_len) != 0) {
        free(wire);
        break;
      }
      tags_seen = 1;
      MC_LOGEV("static_server", "registry fetch: update_tags (%zu B)", body_len);
    } else if (pkt_id == MC_PKT_CFG_RESET_CHAT) {
      if (append_step(out, MC_REG_SYNC_RESET_CHAT, "reset_chat", body, body_len) != 0) {
        free(wire);
        break;
      }
    } else if (pkt_id == MC_PKT_CFG_FINISH) {
      if (registries <= 0 || !tags_seen) {
        MC_LOGE("static_server", "registry fetch: finish without registries/tags (r=%d t=%d)", registries,
                tags_seen);
        free(wire);
        break;
      }
      if (mc_conn_send_frame(&conn, MC_PKT_C2S_CFG_FINISH, NULL, 0) != 0) {
        free(wire);
        break;
      }
      phase_play = 1;
      if (on_config_ready) {
        int config_ok = registries > 0 && tags_seen;
        on_config_ready(out, config_ok, ctx);
        MC_LOGEV("static_server", "registry fetch: entering play capture");
        free(wire);
        continue;
      }
      MC_LOGEV("static_server", "registry fetch: entering play capture");
      free(wire);
      continue;
    } else if (phase_play) {
      if (handle_play_response(&conn, pkt_id, body, body_len) != 0) {
        free(wire);
        break;
      }
      if (capture_play_packet(out, &play_flags, pkt_id, step_label_for_play(pkt_id), body, body_len) != 0) {
        free(wire);
        break;
      }
      if (on_config_ready) {
        if (play_capture_complete(&play_flags)) {
          rc = 0;
          MC_LOGI("static_server", "registry fetch: play join cached (%zu sync steps)", out->step_count);
          free(wire);
          break;
        }
      } else if (capture_complete(registries, tags_seen, &play_flags)) {
        rc = 0;
        MC_LOGI("static_server", "registry fetch: done (%d registries, %zu sync steps, play join cached)",
                registries, out->step_count);
        free(wire);
        break;
      }
    }

    free(wire);
  }

  mc_conn_free(&conn);
  close(fd);
  free(owned_token);
  free(owned_profile);

  if (rc != 0) mc_registry_capture_result_free(out);
  return rc;
}
