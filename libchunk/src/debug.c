#include "internal.h"

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

static int lc_write_meta_value(const lc_metadata_entry *e, char *buf, size_t buflen, int w) {
  switch (e->kind) {
    case LC_META_BYTE:
      return lc_appendf(buf, buflen, w, "%d", (int)e->v.i8);
    case LC_META_INT:
    case LC_META_VARINT:
      return lc_appendf(buf, buflen, w, "%d", e->v.i32);
    case LC_META_LONG:
      return lc_appendf(buf, buflen, w, "%lld", (long long)e->v.i64);
    case LC_META_FLOAT:
      return lc_appendf(buf, buflen, w, "%g", e->v.f32);
    case LC_META_DOUBLE:
      return lc_appendf(buf, buflen, w, "%g", e->v.f64);
    case LC_META_BOOL:
      return lc_appendf(buf, buflen, w, "%s", e->v.boolean ? "true" : "false");
    case LC_META_STRING:
      return lc_appendf(buf, buflen, w, "\"%s\"", e->v.string ? e->v.string : "");
    case LC_META_RAW:
      return lc_appendf(buf, buflen, w, "<raw %zu bytes>", e->v.raw.len);
    default:
      return lc_appendf(buf, buflen, w, "?");
  }
}

int lc_map_chunk_to_string(const lc_map_chunk *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  int w = lc_snprintf(buf, buflen,
                      "map_chunk{x=%d,z=%d,heightmaps=%zu,chunkData=%zu bytes,blockEntities=%zu,"
                      "skyMask=%zu,blockMask=%zu,skyLightSections=%zu,blockLightSections=%zu}",
                      p->x, p->z, p->heightmaps.count, p->chunk_data.len, p->block_entities.count,
                      p->sky_light_mask.count, p->block_light_mask.count, p->sky_light.row_count,
                      p->block_light.row_count);
  return w;
}

int lc_update_light_to_string(const lc_update_light *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen,
                     "update_light{chunk=(%d,%d),skyMask=%zu,blockMask=%zu,skySections=%zu,"
                     "blockSections=%zu (use decode_raw_dir for full dump)}",
                     p->chunk_x, p->chunk_z, p->sky_light_mask.count, p->block_light_mask.count,
                     p->sky_light.row_count, p->block_light.row_count);
}

static void lc_fprint_i64_array(FILE *f, const char *label, const lc_i64_arr *a) {
  fprintf(f, "%s_count: %zu\n", label, a->count);
  for (size_t i = 0; i < a->count; i++)
    fprintf(f, "%s[%zu]: %lld (0x%016llx)\n", label, i, (long long)a->values[i],
            (unsigned long long)a->values[i]);
}

static void lc_fprint_u8_grid(FILE *f, const char *label, const lc_u8_grid *g) {
  fprintf(f, "%s_section_count: %zu\n", label, g->row_count);
  for (size_t i = 0; i < g->row_count; i++) {
    fprintf(f, "%s[%zu]_bytes: %zu\n", label, i, g->row_lens[i]);
    if (g->row_lens[i] == 0) {
      fprintf(f, "%s[%zu]: (empty)\n", label, i);
      continue;
    }
    lc_wire_hex_fprint(f, g->rows[i], g->row_lens[i]);
  }
}

int lc_update_light_fprint(FILE *f, const lc_update_light *p) {
  if (!f || !p) return -1;
  fprintf(f, "chunk_x: %d\n", p->chunk_x);
  fprintf(f, "chunk_z: %d\n\n", p->chunk_z);

  fputs("sky_light_mask:\n", f);
  lc_fprint_i64_array(f, "  sky_light_mask", &p->sky_light_mask);
  fputc('\n', f);

  fputs("block_light_mask:\n", f);
  lc_fprint_i64_array(f, "  block_light_mask", &p->block_light_mask);
  fputc('\n', f);

  fputs("empty_sky_light_mask:\n", f);
  lc_fprint_i64_array(f, "  empty_sky_light_mask", &p->empty_sky_light_mask);
  fputc('\n', f);

  fputs("empty_block_light_mask:\n", f);
  lc_fprint_i64_array(f, "  empty_block_light_mask", &p->empty_block_light_mask);
  fputc('\n', f);

  fputs("sky_light:\n", f);
  lc_fprint_u8_grid(f, "  sky_light", &p->sky_light);
  fputc('\n', f);

  fputs("block_light:\n", f);
  lc_fprint_u8_grid(f, "  block_light", &p->block_light);
  return 0;
}

