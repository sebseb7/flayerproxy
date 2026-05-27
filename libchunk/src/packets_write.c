#include "packets_write.h"

#include "internal.h"
#include "mc_wire.h"

#include <stdlib.h>
#include <string.h>
/* Good for: Steal mc_buf bytes into lc_byte_buf for packet payload.
 * Callers: packets_write.c (same file).
 */

static lc_status finish_mc_buf(mc_buf *b, lc_byte_buf *out) {
  if (!b || !out) return LC_ERR_INVALID;
  out->data = b->data;
  out->len = b->len;
  b->data = NULL;
  b->len = b->cap = 0;
  return LC_OK;
}

static lc_status write_bitfield(mc_buf *b, const lc_bitfield_spec *fields, size_t nfields,
                                const int32_t *vals) {
  int bits_left = 0;
  uint8_t cur = 0;
  for (size_t fi = 0; fi < nfields; fi++) {
    int need = fields[fi].size;
    int val = vals[fi];
    if (fields[fi].signed_f && val < 0) val += (1 << fields[fi].size);
    int written = 0;
    while (written < need) {
      if (bits_left == 0) {
        if (mc_buf_u8(b, 0) != LC_OK) return LC_ERR_OOM;
        cur = b->data[b->len - 1];
        bits_left = 8;
      }
      int take = need - written;
      if (take > bits_left) take = bits_left;
      int shift = need - written - take;
      int mask = (1 << take) - 1;
      int chunk = (val >> shift) & mask;
      cur |= (uint8_t)(chunk << (bits_left - take));
      b->data[b->len - 1] = cur;
      bits_left -= take;
      written += take;
    }
  }
  if (bits_left > 0 && b->len > 0) b->data[b->len - 1] = cur;
  return LC_OK;
}
/* Good for: Encode block position to wire.
 * Callers: packets_write.c (same file).
 */

static lc_status write_block_position(mc_buf *b, const lc_block_pos *p) {
  static const lc_bitfield_spec spec[] = {{26, 1}, {26, 1}, {12, 1}};
  int32_t vals[3] = {p->x, p->z, p->y};
  return write_bitfield(b, spec, 3, vals);
}
/* Good for: Encode optional anonymous NBT blob.
 * Callers: packets_write.c (same file).
 */

static lc_status write_nbt_anon_optional(mc_buf *b, const lc_byte_buf *nbt) {
  if (!nbt || !nbt->len) return mc_buf_u8(b, 0);
  if (nbt->data[0] == 0x0a) return mc_buf_write(b, nbt->data, nbt->len);
  if (mc_buf_u8(b, 1) != LC_OK) return LC_ERR_OOM;
  return mc_buf_write(b, nbt->data, nbt->len);
}
/* Good for: Encode heightmaps for outbound map_chunk.
 * Callers: packets_write.c (same file).
 */

static lc_status write_heightmaps(mc_buf *b, const lc_heightmap_arr *hm) {
  if (mc_buf_varint(b, (int32_t)hm->count) != LC_OK) return LC_ERR_OOM;
  for (size_t i = 0; i < hm->count; i++) {
    if (mc_buf_varint(b, hm->items[i].type_id) != LC_OK) return LC_ERR_OOM;
    if (mc_buf_varint(b, (int32_t)hm->items[i].data.count) != LC_OK) return LC_ERR_OOM;
    for (size_t j = 0; j < hm->items[i].data.count; j++) {
      if (mc_buf_i64_be(b, hm->items[i].data.values[j]) != LC_OK) return LC_ERR_OOM;
    }
  }
  return LC_OK;
}

/** Java BitSet wire form: varint long count (0 if empty) + big-endian longs, trimmed. */
/* Good for: Encode light mask long array.
 * Callers: packets_write.c (same file).
 */
static lc_status write_bitset_mask(mc_buf *b, const lc_i64_arr *a) {
  size_t count = a ? a->count : 0;
  while (count > 0 && a->values[count - 1] == 0) count--;
  if (mc_buf_varint(b, (int32_t)count) != LC_OK) return LC_ERR_OOM;
  for (size_t i = 0; i < count; i++) {
    if (mc_buf_i64_be(b, a->values[i]) != LC_OK) return LC_ERR_OOM;
  }
  return LC_OK;
}
/* Good for: Encode light data grid.
 * Callers: packets_write.c (same file).
 */

