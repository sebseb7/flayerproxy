#include "internal.h"

#include <stdlib.h>
#include <string.h>

#define CMD_NODE_NAME_MAX 48
#define CMD_CHILDREN_MAX 256

typedef struct {
  uint8_t node_type;
  char name[CMD_NODE_NAME_MAX];
  int32_t *children;
  int32_t child_count;
} lc_cmd_node;

static void cmd_node_clear(lc_cmd_node *n) {
  free(n->children);
  n->children = NULL;
  n->child_count = 0;
  n->name[0] = '\0';
  n->node_type = 0;
}

static lc_status skip_brigadier_minmax_f32(lc_buf *b) {
  static const lc_bitfield_spec spec[] = {{6, 0}, {1, 0}, {1, 0}};
  int32_t flags[3];
  if (lc_buf_read_bitfield(b, spec, 3, flags) != LC_OK) return LC_ERR_TRUNCATED;
  if (flags[1]) {
    float f;
    if (lc_buf_read_f32_le(b, &f) != LC_OK) return LC_ERR_TRUNCATED;
  }
  if (flags[2]) {
    float f;
    if (lc_buf_read_f32_le(b, &f) != LC_OK) return LC_ERR_TRUNCATED;
  }
  return LC_OK;
}

static lc_status skip_brigadier_minmax_f64(lc_buf *b) {
  static const lc_bitfield_spec spec[] = {{6, 0}, {1, 0}, {1, 0}};
  int32_t flags[3];
  if (lc_buf_read_bitfield(b, spec, 3, flags) != LC_OK) return LC_ERR_TRUNCATED;
  if (flags[1]) {
    double d;
    if (lc_buf_read_f64_le(b, &d) != LC_OK) return LC_ERR_TRUNCATED;
  }
  if (flags[2]) {
    double d;
    if (lc_buf_read_f64_le(b, &d) != LC_OK) return LC_ERR_TRUNCATED;
  }
  return LC_OK;
}

static lc_status skip_brigadier_minmax_i32(lc_buf *b) {
  static const lc_bitfield_spec spec[] = {{6, 0}, {1, 0}, {1, 0}};
  int32_t flags[3];
  if (lc_buf_read_bitfield(b, spec, 3, flags) != LC_OK) return LC_ERR_TRUNCATED;
  if (flags[1]) {
    int32_t v;
    if (lc_buf_read_i32_le(b, &v) != LC_OK) return LC_ERR_TRUNCATED;
  }
  if (flags[2]) {
    int32_t v;
    if (lc_buf_read_i32_le(b, &v) != LC_OK) return LC_ERR_TRUNCATED;
  }
  return LC_OK;
}

static lc_status skip_brigadier_minmax_i64(lc_buf *b) {
  static const lc_bitfield_spec spec[] = {{6, 0}, {1, 0}, {1, 0}};
  int32_t flags[3];
  if (lc_buf_read_bitfield(b, spec, 3, flags) != LC_OK) return LC_ERR_TRUNCATED;
  if (flags[1]) {
    int64_t v;
    if (lc_buf_read_i64_le(b, &v) != LC_OK) return LC_ERR_TRUNCATED;
  }
  if (flags[2]) {
    int64_t v;
    if (lc_buf_read_i64_le(b, &v) != LC_OK) return LC_ERR_TRUNCATED;
  }
  return LC_OK;
}

static lc_status skip_command_parser_properties(lc_buf *b, int32_t parser) {
  switch (parser) {
    case 0:
      return LC_OK;
    case 1:
      return skip_brigadier_minmax_f32(b);
    case 2:
      return skip_brigadier_minmax_f64(b);
    case 3:
      return skip_brigadier_minmax_i32(b);
    case 4:
      return skip_brigadier_minmax_i64(b);
    case 5: {
      int32_t v;
      return lc_buf_read_varint(b, &v);
    }
    case 6: {
      static const lc_bitfield_spec spec[] = {{6, 0}, {1, 0}, {1, 0}};
      int32_t flags[3];
      return lc_buf_read_bitfield(b, spec, 3, flags);
    }
    case 43: {
      int32_t v;
      return lc_buf_read_i32_le(b, &v);
    }
    case 44:
    case 45:
    case 46:
    case 47:
    case 48: {
      char *s = NULL;
      lc_status st = lc_buf_read_string(b, &s);
      free(s);
      return st;
    }
    case 31: {
      static const lc_bitfield_spec spec[] = {{7, 0}, {1, 0}};
      int32_t flags[2];
      return lc_buf_read_bitfield(b, spec, 2, flags);
    }
    default:
      return LC_OK;
  }
}

