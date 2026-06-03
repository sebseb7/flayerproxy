#include "mc_c2s_log.h"

#include "internal.h"
#include "libchunk.h"
#include "mc_log.h"
#include "mc_packet_ids.h"

#include <stdio.h>
#include <string.h>

static const char *c2s_play_name(int32_t pkt_id) {
  static const char *names[] = {
      "teleport_confirm",           /* 0x00 */
      "block_entity_tag_query",     /* 0x01 */
      "bundle_item_selected",       /* 0x02 */
      "change_difficulty",          /* 0x03 */
      "change_game_mode",           /* 0x04 */
      "chat_ack",                   /* 0x05 */
      "chat_command",               /* 0x06 */
      "chat_command_signed",        /* 0x07 */
      "chat",                       /* 0x08 */
      "chat_session_update",        /* 0x09 */
      "chunk_batch_received",       /* 0x0a */
      "client_command",             /* 0x0b */
      "client_tick_end",            /* 0x0c */
      "client_information",         /* 0x0d */
      "command_suggestion",         /* 0x0e */
      "configuration_acknowledged", /* 0x0f */
      "container_button_click",     /* 0x10 */
      "container_click",            /* 0x11 */
      "container_close",            /* 0x12 */
      "container_slot_state_changed", /* 0x13 */
      "cookie_response",            /* 0x14 */
      "custom_payload",             /* 0x15 */
      "debug_subscription_request", /* 0x16 */
      "edit_book",                  /* 0x17 */
      "entity_tag_query",           /* 0x18 */
      "interact",                   /* 0x19 */
      "jigsaw_generate",            /* 0x1a */
      "keep_alive",                 /* 0x1b */
      "lock_difficulty",            /* 0x1c */
      "move_player_pos",            /* 0x1d */
      "move_player_pos_rot",        /* 0x1e */
      "move_player_rot",            /* 0x1f */
      "move_player_status_only",    /* 0x20 */
      "move_vehicle",               /* 0x21 */
      "paddle_boat",                /* 0x22 */
      "pick_item_from_block",       /* 0x23 */
      "pick_item_from_entity",      /* 0x24 */
      "ping_request",               /* 0x25 */
      "place_recipe",               /* 0x26 */
      "player_abilities",           /* 0x27 */
      "player_action",              /* 0x28 */
      "player_command",             /* 0x29 */
      "player_input",               /* 0x2a */
      "player_loaded",              /* 0x2b */
      "pong",                       /* 0x2c */
      "recipe_book_change_settings", /* 0x2d */
      "recipe_book_seen_recipe",    /* 0x2e */
      "rename_item",                /* 0x2f */
      "resource_pack",              /* 0x30 */
      "seen_advancements",          /* 0x31 */
      "select_trade",               /* 0x32 */
      "set_beacon",                 /* 0x33 */
      "set_carried_item",           /* 0x34 */
      "set_command_block",          /* 0x35 */
      "set_command_minecart",       /* 0x36 */
      "set_creative_mode_slot",     /* 0x37 */
      "set_jigsaw_block",           /* 0x38 */
      "set_structure_block",        /* 0x39 */
      "set_test_block",             /* 0x3a */
      "sign_update",                /* 0x3b */
      "swing",                      /* 0x3c */
      "teleport_to_entity",         /* 0x3d */
      "test_instance_block_action", /* 0x3e */
      "use_item_on",                /* 0x3f */
      "use_item",                   /* 0x40 */
      "custom_click_action",        /* 0x41 */
  };
  if (pkt_id < 0 || (size_t)pkt_id >= sizeof names / sizeof names[0]) return NULL;
  return names[pkt_id];
}

static const char *player_action_name(int32_t action) {
  static const char *names[] = {
      "START_DESTROY_BLOCK", "ABORT_DESTROY_BLOCK", "STOP_DESTROY_BLOCK",
      "DROP_ALL_ITEMS",      "DROP_ITEM",           "RELEASE_USE_ITEM",
      "SWAP_ITEM_WITH_OFFHAND",
  };
  if (action < 0 || (size_t)action >= sizeof names / sizeof names[0]) return "?";
  return names[action];
}

