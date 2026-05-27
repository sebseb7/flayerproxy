#include "../internal.h"
/* Good for: Decode Minecraft wire payload for spawn info into a struct.
 * Callers: packets.c, play_stream.c, respawn.c.
 */

lc_status lc_parse_spawn_info(lc_buf *b, lc_spawn_info *out) {
  memset(out, 0, sizeof(*out));
  if (lc_buf_read_varint(b, &out->dimension) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_string(b, &out->name) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i64_le(b, &out->hashed_seed) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i8(b, &out->gamemode) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_u8(b, &out->previous_gamemode) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_bool(b, &out->is_debug) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_bool(b, &out->is_flat) != LC_OK) return LC_ERR_TRUNCATED;
  {
    uint8_t present;
    if (lc_buf_read_u8(b, &present) != LC_OK) return LC_ERR_TRUNCATED;
    out->has_death = present;
    if (present) {
      if (lc_buf_read_string(b, &out->death_dimension_name) != LC_OK) return LC_ERR_TRUNCATED;
      if (lc_buf_read_position(b, &out->death_pos) != LC_OK) return LC_ERR_TRUNCATED;
    }
  }
  if (lc_buf_read_varint(b, &out->portal_cooldown) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_varint(b, &out->sea_level) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: Release heap owned by lc_spawn info.
 * Callers: packets.c, packets_write.c, play_stream.c, respawn.c.
 */

void lc_spawn_info_free(lc_spawn_info *s) {
  free(s->name);
  free(s->death_dimension_name);
  s->name = NULL;
  s->death_dimension_name = NULL;
}
