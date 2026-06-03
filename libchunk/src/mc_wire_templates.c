#define _POSIX_C_SOURCE 200809L

#include "mc_wire_templates.h"

#include "mc_chunk_stream.h"
#include "play_stream.h"

#include "internal.h"
#include "libchunk.h"
#include "mc_log.h"
#include "mc_packet_ids.h"
#include "mc_s2c_log.h"
#include "mc_static_config.h"
#include "mc_static_grass.h"
#include "mc_static_registries.h"
#include "mc_wire.h"
#include "packets_write.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  mc_server_world world;
} mc_template_store;

static mc_template_store g_store;
/* Good for: Free template store (cached grass map_chunk wire).
 * Callers: mc_wire_templates.c (same file).
 */

static void store_free(mc_template_store *s) {
  if (!s) return;
  memset(s, 0, sizeof *s);
}
/* Good for: Send one framed packet (length + id + payload) on socket.
 * Callers: mc_wire_templates.c (same file).
 */

static int send_payload(int fd, int32_t pkt_id, const uint8_t *payload, size_t len) {
  mc_log_s2c_play(pkt_id, payload, len);
  return mc_send_frame(fd, pkt_id, payload, len);
}
/* Good for: Send framed packet and free payload buffer.
 * Callers: mc_wire_templates.c (same file).
 */

static int send_payload_owned(int fd, int32_t pkt_id, uint8_t *data, size_t len) {
  int rc = send_payload(fd, pkt_id, data, len);
  free(data);
  return rc;
}
/* Good for: Send framed packet from mc_buf and reset buffer.
 * Callers: mc_wire_templates.c (same file).
 */

static int send_mc_buf(int fd, int32_t pkt_id, mc_buf *b) {
  int rc = send_payload_owned(fd, pkt_id, b->data, b->len);
  b->data = NULL;
  b->len = b->cap = 0;
  return rc;
}
/* Good for: Send framed packet from lc_byte_buf.
 * Callers: mc_wire_templates.c (same file).
 */

static int send_byte_buf(int fd, int32_t pkt_id, lc_byte_buf *buf) {
  int rc = send_payload(fd, pkt_id, buf->data, buf->len);
  lc_byte_buf_free(buf);
  return rc;
}
/* Good for: Build lc_map_chunk for grass template at chunk coords.
 * Callers: mc_wire_templates.c (same file).
 */

static lc_status map_chunk_at(int32_t x, int32_t z, lc_map_chunk *out) {
  lc_chunk c;
  lc_status st = mc_static_build_grass_chunk(&c, x, z);
  if (st != LC_OK) return st;
  st = lc_chunk_to_map_chunk(&c, out);
  lc_chunk_free(&c);
  return st;
}
/* Good for: Encode and send map_chunk play packet.
 * Callers: mc_wire_templates.c (same file).
 */

static int send_map_chunk(int fd, const lc_map_chunk *mc, size_t *wire_len_out) {
  lc_byte_buf wire;
  memset(&wire, 0, sizeof wire);
  if (lc_write_map_chunk(mc, &wire) != LC_OK) return -1;
  MC_LOGI("static_server", "  map_chunk (%d,%d) chunk_data=%zuB wire=%zuB heightmaps=%zu",
          mc->x, mc->z, mc->chunk_data.len, wire.len, mc->heightmaps.count);
  if (wire_len_out) *wire_len_out = wire.len;
  int rc = send_payload(fd, MC_PKT_PLAY_MAP_CHUNK, wire.data, wire.len);
  lc_byte_buf_free(&wire);
  return rc;
}

static const char *k_world_names[] = {"minecraft:overworld", "minecraft:the_nether", "minecraft:the_end"};
/* Good for: Fill lc_play_login struct for static join.
 * Callers: mc_wire_templates.c (same file).
 */