static const char *direction_name(uint8_t v) {
  static const char *names[] = {"down", "up", "north", "south", "west", "east"};
  if (v >= sizeof names / sizeof names[0]) return "?";
  return names[v];
}

static const char *hand_name(int32_t hand) {
  return hand == 0 ? "MAIN_HAND" : hand == 1 ? "OFF_HAND" : "?";
}
/* Good for: Unpack packed block position from long.
 * Callers: mc_c2s_log.c (same file).
 */

static void block_pos_from_long(int64_t packed, lc_block_pos *out) {
  out->x = (int32_t)(packed >> 38);
  out->y = (int32_t)((packed << 52) >> 52);
  out->z = (int32_t)((packed << 26) >> 38);
}
/* Good for: Read packed block position from buffer.
 * Callers: mc_c2s_log.c (same file).
 */

static lc_status read_block_pos_long(lc_buf *b, lc_block_pos *out) {
  int64_t packed;
  if (lc_buf_read_i64_be(b, &packed) != LC_OK) return LC_ERR_TRUNCATED;
  block_pos_from_long(packed, out);
  return LC_OK;
}
/* Good for: Format first bytes of payload as hex for logs.
 * Callers: mc_c2s_log.c (same file).
 */

static void log_hex_preview(char *dst, size_t dst_len, const uint8_t *data, size_t len) {
  size_t n = len < 48 ? len : 48;
  size_t off = 0;
  for (size_t i = 0; i < n && off + 3 < dst_len; i++) {
    off += (size_t)lc_snprintf(dst + off, dst_len - off, "%02x%s", data[i], i + 1 < n ? " " : "");
  }
  if (len > n && off + 4 < dst_len) lc_snprintf(dst + off, dst_len - off, "...");
}
/* Good for: Log unconsumed payload bytes after partial parse.
 * Callers: mc_c2s_log.c (same file).
 */

static void log_remaining_hex(const char *who, int32_t pkt_id, const char *name, lc_buf *b) {
  size_t rem = lc_buf_remaining(b);
  if (rem == 0) return;
  char hex[160];
  log_hex_preview(hex, sizeof hex, b->data + b->off, rem);
  MC_LOGI("c2s", "%s C2S 0x%02x %s +%zuB hex=%s", who, pkt_id, name ? name : "?", rem, hex);
}
/* Good for: Log client player input packets.
 * Callers: mc_c2s_log.c (same file).
 */

static void log_player_input(const char *who, int32_t pkt_id, const char *name, lc_buf *b) {
  uint8_t raw;
  if (lc_buf_read_u8(b, &raw) != LC_OK) {
    log_remaining_hex(who, pkt_id, name, b);
    return;
  }
  MC_LOGI("c2s", "%s C2S 0x%02x %s fwd=%d back=%d left=%d right=%d jump=%d sneak=%d sprint=%d raw=0x%02x",
          who, pkt_id, name, (raw & 1) != 0, (raw & 2) != 0, (raw & 4) != 0, (raw & 8) != 0,
          (raw & 16) != 0, (raw & 32) != 0, (raw & 64) != 0, raw);
  log_remaining_hex(who, pkt_id, name, b);
}
/* Good for: Log plugin message / custom payload.
 * Callers: mc_c2s_log.c (same file).
 */

static void log_custom_payload(const char *who, int32_t pkt_id, const char *name, lc_buf *b) {
  char *channel = NULL;
  if (lc_buf_read_string(b, &channel) != LC_OK) {
    log_remaining_hex(who, pkt_id, name, b);
    return;
  }
  lc_byte_buf data = {0};
  if (lc_buf_read_byte_array(b, &data) != LC_OK) {
    free(channel);
    log_remaining_hex(who, pkt_id, name, b);
    return;
  }
  if (channel && strcmp(channel, "minecraft:brand") == 0 && data.len > 0) {
    char brand[128];
    size_t n = data.len < sizeof brand - 1 ? data.len : sizeof brand - 1;
    memcpy(brand, data.data, n);
    brand[n] = '\0';
    MC_LOGI("c2s", "%s C2S 0x%02x %s channel=%s brand=\"%s\" (%zuB)", who, pkt_id, name, channel,
            brand, data.len);
  } else {
    char hex[160];
    log_hex_preview(hex, sizeof hex, data.data, data.len);
    MC_LOGI("c2s", "%s C2S 0x%02x %s channel=%s data=%zuB hex=%s", who, pkt_id, name,
            channel ? channel : "?", data.len, hex);
  }
  free(channel);
  lc_byte_buf_free(&data);
  log_remaining_hex(who, pkt_id, name, b);
}

