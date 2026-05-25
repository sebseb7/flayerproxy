#include "internal.h"

/* entityMetadataEntry types — pc/1.21.9/proto.yml (indices 0..36; 1.21.10 uses this file) */
static const char *META_NAMES[] = {
    "byte",         "int",           "long",        "float",           "string",
    "component",    "optional_component", "item_stack", "boolean",  "rotations",
    "block_pos",    "optional_block_pos", "direction", "optional_uuid", "block_state",
    "optional_block_state", "particle", "particles", "villager_data", "optional_unsigned_int",
    "pose",         "cat_variant",   "cow_variant", "wolf_variant",  "wolf_sound_variant",
    "frog_variant", "pig_variant",   "chicken_variant", "optional_global_pos",
    "painting_variant", "sniffer_state", "armadillo_state", "copper_golem_state",
    "weathering_copper_golem_state", "vector3", "quaternion", "resolvable_profile",
};

const char *lc_metadata_type_name(int id) {
  if (id >= 0 && id < (int)(sizeof(META_NAMES) / sizeof(META_NAMES[0]))) return META_NAMES[id];
  return "unknown";
}

static lc_status lc_skip_option_string(lc_buf *b) {
  uint8_t present;
  if (lc_buf_read_bool(b, &present) != LC_OK) return LC_ERR_TRUNCATED;
  if (!present) return LC_OK;
  char *s = NULL;
  lc_status st = lc_buf_read_string(b, &s);
  free(s);
  return st;
}

static lc_status lc_skip_option_uuid(lc_buf *b) {
  uint8_t present;
  if (lc_buf_read_bool(b, &present) != LC_OK) return LC_ERR_TRUNCATED;
  if (!present) return LC_OK;
  lc_uuid u;
  return lc_buf_read_uuid(b, &u);
}

static lc_status lc_skip_option_anonymous_nbt(lc_buf *b) {
  uint8_t present;
  if (lc_buf_read_bool(b, &present) != LC_OK) return LC_ERR_TRUNCATED;
  if (!present) return LC_OK;
  return lc_nbt_skip_anonymous(b);
}

static lc_status lc_skip_game_profile_property(lc_buf *b) {
  char *s = NULL;
  if (lc_buf_read_string(b, &s) != LC_OK) return LC_ERR_TRUNCATED;
  free(s);
  if (lc_buf_read_string(b, &s) != LC_OK) return LC_ERR_TRUNCATED;
  free(s);
  return lc_skip_option_string(b);
}

static lc_status lc_skip_painting_variant_data(lc_buf *b) {
  int32_t w, h;
  if (lc_buf_read_i32_be(b, &w) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_i32_be(b, &h) != LC_OK) return LC_ERR_TRUNCATED;
  char *s = NULL;
  if (lc_buf_read_string(b, &s) != LC_OK) return LC_ERR_TRUNCATED;
  free(s);
  if (lc_skip_option_anonymous_nbt(b) != LC_OK) return LC_ERR_TRUNCATED;
  return lc_skip_option_anonymous_nbt(b);
}

static lc_status lc_skip_registry_holder_painting(lc_buf *b) {
  int32_t n;
  if (lc_buf_read_varint(b, &n) != LC_OK) return LC_ERR_TRUNCATED;
  if (n == 0) return lc_skip_painting_variant_data(b);
  for (int32_t i = 0; i < n - 1; i++) {
    if (lc_buf_read_varint(b, &n) != LC_OK) return LC_ERR_TRUNCATED;
  }
  return LC_OK;
}