int lc_entity_equipment_fprint(FILE *f, const lc_entity_equipment *p) {
  if (!f || !p) return -1;
  fprintf(f, "entity_id: %d\n", p->entity_id);
  fprintf(f, "equipment_slots: %zu\n\n", p->equipments.count);
  for (size_t i = 0; i < p->equipments.count; i++) {
    fprintf(f, "--- slot[%zu] ---\n", i);
    if (lc_slot_fprint_equipment_entry(f, &p->equipments.items[i], "") != 0)
      fprintf(f, "(slot dump error)\n");
    fputc('\n', f);
  }
  return 0;
}

int lc_block_change_to_string(const lc_block_change *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen, "block_change{pos=(%d,%d,%d),type=%d}", p->location.x, p->location.y,
                     p->location.z, p->type);
}

int lc_unload_chunk_to_string(const lc_unload_chunk *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen, "unload_chunk{x=%d,z=%d}", p->x, p->z);
}

int lc_tile_entity_data_to_string(const lc_tile_entity_data *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen,
                     "tile_entity_data{pos=(%d,%d,%d),action=%d,nbt=%zu b}", p->location.x,
                     p->location.y, p->location.z, p->action, p->nbt.len);
}

int lc_tile_entity_data_fprint(FILE *f, const lc_tile_entity_data *p) {
  if (!f || !p) return -1;
  int32_t cx = p->location.x >> 4;
  int32_t cz = p->location.z >> 4;
  int32_t lx = p->location.x & 15;
  int32_t lz = p->location.z & 15;
  fprintf(f, "world_pos: (%d, %d, %d)\n", p->location.x, p->location.y, p->location.z);
  fprintf(f, "chunk: (%d, %d)  local: (%d, %d)\n", cx, cz, lx, lz);
  fprintf(f, "action: %d\n", p->action);
  if (!p->nbt_present || p->nbt.len == 0) {
    fputs("nbt: (absent)\n", f);
    return 0;
  }
  fprintf(f, "nbt_bytes: %zu\n", p->nbt.len);
  if (lc_nbt_fprint_wire(f, "", p->nbt.data, p->nbt.len) != 0) {
    fputs("nbt: <parse error — hex follows>\n", f);
    lc_wire_hex_fprint(f, p->nbt.data, p->nbt.len);
  }
  return 0;
}

static void lc_unpack_multi_block_record(int32_t record, int *lx, int *ly, int *lz, int32_t *state_id) {
  *lz = (record >> 4) & 0x0f;
  *lx = (record >> 8) & 0x0f;
  *ly = record & 0x0f;
  *state_id = (int32_t)((uint32_t)record >> 12);
}

int lc_multi_block_change_to_string(const lc_multi_block_change *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  const int32_t cx = p->chunk_coordinates.x;
  const int32_t cz = p->chunk_coordinates.z;
  const int32_t sec_y = p->chunk_coordinates.y;
  int w = lc_snprintf(buf, buflen,
                      "multi_block_change{chunk=(%d,%d),sectionY=%d,count=%zu,changes=[", cx, cz,
                      sec_y, p->record_count);
  for (size_t i = 0; i < p->record_count; i++) {
    int lx, ly, lz;
    int32_t state_id;
    lc_unpack_multi_block_record(p->records[i], &lx, &ly, &lz, &state_id);
    const int32_t wx = cx * 16 + lx;
    const int32_t wz = cz * 16 + lz;
    const int32_t wy = sec_y * 16 + ly;
    w = lc_appendf(buf, buflen, w, "%spos=(%d,%d,%d) state=%d", i ? "," : "", wx, wy, wz,
                   state_id);
  }
  return lc_appendf(buf, buflen, w, "]}");
}

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