static void log_chat_string(const char *who, int32_t pkt_id, const char *name, const char *field,
                            lc_buf *b) {
  char *text = NULL;
  if (lc_buf_read_string(b, &text) != LC_OK) {
    log_remaining_hex(who, pkt_id, name, b);
    return;
  }
  MC_LOGI("c2s", "%s C2S 0x%02x %s %s=\"%s\"", who, pkt_id, name, field, text ? text : "");
  free(text);
  log_remaining_hex(who, pkt_id, name, b);
}
/* Good for: Log player action (dig, swap hands, etc.).
 * Callers: mc_c2s_log.c (same file).
 */

static void log_player_action(const char *who, int32_t pkt_id, const char *name, lc_buf *b) {
  int32_t action;
  lc_block_pos pos;
  uint8_t face;
  int32_t seq;
  if (lc_buf_read_varint(b, &action) != LC_OK || read_block_pos_long(b, &pos) != LC_OK ||
/* Good for: Read u8 from packet cursor lc_buf (all parsers).
 * Callers: buf.c, c2s_move.c, chunk.c, mc_c2s_log.c (same file), metadata.c, nbt.c, packets.c, play_stream.c, respawn.c, slot.c, spawn_info.c.
 */
      lc_buf_read_u8(b, &face) != LC_OK || lc_buf_read_varint(b, &seq) != LC_OK) {
    log_remaining_hex(who, pkt_id, name, b);
    return;
  }
  MC_LOGI("c2s", "%s C2S 0x%02x %s action=%s pos=(%d,%d,%d) face=%s seq=%d", who, pkt_id, name,
          player_action_name(action), pos.x, pos.y, pos.z, direction_name(face), seq);
  log_remaining_hex(who, pkt_id, name, b);
}

static lc_status decode_block_hit(lc_buf *b, lc_block_pos *pos, int32_t *face, float *hx, float *hy,
                                  float *hz, uint8_t *inside, uint8_t *world_border) {
  if (read_block_pos_long(b, pos) != LC_OK || lc_buf_read_varint(b, face) != LC_OK ||
      lc_buf_read_f32_le(b, hx) != LC_OK || lc_buf_read_f32_le(b, hy) != LC_OK ||
      lc_buf_read_f32_le(b, hz) != LC_OK || lc_buf_read_u8(b, inside) != LC_OK ||
/* Good for: Read u8 from packet cursor lc_buf (all parsers).
 * Callers: buf.c, c2s_move.c, chunk.c, mc_c2s_log.c (same file), metadata.c, nbt.c, packets.c, play_stream.c, respawn.c, slot.c, spawn_info.c.
 */
      lc_buf_read_u8(b, world_border) != LC_OK) {
    return LC_ERR_TRUNCATED;
  }
  return LC_OK;
}
/* Good for: Log Use Item On block interaction.
 * Callers: mc_c2s_log.c (same file).
 */

