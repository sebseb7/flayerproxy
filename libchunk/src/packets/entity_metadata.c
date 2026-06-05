#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "../internal.h"
/* Good for: Format one metadata entry for toString.
 * Callers: debug.c, entity_metadata.c (same file).
 */

static int lc_write_nbt_as_string(const uint8_t *data, size_t len, char *buf, size_t buflen, int w);

static int lc_write_item_stack(const uint8_t *data, size_t len, char *buf, size_t buflen, int w) {
  lc_buf b;
  lc_buf_init(&b, data, len);
  lc_equipment eq;
  memset(&eq, 0, sizeof eq);
  if (lc_slot_read(&b, &eq) != LC_OK) {
    lc_byte_buf_free(&eq.item_extra);
    return lc_appendf(buf, buflen, w, "<invalid item_stack>");
  }
  if (eq.item_count == 0) {
    lc_byte_buf_free(&eq.item_extra);
    return lc_appendf(buf, buflen, w, "empty");
  }
  int res = lc_appendf(buf, buflen, w, "item_stack{id=%d,count=%d", eq.item_id, eq.item_count);
  if (eq.item_extra.data && eq.item_extra.len > 0) {
    lc_buf eb;
    lc_buf_init(&eb, eq.item_extra.data, eq.item_extra.len);
    int32_t wire_item_id, added, removed;
    if (lc_buf_read_varint(&eb, &wire_item_id) == LC_OK &&
        lc_buf_read_varint(&eb, &added) == LC_OK &&
        lc_buf_read_varint(&eb, &removed) == LC_OK) {
      if (added > 0 || removed > 0) {
        res = lc_appendf(buf, buflen, res, ",components=[");
        int first = 1;
        for (int32_t i = 0; i < added; i++) {
          int32_t comp_type;
          if (lc_buf_read_varint(&eb, &comp_type) != LC_OK) break;
          res = lc_appendf(buf, buflen, res, "%s+%s", first ? "" : ",", lc_slot_component_type_name(comp_type));
          first = 0;
          size_t comp_start = eb.off;
          if (lc_skip_slot_component_data(&eb, comp_type) != LC_OK) break;
          size_t comp_len = eb.off - comp_start;
          if (comp_len > 0) {
            if (comp_type == 5 || comp_type == 6 || comp_type == 0) {
              res = lc_appendf(buf, buflen, res, "=");
              res = lc_write_nbt_as_string(eb.data + comp_start, comp_len, buf, buflen, res);
            } else if (comp_type == 34 || comp_type == 10) {
              lc_buf val_b;
              lc_buf_init(&val_b, eb.data + comp_start, comp_len);
              int32_t ench_count;
              if (lc_buf_read_varint(&val_b, &ench_count) == LC_OK) {
                res = lc_appendf(buf, buflen, res, "=[");
                for (int32_t j = 0; j < ench_count; j++) {
                  int32_t ench_id, ench_lvl;
                  if (lc_buf_read_varint(&val_b, &ench_id) != LC_OK) break;
                  if (lc_buf_read_varint(&val_b, &ench_lvl) != LC_OK) break;
                  res = lc_appendf(buf, buflen, res, "%s{id=%d,lvl=%d}", j ? "," : "", ench_id, ench_lvl);
                }
                res = lc_appendf(buf, buflen, res, "]");
              }
            } else if (comp_type == 1 || comp_type == 2 || comp_type == 3 || comp_type == 9 ||
                       comp_type == 17 || comp_type == 23 || comp_type == 37 || comp_type == 39 ||
                       comp_type == 41 || comp_type == 49 || comp_type == 51 || comp_type == 65 ||
                       comp_type == 67 || comp_type == 75) {
              lc_buf val_b;
              lc_buf_init(&val_b, eb.data + comp_start, comp_len);
              int32_t val;
              if (lc_buf_read_varint(&val_b, &val) == LC_OK) {
                res = lc_appendf(buf, buflen, res, "=%d", val);
              }
            } else if (comp_type == 27 || comp_type == 44 || comp_type == 48 || comp_type == 50 ||
                       comp_type == 56 || comp_type == 74 || comp_type == 35) {
              lc_buf val_b;
              lc_buf_init(&val_b, eb.data + comp_start, comp_len);
              char *s = NULL;
              if (lc_buf_read_string(&val_b, &s) == LC_OK) {
                res = lc_appendf(buf, buflen, res, "=\"%s\"", s ? s : "");
                free(s);
              }
            } else if (comp_type == 18 || comp_type == 30) {
              lc_buf val_b;
              lc_buf_init(&val_b, eb.data + comp_start, comp_len);
              uint8_t val;
              if (lc_buf_read_bool(&val_b, &val) == LC_OK) {
                res = lc_appendf(buf, buflen, res, "=%s", val ? "true" : "false");
              }
            }
          }
        }
        for (int32_t i = 0; i < removed; i++) {
          int32_t comp_type;
          if (lc_buf_read_varint(&eb, &comp_type) != LC_OK) break;
          res = lc_appendf(buf, buflen, res, "%s-%s", first ? "" : ",", lc_slot_component_type_name(comp_type));
          first = 0;
        }
        res = lc_appendf(buf, buflen, res, "]");
      }
    }
  }
  res = lc_appendf(buf, buflen, res, "}");
  lc_byte_buf_free(&eq.item_extra);
  return res;
}


