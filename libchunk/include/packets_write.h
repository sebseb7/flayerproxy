#ifndef LIBCHUNK_PACKETS_WRITE_H
#define LIBCHUNK_PACKETS_WRITE_H

#include "libchunk.h"

/** Play login payload (S2C login). */
typedef struct lc_play_login {
  int32_t entity_id;
  uint8_t hardcore;
  const char **world_names;
  size_t world_name_count;
  int32_t max_players;
  int32_t view_distance;
  int32_t simulation_distance;
  uint8_t reduced_debug_info;
  uint8_t enable_respawn_screen;
  uint8_t do_limited_crafting;
  lc_spawn_info world_state;
  uint8_t enforces_secure_chat;
} lc_play_login;

/** Serialize packet payload (no leading packet-id varint) into out. Caller owns out->data. */
lc_status lc_write_position(const lc_position *p, lc_byte_buf *out);
lc_status lc_write_play_login(const lc_play_login *p, lc_byte_buf *out);
lc_status lc_write_spawn_info(const lc_spawn_info *p, lc_byte_buf *out);
lc_status lc_write_map_chunk(const lc_map_chunk *mc, lc_byte_buf *out);
lc_status lc_write_unload_chunk(const lc_unload_chunk *p, lc_byte_buf *out);
lc_status lc_write_registry_data(const lc_registry_data *p, lc_byte_buf *out);
lc_status lc_write_update_tags(const lc_update_tags *p, lc_byte_buf *out);
lc_status lc_write_initialize_world_border(const lc_initialize_world_border *p, lc_byte_buf *out);

void lc_play_login_free(lc_play_login *p);

#endif /* LIBCHUNK_PACKETS_WRITE_H */