static void log_use_item_on(const char *who, int32_t pkt_id, const char *name, lc_buf *b) {
  int32_t hand, face, seq;
  lc_block_pos pos;
  float hx, hy, hz;
  uint8_t inside, world_border;
  if (lc_buf_read_varint(b, &hand) != LC_OK || decode_block_hit(b, &pos, &face, &hx, &hy, &hz, &inside,
                                                                 &world_border) != LC_OK ||
/* Good for: Read varint from packet cursor lc_buf (all parsers).
 * Callers: block_change.c, buf.c, c2s_move.c, chunk.c, entity_destroy.c, entity_equipment.c, entity_head_rotation.c, entity_metadata.c, entity_move_look.c, entity_velocity.c, initialize_world_border.c, map_chunk.c, mc_c2s_log.c (same file), mc_server_common.c, mc_spectator.c, mc_static_server.c, metadata.c, multi_block_change.c, packets.c, play_stream.c, position.c, registry_data.c, rel_entity_move.c, set_passengers.c, slot.c, slot_fprint.c, spawn_entity.c, spawn_info.c, sync_entity_position.c, update_light.c, update_tags.c.
 */
      lc_buf_read_varint(b, &seq) != LC_OK) {
    log_remaining_hex(who, pkt_id, name, b);
    return;
  }
  MC_LOGI("c2s",
          "%s C2S 0x%02x %s hand=%s seq=%d block=(%d,%d,%d) face=%s hit=(%.3f,%.3f,%.3f) inside=%d",
          who, pkt_id, name, hand_name(hand), seq, pos.x, pos.y, pos.z, direction_name((uint8_t)face),
          (double)hx, (double)hy, (double)hz, inside ? 1 : 0);
  log_remaining_hex(who, pkt_id, name, b);
}

static void log_varint_field(const char *who, int32_t pkt_id, const char *name, const char *field,
                             lc_buf *b) {
  int32_t v;
  if (lc_buf_read_varint(b, &v) != LC_OK) {
    log_remaining_hex(who, pkt_id, name, b);
    return;
  }
  MC_LOGI("c2s", "%s C2S 0x%02x %s %s=%d", who, pkt_id, name, field, v);
  log_remaining_hex(who, pkt_id, name, b);
}

static void log_i64_field(const char *who, int32_t pkt_id, const char *name, const char *field,
                          lc_buf *b) {
  int64_t v;
  if (lc_buf_read_i64_be(b, &v) != LC_OK) {
    log_remaining_hex(who, pkt_id, name, b);
    return;
  }
  MC_LOGI("c2s", "%s C2S 0x%02x %s %s=%lld", who, pkt_id, name, field, (long long)v);
  log_remaining_hex(who, pkt_id, name, b);
}

static void log_move_packet(const char *who, int32_t pkt_id, const char *name, const uint8_t *payload,
                            size_t payload_len) {
  char detail[256];
  detail[0] = '\0';
  if (pkt_id == MC_PKT_C2S_POSITION) {
    lc_c2s_position p;
    if (lc_parse_c2s_position(payload, payload_len, &p) == LC_OK) {
      lc_c2s_position_to_string(&p, detail, sizeof detail);
    }
  } else if (pkt_id == MC_PKT_C2S_POSITION_LOOK) {
    lc_c2s_position_look p;
    if (lc_parse_c2s_position_look(payload, payload_len, &p) == LC_OK) {
      lc_c2s_position_look_to_string(&p, detail, sizeof detail);
    }
  } else if (pkt_id == MC_PKT_C2S_MOVE_ROT) {
    lc_c2s_look p;
    if (lc_parse_c2s_look(payload, payload_len, &p) == LC_OK) {
      lc_c2s_look_to_string(&p, detail, sizeof detail);
    }
  } else if (pkt_id == MC_PKT_C2S_MOVE_STATUS) {
    lc_c2s_flying p;
    if (lc_parse_c2s_flying(payload, payload_len, &p) == LC_OK) {
      lc_c2s_flying_to_string(&p, detail, sizeof detail);
    }
  }
  if (detail[0]) {
    MC_LOGI("c2s", "%s C2S 0x%02x %s %s", who, pkt_id, name, detail);
  } else {
    char hex[160];
    log_hex_preview(hex, sizeof hex, payload, payload_len);
    MC_LOGI("c2s", "%s C2S 0x%02x %s len=%zu hex=%s", who, pkt_id, name, payload_len, hex);
  }
}
/* Good for: Colored stderr logging for mc_* tools.
 * Callers: mc_static_server.c.
 */