static lc_status write_u8_grid(mc_buf *b, const lc_u8_grid *g) {
  if (mc_buf_varint(b, (int32_t)g->row_count) != LC_OK) return LC_ERR_OOM;
  for (size_t i = 0; i < g->row_count; i++) {
    if (mc_buf_varint(b, (int32_t)g->row_lens[i]) != LC_OK) return LC_ERR_OOM;
    if (g->row_lens[i] && mc_buf_write(b, g->rows[i], g->row_lens[i]) != LC_OK) return LC_ERR_OOM;
  }
  return LC_OK;
}
/* Good for: Encode block entities for map_chunk.
 * Callers: packets_write.c (same file).
 */

static lc_status write_block_entities_map_chunk(mc_buf *b, const lc_block_entity_arr *be) {
  if (mc_buf_varint(b, (int32_t)be->count) != LC_OK) return LC_ERR_OOM;
  for (size_t i = 0; i < be->count; i++) {
    const lc_block_entity *e = &be->items[i];
    static const lc_bitfield_spec xz[] = {{4, 0}, {4, 0}};
    int32_t xz_vals[2] = {e->x, e->z};
    if (write_bitfield(b, xz, 2, xz_vals) != LC_OK) return LC_ERR_OOM;
    if (mc_buf_i16_be(b, e->y) != LC_OK) return LC_ERR_OOM;
    if (mc_buf_varint(b, e->type) != LC_OK) return LC_ERR_OOM;
    if (write_nbt_anon_optional(b, &e->nbt) != LC_OK) return LC_ERR_OOM;
  }
  return LC_OK;
}
/* Good for: Encode position packet payload bytes for outbound wire (static server / templates).
 * Callers: mc_wire_templates.c.
 */

lc_status lc_write_position(const lc_position *p, lc_byte_buf *out) {
  if (!p || !out) return LC_ERR_INVALID;
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_varint(&b, p->teleport_id) != LC_OK) goto fail;
  if (mc_buf_f64_be(&b, p->x) != LC_OK) goto fail;
  if (mc_buf_f64_be(&b, p->y) != LC_OK) goto fail;
  if (mc_buf_f64_be(&b, p->z) != LC_OK) goto fail;
  if (mc_buf_f64_be(&b, p->dx) != LC_OK) goto fail;
  if (mc_buf_f64_be(&b, p->dy) != LC_OK) goto fail;
  if (mc_buf_f64_be(&b, p->dz) != LC_OK) goto fail;
  if (mc_buf_f32_be(&b, p->yaw) != LC_OK) goto fail;
  if (mc_buf_f32_be(&b, p->pitch) != LC_OK) goto fail;
  {
    uint32_t flags = p->flags;
    uint8_t x[4] = {(uint8_t)(flags >> 24), (uint8_t)(flags >> 16), (uint8_t)(flags >> 8),
                    (uint8_t)flags};
    if (mc_buf_write(&b, x, 4) != LC_OK) goto fail;
  }
  return finish_mc_buf(&b, out);
fail:
  mc_buf_free(&b);
  return LC_ERR_OOM;
}
/* Good for: Encode spawn info packet payload bytes for outbound wire (static server / templates).
 * Callers: packets_write.c (same file).
 */

lc_status lc_write_spawn_info(const lc_spawn_info *p, lc_byte_buf *out) {
  if (!p || !out) return LC_ERR_INVALID;
  mc_buf b;
  memset(&b, 0, sizeof b);
  /* CommonPlayerSpawnInfo: Holder<DimensionType> then ResourceKey<Level>. */
  if (mc_buf_varint(&b, p->dimension > 0 ? p->dimension : 1) != LC_OK) goto fail;
  if (mc_buf_string(&b, p->name ? p->name : "minecraft:overworld") != LC_OK) goto fail;
  if (mc_buf_i64_be(&b, p->hashed_seed) != LC_OK) goto fail;
  if (mc_buf_u8(&b, (uint8_t)p->gamemode) != LC_OK) goto fail;
  if (mc_buf_u8(&b, p->previous_gamemode) != LC_OK) goto fail;
  if (mc_buf_u8(&b, p->is_debug) != LC_OK) goto fail;
  if (mc_buf_u8(&b, p->is_flat) != LC_OK) goto fail;
  if (mc_buf_u8(&b, p->has_death) != LC_OK) goto fail;
  if (p->has_death) {
    if (mc_buf_string(&b, p->death_dimension_name ? p->death_dimension_name : "") != LC_OK) goto fail;
    if (write_block_position(&b, &p->death_pos) != LC_OK) goto fail;
  }
  if (mc_buf_varint(&b, p->portal_cooldown) != LC_OK) goto fail;
  if (mc_buf_varint(&b, p->sea_level) != LC_OK) goto fail;
  return finish_mc_buf(&b, out);
fail:
  mc_buf_free(&b);
  return LC_ERR_OOM;
}
/* Good for: Encode play login packet payload bytes for outbound wire (static server / templates).
 * Callers: mc_wire_templates.c.
 */