static lc_play_login build_play_login(const mc_patch_ctx *ctx, int32_t view_dist, int32_t sim_dist) {
  lc_play_login login;
  memset(&login, 0, sizeof login);
  if (mc_static_fill_join_login(&login, ctx->entity_id) == 0) return login;

  login.entity_id = ctx->entity_id;
  login.hardcore = 0;
  login.world_names = k_world_names;
  login.world_name_count = 3;
  login.max_players = 420;
  login.view_distance = view_dist;
  login.simulation_distance = sim_dist;
  login.reduced_debug_info = 0;
  login.enable_respawn_screen = 1;
  login.do_limited_crafting = 0;
  login.enforces_secure_chat = 0;

  lc_spawn_info *ws = &login.world_state;
  ws->dimension = 0;
  ws->name = strdup("minecraft:overworld");
  ws->hashed_seed = 0;
  ws->gamemode = (int8_t)ctx->gamemode;
  ws->previous_gamemode = 0xff;
  ws->is_debug = 0;
  ws->is_flat = 1;
  ws->has_death = 0;
  ws->portal_cooldown = 0;
  ws->sea_level = 63;
  return login;
}
/* Good for: Fill teleport position struct for join.
 * Callers: mc_wire_templates.c (same file).
 */

static lc_position build_play_position(const mc_patch_ctx *ctx) {
  lc_position pos;
  if (mc_static_fill_join_position(&pos, ctx->teleport_id) == 0) return pos;

  memset(&pos, 0, sizeof pos);
  pos.teleport_id = ctx->teleport_id;
  pos.x = ctx->spawn_x;
  pos.y = ctx->spawn_y;
  pos.z = ctx->spawn_z;
  return pos;
}
/* Good for: Free strings inside built play_login.
 * Callers: mc_wire_templates.c (same file).
 */

static void free_built_play_login(lc_play_login *login) {
  if (!login) return;
  free(login->world_state.name);
  login->world_state.name = NULL;
}
static void log_play_login_send(const lc_play_login *login, const lc_byte_buf *wire, int from_upstream_cache) {
  const lc_spawn_info *ws = &login->world_state;
  MC_LOGI("static_server", "S2C login 0x%02x payload %zu B (source=%s)", (unsigned)MC_PKT_PLAY_LOGIN, wire->len,
          from_upstream_cache ? "upstream fields (assembled)" : "built-in defaults");
  MC_LOGI("static_server", "  entity_id=%d hardcore=%u max_players=%d view_distance=%d simulation_distance=%d",
          login->entity_id, (unsigned)login->hardcore, login->max_players, login->view_distance,
          login->simulation_distance);
  MC_LOGI("static_server", "  reduced_debug_info=%u enable_respawn_screen=%u do_limited_crafting=%u "
                            "enforces_secure_chat=%u",
          (unsigned)login->reduced_debug_info, (unsigned)login->enable_respawn_screen,
          (unsigned)login->do_limited_crafting, (unsigned)login->enforces_secure_chat);
  MC_LOGI("static_server", "  world_names (%zu):", login->world_name_count);
  for (size_t i = 0; i < login->world_name_count; i++) {
    MC_LOGI("static_server", "    [%zu] %s", i, login->world_names[i] ? login->world_names[i] : "(null)");
  }
  MC_LOGI("static_server",
          "  world_state dimension=%d name=%s hashed_seed=%lld gamemode=%d previous_gamemode=0x%02x "
          "is_debug=%u is_flat=%u has_death=%u portal_cooldown=%d sea_level=%d",
          ws->dimension, ws->name ? ws->name : "(null)", (long long)ws->hashed_seed, (int)ws->gamemode,
          (unsigned)ws->previous_gamemode, (unsigned)ws->is_debug, (unsigned)ws->is_flat, (unsigned)ws->has_death,
          ws->portal_cooldown, ws->sea_level);
  if (ws->has_death) {
    MC_LOGI("static_server", "  death_dimension=%s pos=(%d,%d,%d)", ws->death_dimension_name ? ws->death_dimension_name : "",
            ws->death_pos.x, ws->death_pos.y, ws->death_pos.z);
  }

  char summary[4096];
  if (lc_decode_play_stream_to_string("login", wire->data, wire->len, summary, sizeof summary) > 0) {
    MC_LOGI("static_server", "  decode: %s", summary);
  } else {
    MC_LOGW("static_server", "  decode: libchunk parse failed");
  }

  char hex[2048];
  size_t hex_len = wire->len < 256 ? wire->len : 256;
  size_t pos = 0;
  for (size_t i = 0; i < hex_len && pos + 3 < sizeof hex; i++) {
    pos += (size_t)snprintf(hex + pos, sizeof hex - pos, "%02x", wire->data[i]);
  }
  if (wire->len > hex_len) {
    MC_LOGI("static_server", "  wire_hex (%zu/%zu B): %s...", hex_len, wire->len, hex);
  } else {
    MC_LOGI("static_server", "  wire_hex (%zu B): %s", wire->len, hex);
  }
}