static lc_status skip_registry_entry_holder_trim(lc_buf *b) {
  int32_t tag;
  if (lc_buf_read_varint(b, &tag) != LC_OK) return LC_ERR_TRUNCATED;
  if (tag > 0) return LC_OK;
  char *s = NULL;
  if (lc_buf_read_string(b, &s) != LC_OK) return LC_ERR_TRUNCATED;
  free(s);
  if (lc_nbt_skip_anonymous(b) != LC_OK) return LC_ERR_TRUNCATED;
  uint8_t decal;
  return lc_buf_read_bool(b, &decal);
}

static lc_status skip_slot_display_depth(lc_buf *b, int depth);

static lc_status skip_slot_display_depth(lc_buf *b, int depth) {
  if (depth > 32) return LC_ERR_INVALID;
  int32_t type;
  if (lc_buf_read_varint(b, &type) != LC_OK) return LC_ERR_TRUNCATED;
  switch (type) {
    case 0:
    case 1:
      return LC_OK;
    case 2: {
      int32_t id;
      return lc_buf_read_varint(b, &id);
    }
    case 3:
      return lc_slot_skip(b);
    case 4: {
      char *s = NULL;
      lc_status st = lc_buf_read_string(b, &s);
      free(s);
      return st;
    }
    case 5:
      if (skip_slot_display_depth(b, depth + 1) != LC_OK) return LC_ERR_TRUNCATED;
      if (skip_slot_display_depth(b, depth + 1) != LC_OK) return LC_ERR_TRUNCATED;
      return skip_registry_entry_holder_trim(b);
    case 6:
      if (skip_slot_display_depth(b, depth + 1) != LC_OK) return LC_ERR_TRUNCATED;
      return skip_slot_display_depth(b, depth + 1);
    case 7: {
      int32_t n, i;
      if (lc_buf_read_varint(b, &n) != LC_OK) return LC_ERR_TRUNCATED;
      if (n < 0) return LC_ERR_INVALID;
      for (i = 0; i < n; i++) {
        if (skip_slot_display_depth(b, depth + 1) != LC_OK) return LC_ERR_TRUNCATED;
      }
      return LC_OK;
    }
    default:
      return LC_ERR_INVALID;
  }
}

static lc_status skip_slot_display(lc_buf *b) { return skip_slot_display_depth(b, 0); }

static lc_status skip_id_set(lc_buf *b) {
  int32_t tag;
  if (lc_buf_read_varint(b, &tag) != LC_OK) return LC_ERR_TRUNCATED;
  if (tag == 0) {
    char *s = NULL;
    lc_status st = lc_buf_read_string(b, &s);
    free(s);
    return st;
  }
  if (tag < 0) return LC_ERR_INVALID;
  for (int32_t i = 0; i < tag - 1; i++) {
    int32_t id;
    if (lc_buf_read_varint(b, &id) != LC_OK) return LC_ERR_TRUNCATED;
  }
  return LC_OK;
}

static lc_status read_command_node(lc_buf *b, lc_cmd_node *node, uint8_t has_suggestions_out[1]) {
  static const lc_bitfield_spec flag_spec[] = {{2, 0}, {1, 0}, {1, 0}, {1, 0}, {1, 0}, {2, 0}};
  int32_t flags[6];
  if (lc_buf_read_bitfield(b, flag_spec, 6, flags) != LC_OK) return LC_ERR_TRUNCATED;

  uint8_t has_redirect = (uint8_t)flags[3];
  uint8_t has_suggestions = (uint8_t)flags[2];
  if (has_suggestions_out) *has_suggestions_out = has_suggestions;
  node->node_type = (uint8_t)flags[5];
  node->name[0] = '\0';

  int32_t child_count;
  if (lc_buf_read_varint(b, &child_count) != LC_OK) return LC_ERR_TRUNCATED;
  if (child_count < 0 || child_count > CMD_CHILDREN_MAX) return LC_ERR_INVALID;
  node->child_count = child_count;
  if (child_count > 0) {
    node->children = (int32_t *)calloc((size_t)child_count, sizeof(int32_t));
    if (!node->children) return LC_ERR_OOM;
    for (int32_t i = 0; i < child_count; i++) {
      if (lc_buf_read_varint(b, &node->children[i]) != LC_OK) return LC_ERR_TRUNCATED;
    }
  }

  if (has_redirect) {
    int32_t redirect;
    if (lc_buf_read_varint(b, &redirect) != LC_OK) return LC_ERR_TRUNCATED;
  }

  switch (node->node_type) {
    case 0:
      break;
    case 1: {
      char *s = NULL;
      if (lc_buf_read_string(b, &s) != LC_OK) return LC_ERR_TRUNCATED;
      snprintf(node->name, sizeof node->name, "%s", s ? s : "");
      free(s);
      break;
    }
    case 2: {
      char *s = NULL;
      int32_t parser;
      if (lc_buf_read_string(b, &s) != LC_OK) return LC_ERR_TRUNCATED;
      snprintf(node->name, sizeof node->name, "%s", s ? s : "");
      free(s);
      if (lc_buf_read_varint(b, &parser) != LC_OK) return LC_ERR_TRUNCATED;
      if (skip_command_parser_properties(b, parser) != LC_OK) return LC_ERR_TRUNCATED;
      if (has_suggestions) {
        char *st = NULL;
        if (lc_buf_read_string(b, &st) != LC_OK) return LC_ERR_TRUNCATED;
        free(st);
      }
      break;
    }
    default:
      return LC_ERR_INVALID;
  }
  return LC_OK;
}

