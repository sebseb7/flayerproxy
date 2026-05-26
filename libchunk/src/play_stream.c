#include "play_stream.h"

#include "internal.h"

#include <string.h>

static int summary_only(const char *name, size_t payload_len, char *out, size_t out_sz) {
  return lc_snprintf(out, out_sz, "%s{payload=%zu bytes (structure not fully decoded)}", name,
                     payload_len);
}

static lc_status skip_string_array(lc_buf *b) {
  int32_t n;
  if (lc_buf_read_varint(b, &n) != LC_OK) return LC_ERR_TRUNCATED;
  if (n < 0) return LC_ERR_INVALID;
  for (int32_t i = 0; i < n; i++) {
    char *s = NULL;
    if (lc_buf_read_string(b, &s) != LC_OK) return LC_ERR_TRUNCATED;
    free(s);
  }
  return LC_OK;
}

static lc_status parse_spawn_position(const uint8_t *data, size_t len, char *dim, size_t dim_sz,
                                        lc_block_pos *loc, float *yaw, float *pitch) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  char *dn = NULL;
  if (lc_buf_read_string(&b, &dn) != LC_OK) return LC_ERR_TRUNCATED;
  snprintf(dim, dim_sz, "%s", dn ? dn : "");
  free(dn);
  if (lc_buf_read_position(&b, loc) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f32_le(&b, yaw) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f32_le(&b, pitch) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}

static int decode_login(const uint8_t *payload, size_t payload_len, char *out, size_t out_sz) {
  lc_buf b;
  lc_buf_init(&b, payload, payload_len);
  int32_t entity_id;
  uint8_t hardcore;
  if (lc_buf_read_i32_le(&b, &entity_id) != LC_OK) return -1;
  if (lc_buf_read_bool(&b, &hardcore) != LC_OK) return -1;
  if (skip_string_array(&b) != LC_OK) return -1;
  int32_t max_players, view_dist, sim_dist;
  uint8_t reduced, respawn_screen, limited;
  if (lc_buf_read_varint(&b, &max_players) != LC_OK) return -1;
  if (lc_buf_read_varint(&b, &view_dist) != LC_OK) return -1;
  if (lc_buf_read_varint(&b, &sim_dist) != LC_OK) return -1;
  if (lc_buf_read_bool(&b, &reduced) != LC_OK) return -1;
  if (lc_buf_read_bool(&b, &respawn_screen) != LC_OK) return -1;
  if (lc_buf_read_bool(&b, &limited) != LC_OK) return -1;
  lc_spawn_info ws;
  memset(&ws, 0, sizeof ws);
  if (lc_parse_spawn_info(&b, &ws) != LC_OK) {
    lc_spawn_info_free(&ws);
    return -1;
  }
  uint8_t secure;
  if (lc_buf_read_bool(&b, &secure) != LC_OK) {
    lc_spawn_info_free(&ws);
    return -1;
  }
  int w = lc_snprintf(out, out_sz,
                      "login{entityId=%d,hardcore=%s,maxPlayers=%d,viewDistance=%d,"
                      "simulationDistance=%d,dimension=%d,name=%s,gamemode=%d,seaLevel=%d,"
                      "enforcesSecureChat=%s}",
                      entity_id, hardcore ? "true" : "false", max_players, view_dist, sim_dist,
                      ws.dimension, ws.name ? ws.name : "", ws.gamemode, ws.sea_level,
                      secure ? "true" : "false");
  lc_spawn_info_free(&ws);
  return w > 0 && (size_t)w < out_sz ? 1 : -1;
}

static int decode_window_items(const uint8_t *payload, size_t payload_len, char *out,
                               size_t out_sz) {
  lc_buf b;
  lc_buf_init(&b, payload, payload_len);
  uint8_t window_id;
  int32_t state_id, n;
  if (lc_buf_read_u8(&b, &window_id) != LC_OK) return -1;
  if (lc_buf_read_varint(&b, &state_id) != LC_OK) return -1;
  if (lc_buf_read_varint(&b, &n) != LC_OK) return -1;
  if (n < 0) return -1;
  int32_t non_empty = 0;
  for (int32_t i = 0; i < n; i++) {
    lc_equipment slot;
    memset(&slot, 0, sizeof slot);
    if (lc_slot_read(&b, &slot) != LC_OK) return -1;
    if (slot.item_count > 0) non_empty++;
    lc_byte_buf_free(&slot.item_extra);
  }
  lc_equipment carried;
  memset(&carried, 0, sizeof carried);
  if (lc_slot_read(&b, &carried) != LC_OK) return -1;
  lc_byte_buf_free(&carried.item_extra);
  return lc_snprintf(out, out_sz,
                     "window_items{windowId=%u,stateId=%d,slots=%d,nonEmpty=%d,carriedCount=%d}",
                     (unsigned)window_id, state_id, n, non_empty, carried.item_count) > 0
             ? 1
             : -1;
}

