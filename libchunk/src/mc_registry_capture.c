#define _DEFAULT_SOURCE
/**
 * Minimal client: login → configuration, collect registry_data + update_tags
 * in server send order (same payloads mc_static_send_registry_sync replays).
 */
#include "mc_registry_capture.h"

#include "mc_registry_join_template.h"

#include "internal.h"
#include "libchunk.h"
#include "mc_conn.h"
#include "mc_chunk_stream.h"
#include "mc_dns.h"
#include "mc_conn_state.h"
#include "mc_log.h"
#include "mc_upstream_bridge.h"
#include "mc_online.h"
#include "mc_s2c_log.h"
#include "mc_packet_ids.h"
#include "mc_server_common.h"
#include "mc_wire.h"

#include <errno.h>
#include <math.h>
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

typedef struct {
  int map_chunks_seen;
  int chunk_batches_finished;
} chunk_capture_flags;

static int play_capture_complete(const play_capture_flags *flags) {
  return flags->update_recipes && flags->recipe_book_settings && flags->recipe_book_add &&
         flags->world_border && flags->update_time;
}

/* Enough to replay play join (see capture/1: login → recipes → position → border → time → spawn). */
static int play_join_minimum_ready(const play_capture_flags *flags, const mc_registry_join_template *join) {
  return flags->login && flags->position && play_capture_complete(flags) && join->login_valid &&
         join->position_valid && join->world_border_valid && join->update_time_valid &&
         join->spawn_position_valid;
}

static int chunk_capture_complete(const chunk_capture_flags *flags) {
  (void)flags;
  return mc_static_chunks_count() > 0;
}

static int config_capture_complete(int registries, int tags_seen, const play_capture_flags *flags) {
  return registries > 0 && tags_seen && flags->select_known_packs && play_capture_complete(flags);
}