/* Good for: Send play Login packet during join.
 * Callers: mc_wire_templates.c (same file).
 */

static int send_play_login(int fd, const mc_patch_ctx *ctx, int32_t view_dist, int32_t sim_dist) {
  lc_play_login login;
  memset(&login, 0, sizeof login);
  int from_cache = mc_static_fill_join_login(&login, ctx->entity_id) == 0;
  if (!from_cache) {
    login = build_play_login(ctx, view_dist, sim_dist);
  }
  lc_byte_buf wire;
  memset(&wire, 0, sizeof wire);
  if (lc_write_play_login(&login, &wire) != LC_OK) {
    free_built_play_login(&login);
    return -1;
  }
  log_play_login_send(&login, &wire, from_cache);
  free_built_play_login(&login);
  return send_byte_buf(fd, MC_PKT_PLAY_LOGIN, &wire);
}
/* Good for: Send Synchronize Player Position after join.
 * Callers: mc_wire_templates.c (same file).
 */

static int send_play_position(int fd, const mc_patch_ctx *ctx) {
  lc_position pos = build_play_position(ctx);
  lc_byte_buf wire;
  memset(&wire, 0, sizeof wire);
  if (lc_write_position(&pos, &wire) != LC_OK) return -1;
  return send_byte_buf(fd, MC_PKT_PLAY_POSITION, &wire);
}
/* Good for: Send Player Info (gamemode/skin slot) on join.
 * Callers: mc_wire_templates.c (same file).
 */

static int send_play_player_info(int fd, const mc_patch_ctx *ctx) {
  if (!ctx->uuid || !ctx->username) return -1;
  mc_buf b;
  memset(&b, 0, sizeof b);
  const uint8_t action = 0x1d;
  if (mc_buf_u8(&b, action) != LC_OK) goto fail;
  if (mc_buf_varint(&b, 1) != LC_OK) goto fail;
  if (mc_buf_uuid(&b, ctx->uuid) != LC_OK) goto fail;
  if (mc_buf_string(&b, ctx->username) != LC_OK) goto fail;
  if (mc_buf_varint(&b, 0) != LC_OK) goto fail;
  if (mc_buf_varint(&b, (int32_t)ctx->gamemode) != LC_OK) goto fail;
  if (mc_buf_u8(&b, 1) != LC_OK) goto fail;
  if (mc_buf_varint(&b, 0) != LC_OK) goto fail;
  return send_mc_buf(fd, MC_PKT_PLAY_PLAYER_INFO, &b);
fail:
  mc_buf_free(&b);
  return -1;
}
/* Good for: Send world border packet on join.
 * Callers: mc_wire_templates.c (same file).
 */

static int send_play_initialize_world_border(int fd) {
  lc_initialize_world_border wb;
  if (mc_static_fill_join_world_border(&wb) != 0) {
    wb = (lc_initialize_world_border){
      .x = 0,
      .z = 0,
      .old_diameter = 59999968.0,
      .new_diameter = 59999968.0,
      .speed = 0,
      .portal_teleport_boundary = 29999984,
      .warning_blocks = 5,
      .warning_time = 15,
    };
  }
  lc_byte_buf wire;
  memset(&wire, 0, sizeof wire);
  if (lc_write_initialize_world_border(&wb, &wire) != LC_OK) return -1;
  return send_byte_buf(fd, MC_PKT_PLAY_INITIALIZE_WORLD_BORDER, &wire);
}
/* Good for: Send Update View Position (chunk center).
 * Callers: mc_wire_templates.c (same file).
 */

static int send_play_update_view_position(int fd, int32_t chunk_x, int32_t chunk_z) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_varint(&b, chunk_x) != LC_OK) return -1;
  if (mc_buf_varint(&b, chunk_z) != LC_OK) return -1;
  return send_mc_buf(fd, MC_PKT_PLAY_UPDATE_VIEW_POSITION, &b);
}
/* Good for: Prebuilt play/configuration packet templates for static server join.
 * Callers: mc_chunk_stream.c.
 */

