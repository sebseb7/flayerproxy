#include "internal.h"

/* pc/1.21.9 SlotComponentType names (proto.yml) */
static const char *SLOT_COMPONENT_NAMES[] = {
    "custom_data",         "max_stack_size",      "max_damage",           "damage",
    "unbreakable",         "custom_name",         "item_name",            "item_model",
    "lore",                "rarity",              "enchantments",         "can_place_on",
    "can_break",           "attribute_modifiers", "custom_model_data",    "tooltip_display",
    "repair_cost",         "creative_slot_lock",  "enchantment_glint_override",
    "intangible_projectile", "food",              "consumable",           "use_remainder",
    "use_cooldown",        "damage_resistant",    "tool",                 "weapon",
    "enchantable",         "equippable",          "repairable",           "glider",
    "tooltip_style",       "death_protection",    "blocks_attacks",       "stored_enchantments",
    "dyed_color",          "map_color",           "map_id",               "map_decorations",
    "map_post_processing", "charged_projectiles", "bundle_contents",      "potion_contents",
    "potion_duration_scale", "suspicious_stew_effects", "writable_book_content",
    "written_book_content", "trim",               "debug_stick_state",    "entity_data",
    "bucket_entity_data",  "block_entity_data",   "instrument",           "provides_trim_material",
    "ominous_bottle_amplifier", "jukebox_playable", "provides_banner_patterns", "recipes",
    "lodestone_tracker",   "firework_explosion",  "fireworks",            "profile",
    "note_block_sound",    "banner_patterns",     "base_color",           "pot_decorations",
    "container",           "block_state",         "bees",                 "lock",
    "container_loot",      "break_sound",         "villager/variant",     "wolf/variant",
    "wolf/sound_variant",  "wolf/collar",         "fox/variant",          "salmon/size",
    "parrot/variant",      "tropical_fish/pattern", "tropical_fish/base_color",
    "tropical_fish/pattern_color", "mooshroom/variant", "rabbit/variant", "pig/variant",
    "cow/variant",         "chicken/variant",     "frog/variant",         "horse/variant",
    "painting/variant",    "llama/variant",       "axolotl/variant",      "cat/variant",
    "cat/collar",          "sheep/color",         "shulker/color",
};

const char *lc_slot_component_type_name(int32_t id) {
  if (id >= 0 && id < (int32_t)(sizeof(SLOT_COMPONENT_NAMES) / sizeof(SLOT_COMPONENT_NAMES[0])))
    return SLOT_COMPONENT_NAMES[id];
  return "unknown";
}

static const char *lc_equipment_slot_name(int8_t slot) {
  static const char *names[] = {"mainhand", "offhand", "feet", "legs", "chest", "head", "body", "saddle"};
  if (slot >= 0 && slot < 8) return names[slot];
  return "unknown";
}

static int lc_fprint_enchantment_list(FILE *f, lc_buf *b, const char *indent) {
  int32_t count;
  if (lc_buf_read_varint(b, &count) != LC_OK) return -1;
  fprintf(f, "%senchantment_count: %d\n", indent, count);
  for (int32_t i = 0; i < count; i++) {
    int32_t id, level;
    if (lc_buf_read_varint(b, &id) != LC_OK) return -1;
    if (lc_buf_read_varint(b, &level) != LC_OK) return -1;
    fprintf(f, "%s  [%d] id=%d level=%d\n", indent, (int)i, id, level);
  }
  return 0;
}

static int lc_fprint_slot_body(FILE *f, lc_buf *b, const char *indent);

static int lc_fprint_slot_component_data(FILE *f, lc_buf *b, int32_t comp_type, const char *indent) {
  int32_t v;
  const char *name = lc_slot_component_type_name(comp_type);
  fprintf(f, "%scomponent_type: %d (%s)\n", indent, comp_type, name);

  switch (comp_type) {
    case 0:
    case 5:
    case 6:
    case 55:
    case 57:
    case 58:
    case 71: {
      size_t start = b->off;
      if (lc_nbt_skip_anonymous(b) != LC_OK) return -1;
      fprintf(f, "%snbt_bytes: %zu\n", indent, b->off - start);
      return lc_nbt_fprint_wire(f, indent, b->data + start, b->off - start);
    }
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
      if (lc_buf_read_varint(b, &v) != LC_OK) return -1;
      fprintf(f, "%svalue: %d\n", indent, v);
      return 0;
    case 4:
    case 29:
    case 31:
    case 38:
    case 43:
      fprintf(f, "%s(void)\n", indent);
      return 0;
    case 7:
    case 44:
    case 48:
    case 50:
    case 74:
    case 35: {
      char *s = NULL;
      if (lc_buf_read_string(b, &s) != LC_OK) return -1;
      fprintf(f, "%sstring: \"%s\"\n", indent, s ? s : "");
      free(s);
      return 0;
    }
    case 8: {
      int32_t n;
      if (lc_buf_read_varint(b, &n) != LC_OK) return -1;
      fprintf(f, "%slore_lines: %d\n", indent, n);
      for (int32_t i = 0; i < n; i++) {
        size_t start = b->off;
        if (lc_nbt_skip_anonymous(b) != LC_OK) return -1;
        fprintf(f, "%s  line[%d] (%zu bytes):\n", indent, (int)i, b->off - start);
        if (lc_nbt_fprint_wire(f, "    ", b->data + start, b->off - start) != 0) {
          lc_wire_hex_fprint(f, b->data + start, b->off - start);
        }
      }
      return 0;
    }
    case 10:
    case 11:
      return lc_fprint_enchantment_list(f, b, indent);
    case 18:
    case 30: {
      uint8_t v8;
      if (lc_buf_read_bool(b, &v8) != LC_OK) return -1;
      fprintf(f, "%sbool: %u\n", indent, v8);
      return 0;
    }
    case 25:
    case 26:
    case 27:
    case 28:
    case 32:
    case 42:
    case 52:
    case 53:
      fprintf(f, "%snested_slot:\n", indent);
      return lc_fprint_slot_body(f, b, indent);
    case 40: {
      if (lc_buf_read_varint(b, &v) != LC_OK) return -1;
      float fl;
      if (lc_buf_read_f32_le(b, &fl) != LC_OK) return -1;
      fprintf(f, "%svarint: %d  f32: %g\n", indent, v, fl);
      return 0;
    }
    default:
      fprintf(f, "%s(unparsed component — hex from here)\n", indent);
      return -1;
  }
}