static int lc_write_nbt_as_string(const uint8_t *data, size_t len, char *buf, size_t buflen, int w) {
  if (!data || len == 0) {
    return lc_appendf(buf, buflen, w, "(empty)");
  }
#if defined(__GLIBC__)
  char *mptr = NULL;
  size_t msize = 0;
  FILE *mem = open_memstream(&mptr, &msize);
  if (mem) {
    lc_nbt_fprint_wire(mem, "", data, len);
    fclose(mem);
    if (mptr) {
      for (size_t i = 0; i < msize; i++) {
        if (mptr[i] == '\n' || mptr[i] == '\r' || mptr[i] == '\t') {
          mptr[i] = ' ';
        }
      }
      size_t dst = 0;
      int last_space = 0;
      for (size_t src = 0; src < msize; src++) {
        if (mptr[src] == ' ') {
          if (!last_space) {
            mptr[dst++] = ' ';
            last_space = 1;
          }
        } else {
          mptr[dst++] = mptr[src];
          last_space = 0;
        }
      }
      if (dst > 0 && mptr[dst - 1] == ' ') dst--;
      mptr[dst] = '\0';
      int written = lc_appendf(buf, buflen, w, "%s", mptr);
      free(mptr);
      return written;
    }
  }
#endif
  return lc_appendf(buf, buflen, w, "<nbt %zu bytes>", len);
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
      if (e->type_id == 7) {
        return lc_write_item_stack(e->v.raw.data, e->v.raw.len, buf, buflen, w);
      }
      if (e->type_id == 6) { // optional_component
        if (e->v.raw.len == 0) {
          return lc_appendf(buf, buflen, w, "absent");
        }
        uint8_t present = e->v.raw.data[0];
        if (!present) {
          return lc_appendf(buf, buflen, w, "absent");
        }
        return lc_write_nbt_as_string(e->v.raw.data + 1, e->v.raw.len - 1, buf, buflen, w);
      }
      if (e->type_id == 5) { // component
        return lc_write_nbt_as_string(e->v.raw.data, e->v.raw.len, buf, buflen, w);
      }
      return lc_appendf(buf, buflen, w, "<raw %zu bytes>", e->v.raw.len);
    default:
      return lc_appendf(buf, buflen, w, "?");
  }
}
/* Good for: Decode Minecraft wire payload for entity metadata into a struct.
 * Callers: chunk_stream_receiver.c, decode_wire.c.
 */

lc_status lc_parse_entity_metadata(const uint8_t *data, size_t len, lc_entity_metadata *out) {
  memset(out, 0, sizeof(*out));
  lc_buf b;
  lc_buf_init(&b, data, len);
  if (lc_buf_read_varint(&b, &out->entity_id) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_metadata_read_loop(&b, &out->metadata) != LC_OK) return LC_ERR_TRUNCATED;
  return LC_OK;
}
/* Good for: Release heap owned by lc_entity metadata.
 * Callers: chunk_stream_receiver.c, decode_wire.c.
 */

void lc_entity_metadata_free(lc_entity_metadata *p) {
  lc_metadata_arr_free(&p->metadata);
  memset(p, 0, sizeof(*p));
}
/* Good for: One-line debug summary of lc_entity metadata (sniffer / decode tools).
 * Callers: decode_wire.c.
 */

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