int mc_template_send_update_view_position(int fd, int32_t chunk_x, int32_t chunk_z) {
  return send_play_update_view_position(fd, chunk_x, chunk_z);
}
/* Good for: Prebuilt play/configuration packet templates for static server join.
 * Callers: mc_chunk_stream.c.
 */

int mc_template_send_map_chunk_at(int fd, int32_t chunk_x, int32_t chunk_z) {
  lc_map_chunk mc;
  memset(&mc, 0, sizeof mc);
  if (map_chunk_at(chunk_x, chunk_z, &mc) != LC_OK) return -1;
  int rc = send_map_chunk(fd, &mc, NULL);
  lc_map_chunk_free(&mc);
  return rc;
}
/* Good for: Prebuilt play/configuration packet templates for static server join.
 * Callers: mc_chunk_stream.c.
 */

int mc_template_send_unload_chunk_at(int fd, int32_t chunk_x, int32_t chunk_z) {
  lc_unload_chunk uc = {.x = chunk_x, .z = chunk_z};
  lc_byte_buf wire;
  memset(&wire, 0, sizeof wire);
  if (lc_write_unload_chunk(&uc, &wire) != LC_OK) return -1;
  int rc = send_payload(fd, MC_PKT_PLAY_UNLOAD_CHUNK, wire.data, wire.len);
  lc_byte_buf_free(&wire);
  return rc;
}
/* Good for: Send view distance packet.
 * Callers: mc_wire_templates.c (same file).
 */

static int send_play_update_view_distance(int fd, int32_t dist) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_varint(&b, dist) != LC_OK) return -1;
  return send_mc_buf(fd, MC_PKT_PLAY_UPDATE_VIEW_DISTANCE, &b);
}
/* Good for: Send simulation distance packet.
 * Callers: mc_wire_templates.c (same file).
 */

static int send_play_simulation_distance(int fd, int32_t dist) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_varint(&b, dist) != LC_OK) return -1;
  return send_mc_buf(fd, MC_PKT_PLAY_SIMULATION_DISTANCE, &b);
}
/* Good for: Send difficulty settings packet.
 * Callers: mc_wire_templates.c (same file).
 */

static int send_play_difficulty(int fd) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  /* 2 = normal; matches reference join (easy=1 also valid). */
  if (mc_buf_varint(&b, 2) != LC_OK) return -1;
  if (mc_buf_u8(&b, 0) != LC_OK) return -1;
  return send_mc_buf(fd, MC_PKT_PLAY_DIFFICULTY, &b);
}
/* Good for: Send Chunk Batch Start before bulk chunks.
 * Callers: mc_wire_templates.c (same file).
 */

static int send_chunk_batch_start(int fd) {
  return send_payload(fd, MC_PKT_PLAY_CHUNK_BATCH_START, NULL, 0);
}
/* Good for: Send Chunk Batch Finished with batch size.
 * Callers: mc_wire_templates.c (same file).
 */

static int send_chunk_batch_finished(int fd, int32_t batch_size) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_varint(&b, batch_size) != LC_OK) return -1;
  return send_mc_buf(fd, MC_PKT_PLAY_CHUNK_BATCH_FINISHED, &b);
}
/* Good for: Send player abilities (flying, walk speed).
 * Callers: mc_wire_templates.c (same file).
 */

static int send_play_abilities(int fd) {
  const uint8_t payload[] = {0x00, 0x3d, 0xcc, 0xcc, 0xcd, 0x3d, 0xcc, 0xcc, 0xcd};
  return send_payload(fd, MC_PKT_PLAY_ABILITIES, payload, sizeof payload);
}
/* Good for: Send world time packet.
 * Callers: mc_wire_templates.c (same file).
 */