int lc_entity_metadata_to_string(const lc_entity_metadata *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  int w = lc_snprintf(buf, buflen, "entity_metadata{id=%d,entries=[", p->entity_id);
  for (size_t i = 0; i < p->metadata.count; i++) {
    const lc_metadata_entry *e = &p->metadata.items[i];
    w = lc_appendf(buf, buflen, w, "%s{%u:%s=", i ? "," : "", e->key, e->type_name);
    w = lc_write_meta_value(e, buf, buflen, w);
    w = lc_appendf(buf, buflen, w, "}");
  }
  return lc_appendf(buf, buflen, w, "]}");
}

int lc_entity_equipment_to_string(const lc_entity_equipment *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen,
                     "entity_equipment{id=%d,slots=%zu (use decode_raw_dir for full dump)}",
                     p->entity_id, p->equipments.count);
}

int lc_entity_destroy_to_string(const lc_entity_destroy *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  int w = lc_snprintf(buf, buflen, "entity_destroy{ids=[");
  for (size_t i = 0; i < p->count; i++) w = lc_appendf(buf, buflen, w, "%s%d", i ? "," : "", p->entity_ids[i]);
  return lc_appendf(buf, buflen, w, "]}");
}

int lc_set_passengers_to_string(const lc_set_passengers *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  int w = lc_snprintf(buf, buflen, "set_passengers{vehicle=%d,passengers=[", p->entity_id);
  for (size_t i = 0; i < p->passenger_count; i++)
    w = lc_appendf(buf, buflen, w, "%s%d", i ? "," : "", p->passengers[i]);
  return lc_appendf(buf, buflen, w, "]}");
}

int lc_rel_entity_move_to_string(const lc_rel_entity_move *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen, "rel_entity_move{id=%d,d=(%d,%d,%d),onGround=%u}", p->entity_id,
                     (int)p->dx, (int)p->dy, (int)p->dz, p->on_ground);
}

int lc_entity_move_look_to_string(const lc_entity_move_look *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen,
                     "entity_move_look{id=%d,d=(%d,%d,%d),yaw=%d,pitch=%d,onGround=%u}", p->entity_id,
                     (int)p->dx, (int)p->dy, (int)p->dz, (int)p->yaw, (int)p->pitch, p->on_ground);
}

int lc_sync_entity_position_to_string(const lc_sync_entity_position *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen,
                     "sync_entity_position{id=%d,pos=(%.3f,%.3f,%.3f),delta=(%.3f,%.3f,%.3f),"
                     "rot=(%.2f,%.2f),onGround=%u}",
                     p->entity_id, p->x, p->y, p->z, p->dx, p->dy, p->dz, p->yaw, p->pitch,
                     p->on_ground);
}

int lc_entity_velocity_to_string(const lc_entity_velocity *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen, "entity_velocity{id=%d,vel=(%.3f,%.3f,%.3f)}", p->entity_id,
                     p->velocity.x, p->velocity.y, p->velocity.z);
}

int lc_entity_head_rotation_to_string(const lc_entity_head_rotation *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen, "entity_head_rotation{id=%d,headYaw=%d}", p->entity_id,
                     (int)p->head_yaw);
}

static const char *lc_entity_attribute_operation_name(int8_t op) {
  switch (op) {
    case 0: return "add";
    case 1: return "multiply_base";
    case 2: return "multiply_total";
    default: return "unknown";
  }
}

int lc_entity_update_attributes_to_string(const lc_entity_update_attributes *p, char *buf,
                                            size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  int w = lc_snprintf(buf, buflen, "entity_update_attributes{entityId=%d,properties=[", p->entity_id);
  for (size_t i = 0; i < p->property_count; i++) {
    const lc_entity_attribute_property *prop = &p->properties[i];
    w = lc_appendf(buf, buflen, w, "%s{key=\"%s\",value=%g,modifiers=[", i ? "," : "",
                   lc_entity_attribute_key_name(prop->key), prop->value);
    for (size_t j = 0; j < prop->modifier_count; j++) {
      const lc_entity_attribute_modifier *mod = &prop->modifiers[j];
      w = lc_appendf(buf, buflen, w, "%s{uuid=\"%s\",amount=%g,operation=%s}", j ? "," : "",
                     mod->uuid ? mod->uuid : "", mod->amount,
                     lc_entity_attribute_operation_name(mod->operation));
    }
    w = lc_appendf(buf, buflen, w, "]}");
  }
  return lc_appendf(buf, buflen, w, "]}");
}