static void log_registry_capture_incomplete(int packets_read, int32_t last_pkt_id, int hit_packet_limit,
                                            const play_capture_flags *play, const chunk_capture_flags *chunks) {
  MC_LOGW("static_server",
          "registry fetch: capture incomplete after %d packets (last_pkt=0x%02x%s)", packets_read, last_pkt_id,
          hit_packet_limit ? ", hit 1024 packet limit" : "");
  if (!play->update_recipes) MC_LOGW("static_server", "registry fetch: still waiting on: update_recipes");
  if (!play->recipe_book_settings)
    MC_LOGW("static_server", "registry fetch: still waiting on: recipe_book_settings");
  if (!play->recipe_book_add) MC_LOGW("static_server", "registry fetch: still waiting on: recipe_book_add");
  if (!play->world_border) MC_LOGW("static_server", "registry fetch: still waiting on: world_border");
  if (!play->update_time) MC_LOGW("static_server", "registry fetch: still waiting on: update_time");
  if (chunks->map_chunks_seen == 0)
    MC_LOGW("static_server", "registry fetch: still waiting on: map_chunk (0x%02x)", MC_PKT_PLAY_MAP_CHUNK);
  else
    MC_LOGW("static_server", "registry fetch: map_chunks_seen=%d", chunks->map_chunks_seen);
  if (mc_static_chunks_count() == 0)
    MC_LOGW("static_server", "registry fetch: still waiting on: cached chunks (count=0)");
  else
    MC_LOGW("static_server", "registry fetch: cached_chunks=%zu", mc_static_chunks_count());
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

static int send_player_loaded(mc_conn *conn) {
  return mc_conn_send_frame(conn, MC_PKT_C2S_PLAYER_LOADED, NULL, 0);
}

static int send_tick_end(mc_conn *conn) {
  return mc_conn_send_frame(conn, MC_PKT_C2S_TICK_END, NULL, 0);
}

static int handle_play_position(mc_conn *conn, const uint8_t *body, size_t body_len, int *player_loaded_sent) {
  size_t off = 0;
  int32_t tid = 0;
  if (read_varint(body, body_len, &off, &tid) != 0) return -1;
  if (send_teleport_confirm(conn, tid) != 0) return -1;
  if (!*player_loaded_sent) {
    *player_loaded_sent = 1;
    return send_player_loaded(conn);
  }
  return 0;
}

static int handle_play_response(mc_conn *conn, int32_t pkt_id, const uint8_t *body, size_t body_len,
                               chunk_capture_flags *chunk_flags, int *player_loaded_sent) {
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
  if (pkt_id == MC_PKT_PLAY_POSITION) return handle_play_position(conn, body, body_len, player_loaded_sent);
  if (pkt_id == MC_PKT_PLAY_MAP_CHUNK) {
    int stored = mc_static_chunks_store(body, body_len) == 0;
    if (stored) {
      chunk_flags->map_chunks_seen++;
      MC_LOGEV("static_server", "registry fetch: map_chunk cached (seen=%d total=%zu)",
               chunk_flags->map_chunks_seen, mc_static_chunks_count());
    }
    if (!*player_loaded_sent && chunk_flags->map_chunks_seen >= 3) {
      *player_loaded_sent = 1;
      if (send_player_loaded(conn) != 0) return -1;
    }
    return send_tick_end(conn);
  }
  if (pkt_id == MC_PKT_PLAY_CHUNK_BATCH_FINISHED) {
    chunk_flags->chunk_batches_finished++;
    mc_buf b;
    memset(&b, 0, sizeof b);
    if (mc_buf_f32_be(&b, 6.0f) != LC_OK) return -1;
    int rc = mc_conn_send_frame(conn, MC_PKT_C2S_CHUNK_BATCH_RECEIVED, b.data, b.len);
    mc_buf_free(&b);
    return rc;
  }
  return 0;
}

static void capture_try_bridge_enable(mc_conn *conn, const mc_registry_capture_result *out, int play_join_ready_sent,
                                    int *bridge_relay, int *rc, play_capture_flags *play_flags) {
  if (!play_join_ready_sent || *bridge_relay) return;

  int32_t radius =
      out->join.login_valid ? mc_static_chunk_radius_from_view(out->join.login_view_distance) : 1;
  if (radius <= 0) radius = 1;
  int32_t cx = 0;
  int32_t cz = 0;
  if (out->join.position_valid) {
    cx = (int32_t)floor(out->join.position.x / 16.0);
    cz = (int32_t)floor(out->join.position.z / 16.0);
  }
  int expect = mc_static_chunks_expected_grid_count(radius);
  int filled = mc_static_chunks_count_in_grid(cx, cz, radius);
  if (filled < expect) return;

  mc_upstream_bridge_enable(conn);
  *bridge_relay = 1;
  if (play_capture_complete(play_flags)) *rc = 0;
  MC_LOGI("static_server",
          "registry fetch: C2S relay enabled (%d/%d chunks at %d,%d; same thread keeps reading upstream)",
          filled, expect, cx, cz);
}

static const char *recipe_step_label(int32_t pkt_id) {
  switch (pkt_id) {
  case MC_PKT_PLAY_UPDATE_RECIPES:
    return "update_recipes";
  case MC_PKT_PLAY_RECIPE_BOOK_SETTINGS:
    return "recipe_book_settings";
  case MC_PKT_PLAY_RECIPE_BOOK_ADD:
    return "recipe_book_add";
  default:
    return "play";
  }
}

static int is_raw_cached_play_pkt(int32_t pkt_id) {
  return pkt_id == MC_PKT_PLAY_UPDATE_RECIPES || pkt_id == MC_PKT_PLAY_RECIPE_BOOK_SETTINGS ||
         pkt_id == MC_PKT_PLAY_RECIPE_BOOK_ADD;
}

static int capture_join_login(mc_registry_join_template *join, const uint8_t *body, size_t body_len) {
  if (join->login_valid) return 0;
  lc_play_login parsed;
  char **wnames = NULL;
  size_t wcount = 0;
  if (lc_parse_play_login(body, body_len, &parsed, &wnames, &wcount) != LC_OK) return -1;
  join->login_hardcore = parsed.hardcore;
  join->login_world_names = wnames;
  join->login_world_name_count = wcount;
  join->login_max_players = parsed.max_players;
  join->login_view_distance = parsed.view_distance;
  join->login_simulation_distance = parsed.simulation_distance;
  join->login_reduced_debug_info = parsed.reduced_debug_info;
  join->login_enable_respawn_screen = parsed.enable_respawn_screen;
  join->login_do_limited_crafting = parsed.do_limited_crafting;
  join->login_enforces_secure_chat = parsed.enforces_secure_chat;
  join->login_world_state = parsed.world_state;
  parsed.world_state.name = NULL;
  parsed.world_state.death_dimension_name = NULL;
  join->login_valid = 1;
  MC_LOGEV("static_server", "registry fetch: login fields (view=%d sim=%d)", join->login_view_distance,
           join->login_simulation_distance);
  return 0;
}

static int capture_join_spawn_position(mc_registry_join_template *join, const uint8_t *body, size_t body_len) {
  if (join->spawn_position_valid) return 0;
  lc_buf b;
  lc_buf_init(&b, body, body_len);
  char *dim = NULL;
  if (lc_buf_read_string(&b, &dim) != LC_OK) return -1;
  if (lc_buf_read_position(&b, &join->spawn_pos) != LC_OK) {
    free(dim);
    return -1;
  }
  if (lc_buf_read_f32_be(&b, &join->spawn_yaw) != LC_OK || lc_buf_read_f32_be(&b, &join->spawn_pitch) != LC_OK) {
    free(dim);
    return -1;
  }
  join->spawn_dimension = dim;
  join->spawn_position_valid = 1;
  MC_LOGEV("static_server", "registry fetch: spawn_position (%s)", dim ? dim : "?");
  return 0;
}

static int capture_join_update_time(mc_registry_join_template *join, const uint8_t *body, size_t body_len) {
  if (join->update_time_valid) return 0;
  lc_buf b;
  lc_buf_init(&b, body, body_len);
  if (lc_buf_read_i64_be(&b, &join->time_world_age) != LC_OK) return -1;
  if (lc_buf_read_i64_be(&b, &join->time_time_of_day) != LC_OK) return -1;
  if (lc_buf_read_u8(&b, &join->time_tick_day_time) != LC_OK) return -1;
  join->update_time_valid = 1;
  MC_LOGEV("static_server", "registry fetch: update_time (age=%lld)", (long long)join->time_world_age);
  return 0;
}

static int capture_play_packet(mc_registry_capture_result *out, play_capture_flags *flags, int32_t pkt_id,
                               const char *label, const uint8_t *body, size_t body_len) {
  mc_registry_join_template *join = &out->join;

  if (pkt_id == MC_PKT_PLAY_LOGIN) {
    if (flags->login) return 0;
    if (capture_join_login(join, body, body_len) != 0) return -1;
    flags->login = 1;
    return 0;
  }
  if (pkt_id == MC_PKT_PLAY_POSITION) {
    if (flags->position) return 0;
    if (lc_parse_position(body, body_len, &join->position) != LC_OK) return -1;
    join->position_valid = 1;
    flags->position = 1;
    MC_LOGEV("static_server", "registry fetch: position (%.3f, %.3f, %.3f)", join->position.x, join->position.y,
             join->position.z);
    return 0;
  }
  if (pkt_id == MC_PKT_PLAY_INITIALIZE_WORLD_BORDER) {
    if (flags->world_border) return 0;
    if (lc_parse_initialize_world_border(body, body_len, &join->world_border) != LC_OK) return -1;
    join->world_border_valid = 1;
    flags->world_border = 1;
    MC_LOGEV("static_server", "registry fetch: world_border");
    return 0;
  }
  if (pkt_id == MC_PKT_PLAY_UPDATE_TIME) {
    if (flags->update_time) return 0;
    if (capture_join_update_time(join, body, body_len) != 0) return -1;
    flags->update_time = 1;
    return 0;
  }
  if (pkt_id == MC_PKT_PLAY_SPAWN_POSITION) {
    if (capture_join_spawn_position(join, body, body_len) != 0) return -1;
    return 0;
  }

  if (!is_raw_cached_play_pkt(pkt_id)) return 0;

  int *seen = NULL;
  if (pkt_id == MC_PKT_PLAY_UPDATE_RECIPES) seen = &flags->update_recipes;
  else if (pkt_id == MC_PKT_PLAY_RECIPE_BOOK_SETTINGS) seen = &flags->recipe_book_settings;
  else if (pkt_id == MC_PKT_PLAY_RECIPE_BOOK_ADD) seen = &flags->recipe_book_add;
  if (!seen || *seen) return 0;
  if (append_play_step(out, pkt_id, label, body, body_len) != 0) return -1;
  *seen = 1;
  MC_LOGEV("static_server", "registry fetch: %s raw (0x%02x, %zu B)", label, (unsigned)(pkt_id & 0xff), body_len);
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
  mc_registry_join_template_free(&out->join);
  memset(out, 0, sizeof *out);
}

int mc_registry_capture_configuration(const mc_registry_capture_config *cfg, mc_registry_capture_result *out,
                                      mc_registry_config_ready_fn on_config_ready,
                                      mc_registry_play_join_ready_fn on_play_join_ready,
                                      mc_registry_capture_wake_fn on_capture_wake, void *ctx) {
  if (!cfg || !cfg->host || cfg->port <= 0 || !out) return -1;
  memset(out, 0, sizeof *out);

  const char *username = cfg->username ? cfg->username : "FlayerBot";
  char port_str[16];
  snprintf(port_str, sizeof port_str, "%d", cfg->port);
  char upstream_label[280];
  snprintf(upstream_label, sizeof upstream_label, "%s:%s", cfg->host, port_str);

  if (on_config_ready) mc_conn_state_upstream_reset();

  int fd = mc_tcp_connect(cfg->host, port_str, 15);
  if (fd < 0) {
    MC_LOGE("static_server", "registry fetch: connect failed to %s:%s", cfg->host, port_str);
    return -1;
  }

  if (on_config_ready) mc_conn_state_upstream(MC_CONN_STATE_INITIATED, upstream_label);

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
  chunk_capture_flags chunk_flags;
  memset(&play_flags, 0, sizeof play_flags);
  memset(&chunk_flags, 0, sizeof chunk_flags);
  int player_loaded_sent = 0;
  int play_join_ready_sent = 0;
  int bridge_relay = 0;
  int rc = -1;
  int32_t last_pkt_id = -1;
  int packets_read = 0;
  const int packet_limit = on_config_ready ? 0 : 1024;

  for (int seq = 0; packet_limit == 0 || seq < packet_limit; seq++) {
    uint8_t *wire = NULL;
    size_t wire_len = 0;
    int32_t pkt_id = 0;

    if (bridge_relay) {
      mc_upstream_bridge_lock();
      mc_upstream_bridge_flush(&conn);
    }
    int read_rc = mc_conn_read_packet(&conn, &wire, &wire_len, &pkt_id);
    if (bridge_relay) mc_upstream_bridge_unlock();

    if (read_rc != 0) {
      MC_LOGE("static_server", "registry fetch: read failed after %d packets: %s", packets_read, strerror(errno));
      free(wire);
      break;
    }
    last_pkt_id = pkt_id;
    packets_read++;

    if (phase_login) {
      const uint8_t *login_body = NULL;
      size_t login_body_len = 0;
      if (payload_after_id(wire, wire_len, &login_body, &login_body_len) == 0) {
        mc_log_upstream_s2c(MC_UPSTREAM_S2C_LOGIN, pkt_id, login_body, login_body_len, NULL);
      }

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
        if (on_config_ready) mc_conn_state_upstream(MC_CONN_STATE_DISCONNECTED, upstream_label);
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

    {
      mc_upstream_s2c_phase log_phase = phase_play ? MC_UPSTREAM_S2C_PLAY : MC_UPSTREAM_S2C_CONFIG;
      const char *log_detail = NULL;
      char log_detail_buf[96];
      if (pkt_id == MC_PKT_CFG_REGISTRY_DATA) {
        registry_label_from_payload(body, body_len, log_detail_buf, sizeof log_detail_buf);
        log_detail = log_detail_buf;
      } else if (phase_play && pkt_id == MC_PKT_PLAY_MAP_CHUNK) {
        lc_map_chunk mc;
        memset(&mc, 0, sizeof mc);
        if (lc_parse_map_chunk(body, body_len, &mc) == LC_OK) {
          snprintf(log_detail_buf, sizeof log_detail_buf, "chunk=(%d,%d)", mc.x, mc.z);
          log_detail = log_detail_buf;
          lc_map_chunk_free(&mc);
        }
      }
      mc_log_upstream_s2c(log_phase, pkt_id, body, body_len, log_detail);
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
        mc_conn_state_upstream(MC_CONN_STATE_PLAYING, upstream_label);
        MC_LOGEV("static_server", "registry fetch: entering play capture");
        free(wire);
        continue;
      }
      MC_LOGEV("static_server", "registry fetch: entering play capture");
      free(wire);
      continue;
    } else if (phase_play) {
      size_t chunks_before = 0;
      if (on_capture_wake && pkt_id == MC_PKT_PLAY_MAP_CHUNK) chunks_before = mc_static_chunks_count();

      if (handle_play_response(&conn, pkt_id, body, body_len, &chunk_flags, &player_loaded_sent) != 0) {
        free(wire);
        break;
      }
      if (capture_play_packet(out, &play_flags, pkt_id, recipe_step_label(pkt_id), body, body_len) != 0) {
        free(wire);
        break;
      }
      if (on_capture_wake && pkt_id == MC_PKT_PLAY_MAP_CHUNK && mc_static_chunks_count() > chunks_before) {
        on_capture_wake(ctx);
      }
      if (on_config_ready) {
        if (play_capture_complete(&play_flags)) rc = 0;
        if (on_play_join_ready && !play_join_ready_sent && play_join_minimum_ready(&play_flags, &out->join)) {
          play_join_ready_sent = 1;
          MC_LOGI("static_server",
                  "registry fetch: play join fields captured after %d packets (continuing for map_chunk)",
                  packets_read);
          on_play_join_ready(out, ctx);
        }
        capture_try_bridge_enable(&conn, out, play_join_ready_sent, &bridge_relay, &rc, &play_flags);
      } else if (config_capture_complete(registries, tags_seen, &play_flags) &&
                 chunk_capture_complete(&chunk_flags)) {
        rc = 0;
        MC_LOGI("static_server", "registry fetch: done (%d registries, %zu sync steps, %zu chunks)",
                registries, out->step_count, mc_static_chunks_count());
        free(wire);
        break;
      }
    }

    free(wire);
  }

  if (last_pkt_id >= 0) {
    MC_LOGI("static_server",
            "registry fetch: upstream %s:%d closed (%d packets read, rc=%d, last_pkt=0x%02x)", cfg->host,
            cfg->port, packets_read, rc, (unsigned)(last_pkt_id & 0xff));
  } else {
    MC_LOGI("static_server", "registry fetch: upstream %s:%d closed (%d packets read, rc=%d)", cfg->host,
            cfg->port, packets_read, rc);
  }

  if (bridge_relay) mc_upstream_bridge_disable();
  mc_conn_free(&conn);
  close(fd);
  if (on_config_ready) mc_conn_state_upstream(MC_CONN_STATE_DISCONNECTED, upstream_label);

  free(owned_token);
  free(owned_profile);

  if (rc == 0) {
    MC_LOGI("static_server", "registry fetch: play join cached (%zu sync steps, %zu chunks)", out->step_count,
            mc_static_chunks_count());
  } else {
    log_registry_capture_incomplete(packets_read, last_pkt_id, packets_read >= 1024, &play_flags, &chunk_flags);
    mc_registry_capture_result_free(out);
  }
  return rc;
}