static int send_play_update_time(int fd) {
  int64_t age = 0;
  int64_t time = 0;
  uint8_t tick = 1;
  if (mc_static_fill_join_update_time(&age, &time, &tick) != 0) {
    age = 0x125e330;
    time = 0x125ea35c7LL;
    tick = 1;
  }
  lc_byte_buf wire;
  memset(&wire, 0, sizeof wire);
  if (lc_write_update_time(age, time, tick, &wire) != LC_OK) return -1;
  return send_byte_buf(fd, MC_PKT_PLAY_UPDATE_TIME, &wire);
}
/* Good for: Send spawn position packet.
 * Callers: mc_wire_templates.c (same file).
 */

static int send_play_spawn_position(int fd) {
  char *dimension = NULL;
  lc_block_pos pos;
  float yaw = 0.0f;
  float pitch = 0.0f;
  const char *dim = "minecraft:overworld";
  memset(&pos, 0, sizeof pos);
  if (mc_static_fill_join_spawn_position(&dimension, &pos, &yaw, &pitch) == 0 && dimension) dim = dimension;

  lc_byte_buf wire;
  memset(&wire, 0, sizeof wire);
  if (lc_write_spawn_position(dim, &pos, yaw, pitch, &wire) != LC_OK) return -1;
  return send_byte_buf(fd, MC_PKT_PLAY_SPAWN_POSITION, &wire);
}
/* Good for: Send game state change (e.g. no rain).
 * Callers: mc_wire_templates.c (same file).
 */

static int send_play_game_state_change(int fd) {
  const uint8_t payload[] = {0x0d, 0x00, 0x00, 0x00, 0x00};
  return send_payload(fd, MC_PKT_PLAY_GAME_STATE_CHANGE, payload, sizeof payload);
}
/* Good for: Send ticking state for frozen join.
 * Callers: mc_wire_templates.c (same file).
 */

static int send_play_set_ticking_state(int fd) {
  const uint8_t payload[] = {0x41, 0xa0, 0x00, 0x00, 0x00};
  return send_payload(fd, MC_PKT_PLAY_SET_TICKING_STATE, payload, sizeof payload);
}
/* Good for: Send step tick packet after join.
 * Callers: mc_wire_templates.c (same file).
 */

static int send_play_step_tick(int fd) {
  const uint8_t payload[] = {0x00};
  return send_payload(fd, MC_PKT_PLAY_STEP_TICK, payload, sizeof payload);
}
/* Good for: Send health packet on join.
 * Callers: mc_wire_templates.c (same file).
 */

static int send_play_update_health(int fd) {
  const uint8_t payload[] = {0x41, 0xa0, 0x00, 0x00, 0x14, 0x40, 0x33, 0x33, 0x33};
  return send_payload(fd, MC_PKT_PLAY_UPDATE_HEALTH, payload, sizeof payload);
}
/* Good for: Send XP bar packet on join.
 * Callers: mc_wire_templates.c (same file).
 */

static int send_play_experience(int fd) {
  const uint8_t payload[] = {0x3f, 0x15, 0x58, 0x54, 0x23, 0xb2, 0x5e};
  return send_payload(fd, MC_PKT_PLAY_EXPERIENCE, payload, sizeof payload);
}
/* Good for: Send keepalive ping on join.
 * Callers: mc_wire_templates.c (same file).
 */

static int send_play_ping(int fd) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_i32_be(&b, -2) != LC_OK) return -1;
  int rc = send_mc_buf(fd, MC_PKT_PLAY_PING, &b);
  return rc;
}
/* Good for: Send view distance + simulation distance together.
 * Callers: mc_wire_templates.c (same file).
 */

static int send_view_state(int fd, const mc_server_world *w, int32_t view, int32_t sim) {
  if (send_play_update_view_distance(fd, view) != 0) return -1;
  if (send_play_simulation_distance(fd, sim) != 0) return -1;
  if (send_play_update_view_position(fd, w->spawn_chunk_x, w->spawn_chunk_z) != 0) return -1;
  return 0;
}
/* Good for: Prebuilt play/configuration packet templates for static server join.
 * Callers: mc_spectator.c, mc_static_server.c.
 */

int mc_templates_init(void) {
  store_free(&g_store);
  g_store.world.view_radius = MC_STATIC_CHUNK_RADIUS;
  g_store.world.spawn_x = MC_STATIC_SPAWN_X;
  g_store.world.spawn_y = MC_STATIC_SPAWN_Y;
  g_store.world.spawn_z = MC_STATIC_SPAWN_Z;
  g_store.world.spawn_chunk_x = MC_STATIC_SPAWN_CHUNK_X;
  g_store.world.spawn_chunk_z = MC_STATIC_SPAWN_CHUNK_Z;
  return mc_static_registries_init();
}
/* Good for: Prebuilt play/configuration packet templates for static server join.
 * Callers: mc_spectator.c, mc_static_server.c.
 */