void mc_log_c2s_play(const char *username, int32_t pkt_id, const uint8_t *payload, size_t payload_len) {
  if (pkt_id == MC_PKT_C2S_TICK_END) return;

  const char *who = (username && username[0]) ? username : "?";
  const char *name = c2s_play_name(pkt_id);
  if (!name) name = "?";

  if (payload_len == 0) {
    MC_LOGD("c2s", "%s C2S 0x%02x %s (empty)", who, pkt_id, name);
    return;
  }

  lc_buf b;
  lc_buf_init(&b, payload, payload_len);

  switch (pkt_id) {
    case MC_PKT_C2S_PLAYER_LOADED:
      MC_LOGI("c2s", "%s C2S 0x%02x %s (empty)", who, pkt_id, name);
      return;

    case MC_PKT_C2S_TELEPORT_CONFIRM: {
      lc_c2s_teleport_confirm tc;
      if (lc_parse_c2s_teleport_confirm(payload, payload_len, &tc) == LC_OK) {
        char detail[64];
        lc_c2s_teleport_confirm_to_string(&tc, detail, sizeof detail);
        MC_LOGI("c2s", "%s C2S 0x%02x %s %s", who, pkt_id, name, detail);
      } else {
        log_remaining_hex(who, pkt_id, name, &b);
      }
      return;
    }

    case MC_PKT_C2S_KEEP_ALIVE:
      log_i64_field(who, pkt_id, name, "id", &b);
      return;

    case MC_PKT_C2S_PONG:
      log_varint_field(who, pkt_id, name, "id", &b);
      return;

    case MC_PKT_C2S_POSITION:
    case MC_PKT_C2S_POSITION_LOOK:
    case MC_PKT_C2S_MOVE_ROT:
    case MC_PKT_C2S_MOVE_STATUS:
      log_move_packet(who, pkt_id, name, payload, payload_len);
      return;

    case MC_PKT_C2S_CHAT_COMMAND:
      log_chat_string(who, pkt_id, name, "command", &b);
      return;

    case MC_PKT_C2S_CHAT:
      log_chat_string(who, pkt_id, name, "message", &b);
      return;

    case MC_PKT_C2S_CUSTOM_PAYLOAD:
      log_custom_payload(who, pkt_id, name, &b);
      return;

    case MC_PKT_C2S_PLAYER_ACTION:
      log_player_action(who, pkt_id, name, &b);
      return;

    case MC_PKT_C2S_PLAYER_INPUT:
      log_player_input(who, pkt_id, name, &b);
      return;

    case MC_PKT_C2S_SWING: {
      int32_t hand;
      if (lc_buf_read_varint(&b, &hand) == LC_OK) {
        MC_LOGI("c2s", "%s C2S 0x%02x %s hand=%s", who, pkt_id, name, hand_name(hand));
      } else {
        log_remaining_hex(who, pkt_id, name, &b);
      }
      return;
    }

    case MC_PKT_C2S_USE_ITEM_ON:
      log_use_item_on(who, pkt_id, name, &b);
      return;

    case MC_PKT_C2S_USE_ITEM: {
      int32_t hand, seq;
      float yaw, pitch;
      if (lc_buf_read_varint(&b, &hand) == LC_OK && lc_buf_read_varint(&b, &seq) == LC_OK &&
/* Good for: Read f32_le from packet cursor lc_buf (all parsers).
 * Callers: c2s_move.c, mc_c2s_log.c (same file), metadata.c, packets.c, play_stream.c, position.c, slot.c, slot_fprint.c, sync_entity_position.c.
 */
          lc_buf_read_f32_le(&b, &yaw) == LC_OK && lc_buf_read_f32_le(&b, &pitch) == LC_OK) {
        MC_LOGI("c2s", "%s C2S 0x%02x %s hand=%s seq=%d yaw=%.2f pitch=%.2f", who, pkt_id, name,
                hand_name(hand), seq, (double)yaw, (double)pitch);
        log_remaining_hex(who, pkt_id, name, &b);
      } else {
        log_remaining_hex(who, pkt_id, name, &b);
      }
      return;
    }

    case MC_PKT_C2S_CHUNK_BATCH_RECEIVED:
      log_varint_field(who, pkt_id, name, "batchSize", &b);
      return;

    case MC_PKT_C2S_SET_CARRIED_ITEM:
      log_varint_field(who, pkt_id, name, "slot", &b);
      return;

    default: {
      char hex[160];
      log_hex_preview(hex, sizeof hex, payload, payload_len);
      MC_LOGI("c2s", "%s C2S 0x%02x %s len=%zu hex=%s", who, pkt_id, name, payload_len, hex);
      return;
    }
  }
}