static lc_status lc_skip_particle(lc_buf *b) {
  int32_t type;
  if (lc_buf_read_varint(b, &type) != LC_OK) return LC_ERR_TRUNCATED;
  switch (type) {
    case 1:
    case 2:
    case 29:
    case 109:
    case 113:
      return lc_buf_read_varint(b, &(int32_t){0});
    case 14:
      for (int i = 0; i < 4; i++) {
        float f;
        if (lc_buf_read_f32_le(b, &f) != LC_OK) return LC_ERR_TRUNCATED;
      }
      return LC_OK;
    case 15:
      for (int i = 0; i < 7; i++) {
        float f;
        if (lc_buf_read_f32_le(b, &f) != LC_OK) return LC_ERR_TRUNCATED;
      }
      return LC_OK;
    case 16:
    case 46: {
      int32_t v;
      if (lc_buf_read_i32_be(b, &v) != LC_OK) return LC_ERR_TRUNCATED;
      float f;
      return lc_buf_read_f32_le(b, &f);
    }
    case 21:
    case 36:
    case 42: {
      int32_t v;
      return lc_buf_read_i32_be(b, &v);
    }
    case 8: {
      float f;
      return lc_buf_read_f32_le(b, &f);
    }
    case 38: {
      float f;
      return lc_buf_read_f32_le(b, &f);
    }
    case 47: {
      lc_equipment eq;
      return lc_slot_read(b, &eq);
    }
    case 103:
      return lc_buf_read_varint(b, &(int32_t){0});
    case 48: {
      int32_t pos_type;
      if (lc_buf_read_varint(b, &pos_type) != LC_OK) return LC_ERR_TRUNCATED;
      if (pos_type == 0) {
        lc_block_pos p;
        if (lc_buf_read_position(b, &p) != LC_OK) return LC_ERR_TRUNCATED;
      } else if (pos_type == 1) {
        if (lc_buf_read_varint(b, &pos_type) != LC_OK) return LC_ERR_TRUNCATED;
        float f;
        if (lc_buf_read_f32_le(b, &f) != LC_OK) return LC_ERR_TRUNCATED;
      } else {
        return LC_ERR_INVALID;
      }
      return lc_buf_read_varint(b, &(int32_t){0});
    }
    case 49: {
      double d;
      for (int i = 0; i < 3; i++)
        if (lc_buf_read_f64_le(b, &d) != LC_OK) return LC_ERR_TRUNCATED;
      uint8_t u;
      return lc_buf_read_u8(b, &u);
    }
    default:
      return LC_OK;
  }
}