void mc_templates_free(void) {
  store_free(&g_store);
  mc_static_registries_free();
}

const mc_server_world *mc_templates_world(void) { return &g_store.world; }
/* Good for: Prebuilt play/configuration packet templates for static server join.
 * Callers: mc_static_server.c.
 */

int mc_template_send_config_sequence(int fd, const mc_patch_ctx *ctx) {
  (void)ctx;
  return mc_static_send_config_preamble(fd);
}
/* Good for: Prebuilt play/configuration packet templates for static server join.
 * Callers: mc_static_server.c.
 */

int mc_template_send_play_join(int fd, const mc_patch_ctx *ctx) {
  const mc_server_world *w = &g_store.world;
  mc_static_wait_play_cache();

  lc_position spawn_pos;
  if (mc_static_fill_join_position(&spawn_pos, 1) == 0) {
    g_store.world.spawn_x = spawn_pos.x;
    g_store.world.spawn_y = spawn_pos.y;
    g_store.world.spawn_z = spawn_pos.z;
    g_store.world.spawn_chunk_x = (int32_t)floor(spawn_pos.x / 16.0);
    g_store.world.spawn_chunk_z = (int32_t)floor(spawn_pos.z / 16.0);
  }

  int32_t view = MC_STATIC_VIEW_DISTANCE;
  int32_t sim = MC_STATIC_SIM_DISTANCE;
  {
    lc_play_login preview;
    memset(&preview, 0, sizeof preview);
    if (mc_static_fill_join_login(&preview, ctx->entity_id) == 0) {
      view = preview.view_distance;
      sim = preview.simulation_distance;
      free(preview.world_state.name);
    }
    if (mc_static_chunks_upstream()) {
      g_store.world.view_radius = mc_static_chunk_radius_from_view(view);
    }
  }

  MC_LOGI("static_server", "play join for %s entity=%d view=%d sim=%d spawn=(%.1f,%.1f,%.1f) view_chunk=(%d,%d)",
          ctx->username ? ctx->username : "?", ctx->entity_id, view, sim, g_store.world.spawn_x, g_store.world.spawn_y,
          g_store.world.spawn_z, g_store.world.spawn_chunk_x, g_store.world.spawn_chunk_z);

  if (send_play_login(fd, ctx, view, sim) != 0) return -1;
  if (mc_static_send_cached_recipe_burst(fd) != 0) return -1;
  if (send_play_difficulty(fd) != 0) return -1;
  if (send_play_abilities(fd) != 0) return -1;
  if (send_play_position(fd, ctx) != 0) return -1;
  if (send_view_state(fd, w, view, sim) != 0) return -1;
  if (send_play_initialize_world_border(fd) != 0) return -1;
  if (send_play_update_time(fd) != 0) return -1;
  if (send_play_spawn_position(fd) != 0) return -1;
  if (send_play_game_state_change(fd) != 0) return -1;
  if (send_play_set_ticking_state(fd) != 0) return -1;
  if (send_play_step_tick(fd) != 0) return -1;
  if (send_play_player_info(fd, ctx) != 0) return -1;
  if (send_view_state(fd, w, view, sim) != 0) return -1;
  if (send_play_update_health(fd) != 0) return -1;
  if (send_play_experience(fd) != 0) return -1;
  if (send_play_ping(fd) != 0) return -1;
  MC_LOGI("static_server", "play join burst sent (login, position, view, border, time, ping, …)");
  return 0;
}
/* Good for: Prebuilt play/configuration packet templates for static server join.
 * Callers: mc_spectator.c.
 */