static void append_escaped_name(char *buf, size_t bufsz, int *written, const char *name) {
  if (!name || !name[0]) return;
  if (*written > 0) {
    *written = lc_appendf(buf, bufsz, *written, ",");
    if (*written < 0) return;
  }
  for (const char *p = name; *p; p++) {
    char c = *p;
    if (c == '"' || c == '\\') {
      *written = lc_appendf(buf, bufsz, *written, "\\%c", c);
    } else if ((unsigned char)c >= 32) {
      *written = lc_appendf(buf, bufsz, *written, "%c", c);
    }
    if (*written < 0) return;
  }
}

int lc_decode_declare_commands(const uint8_t *payload, size_t payload_len, char *out, size_t out_sz) {
  lc_buf b;
  lc_buf_init(&b, payload, payload_len);
  int32_t node_count;
  if (lc_buf_read_varint(&b, &node_count) != LC_OK) return -1;
  if (node_count < 0 || node_count > 4096) return -1;

  lc_cmd_node *nodes = (lc_cmd_node *)calloc((size_t)node_count, sizeof(lc_cmd_node));
  if (!nodes) return -1;

  for (int32_t i = 0; i < node_count; i++) {
    uint8_t sug = 0;
    if (read_command_node(&b, &nodes[i], &sug) != LC_OK) {
      for (int32_t j = 0; j <= i; j++) cmd_node_clear(&nodes[j]);
      free(nodes);
      return -1;
    }
  }

  int32_t root_index;
  if (lc_buf_read_varint(&b, &root_index) != LC_OK) {
    for (int32_t j = 0; j < node_count; j++) cmd_node_clear(&nodes[j]);
    free(nodes);
    return -1;
  }

  int w = lc_snprintf(out, out_sz, "declare_commands{nodes=%d,rootIndex=%d", node_count, root_index);
  if (root_index >= 0 && root_index < node_count && nodes[root_index].child_count > 0) {
    w = lc_appendf(out, out_sz, w, ",rootChildren=[");
    for (int32_t i = 0; i < nodes[root_index].child_count && w > 0; i++) {
      int32_t ci = nodes[root_index].children[i];
      if (ci >= 0 && ci < node_count) append_escaped_name(out, out_sz, &w, nodes[ci].name);
    }
    if (w > 0) w = lc_appendf(out, out_sz, w, "]");
  }

  int literals = 0;
  for (int32_t i = 0; i < node_count && literals < 8; i++) {
    if (nodes[i].node_type == 1 && nodes[i].name[0]) literals++;
  }
  if (literals > 0 && w > 0) {
    w = lc_appendf(out, out_sz, w, ",literals=[");
    int shown = 0;
    for (int32_t i = 0; i < node_count && shown < 8 && w > 0; i++) {
      if (nodes[i].node_type != 1 || !nodes[i].name[0]) continue;
      append_escaped_name(out, out_sz, &w, nodes[i].name);
      shown++;
    }
    if (w > 0) w = lc_appendf(out, out_sz, w, "]");
  }

  if (w > 0) w = lc_appendf(out, out_sz, w, ",bytesRemaining=%zu}", lc_buf_remaining(&b));
  else if (w > 0) w = lc_appendf(out, out_sz, w, "}");

  for (int32_t j = 0; j < node_count; j++) cmd_node_clear(&nodes[j]);
  free(nodes);
  return w > 0 && (size_t)w < out_sz ? 1 : -1;
}