lc_status lc_write_play_login(const lc_play_login *p, lc_byte_buf *out) {
  if (!p || !out) return LC_ERR_INVALID;
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_i32_be(&b, p->entity_id) != LC_OK) goto fail;
  if (mc_buf_u8(&b, p->hardcore) != LC_OK) goto fail;
  if (mc_buf_varint(&b, (int32_t)p->world_name_count) != LC_OK) goto fail;
  for (size_t i = 0; i < p->world_name_count; i++) {
    if (mc_buf_string(&b, p->world_names[i]) != LC_OK) goto fail;
  }
  if (mc_buf_varint(&b, p->max_players) != LC_OK) goto fail;
  if (mc_buf_varint(&b, p->view_distance) != LC_OK) goto fail;
  if (mc_buf_varint(&b, p->simulation_distance) != LC_OK) goto fail;
  if (mc_buf_u8(&b, p->reduced_debug_info) != LC_OK) goto fail;
  if (mc_buf_u8(&b, p->enable_respawn_screen) != LC_OK) goto fail;
  if (mc_buf_u8(&b, p->do_limited_crafting) != LC_OK) goto fail;
  {
    lc_byte_buf ws;
    memset(&ws, 0, sizeof ws);
    if (lc_write_spawn_info(&p->world_state, &ws) != LC_OK) goto fail;
    if (mc_buf_write(&b, ws.data, ws.len) != LC_OK) {
      lc_byte_buf_free(&ws);
      goto fail;
    }
    lc_byte_buf_free(&ws);
  }
  if (mc_buf_u8(&b, p->enforces_secure_chat) != LC_OK) goto fail;
  return finish_mc_buf(&b, out);
fail:
  mc_buf_free(&b);
  return LC_ERR_OOM;
}
/* Good for: Encode registry data packet payload bytes for outbound wire (static server / templates).
 * Callers: libchunk.h (public API, no .c callers in tree).
 */

lc_status lc_write_registry_data(const lc_registry_data *p, lc_byte_buf *out) {
  if (!p || !out) return LC_ERR_INVALID;
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_string(&b, p->id ? p->id : "") != LC_OK) goto fail;
  if (mc_buf_varint(&b, (int32_t)p->entries.count) != LC_OK) goto fail;
  for (size_t i = 0; i < p->entries.count; i++) {
    const lc_registry_entry *e = &p->entries.items[i];
    if (mc_buf_string(&b, e->key ? e->key : "") != LC_OK) goto fail;
    if (write_nbt_anon_optional(&b, &e->nbt) != LC_OK) goto fail;
  }
  return finish_mc_buf(&b, out);
fail:
  mc_buf_free(&b);
  return LC_ERR_OOM;
}
/* Good for: Encode update tags packet payload bytes for outbound wire (static server / templates).
 * Callers: libchunk.h (public API, no .c callers in tree).
 */

lc_status lc_write_update_tags(const lc_update_tags *p, lc_byte_buf *out) {
  if (!p || !out) return LC_ERR_INVALID;
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_varint(&b, (int32_t)p->group_count) != LC_OK) goto fail;
  for (size_t gi = 0; gi < p->group_count; gi++) {
    const lc_tag_group *g = &p->groups[gi];
    if (mc_buf_string(&b, g->registry_id ? g->registry_id : "") != LC_OK) goto fail;
    if (mc_buf_varint(&b, (int32_t)g->tag_count) != LC_OK) goto fail;
    for (size_t ti = 0; ti < g->tag_count; ti++) {
      const lc_tag_group_entry *t = &g->tags[ti];
      if (mc_buf_string(&b, t->name ? t->name : "") != LC_OK) goto fail;
      if (mc_buf_varint(&b, (int32_t)t->id_count) != LC_OK) goto fail;
      for (size_t ii = 0; ii < t->id_count; ii++) {
        if (mc_buf_varint(&b, t->ids[ii]) != LC_OK) goto fail;
      }
    }
  }
  return finish_mc_buf(&b, out);