static int decode_set_slot(const uint8_t *payload, size_t payload_len, char *out, size_t out_sz) {
  lc_buf b;
  lc_buf_init(&b, payload, payload_len);
  uint8_t window_id;
  int32_t state_id;
  int16_t slot;
  lc_equipment item;
  memset(&item, 0, sizeof item);
  if (lc_buf_read_u8(&b, &window_id) != LC_OK) return -1;
  if (lc_buf_read_varint(&b, &state_id) != LC_OK) return -1;
  if (lc_buf_read_i16_le(&b, &slot) != LC_OK) return -1;
  if (lc_slot_read(&b, &item) != LC_OK) return -1;
  int w = lc_snprintf(out, out_sz,
                      "set_slot{windowId=%u,stateId=%d,slot=%d,itemId=%d,count=%d}", (unsigned)window_id,
                      state_id, (int)slot, item.item_id, item.item_count);
  lc_byte_buf_free(&item.item_extra);
  return w > 0 && (size_t)w < out_sz ? 1 : -1;
}

static int decode_set_player_inventory(const uint8_t *payload, size_t payload_len, char *out,
                                       size_t out_sz) {
  lc_buf b;
  lc_buf_init(&b, payload, payload_len);
  int32_t slot_id;
  lc_equipment item;
  memset(&item, 0, sizeof item);
  if (lc_buf_read_varint(&b, &slot_id) != LC_OK) return -1;
  if (lc_slot_read(&b, &item) != LC_OK) return -1;
  int w = lc_snprintf(out, out_sz, "set_player_inventory{slotId=%d,itemId=%d,count=%d}", slot_id,
                      item.item_id, item.item_count);
  lc_byte_buf_free(&item.item_extra);
  return w > 0 && (size_t)w < out_sz ? 1 : -1;
}

static int decode_set_cursor_item(const uint8_t *payload, size_t payload_len, char *out,
                                  size_t out_sz) {
  lc_buf b;
  lc_buf_init(&b, payload, payload_len);
  lc_equipment item;
  memset(&item, 0, sizeof item);
  if (lc_slot_read(&b, &item) != LC_OK) return -1;
  int w = lc_snprintf(out, out_sz, "set_cursor_item{itemId=%d,count=%d}", item.item_id,
                      item.item_count);
  lc_byte_buf_free(&item.item_extra);
  return w > 0 && (size_t)w < out_sz ? 1 : -1;
}

static int decode_entity_teleport(const uint8_t *payload, size_t payload_len, char *out,
                                  size_t out_sz) {
  lc_buf b;
  lc_buf_init(&b, payload, payload_len);
  int32_t entity_id;
  double x, y, z;
  int8_t yaw, pitch;
  uint8_t on_ground;
  if (lc_buf_read_varint(&b, &entity_id) != LC_OK) return -1;
  if (lc_buf_read_f64_le(&b, &x) != LC_OK) return -1;
  if (lc_buf_read_f64_le(&b, &y) != LC_OK) return -1;
  if (lc_buf_read_f64_le(&b, &z) != LC_OK) return -1;
  if (lc_buf_read_i8(&b, &yaw) != LC_OK) return -1;
  if (lc_buf_read_i8(&b, &pitch) != LC_OK) return -1;
  if (lc_buf_read_bool(&b, &on_ground) != LC_OK) return -1;
  return lc_snprintf(out, out_sz,
                     "entity_teleport{entityId=%d,pos=(%.3f,%.3f,%.3f),yaw=%d,pitch=%d,onGround=%s}",
                     entity_id, x, y, z, (int)yaw, (int)pitch, on_ground ? "true" : "false") > 0
             ? 1
             : -1;
}