int lc_decode_update_recipes(const uint8_t *payload, size_t payload_len, char *out, size_t out_sz) {
  lc_buf b;
  lc_buf_init(&b, payload, payload_len);
  int32_t recipe_count;
  if (lc_buf_read_varint(&b, &recipe_count) != LC_OK) return -1;
  if (recipe_count < 0) return -1;

  char first_name[64] = "";
  char second_name[64] = "";
  int32_t first_items = 0;

  for (int32_t i = 0; i < recipe_count; i++) {
    char *name = NULL;
    int32_t item_count;
    if (lc_buf_read_string(&b, &name) != LC_OK) return -1;
    if (lc_buf_read_varint(&b, &item_count) != LC_OK) {
      free(name);
      return -1;
    }
    if (item_count < 0) {
      free(name);
      return -1;
    }
    for (int32_t j = 0; j < item_count; j++) {
      int32_t id;
      if (lc_buf_read_varint(&b, &id) != LC_OK) {
        free(name);
        return -1;
      }
    }
    if (i == 0 && name) {
      snprintf(first_name, sizeof first_name, "%s", name);
      first_items = item_count;
    } else if (i == 1 && name) {
      snprintf(second_name, sizeof second_name, "%s", name);
    }
    free(name);
  }

  int32_t stone_count;
  if (lc_buf_read_varint(&b, &stone_count) != LC_OK) return -1;
  if (stone_count < 0) return -1;
  for (int32_t i = 0; i < stone_count; i++) {
    if (skip_id_set(&b) != LC_OK) return -1;
    if (skip_slot_display(&b) != LC_OK) return -1;
  }

  int w;
  if (first_name[0] && second_name[0]) {
    w = lc_snprintf(out, out_sz,
                    "update_recipes{recipes=%d,first=%s,firstItems=%d,second=%s,"
                    "stoneCutter=%d,bytesRemaining=%zu}",
                    recipe_count, first_name, first_items, second_name, stone_count,
                    lc_buf_remaining(&b));
  } else if (first_name[0]) {
    w = lc_snprintf(out, out_sz,
                    "update_recipes{recipes=%d,first=%s,firstItems=%d,stoneCutter=%d,"
                    "bytesRemaining=%zu}",
                    recipe_count, first_name, first_items, stone_count, lc_buf_remaining(&b));
  } else {
    w = lc_snprintf(out, out_sz,
                    "update_recipes{recipes=%d,stoneCutter=%d,bytesRemaining=%zu}",
                    recipe_count, stone_count, lc_buf_remaining(&b));
  }
  return w > 0 && (size_t)w < out_sz ? 1 : -1;
}

int lc_decode_system_chat(const uint8_t *payload, size_t payload_len, char *out, size_t out_sz) {
  lc_buf b;
  lc_buf_init(&b, payload, payload_len);
  char *text = NULL;
  char *translate = NULL;
  if (lc_nbt_extract_chat_summary(&b, &text, &translate) != LC_OK) {
    free(text);
    free(translate);
    return -1;
  }
  uint8_t action_bar;
  if (lc_buf_read_bool(&b, &action_bar) != LC_OK) {
    free(text);
    free(translate);
    return -1;
  }

  int w;
  if (translate && translate[0]) {
    w = lc_snprintf(out, out_sz, "system_chat{text=%s,translate=%s,isActionBar=%s}",
                    text && text[0] ? text : "", translate, action_bar ? "true" : "false");
  } else {
    w = lc_snprintf(out, out_sz, "system_chat{text=%s,isActionBar=%s}", text && text[0] ? text : "",
                    action_bar ? "true" : "false");
  }
  free(text);
  free(translate);
  return w > 0 && (size_t)w < out_sz ? 1 : -1;
}

int lc_decode_set_ticking_state(const uint8_t *payload, size_t payload_len, char *out, size_t out_sz) {
  lc_buf b;
  lc_buf_init(&b, payload, payload_len);
  float tick_rate;
  uint8_t frozen;
  if (lc_buf_read_f32_le(&b, &tick_rate) != LC_OK) return -1;
  if (lc_buf_read_bool(&b, &frozen) != LC_OK) return -1;
  int w = lc_snprintf(out, out_sz, "set_ticking_state{tickRate=%.2f,isFrozen=%s}", tick_rate,
                      frozen ? "true" : "false");
  return w > 0 && (size_t)w < out_sz ? 1 : -1;
}
