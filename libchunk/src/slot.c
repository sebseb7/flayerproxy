#include "internal.h"

static lc_status lc_skip_registry_entry_holder_set(lc_buf *b);
static lc_status lc_skip_slot_component_data(lc_buf *b, int32_t comp_type);
/* Good for: Minecraft 1.21+ item slot / equipment component wire skipping or parsing.
 * Callers: metadata.c, slot.c (same file).
 */

static lc_status lc_skip_option_string(lc_buf *b) {
  uint8_t present;
  if (lc_buf_read_bool(b, &present) != LC_OK) return LC_ERR_TRUNCATED;
  if (!present) return LC_OK;
  char *s = NULL;
  lc_status st = lc_buf_read_string(b, &s);
  free(s);
  return st;
}
/* Good for: Minecraft 1.21+ item slot / equipment component wire skipping or parsing.
 * Callers: slot.c (same file).
 */

static lc_status lc_skip_nbt_array(lc_buf *b) {
  int32_t n;
  if (lc_buf_read_varint(b, &n) != LC_OK) return LC_ERR_TRUNCATED;
  if (n < 0) return LC_ERR_INVALID;
  for (int32_t i = 0; i < n; i++)
    if (lc_nbt_skip_anonymous(b) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: Minecraft 1.21+ item slot / equipment component wire skipping or parsing.
 * Callers: slot.c (same file).
 */

static lc_status lc_skip_registry_entry_holder(lc_buf *b) {
  int32_t n;
  if (lc_buf_read_varint(b, &n) != LC_OK) return LC_ERR_TRUNCATED;
  if (n == 0) {
    char *s = NULL;
    lc_status st = lc_buf_read_string(b, &s);
    free(s);
    if (st != LC_OK) return st;
    uint8_t has;
    if (lc_buf_read_bool(b, &has) != LC_OK) return LC_ERR_TRUNCATED;
    if (!has) return LC_OK;
    float f;
    return lc_buf_read_f32_le(b, &f);
  }
  return lc_buf_read_varint(b, &n);
}
/* Good for: Minecraft 1.21+ item slot / equipment component wire skipping or parsing.
 * Callers: slot.c (same file).
 */

static lc_status lc_skip_option_registry_entry_holder(lc_buf *b) {
  uint8_t present;
  if (lc_buf_read_bool(b, &present) != LC_OK) return LC_ERR_TRUNCATED;
  if (!present) return LC_OK;
  return lc_skip_registry_entry_holder(b);
}
/* Good for: Minecraft 1.21+ item slot / equipment component wire skipping or parsing.
 * Callers: slot.c (same file).
 */

static lc_status lc_skip_option_registry_entry_holder_set(lc_buf *b) {
  uint8_t present;
  if (lc_buf_read_bool(b, &present) != LC_OK) return LC_ERR_TRUNCATED;
  if (!present) return LC_OK;
  return lc_skip_registry_entry_holder_set(b);
}
/* Good for: Minecraft 1.21+ item slot / equipment component wire skipping or parsing.
 * Callers: slot.c (same file).
 */

static lc_status lc_skip_blocks_attacks(lc_buf *b) {
  float f;
  if (lc_buf_read_f32_le(b, &f) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f32_le(b, &f) != LC_OK) return LC_ERR_TRUNCATED;
  int32_t n;
  if (lc_buf_read_varint(b, &n) != LC_OK) return LC_ERR_TRUNCATED;
  if (n < 0) return LC_ERR_INVALID;
  for (int32_t i = 0; i < n; i++) {
    if (lc_buf_read_f32_le(b, &f) != LC_OK) return LC_ERR_TRUNCATED;
    if (lc_skip_option_registry_entry_holder_set(b) != LC_OK) return LC_ERR_TRUNCATED;
    if (lc_buf_read_f32_le(b, &f) != LC_OK) return LC_ERR_TRUNCATED;
    if (lc_buf_read_f32_le(b, &f) != LC_OK) return LC_ERR_TRUNCATED;
  }
  if (lc_buf_read_f32_le(b, &f) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f32_le(b, &f) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_f32_le(b, &f) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_skip_option_string(b) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_skip_option_registry_entry_holder(b) != LC_OK) return LC_ERR_TRUNCATED;
  return lc_skip_option_registry_entry_holder(b);
}
/* Good for: Minecraft 1.21+ item slot / equipment component wire skipping or parsing.
 * Callers: slot.c (same file).
 */

static lc_status lc_skip_item_block_property(lc_buf *b) {
  char *s = NULL;
  if (lc_buf_read_string(b, &s) != LC_OK) return LC_ERR_TRUNCATED;
  free(s);
  uint8_t exact;
  if (lc_buf_read_bool(b, &exact) != LC_OK) return LC_ERR_TRUNCATED;
  if (exact) {
    if (lc_buf_read_string(b, &s) != LC_OK) return LC_ERR_TRUNCATED;
    free(s);
  } else {
    if (lc_buf_read_string(b, &s) != LC_OK) return LC_ERR_TRUNCATED;
    free(s);
    if (lc_buf_read_string(b, &s) != LC_OK) return LC_ERR_TRUNCATED;
    free(s);
  }
  return LC_OK;
}
/* Good for: Minecraft 1.21+ item slot / equipment component wire skipping or parsing.
 * Callers: slot.c (same file).
 */

static lc_status lc_skip_exact_component_matcher(lc_buf *b) {
  int32_t n;
  if (lc_buf_read_varint(b, &n) != LC_OK) return LC_ERR_TRUNCATED;
  if (n < 0) return LC_ERR_INVALID;
  for (int32_t i = 0; i < n; i++) {
    int32_t ct;
    if (lc_buf_read_varint(b, &ct) != LC_OK) return LC_ERR_TRUNCATED;
    if (lc_skip_slot_component_data(b, ct) != LC_OK) return LC_ERR_TRUNCATED;
  }
  return LC_OK;
}
/* Good for: Minecraft 1.21+ item slot / equipment component wire skipping or parsing.
 * Callers: slot.c (same file).
 */

static lc_status lc_skip_data_component_matchers(lc_buf *b) {
  if (lc_skip_exact_component_matcher(b) != LC_OK) return LC_ERR_TRUNCATED;
  int32_t n;
  if (lc_buf_read_varint(b, &n) != LC_OK) return LC_ERR_TRUNCATED;
  if (n < 0) return LC_ERR_INVALID;
  for (int32_t i = 0; i < n; i++) {
    if (lc_buf_read_varint(b, &n) != LC_OK) return LC_ERR_TRUNCATED;
  }
  return LC_OK;
}
/* Good for: Minecraft 1.21+ item slot / equipment component wire skipping or parsing.
 * Callers: slot.c (same file).
 */

static lc_status lc_skip_item_block_predicate(lc_buf *b) {
  if (lc_skip_option_registry_entry_holder_set(b) != LC_OK) return LC_ERR_TRUNCATED;
  uint8_t has;
  if (lc_buf_read_bool(b, &has) != LC_OK) return LC_ERR_TRUNCATED;
  if (has) {
    int32_t n;
    if (lc_buf_read_varint(b, &n) != LC_OK) return LC_ERR_TRUNCATED;
    if (n < 0) return LC_ERR_INVALID;
    for (int32_t i = 0; i < n; i++) {
      if (lc_skip_item_block_property(b) != LC_OK) return LC_ERR_TRUNCATED;
    }
  }
  if (lc_nbt_skip_anon_optional(b) != LC_OK) return LC_ERR_TRUNCATED;
  return lc_skip_data_component_matchers(b);
}

/* banner_patterns (1.21.9+): layers[] of pattern registry id + dye color */
/* Good for: Minecraft 1.21+ item slot / equipment component wire skipping or parsing.
 * Callers: slot.c (same file).
 */
static lc_status lc_skip_banner_patterns(lc_buf *b) {
  int32_t count;
  if (lc_buf_read_varint(b, &count) != LC_OK) return LC_ERR_TRUNCATED;
  if (count < 0) return LC_ERR_INVALID;
  for (int32_t i = 0; i < count; i++) {
    int32_t v;
    if (lc_buf_read_varint(b, &v) != LC_OK) return LC_ERR_TRUNCATED;
    if (lc_buf_read_varint(b, &v) != LC_OK) return LC_ERR_TRUNCATED;
  }
  return LC_OK;
}
/* Good for: Minecraft 1.21+ item slot / equipment component wire skipping or parsing.
 * Callers: slot.c (same file).
 */

static lc_status lc_skip_can_place_on_break(lc_buf *b) {
  int32_t n;
  if (lc_buf_read_varint(b, &n) != LC_OK) return LC_ERR_TRUNCATED;
  if (n < 0) return LC_ERR_INVALID;
  for (int32_t i = 0; i < n; i++) {
    if (lc_skip_item_block_predicate(b) != LC_OK) return LC_ERR_TRUNCATED;
  }
  return LC_OK;
}

/* IDSet / registryEntryHolderSet (proto.yml) */
/* Good for: Minecraft 1.21+ item slot / equipment component wire skipping or parsing.
 * Callers: slot.c (same file).
 */
static lc_status lc_skip_registry_entry_holder_set(lc_buf *b) {
  int32_t n;
  if (lc_buf_read_varint(b, &n) != LC_OK) return LC_ERR_TRUNCATED;
  if (n == 0) {
    char *s = NULL;
    lc_status st = lc_buf_read_string(b, &s);
    free(s);
    return st;
  }
  for (int32_t i = 0; i < n - 1; i++) {
    if (lc_buf_read_varint(b, &n) != LC_OK) return LC_ERR_TRUNCATED;
  }
  return LC_OK;
}
/* Good for: Minecraft 1.21+ item slot / equipment component wire skipping or parsing.
 * Callers: slot.c (same file).
 */

static lc_status lc_skip_attribute_modifier_display(lc_buf *b) {
  int32_t dtype;
  if (lc_buf_read_varint(b, &dtype) != LC_OK) return LC_ERR_TRUNCATED;
  if (dtype == 2) return lc_nbt_skip_anonymous(b);
  return LC_OK;
}
/* Good for: Minecraft 1.21+ item slot / equipment component wire skipping or parsing.
 * Callers: slot.c (same file).
 */

static lc_status lc_skip_attribute_modifiers(lc_buf *b) {
  int32_t count;
  if (lc_buf_read_varint(b, &count) != LC_OK) return LC_ERR_TRUNCATED;
  if (count < 0) return LC_ERR_INVALID;
  for (int32_t i = 0; i < count; i++) {
    char *s = NULL;
    int32_t v;
    double d;
    if (lc_buf_read_varint(b, &v) != LC_OK) return LC_ERR_TRUNCATED;
    if (lc_buf_read_string(b, &s) != LC_OK) return LC_ERR_TRUNCATED;
    free(s);
    if (lc_buf_read_f64_le(b, &d) != LC_OK) return LC_ERR_TRUNCATED;
    if (lc_buf_read_varint(b, &v) != LC_OK) return LC_ERR_TRUNCATED;
    if (lc_buf_read_varint(b, &v) != LC_OK) return LC_ERR_TRUNCATED;
    if (lc_skip_attribute_modifier_display(b) != LC_OK) return LC_ERR_TRUNCATED;
  }
  return LC_OK;
}
/* Good for: Minecraft 1.21+ item slot / equipment component wire skipping or parsing.
 * Callers: slot.c (same file).
 */

static lc_status lc_skip_enchantment_list(lc_buf *b) {
  int32_t count;
  if (lc_buf_read_varint(b, &count) != LC_OK) return LC_ERR_TRUNCATED;
  if (count < 0) return LC_ERR_INVALID;
  for (int32_t i = 0; i < count; i++) {
    int32_t id, level;
    if (lc_buf_read_varint(b, &id) != LC_OK) return LC_ERR_TRUNCATED;
    if (lc_buf_read_varint(b, &level) != LC_OK) return LC_ERR_TRUNCATED;
    (void)id;
    (void)level;
  }
  return LC_OK;
}

/* pc/1.21.9 SlotComponent.data — partial skip (proto.yml SlotComponent) */
/* Good for: Minecraft 1.21+ item slot / equipment component wire skipping or parsing.
 * Callers: slot.c (same file).
 */
static lc_status lc_skip_slot_component_data(lc_buf *b, int32_t comp_type) {
  int32_t v;
  switch (comp_type) {
    case 0:
    case 5:
    case 6:
    case 38:
    case 55:
    case 57:
    case 58:
    case 71:
      return lc_nbt_skip_anonymous(b);
    case 1:
    case 2:
    case 3:
    case 9:
    case 17:
    case 23:
    case 37:
    case 39:
    case 41:
    case 49:
    case 51:
    case 65:
    case 67:
    case 75:
      return lc_buf_read_varint(b, &v);
    case 4:
    case 20:
    case 22:
    case 30:
    case 31:
    case 43:
      return LC_OK;
    case 14:
    case 15:
      return lc_skip_can_place_on_break(b);
    case 33:
      return lc_skip_blocks_attacks(b);
    case 7: {
      float f;
      return lc_buf_read_f32_le(b, &f);
    }
    case 27:
    case 44:
    case 48:
    case 50:
    case 56:
    case 74: {
      char *s = NULL;
      lc_status st = lc_buf_read_string(b, &s);
      free(s);
      return st;
    }
    case 63:
      return lc_skip_banner_patterns(b);
    case 29:
      return lc_skip_registry_entry_holder_set(b);
    case 34:
      return lc_skip_enchantment_list(b);
    case 36: {
      int32_t v;
      return lc_buf_read_i32_be(b, &v);
    }
    case 8:
      return lc_skip_nbt_array(b);
    case 10:
      return lc_skip_enchantment_list(b);
    case 11:
      return lc_skip_nbt_array(b);
    case 13:
      return lc_skip_attribute_modifiers(b);
    case 18:
/* Good for: Read bool from packet cursor lc_buf (all parsers).
 * Callers: entity_move_look.c, metadata.c, packets.c, play_stream.c, rel_entity_move.c, slot.c (same file), slot_fprint.c, spawn_info.c, sync_entity_position.c.
 */
      return lc_buf_read_bool(b, &(uint8_t){0});
    case 25:
    case 26:
    case 28:
    case 32:
    case 42:
    case 52:
    case 53: {
      lc_equipment tmp;
      return lc_slot_read(b, &tmp);
    }
    case 35: {
      char *s = NULL;
      lc_status st = lc_buf_read_string(b, &s);
      free(s);
      return st;
    }
    case 40: {
      if (lc_buf_read_varint(b, &v) != LC_OK) return LC_ERR_TRUNCATED;
      float f;
      return lc_buf_read_f32_le(b, &f);
    }
    case 54: {
      uint8_t has;
      if (lc_buf_read_bool(b, &has) != LC_OK) return LC_ERR_TRUNCATED;
      if (has && lc_buf_read_varint(b, &v) != LC_OK) return LC_ERR_TRUNCATED;
      if (lc_buf_read_bool(b, &has) != LC_OK) return LC_ERR_TRUNCATED;
      if (has && lc_buf_read_i32_be(b, &v) != LC_OK) return LC_ERR_TRUNCATED;
      if (lc_buf_read_varint(b, &v) != LC_OK) return LC_ERR_TRUNCATED;
      return lc_skip_option_string(b);
    }
    default:
      return LC_ERR_INVALID;
  }
}

/* Minecraft 1.21+ Slot: varint count; if count>0 => itemId, added/removed counts, components */
/* Good for: Minecraft 1.21+ item slot / equipment component wire skipping or parsing.
 * Callers: metadata.c, play_stream.c, slot.c (same file).
 */
lc_status lc_slot_read(lc_buf *b, lc_equipment *eq) {
  memset(eq, 0, sizeof(*eq));
  eq->item_id = -1;
  int32_t count;
  if (lc_buf_read_varint(b, &count) != LC_OK) return LC_ERR_TRUNCATED;
  eq->item_count = count;
  if (count == 0) return LC_OK;

  size_t start = b->off;
  int32_t item_id, added, removed;
  if (lc_buf_read_varint(b, &item_id) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_varint(b, &added) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_varint(b, &removed) != LC_OK) return LC_ERR_TRUNCATED;
  eq->item_id = item_id;

  for (int32_t i = 0; i < added; i++) {
    int32_t comp_type;
    if (lc_buf_read_varint(b, &comp_type) != LC_OK) return LC_ERR_TRUNCATED;
    if (lc_skip_slot_component_data(b, comp_type) != LC_OK) return LC_ERR_TRUNCATED;
  }
  for (int32_t i = 0; i < removed; i++) {
    int32_t comp_type;
    if (lc_buf_read_varint(b, &comp_type) != LC_OK) return LC_ERR_TRUNCATED;
  }

  size_t extra = b->off - start;
  eq->item_extra.data = extra ? (uint8_t *)malloc(extra) : NULL;
  if (extra && !eq->item_extra.data) return LC_ERR_OOM;
  if (extra) memcpy(eq->item_extra.data, b->data + start, extra);
  eq->item_extra.len = extra;
  return LC_OK;
}
/* Good for: Minecraft 1.21+ item slot / equipment component wire skipping or parsing.
 * Callers: entity_equipment.c, packets.c.
 */

lc_status lc_read_top_bit_array(lc_buf *b, lc_equipment_arr *out) {
  size_t cap = 4;
  size_t n = 0;
  lc_equipment *items = (lc_equipment *)calloc(cap, sizeof(lc_equipment));
  if (!items) return LC_ERR_OOM;

  while (1) {
    uint8_t header;
    if (lc_buf_read_u8(b, &header) != LC_OK) {
      free(items);
      return LC_ERR_TRUNCATED;
    }
    items[n].slot = (int8_t)(header & 0x7f);
    if (n >= cap) {
      cap *= 2;
      lc_equipment *next = (lc_equipment *)realloc(items, cap * sizeof(lc_equipment));
      if (!next) {
        lc_equipment_arr tmp = {items, n};
        lc_equipment_arr_free(&tmp);
        return LC_ERR_OOM;
      }
      items = next;
    }
    if (lc_slot_read(b, &items[n]) != LC_OK) goto fail;
    n++;
    if ((header & 0x80) == 0) break;
  }
  out->items = items;
  out->count = n;
  return LC_OK;

fail:
  {
    lc_equipment_arr tmp = {items, n};
    lc_equipment_arr_free(&tmp);
  }
  return LC_ERR_TRUNCATED;
}
/* Good for: Release heap owned by lc_equipment arr.
 * Callers: entity_equipment.c, packets.c, slot.c (same file).
 */

void lc_equipment_arr_free(lc_equipment_arr *a) {
  if (!a->items) return;
  for (size_t i = 0; i < a->count; i++) lc_byte_buf_free(&a->items[i].item_extra);
  free(a->items);
  a->items = NULL;
  a->count = 0;
}