fail:
  mc_buf_free(&b);
  return LC_ERR_OOM;
}
/* Good for: Encode initialize world border packet payload bytes for outbound wire (static server / templates).
 * Callers: mc_wire_templates.c.
 */

lc_status lc_write_initialize_world_border(const lc_initialize_world_border *p, lc_byte_buf *out) {
  if (!p || !out) return LC_ERR_INVALID;
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_f64_be(&b, p->x) != LC_OK) goto fail;
  if (mc_buf_f64_be(&b, p->z) != LC_OK) goto fail;
  if (mc_buf_f64_be(&b, p->old_diameter) != LC_OK) goto fail;
  if (mc_buf_f64_be(&b, p->new_diameter) != LC_OK) goto fail;
  if (mc_buf_varint(&b, p->speed) != LC_OK) goto fail;
  if (mc_buf_varint(&b, p->portal_teleport_boundary) != LC_OK) goto fail;
  if (mc_buf_varint(&b, p->warning_blocks) != LC_OK) goto fail;
  if (mc_buf_varint(&b, p->warning_time) != LC_OK) goto fail;
  return finish_mc_buf(&b, out);
fail:
  mc_buf_free(&b);
  return LC_ERR_OOM;
}
/* Good for: Encode unload chunk packet payload bytes for outbound wire (static server / templates).
 * Callers: mc_wire_templates.c.
 */

lc_status lc_write_unload_chunk(const lc_unload_chunk *p, lc_byte_buf *out) {
  if (!p || !out) return LC_ERR_INVALID;
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_i32_be(&b, p->z) != LC_OK) goto fail;
  if (mc_buf_i32_be(&b, p->x) != LC_OK) goto fail;
  return finish_mc_buf(&b, out);
fail:
  mc_buf_free(&b);
  return LC_ERR_OOM;
}
/* Good for: Encode map chunk packet payload bytes for outbound wire (static server / templates).
 * Callers: mc_wire_templates.c.
 */

lc_status lc_write_map_chunk(const lc_map_chunk *mc, lc_byte_buf *out) {
  if (!mc || !out) return LC_ERR_INVALID;
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_i32_be(&b, mc->x) != LC_OK) goto fail;
  if (mc_buf_i32_be(&b, mc->z) != LC_OK) goto fail;
  if (write_heightmaps(&b, &mc->heightmaps) != LC_OK) goto fail;
  if (mc_buf_varint(&b, (int32_t)mc->chunk_data.len) != LC_OK) goto fail;
  if (mc->chunk_data.len && mc_buf_write(&b, mc->chunk_data.data, mc->chunk_data.len) != LC_OK) goto fail;
  if (write_block_entities_map_chunk(&b, &mc->block_entities) != LC_OK) goto fail;
  if (write_bitset_mask(&b, &mc->sky_light_mask) != LC_OK) goto fail;
  if (write_bitset_mask(&b, &mc->block_light_mask) != LC_OK) goto fail;
  if (write_bitset_mask(&b, &mc->empty_sky_light_mask) != LC_OK) goto fail;
  if (write_bitset_mask(&b, &mc->empty_block_light_mask) != LC_OK) goto fail;
  if (write_u8_grid(&b, &mc->sky_light) != LC_OK) goto fail;
  if (write_u8_grid(&b, &mc->block_light) != LC_OK) goto fail;
  return finish_mc_buf(&b, out);
fail:
  mc_buf_free(&b);
  return LC_ERR_OOM;
}
/* Good for: Release heap owned by lc_play login.
 * Callers: libchunk.h (public API, no .c callers in tree).
 */

void lc_play_login_free(lc_play_login *p) {
  if (!p) return;
  free((void *)p->world_names);
  lc_spawn_info_free(&p->world_state);
  memset(p, 0, sizeof *p);
}
