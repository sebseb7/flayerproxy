#include "../internal.h"
/* Good for: One-line debug summary of lc_uuid (sniffer / decode tools).
 * Callers: debug.c, play_stream.c, spawn_entity.c (same file).
 */

int lc_uuid_to_string(const lc_uuid *u, char *buf, size_t buflen) {
  if (!u || !buf || buflen < 37) return 0;
  return snprintf(buf, buflen,
                  "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                  (unsigned)u->bytes[0], (unsigned)u->bytes[1], (unsigned)u->bytes[2],
                  (unsigned)u->bytes[3], (unsigned)u->bytes[4], (unsigned)u->bytes[5],
                  (unsigned)u->bytes[6], (unsigned)u->bytes[7], (unsigned)u->bytes[8],
                  (unsigned)u->bytes[9], (unsigned)u->bytes[10], (unsigned)u->bytes[11],
                  (unsigned)u->bytes[12], (unsigned)u->bytes[13], (unsigned)u->bytes[14],
                  (unsigned)u->bytes[15]);
}
/* Good for: Decode Minecraft wire payload for spawn entity into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c.
 */

lc_status lc_parse_spawn_entity(const uint8_t *data, size_t len, lc_spawn_entity *out) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->entity_id) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_uuid(&b, &out->object_uuid) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_varint(&b, &out->type) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->x) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->y) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f64_le(&b, &out->z) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_lpvec3(&b, &out->velocity) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i8(&b, &out->pitch) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i8(&b, &out->yaw) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i8(&b, &out->head_pitch) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_varint(&b, &out->object_data) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: One-line debug summary of lc_spawn entity (sniffer / decode tools).
 * Callers: decode_wire.c.
 */

int lc_spawn_entity_to_string(const lc_spawn_entity *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  char uuid[40];
  lc_uuid_to_string(&p->object_uuid, uuid, sizeof(uuid));
  return lc_snprintf(buf, buflen,
                     "spawn_entity{id=%d,uuid=%s,type=%d,pos=(%.3f,%.3f,%.3f),vel=(%.3f,%.3f,%.3f),"
                     "rot=(%d,%d,%d),data=%d}",
                     p->entity_id, uuid, p->type, p->x, p->y, p->z, p->velocity.x, p->velocity.y,
                     p->velocity.z, (int)p->pitch, (int)p->yaw, (int)p->head_pitch, p->object_data);
}