int mc_templates_grass_packet_wire(uint8_t **wire, size_t *wire_len) {
  if (!wire || !wire_len) return -1;
  lc_chunk c;
  if (mc_static_build_grass_chunk(&c, 0, 0) != LC_OK) return -1;
  lc_map_chunk mc;
  memset(&mc, 0, sizeof mc);
  if (lc_chunk_to_map_chunk(&c, &mc) != LC_OK) {
    lc_chunk_free(&c);
    return -1;
  }
  lc_chunk_free(&c);
  lc_byte_buf payload;
  memset(&payload, 0, sizeof payload);
  if (lc_write_map_chunk(&mc, &payload) != LC_OK) {
    lc_map_chunk_free(&mc);
    return -1;
  }
  lc_map_chunk_free(&mc);
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_varint(&b, MC_PKT_PLAY_MAP_CHUNK) != LC_OK) {
    lc_byte_buf_free(&payload);
    return -1;
  }
  if (mc_buf_write(&b, payload.data, payload.len) != LC_OK) {
    lc_byte_buf_free(&payload);
    mc_buf_free(&b);
    return -1;
  }
  lc_byte_buf_free(&payload);
  *wire = b.data;
  *wire_len = b.len;
  return 0;
}
/* Good for: Prebuilt play/configuration packet templates for static server join.
 * Callers: mc_static_server.c.
 */

int mc_template_send_grass_world(int fd, const mc_patch_ctx *ctx) {
  const mc_server_world *w = &g_store.world;
  int32_t cx = w->spawn_chunk_x ? w->spawn_chunk_x : (int32_t)floor(ctx->spawn_x / 16.0);
  int32_t cz = w->spawn_chunk_z ? w->spawn_chunk_z : (int32_t)floor(ctx->spawn_z / 16.0);
  int radius = w->view_radius;
  int side = radius * 2 + 1;
  int expect = side * side;
  int sent = 0;
  size_t wire_total = 0;

  MC_LOGI("static_server", "chunk grid center=(%d,%d) radius=%d (%dx%d=%d chunks)", cx, cz, radius, side, side,
          expect);

  if (send_chunk_batch_start(fd) != 0) return -1;

  for (int dx = -radius; dx <= radius; dx++) {
    for (int dz = -radius; dz <= radius; dz++) {
      lc_map_chunk mc;
      memset(&mc, 0, sizeof mc);
      if (map_chunk_at(cx + dx, cz + dz, &mc) != LC_OK) {
        MC_LOGE("static_server", "failed to build chunk (%d,%d)", cx + dx, cz + dz);
        return -1;
      }
      size_t wire_len = 0;
      if (send_map_chunk(fd, &mc, &wire_len) != 0) {
        MC_LOGE("static_server", "failed to send chunk (%d,%d)", mc.x, mc.z);
        lc_map_chunk_free(&mc);
        return -1;
      }
      wire_total += wire_len;
      sent++;
      lc_map_chunk_free(&mc);
    }
  }

  MC_LOGI("static_server", "chunk grid done: sent %d/%d chunks, wire=%zuKiB", sent, expect,
          (wire_total + 512) / 1024);
  if (send_chunk_batch_finished(fd, sent) != 0) return -1;
  (void)ctx;
  return 0;
}

int mc_template_send_upstream_world(int fd, const mc_patch_ctx *ctx) {
  mc_static_wait_upstream_chunks();
  const mc_server_world *w = &g_store.world;
  int32_t cx = w->spawn_chunk_x ? w->spawn_chunk_x : (int32_t)floor(ctx->spawn_x / 16.0);
  int32_t cz = w->spawn_chunk_z ? w->spawn_chunk_z : (int32_t)floor(ctx->spawn_z / 16.0);
  int32_t radius = w->view_radius;
  int filled = mc_static_chunks_count_in_grid(cx, cz, radius);
  int expect = mc_static_chunks_expected_grid_count(radius);
  int32_t cached_r = mc_static_chunks_max_cached_radius(cx, cz);
  if (cached_r < radius) {
    MC_LOGI("static_server", "upstream chunk radius %d (cached extent; login implied %d; grid %d/%d)", cached_r,
            radius, filled, expect);
    radius = cached_r;
    g_store.world.view_radius = cached_r;
  } else if (filled < expect) {
    MC_LOGW("static_server", "upstream chunk grid incomplete: %d/%d at (%d,%d) radius=%d", filled, expect, cx,
            cz, radius);
  }
  return mc_static_chunks_send_grid(fd, cx, cz, radius);
}