static lc_status lc_skip_particles(lc_buf *b) {
  int32_t count;
  if (lc_buf_read_varint(b, &count) != LC_OK) return LC_ERR_TRUNCATED;
  if (count < 0) return LC_ERR_INVALID;
  for (int32_t i = 0; i < count; i++)
    if (lc_skip_particle(b) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}

static lc_status lc_skip_optional_global_pos(lc_buf *b) {
  uint8_t present;
  if (lc_buf_read_bool(b, &present) != LC_OK) return LC_ERR_TRUNCATED;
  if (!present) return LC_OK;
  char *dim = NULL;
  if (lc_buf_read_string(b, &dim) != LC_OK) return LC_ERR_TRUNCATED;
  free(dim);
  lc_block_pos p;
  return lc_buf_read_position(b, &p);
}

static lc_status lc_skip_resolvable_profile(lc_buf *b) {
  int32_t kind;
  if (lc_buf_read_varint(b, &kind) != LC_OK) return LC_ERR_TRUNCATED;
  if (kind == 0) {
    if (lc_skip_option_string(b) != LC_OK) return LC_ERR_TRUNCATED;
    if (lc_skip_option_uuid(b) != LC_OK) return LC_ERR_TRUNCATED;
    int32_t n;
    if (lc_buf_read_varint(b, &n) != LC_OK) return LC_ERR_TRUNCATED;
    if (n < 0) return LC_ERR_INVALID;
    for (int32_t i = 0; i < n; i++)
      if (lc_skip_game_profile_property(b) != LC_OK) return LC_ERR_TRUNCATED;
  } else if (kind == 1) {
    lc_uuid u;
    if (lc_buf_read_uuid(b, &u) != LC_OK) return LC_ERR_TRUNCATED;
    char *name = NULL;
    if (lc_buf_read_string(b, &name) != LC_OK) return LC_ERR_TRUNCATED;
    free(name);
    int32_t n;
    if (lc_buf_read_varint(b, &n) != LC_OK) return LC_ERR_TRUNCATED;
    if (n < 0) return LC_ERR_INVALID;
    for (int32_t i = 0; i < n; i++)
      if (lc_skip_game_profile_property(b) != LC_OK) return LC_ERR_TRUNCATED;
  } else {
    return LC_ERR_INVALID;
  }
  if (lc_skip_option_string(b) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_skip_option_string(b) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_skip_option_string(b) != LC_OK) return LC_ERR_TRUNCATED;
  uint8_t present;
  if (lc_buf_read_bool(b, &present) != LC_OK) return LC_ERR_TRUNCATED;
  if (present && lc_buf_read_varint(b, &kind) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}

static lc_status lc_skip_metadata_value(lc_buf *b, int type_id) {
  switch (type_id) {
    case 0: {
      int8_t v;
      return lc_buf_read_i8(b, &v);
    }
    case 1: {
      int32_t v;
      return lc_buf_read_varint(b, &v);
    }
    case 2: {
      int64_t v;
      return lc_buf_read_varlong(b, &v);
    }
    case 3: {
      float f;
      return lc_buf_read_f32_le(b, &f);
    }
    case 4: {
      char *s = NULL;
      lc_status st = lc_buf_read_string(b, &s);
      free(s);
      return st;
    }
    case 5:
      return lc_nbt_skip_anonymous(b);
    case 6:
      return lc_skip_option_anonymous_nbt(b);
    case 7: {
      lc_equipment eq;
      return lc_slot_read(b, &eq);
    }
    case 8: {
      uint8_t v;
      return lc_buf_read_bool(b, &v);
    }
    case 9: {
      float f;
      if (lc_buf_read_f32_le(b, &f) != LC_OK) return LC_ERR_TRUNCATED;
      if (lc_buf_read_f32_le(b, &f) != LC_OK) return LC_ERR_TRUNCATED;
      return lc_buf_read_f32_le(b, &f);
    }
    case 10:
      return lc_buf_read_position(b, &(lc_block_pos){0});
    case 11: {
      uint8_t present;
      if (lc_buf_read_bool(b, &present) != LC_OK) return LC_ERR_TRUNCATED;
      if (!present) return LC_OK;
      return lc_buf_read_position(b, &(lc_block_pos){0});
    }
    case 12:
      return lc_buf_read_varint(b, &(int32_t){0});
    case 13:
      return lc_skip_option_uuid(b);
    case 14:
      return lc_buf_read_varint(b, &(int32_t){0});
    case 15: {
      int32_t v;
      return lc_buf_read_varint(b, &v);
    }
    case 16:
      return lc_skip_particle(b);
    case 17:
      return lc_skip_particles(b);
    case 18: {
      int32_t v;
      if (lc_buf_read_varint(b, &v) != LC_OK) return LC_ERR_TRUNCATED;
      if (lc_buf_read_varint(b, &v) != LC_OK) return LC_ERR_TRUNCATED;
      return lc_buf_read_varint(b, &v);
    }
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
    case 30:
    case 31:
    case 32:
    case 33: {
      int32_t v;
      return lc_buf_read_varint(b, &v);
    }
    case 28:
      return lc_skip_optional_global_pos(b);
    case 29:
      return lc_skip_registry_holder_painting(b);
    case 34: {
      float f;
      if (lc_buf_read_f32_le(b, &f) != LC_OK) return LC_ERR_TRUNCATED;
      if (lc_buf_read_f32_le(b, &f) != LC_OK) return LC_ERR_TRUNCATED;
      return lc_buf_read_f32_le(b, &f);
    }
    case 35: {
      float f;
      for (int i = 0; i < 4; i++)
        if (lc_buf_read_f32_le(b, &f) != LC_OK) return LC_ERR_TRUNCATED;
      return LC_OK;
    }
    case 36:
      return lc_skip_resolvable_profile(b);
    default:
      return LC_ERR_INVALID;
  }
}

static lc_status lc_parse_metadata_value(lc_buf *b, int type_id, lc_metadata_entry *e) {
  size_t start = b->off;
  e->type_id = type_id;
  e->type_name = lc_metadata_type_name(type_id);

  switch (type_id) {
    case 0:
      e->kind = LC_META_BYTE;
      return lc_buf_read_i8(b, &e->v.i8);
    case 1:
      e->kind = LC_META_INT;
      return lc_buf_read_varint(b, &e->v.i32);
    case 2:
      e->kind = LC_META_LONG;
      return lc_buf_read_varlong(b, &e->v.i64);
    case 3:
      e->kind = LC_META_FLOAT;
      return lc_buf_read_f32_le(b, &e->v.f32);
    case 4:
      e->kind = LC_META_STRING;
      return lc_buf_read_string(b, &e->v.string);
    case 8:
      e->kind = LC_META_BOOL;
      return lc_buf_read_bool(b, &e->v.boolean);
    case 14:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
    case 30:
    case 31:
    case 32:
    case 33:
      e->kind = LC_META_VARINT;
      return lc_buf_read_varint(b, &e->v.i32);
    default: {
      lc_buf tmp = *b;
      lc_status st = lc_skip_metadata_value(&tmp, type_id);
      if (st != LC_OK) return st;
      size_t n = tmp.off - start;
      e->kind = LC_META_RAW;
      e->v.raw.data = n ? (uint8_t *)malloc(n) : NULL;
      if (n && !e->v.raw.data) return LC_ERR_OOM;
      if (n) memcpy(e->v.raw.data, b->data + start, n);
      e->v.raw.len = n;
      b->off = tmp.off;
      return LC_OK;
    }
  }
}

lc_status lc_metadata_read_loop(lc_buf *b, lc_metadata_arr *out) {
  size_t cap = 8;
  size_t n = 0;
  lc_metadata_entry *items = (lc_metadata_entry *)calloc(cap, sizeof(lc_metadata_entry));
  if (!items) return LC_ERR_OOM;

  while (1) {
    uint8_t key;
    if (lc_buf_read_u8(b, &key) != LC_OK) {
      free(items);
      return LC_ERR_TRUNCATED;
    }
    if (key == LC_META_END) break;
    if (n >= cap) {
      cap *= 2;
      lc_metadata_entry *next = (lc_metadata_entry *)realloc(items, cap * sizeof(lc_metadata_entry));
      if (!next) {
        lc_metadata_arr tmp = {items, n};
        lc_metadata_arr_free(&tmp);
        return LC_ERR_OOM;
      }
      items = next;
    }
    items[n].key = key;
    int32_t type_id;
    if (lc_buf_read_varint(b, &type_id) != LC_OK) {
      lc_metadata_arr tmp = {items, n};
      lc_metadata_arr_free(&tmp);
      return LC_ERR_TRUNCATED;
    }
    if (lc_parse_metadata_value(b, type_id, &items[n]) != LC_OK) {
      lc_metadata_arr tmp = {items, n + 1};
      lc_metadata_arr_free(&tmp);
      return LC_ERR_TRUNCATED;
    }
    n++;
  }
  out->items = items;
  out->count = n;
  return LC_OK;
}

void lc_metadata_arr_free(lc_metadata_arr *a) {
  if (!a->items) return;
  for (size_t i = 0; i < a->count; i++) {
    lc_metadata_entry *e = &a->items[i];
    if (e->kind == LC_META_STRING) free(e->v.string);
    if (e->kind == LC_META_RAW) lc_byte_buf_free(&e->v.raw);
  }
  free(a->items);
  a->items = NULL;
  a->count = 0;
}