static int decode_entity_effect(const uint8_t *payload, size_t payload_len, char *out,
                                size_t out_sz) {
  lc_buf b;
  lc_buf_init(&b, payload, payload_len);
  int32_t entity_id, effect_id, amplifier, duration;
  uint8_t flags;
  if (lc_buf_read_varint(&b, &entity_id) != LC_OK) return -1;
  if (lc_buf_read_varint(&b, &effect_id) != LC_OK) return -1;
  if (lc_buf_read_varint(&b, &amplifier) != LC_OK) return -1;
  if (lc_buf_read_varint(&b, &duration) != LC_OK) return -1;
  if (lc_buf_read_u8(&b, &flags) != LC_OK) return -1;
  return lc_snprintf(out, out_sz,
                     "entity_effect{entityId=%d,effectId=%d,amplifier=%d,duration=%d,flags=0x%02x}",
                     entity_id, effect_id, amplifier, duration, (unsigned)flags) > 0
             ? 1
             : -1;
}

static int decode_remove_entity_effect(const uint8_t *payload, size_t payload_len, char *out,
                                       size_t out_sz) {
  lc_buf b;
  lc_buf_init(&b, payload, payload_len);
  int32_t entity_id, effect_id;
  if (lc_buf_read_varint(&b, &entity_id) != LC_OK) return -1;
  if (lc_buf_read_varint(&b, &effect_id) != LC_OK) return -1;
  return lc_snprintf(out, out_sz, "remove_entity_effect{entityId=%d,effectId=%d}", entity_id,
                     effect_id) > 0
             ? 1
             : -1;
}

static int decode_entity_look(const uint8_t *payload, size_t payload_len, char *out, size_t out_sz) {
  lc_buf b;
  lc_buf_init(&b, payload, payload_len);
  int32_t entity_id;
  int8_t yaw, pitch;
  uint8_t on_ground;
  if (lc_buf_read_varint(&b, &entity_id) != LC_OK) return -1;
  if (lc_buf_read_i8(&b, &yaw) != LC_OK) return -1;
  if (lc_buf_read_i8(&b, &pitch) != LC_OK) return -1;
  if (lc_buf_read_bool(&b, &on_ground) != LC_OK) return -1;
  return lc_snprintf(out, out_sz, "entity_look{entityId=%d,yaw=%d,pitch=%d,onGround=%s}", entity_id,
                     (int)yaw, (int)pitch, on_ground ? "true" : "false") > 0
             ? 1
             : -1;
}

static int decode_player_info(const uint8_t *payload, size_t payload_len, char *out, size_t out_sz) {
  lc_buf b;
  lc_buf_init(&b, payload, payload_len);
  uint8_t action;
  int32_t n;
  if (lc_buf_read_u8(&b, &action) != LC_OK) return -1;
  if (lc_buf_read_varint(&b, &n) != LC_OK) return -1;
  return lc_snprintf(out, out_sz, "player_info{action=0x%02x,entries=%d,payloadRemaining=%zu}",
                     (unsigned)action, n, lc_buf_remaining(&b)) > 0
             ? 1
             : -1;
}

static int decode_tags(const uint8_t *payload, size_t payload_len, char *out, size_t out_sz) {
  lc_buf b;
  lc_buf_init(&b, payload, payload_len);
  int32_t groups;
  if (lc_buf_read_varint(&b, &groups) != LC_OK) return -1;
  return lc_snprintf(out, out_sz, "tags{groups=%d,payloadRemaining=%zu}", groups,
                     lc_buf_remaining(&b)) > 0
             ? 1
             : -1;
}

static const char *PLAY_STREAM_NAMES[] = {
    "login",
    "update_health",
    "experience",
    "abilities",
    "entity_status",
    "spawn_position",
    "difficulty",
    "game_state_change",
    "window_items",
    "set_slot",
    "held_item_slot",
    "set_player_inventory",
    "set_cursor_item",
    "update_time",
    "chunk_batch_start",
    "chunk_batch_finished",
    "world_border_center",
    "world_border_size",
    "world_border_lerp_size",
    "world_border_warning_delay",
    "world_border_warning_reach",
    "simulation_distance",
    "update_view_distance",
    "update_view_position",
    "declare_commands",
    "player_info",
    "player_remove",
    "playerlist_header",
    "scoreboard_objective",
    "scoreboard_display_objective",
    "scoreboard_score",
    "reset_score",
    "teams",
    "boss_bar",
    "tracked_waypoint",
    "tags",
    "server_data",
    "update_recipes",
    "declare_recipes",
    "advancements",
    "recipe_book_add",
    "recipe_book_settings",
    "entity_look",
    "entity_teleport",
    "entity_effect",
    "remove_entity_effect",
    NULL,
};

