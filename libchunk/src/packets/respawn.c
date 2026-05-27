#include "../internal.h"

static const char *gamemode_name(int g) {
  switch (g) {
    case 0:
      return "survival";
    case 1:
      return "creative";
    case 2:
      return "adventure";
    case 3:
      return "spectator";
    default:
      return "?";
  }
}
/* Good for: Decode Minecraft wire payload for respawn into a struct.
 * Callers: decode_wire.c.
 */

lc_status lc_parse_respawn(const uint8_t *data, size_t len, lc_respawn *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_parse_spawn_info(&b, &out->world_state) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_u8(&b, &out->copy_metadata) != LC_OK) {
    lc_spawn_info_free(&out->world_state);
    return LC_ERR_TRUNCATED;
  }
  return LC_OK;
}
/* Good for: Release heap owned by lc_respawn.
 * Callers: decode_wire.c.
 */

void lc_respawn_free(lc_respawn *p) {
  lc_spawn_info_free(&p->world_state);
  memset(p, 0, sizeof(*p));
}
/* Good for: One-line debug summary of lc_respawn (sniffer / decode tools).
 * Callers: decode_wire.c.
 */

int lc_respawn_to_string(const lc_respawn *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  const lc_spawn_info *w = &p->world_state;
  int n = lc_snprintf(buf, buflen,
                      "respawn{dimension=%d,name=%s,gamemode=%s,seed=%lld,copyMeta=%u,seaLevel=%d",
                      w->dimension, w->name ? w->name : "?", gamemode_name(w->gamemode),
                      (long long)w->hashed_seed, p->copy_metadata, w->sea_level);
  if (w->has_death)
    n = lc_appendf(buf, buflen, n, ",death=%s@(%d,%d,%d)", w->death_dimension_name ? w->death_dimension_name : "?",
                   w->death_pos.x, w->death_pos.y, w->death_pos.z);
  return lc_appendf(buf, buflen, n, "}");
}