static int lc_fprint_slot_body(FILE *f, lc_buf *b, const char *indent) {
  int32_t count;
  if (lc_buf_read_varint(b, &count) != LC_OK) return -1;
  fprintf(f, "%sitemCount: %d\n", indent, count);
  if (count == 0) return 0;

  int32_t item_id, added, removed;
  if (lc_buf_read_varint(b, &item_id) != LC_OK) return -1;
  if (lc_buf_read_varint(b, &added) != LC_OK) return -1;
  if (lc_buf_read_varint(b, &removed) != LC_OK) return -1;
  fprintf(f, "%sitemId: %d\n", indent, item_id);
  fprintf(f, "%saddedComponentCount: %d\n", indent, added);
  fprintf(f, "%sremovedComponentCount: %d\n", indent, removed);

  for (int32_t i = 0; i < added; i++) {
    int32_t comp_type;
    size_t comp_start = b->off;
    if (lc_buf_read_varint(b, &comp_type) != LC_OK) return -1;
    fprintf(f, "%sadded[%d]:\n", indent, (int)i);
    char sub[128];
    snprintf(sub, sizeof sub, "%s  ", indent);
    if (lc_fprint_slot_component_data(f, b, comp_type, sub) != 0) {
      size_t left = b->len > comp_start ? b->len - comp_start : 0;
      fprintf(f, "%s  wire_hex:\n", indent);
      lc_wire_hex_fprint(f, b->data + comp_start, left);
      b->off = b->len;
    }
  }
  for (int32_t i = 0; i < removed; i++) {
    int32_t comp_type;
    if (lc_buf_read_varint(b, &comp_type) != LC_OK) return -1;
    fprintf(f, "%sremoved[%d]: type=%d (%s)\n", indent, (int)i, comp_type,
            lc_slot_component_type_name(comp_type));
  }
  if (b->off < b->len) {
    fprintf(f, "%strailing_bytes: %zu\n", indent, b->len - b->off);
    lc_wire_hex_fprint(f, b->data + b->off, b->len - b->off);
  }
  return 0;
}

int lc_slot_fprint_equipment_entry(FILE *f, const lc_equipment *eq, const char *indent) {
  if (!f || !eq) return -1;
  fprintf(f, "%sslot: %d (%s)\n", indent, (int)eq->slot, lc_equipment_slot_name(eq->slot));
  fprintf(f, "%sitemCount: %d\n", indent, eq->item_count);
  if (eq->item_count == 0) return 0;

  fprintf(f, "%sitemId: %d\n", indent, eq->item_id);
  if (!eq->item_extra.data || eq->item_extra.len == 0) {
    fprintf(f, "%s(no component wire)\n", indent);
    return 0;
  }

  lc_buf b;
  lc_buf_init(&b, eq->item_extra.data, eq->item_extra.len);
  int32_t wire_item_id;
  if (lc_buf_read_varint(&b, &wire_item_id) != LC_OK) return -1;
  fprintf(f, "%swireItemId: %d\n", indent, wire_item_id);

  int32_t added, removed;
  if (lc_buf_read_varint(&b, &added) != LC_OK) return -1;
  if (lc_buf_read_varint(&b, &removed) != LC_OK) return -1;
  fprintf(f, "%saddedComponentCount: %d\n", indent, added);
  fprintf(f, "%sremovedComponentCount: %d\n", indent, removed);

  for (int32_t i = 0; i < added; i++) {
    int32_t comp_type;
    size_t comp_start = b.off;
    if (lc_buf_read_varint(&b, &comp_type) != LC_OK) return -1;
    fprintf(f, "%sadded[%d]:\n", indent, (int)i);
    char sub[128];
    snprintf(sub, sizeof sub, "%s  ", indent);
    if (lc_fprint_slot_component_data(f, &b, comp_type, sub) != 0) {
      size_t left = b.len > comp_start ? b.len - comp_start : 0;
      fprintf(f, "%s  wire_hex:\n", indent);
      lc_wire_hex_fprint(f, b.data + comp_start, left);
      b.off = b.len;
    }
  }
  for (int32_t i = 0; i < removed; i++) {
    int32_t comp_type;
    if (lc_buf_read_varint(&b, &comp_type) != LC_OK) return -1;
    fprintf(f, "%sremoved[%d]: type=%d (%s)\n", indent, (int)i, comp_type,
            lc_slot_component_type_name(comp_type));
  }
  if (b.off < b.len) {
    fprintf(f, "%strailing_bytes: %zu\n", indent, b.len - b.off);
    lc_wire_hex_fprint(f, b.data + b.off, b.len - b.off);
  }
  return 0;
}