int lc_play_stream_packet_supported(const char *name) {
  if (!name) return 0;
  for (size_t i = 0; PLAY_STREAM_NAMES[i]; i++) {
    if (strcmp(name, PLAY_STREAM_NAMES[i]) == 0) return 1;
  }
  return 0;
}

int lc_decode_play_stream_to_string(const char *name, const uint8_t *payload, size_t payload_len,
                                    char *out, size_t out_sz) {
  if (!name || !out || out_sz == 0) return -1;

  if (strcmp(name, "login") == 0) return decode_login(payload, payload_len, out, out_sz);
  if (strcmp(name, "window_items") == 0)
    return decode_window_items(payload, payload_len, out, out_sz);
  if (strcmp(name, "set_slot") == 0) return decode_set_slot(payload, payload_len, out, out_sz);
  if (strcmp(name, "set_player_inventory") == 0)
    return decode_set_player_inventory(payload, payload_len, out, out_sz);
  if (strcmp(name, "set_cursor_item") == 0)
    return decode_set_cursor_item(payload, payload_len, out, out_sz);
  if (strcmp(name, "entity_teleport") == 0)
    return decode_entity_teleport(payload, payload_len, out, out_sz);
  if (strcmp(name, "entity_effect") == 0)
    return decode_entity_effect(payload, payload_len, out, out_sz);
  if (strcmp(name, "remove_entity_effect") == 0)
    return decode_remove_entity_effect(payload, payload_len, out, out_sz);
  if (strcmp(name, "entity_look") == 0)
    return decode_entity_look(payload, payload_len, out, out_sz);
  if (strcmp(name, "player_info") == 0)
    return decode_player_info(payload, payload_len, out, out_sz);
  if (strcmp(name, "tags") == 0) return decode_tags(payload, payload_len, out, out_sz);

  if (strcmp(name, "update_health") == 0) {
    lc_buf b;
    lc_buf_init(&b, payload, payload_len);
    float health, sat;
    int32_t food;
    if (lc_buf_read_f32_le(&b, &health) != LC_OK) return -1;
    if (lc_buf_read_varint(&b, &food) != LC_OK) return -1;
    if (lc_buf_read_f32_le(&b, &sat) != LC_OK) return -1;
    return lc_snprintf(out, out_sz, "update_health{health=%.1f,food=%d,saturation=%.2f}", health, food,
                      sat) > 0
               ? 1
               : -1;
  }
  if (strcmp(name, "experience") == 0) {
    lc_buf b;
    lc_buf_init(&b, payload, payload_len);
    float bar;
    int32_t level, total;
    if (lc_buf_read_f32_le(&b, &bar) != LC_OK) return -1;
    if (lc_buf_read_varint(&b, &level) != LC_OK) return -1;
    if (lc_buf_read_varint(&b, &total) != LC_OK) return -1;
    return lc_snprintf(out, out_sz, "experience{bar=%.3f,level=%d,totalXp=%d}", bar, level, total) > 0
               ? 1
               : -1;
  }
  if (strcmp(name, "abilities") == 0) {
    lc_buf b;
    lc_buf_init(&b, payload, payload_len);
    int8_t flags;
    float fly, walk;
    if (lc_buf_read_i8(&b, &flags) != LC_OK) return -1;
    if (lc_buf_read_f32_le(&b, &fly) != LC_OK) return -1;
    if (lc_buf_read_f32_le(&b, &walk) != LC_OK) return -1;
    return lc_snprintf(out, out_sz, "abilities{flags=0x%02x,flyingSpeed=%.3f,walkingSpeed=%.3f}",
                      (unsigned)(uint8_t)flags, fly, walk) > 0
               ? 1
               : -1;
  }
  if (strcmp(name, "entity_status") == 0) {
    lc_buf b;
    lc_buf_init(&b, payload, payload_len);
    int32_t entity_id;
    int8_t status;
    if (lc_buf_read_i32_le(&b, &entity_id) != LC_OK) return -1;
    if (lc_buf_read_i8(&b, &status) != LC_OK) return -1;
    return lc_snprintf(out, out_sz, "entity_status{entityId=%d,status=%d}", entity_id, (int)status) > 0
               ? 1
               : -1;
  }
  if (strcmp(name, "spawn_position") == 0) {
    char dim[128];
    lc_block_pos loc;
    float yaw, pitch;
    if (parse_spawn_position(payload, payload_len, dim, sizeof dim, &loc, &yaw, &pitch) != LC_OK)
      return -1;
    return lc_snprintf(out, out_sz,
                       "spawn_position{dimension=%s,pos=(%d,%d,%d),yaw=%.2f,pitch=%.2f}", dim, loc.x,
                       loc.y, loc.z, yaw, pitch) > 0
               ? 1
               : -1;
  }
  if (strcmp(name, "difficulty") == 0) {
    lc_buf b;
    lc_buf_init(&b, payload, payload_len);
    int32_t diff;
    uint8_t locked;
    if (lc_buf_read_varint(&b, &diff) != LC_OK) return -1;
    if (lc_buf_read_bool(&b, &locked) != LC_OK) return -1;
    return lc_snprintf(out, out_sz, "difficulty{level=%d,locked=%s}", diff, locked ? "true" : "false") >
                   0
               ? 1
               : -1;
  }
  if (strcmp(name, "game_state_change") == 0) {
    lc_buf b;
    lc_buf_init(&b, payload, payload_len);
    uint8_t reason;
    float mode;
    if (lc_buf_read_u8(&b, &reason) != LC_OK) return -1;
    if (lc_buf_read_f32_le(&b, &mode) != LC_OK) return -1;
    return lc_snprintf(out, out_sz, "game_state_change{reason=%u,value=%.3f}", (unsigned)reason,
                      mode) > 0
               ? 1
               : -1;
  }
  if (strcmp(name, "held_item_slot") == 0) {
    lc_buf b;
    lc_buf_init(&b, payload, payload_len);
    int32_t slot;
    if (lc_buf_read_varint(&b, &slot) != LC_OK) return -1;
    return lc_snprintf(out, out_sz, "held_item_slot{slot=%d}", slot) > 0 ? 1 : -1;
  }
  if (strcmp(name, "update_time") == 0) {
    lc_buf b;
    lc_buf_init(&b, payload, payload_len);
    int64_t age, time;
    uint8_t tick;
    if (lc_buf_read_i64_le(&b, &age) != LC_OK) return -1;
    if (lc_buf_read_i64_le(&b, &time) != LC_OK) return -1;
    if (lc_buf_read_bool(&b, &tick) != LC_OK) return -1;
    return lc_snprintf(out, out_sz, "update_time{age=%lld,time=%lld,tickDayTime=%s}",
                      (long long)age, (long long)time, tick ? "true" : "false") > 0
               ? 1
               : -1;
  }
  if (strcmp(name, "chunk_batch_start") == 0) {
    return lc_snprintf(out, out_sz, "chunk_batch_start{}") > 0 ? 1 : -1;
  }
  if (strcmp(name, "chunk_batch_finished") == 0) {
    lc_buf b;
    lc_buf_init(&b, payload, payload_len);
    int32_t batch;
    if (lc_buf_read_varint(&b, &batch) != LC_OK) return -1;
    return lc_snprintf(out, out_sz, "chunk_batch_finished{batchSize=%d}", batch) > 0 ? 1 : -1;
  }
  if (strcmp(name, "world_border_center") == 0) {
    lc_buf b;
    lc_buf_init(&b, payload, payload_len);
    double x, z;
    if (lc_buf_read_f64_le(&b, &x) != LC_OK) return -1;
    if (lc_buf_read_f64_le(&b, &z) != LC_OK) return -1;
    return lc_snprintf(out, out_sz, "world_border_center{x=%.1f,z=%.1f}", x, z) > 0 ? 1 : -1;
  }
  if (strcmp(name, "world_border_size") == 0) {
    lc_buf b;
    lc_buf_init(&b, payload, payload_len);
    double d;
    if (lc_buf_read_f64_le(&b, &d) != LC_OK) return -1;
    return lc_snprintf(out, out_sz, "world_border_size{diameter=%.0f}", d) > 0 ? 1 : -1;
  }
  if (strcmp(name, "world_border_lerp_size") == 0) {
    lc_buf b;
    lc_buf_init(&b, payload, payload_len);
    double old_d, new_d;
    int64_t time;
    if (lc_buf_read_f64_le(&b, &old_d) != LC_OK) return -1;
    if (lc_buf_read_f64_le(&b, &new_d) != LC_OK) return -1;
    if (lc_buf_read_varlong(&b, &time) != LC_OK) return -1;
    return lc_snprintf(out, out_sz, "world_border_lerp_size{old=%.0f,new=%.0f,timeMs=%lld}", old_d,
                      new_d, (long long)time) > 0
               ? 1
               : -1;
  }
  if (strcmp(name, "world_border_warning_delay") == 0 ||
      strcmp(name, "world_border_warning_reach") == 0) {
    lc_buf b;
    lc_buf_init(&b, payload, payload_len);
    int32_t v;
    if (lc_buf_read_varint(&b, &v) != LC_OK) return -1;
    return lc_snprintf(out, out_sz, "%s{%s=%d}", name,
                      strcmp(name, "world_border_warning_delay") == 0 ? "warningTime" : "warningBlocks",
                      v) > 0
               ? 1
               : -1;
  }
  if (strcmp(name, "simulation_distance") == 0 || strcmp(name, "update_view_distance") == 0) {
    lc_buf b;
    lc_buf_init(&b, payload, payload_len);
    int32_t d;
    if (lc_buf_read_varint(&b, &d) != LC_OK) return -1;
    return lc_snprintf(out, out_sz, "%s{distance=%d}", name, d) > 0 ? 1 : -1;
  }
  if (strcmp(name, "update_view_position") == 0) {
    lc_buf b;
    lc_buf_init(&b, payload, payload_len);
    int32_t cx, cz;
    if (lc_buf_read_varint(&b, &cx) != LC_OK) return -1;
    if (lc_buf_read_varint(&b, &cz) != LC_OK) return -1;
    return lc_snprintf(out, out_sz, "update_view_position{chunkX=%d,chunkZ=%d}", cx, cz) > 0 ? 1 : -1;
  }
  if (strcmp(name, "scoreboard_display_objective") == 0) {
    lc_buf b;
    lc_buf_init(&b, payload, payload_len);
    int32_t pos;
    char *obj = NULL;
    if (lc_buf_read_varint(&b, &pos) != LC_OK) return -1;
    if (lc_buf_read_string(&b, &obj) != LC_OK) return -1;
    int w = lc_snprintf(out, out_sz, "scoreboard_display_objective{position=%d,name=%s}", pos,
                        obj ? obj : "");
    free(obj);
    return w > 0 && (size_t)w < out_sz ? 1 : -1;
  }
  if (strcmp(name, "reset_score") == 0) {
    lc_buf b;
    lc_buf_init(&b, payload, payload_len);
    char *entity = NULL;
    if (lc_buf_read_string(&b, &entity) != LC_OK) return -1;
    uint8_t has_obj;
    char *obj = NULL;
    if (lc_buf_read_bool(&b, &has_obj) != LC_OK) {
      free(entity);
      return -1;
    }
    if (has_obj && lc_buf_read_string(&b, &obj) != LC_OK) {
      free(entity);
      return -1;
    }
    int w = lc_snprintf(out, out_sz, "reset_score{entity=%s,objective=%s}", entity ? entity : "",
                        obj ? obj : has_obj ? "?" : "(all)");
    free(entity);
    free(obj);
    return w > 0 && (size_t)w < out_sz ? 1 : -1;
  }

  if (strcmp(name, "initialize_world_border") == 0) {
    lc_initialize_world_border wb;
    if (lc_parse_initialize_world_border(payload, payload_len, &wb) != LC_OK) return -1;
    return lc_initialize_world_border_to_string(&wb, out, out_sz) > 0 ? 1 : -1;
  }

  return summary_only(name, payload_len, out, out_sz) > 0 ? 1 : -1;
}