int lc_entity_update_attributes_fprint(FILE *f, const lc_entity_update_attributes *p) {
  if (!f || !p) return -1;
  fprintf(f, "entity_id: %d\n", p->entity_id);
  fprintf(f, "properties[%zu]:\n", p->property_count);
  for (size_t i = 0; i < p->property_count; i++) {
    const lc_entity_attribute_property *prop = &p->properties[i];
    fprintf(f, "  [%zu] %s (%d) = %g\n", i, lc_entity_attribute_key_name(prop->key), prop->key,
            prop->value);
    if (prop->modifier_count == 0) {
      fputs("      modifiers: (none)\n", f);
      continue;
    }
    fprintf(f, "      modifiers[%zu]:\n", prop->modifier_count);
    for (size_t j = 0; j < prop->modifier_count; j++) {
      const lc_entity_attribute_modifier *mod = &prop->modifiers[j];
      fprintf(f, "        [%zu] uuid=%s amount=%g operation=%s (%d)\n", j,
              mod->uuid ? mod->uuid : "", mod->amount,
              lc_entity_attribute_operation_name(mod->operation), (int)mod->operation);
    }
  }
  return 0;
}

int lc_position_to_string(const lc_position *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen,
                     "position{teleportId=%d,pos=(%.3f,%.3f,%.3f),delta=(%.3f,%.3f,%.3f),"
                     "rot=(%.2f,%.2f),flags=0x%x}",
                     p->teleport_id, p->x, p->y, p->z, p->dx, p->dy, p->dz, p->yaw, p->pitch,
                     p->flags);
}

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

int lc_initialize_world_border_to_string(const lc_initialize_world_border *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen,
                     "initialize_world_border{center=(%.1f,%.1f),oldD=%.0f,newD=%.0f,speed=%d,"
                     "portalBoundary=%d,warnBlocks=%d,warnTime=%d}",
                     p->x, p->z, p->old_diameter, p->new_diameter, p->speed, p->portal_teleport_boundary,
                     p->warning_blocks, p->warning_time);
}

int lc_registry_data_to_string(const lc_registry_data *p, char *buf, size_t buflen) {
  if (!p || !buf || buflen == 0) return 0;
  return lc_snprintf(buf, buflen, "registry_data{id=%s,entries=%zu (use decode_raw_dir for full dump)}",
                     p->id ? p->id : "?", p->entries.count);
}

int lc_wire_hex_fprint(FILE *f, const uint8_t *wire, size_t len) {
  if (!f) return -1;
  for (size_t i = 0; i < len; i += 16) {
    fprintf(f, "%04zx: ", i);
    size_t row = len - i < 16 ? len - i : 16;
    for (size_t j = 0; j < row; j++) fprintf(f, "%02x ", wire[i + j]);
    for (size_t j = row; j < 16; j++) fputs("   ", f);
    fputs(" ", f);
    for (size_t j = 0; j < row; j++) {
      unsigned char c = wire[i + j];
      fputc(c >= 32 && c < 127 ? (int)c : '.', f);
    }
    fputc('\n', f);
  }
  return 0;
}

int lc_registry_data_fprint(FILE *f, const lc_registry_data *p) {
  if (!f || !p) return -1;
  fprintf(f, "registry_id: %s\n", p->id ? p->id : "?");
  fprintf(f, "entry_count: %zu\n\n", p->entries.count);
  for (size_t i = 0; i < p->entries.count; i++) {
    const lc_registry_entry *e = &p->entries.items[i];
    fprintf(f, "--- entry[%zu] ---\n", i);
    fprintf(f, "key: %s\n", e->key ? e->key : "?");
    if (e->nbt.len == 0) {
      fputs("nbt: (absent)\n\n", f);
      continue;
    }
    fprintf(f, "nbt_bytes: %zu\n", e->nbt.len);
    if (lc_nbt_fprint_wire(f, "", e->nbt.data, e->nbt.len) != 0) {
      fputs("nbt: <parse error — hex follows>\n", f);
      lc_wire_hex_fprint(f, e->nbt.data, e->nbt.len);
    }
    fputc('\n', f);
  }
  return 0;
}
