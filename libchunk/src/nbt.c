#include "internal.h"

#include <stdio.h>

enum {
  TAG_END = 0,
  TAG_BYTE = 1,
  TAG_SHORT = 2,
  TAG_INT = 3,
  TAG_LONG = 4,
  TAG_FLOAT = 5,
  TAG_DOUBLE = 6,
  TAG_BYTE_ARRAY = 7,
  TAG_STRING = 8,
  TAG_LIST = 9,
  TAG_COMPOUND = 10,
  TAG_INT_ARRAY = 11,
  TAG_LONG_ARRAY = 12,
};

/* Minecraft play/configuration wire NBT (protodef "big" / network format). */

static lc_status lc_nbt_skip_payload(lc_buf *b, int tag);
/* Good for: Skip NBT string (length + UTF-8).
 * Callers: nbt.c (same file).
 */

static lc_status lc_nbt_skip_string(lc_buf *b) {
  uint16_t len;
  if (lc_buf_read_u16_be(b, &len) != LC_OK) return LC_ERR_TRUNCATED;
  if ((size_t)len > lc_buf_remaining(b)) return LC_ERR_TRUNCATED;
  b->off += len;
  return LC_OK;
}
/* Good for: Skip one named NBT tag.
 * Callers: nbt.c (same file).
 */

static lc_status lc_nbt_skip_named(lc_buf *b, int tag) {
  if (tag != TAG_END) {
    if (lc_nbt_skip_string(b) != LC_OK) return LC_ERR_TRUNCATED;
  }
  return lc_nbt_skip_payload(b, tag);
}
/* Good for: Skip NBT payload by tag type.
 * Callers: nbt.c (same file).
 */

static lc_status lc_nbt_skip_payload(lc_buf *b, int tag) {
  switch (tag) {
    case TAG_BYTE:
      if (lc_buf_need(b, 1) != LC_OK) return LC_ERR_TRUNCATED;
      b->off++;
      return LC_OK;
    case TAG_SHORT:
      if (lc_buf_need(b, 2) != LC_OK) return LC_ERR_TRUNCATED;
      b->off += 2;
      return LC_OK;
    case TAG_INT:
      return lc_buf_need(b, 4) == LC_OK ? (b->off += 4, LC_OK) : LC_ERR_TRUNCATED;
    case TAG_LONG:
      return lc_buf_need(b, 8) == LC_OK ? (b->off += 8, LC_OK) : LC_ERR_TRUNCATED;
    case TAG_FLOAT:
      return lc_buf_need(b, 4) == LC_OK ? (b->off += 4, LC_OK) : LC_ERR_TRUNCATED;
    case TAG_DOUBLE:
      return lc_buf_need(b, 8) == LC_OK ? (b->off += 8, LC_OK) : LC_ERR_TRUNCATED;
    case TAG_BYTE_ARRAY: {
      uint32_t len;
      if (lc_buf_read_u32_be(b, &len) != LC_OK) return LC_ERR_TRUNCATED;
      if (len > lc_buf_remaining(b)) return LC_ERR_TRUNCATED;
      b->off += len;
      return LC_OK;
    }
    case TAG_STRING:
      return lc_nbt_skip_string(b);
    case TAG_LIST: {
      uint8_t elem;
      uint32_t len;
      if (lc_buf_read_u8(b, &elem) != LC_OK) return LC_ERR_TRUNCATED;
      if (lc_buf_read_u32_be(b, &len) != LC_OK) return LC_ERR_TRUNCATED;
      for (uint32_t i = 0; i < len; i++) {
        if (lc_nbt_skip_payload(b, elem) != LC_OK) return LC_ERR_TRUNCATED;
      }
      return LC_OK;
    }
    case TAG_COMPOUND: {
      while (1) {
        uint8_t child;
        if (lc_buf_read_u8(b, &child) != LC_OK) return LC_ERR_TRUNCATED;
        if (child == TAG_END) return LC_OK;
        if (lc_nbt_skip_named(b, child) != LC_OK) return LC_ERR_TRUNCATED;
      }
    }
    case TAG_INT_ARRAY: {
      uint32_t len;
      if (lc_buf_read_u32_be(b, &len) != LC_OK) return LC_ERR_TRUNCATED;
      if ((size_t)len * 4 > lc_buf_remaining(b)) return LC_ERR_TRUNCATED;
      b->off += (size_t)len * 4;
      return LC_OK;
    }
    case TAG_LONG_ARRAY: {
      uint32_t len;
      if (lc_buf_read_u32_be(b, &len) != LC_OK) return LC_ERR_TRUNCATED;
      if ((size_t)len * 8 > lc_buf_remaining(b)) return LC_ERR_TRUNCATED;
      b->off += (size_t)len * 8;
      return LC_OK;
    }
    default:
      return LC_ERR_INVALID;
  }
}
/* Good for: Read or print Minecraft NBT embedded in packets.
 * Callers: metadata.c, nbt.c (same file), play_stream.c, slot.c, slot_fprint.c.
 */

lc_status lc_nbt_skip_anonymous(lc_buf *b) {
  uint8_t tag;
  if (lc_buf_read_u8(b, &tag) != LC_OK) return LC_ERR_TRUNCATED;
  if (tag == TAG_END) return LC_OK;
  return lc_nbt_skip_payload(b, tag);
}
/* Good for: Read or print Minecraft NBT embedded in packets.
 * Callers: nbt.c (same file).
 */

lc_status lc_nbt_read_anonymous(lc_buf *b, lc_byte_buf *out) {
  size_t start = b->off;
  lc_status st = lc_nbt_skip_anonymous(b);
  if (st != LC_OK) return st;
  size_t n = b->off - start;
  out->data = n ? (uint8_t *)malloc(n) : NULL;
  if (n && !out->data) return LC_ERR_OOM;
  if (n) memcpy(out->data, b->data + start, n);
  out->len = n;
  return LC_OK;
}
/* Good for: Read or print Minecraft NBT embedded in packets.
 * Callers: none.
 */

lc_status lc_nbt_read_optional(lc_buf *b, lc_byte_buf *out, uint8_t *present) {
  uint8_t tag;
  if (lc_buf_read_u8(b, &tag) != LC_OK) return LC_ERR_TRUNCATED;
  if (tag == TAG_END) {
    *present = 0;
    out->data = NULL;
    out->len = 0;
    return LC_OK;
  }
  *present = 1;
  size_t start = b->off - 1;
  if (lc_nbt_skip_payload(b, tag) != LC_OK) return LC_ERR_TRUNCATED;
  size_t n = b->off - start;
  out->data = (uint8_t *)malloc(n);
  if (!out->data) return LC_ERR_OOM;
  memcpy(out->data, b->data + start, n);
  out->len = n;
  return LC_OK;
}

/*
 * anonOptionalNbt (map_chunk block entities, registry entries, …):
 *   0 = absent
 *   1 = present, then anonymous NBT
 *   any other byte = present with NBT starting on that byte (tag type, usually 0x0a compound)
 */
/* Good for: Read or print Minecraft NBT embedded in packets.
 * Callers: map_chunk.c, slot.c.
 */
lc_status lc_nbt_skip_anon_optional(lc_buf *b) {
  int8_t marker;
  if (lc_buf_read_i8(b, &marker) != LC_OK) return LC_ERR_TRUNCATED;
  if (marker == 0) return LC_OK;
  if (marker != 1) b->off--;
  return lc_nbt_skip_anonymous(b);
}
/* Good for: Read or print Minecraft NBT embedded in packets.
 * Callers: packets.c, registry_data.c.
 */

lc_status lc_nbt_read_anon_optional(lc_buf *b, lc_byte_buf *out, uint8_t *present) {
  int8_t marker;
  if (lc_buf_read_i8(b, &marker) != LC_OK) return LC_ERR_TRUNCATED;
  if (marker == 0) {
    *present = 0;
    out->data = NULL;
    out->len = 0;
    return LC_OK;
  }
  *present = 1;
  if (marker != 1) b->off--;
  return lc_nbt_read_anonymous(b, out);
}

/* --- human-readable NBT dump (network / big-endian wire) --- */

static const char *lc_nbt_tag_name(int tag) {
  static const char *const names[] = {
      "TAG_END",      "TAG_BYTE",   "TAG_SHORT",  "TAG_INT",        "TAG_LONG",
      "TAG_FLOAT",    "TAG_DOUBLE", "TAG_BYTE[]", "TAG_STRING",   "TAG_LIST",
      "TAG_COMPOUND", "TAG_INT[]",  "TAG_LONG[]",
  };
  if (tag < 0 || tag > TAG_LONG_ARRAY) return "TAG_?";
  return names[tag];
}
/* Good for: Print indentation for NBT tree dump.
 * Callers: nbt.c (same file).
 */

static void lc_nbt_fprint_indent(FILE *f, const char *base, int depth) {
  fputs(base, f);
  for (int i = 0; i < depth; i++) fputs("  ", f);
}
/* Good for: Print escaped NBT string to FILE.
 * Callers: nbt.c (same file).
 */

static void lc_nbt_fprint_escaped(FILE *f, const char *s, size_t len) {
  fputc('"', f);
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    if (c == '"' || c == '\\') fputc('\\', f);
    if (c >= 32 && c < 127) {
      fputc((int)c, f);
    } else {
      fprintf(f, "\\x%02x", c);
    }
  }
  fputc('"', f);
}
/* Good for: Read big-endian float from NBT wire.
 * Callers: nbt.c (same file).
 */

static lc_status lc_nbt_read_f32_be(lc_buf *b, float *out) {
  if (lc_buf_need(b, 4) != LC_OK) return LC_ERR_TRUNCATED;
  uint32_t bits = ((uint32_t)b->data[b->off] << 24) | ((uint32_t)b->data[b->off + 1] << 16) |
                  ((uint32_t)b->data[b->off + 2] << 8) | (uint32_t)b->data[b->off + 3];
  b->off += 4;
  memcpy(out, &bits, sizeof(bits));
  return LC_OK;
}
/* Good for: Read big-endian double from NBT wire.
 * Callers: nbt.c (same file).
 */

static lc_status lc_nbt_read_f64_be(lc_buf *b, double *out) {
  if (lc_buf_need(b, 8) != LC_OK) return LC_ERR_TRUNCATED;
  uint64_t bits = ((uint64_t)b->data[b->off] << 56) | ((uint64_t)b->data[b->off + 1] << 48) |
                  ((uint64_t)b->data[b->off + 2] << 40) | ((uint64_t)b->data[b->off + 3] << 32) |
                  ((uint64_t)b->data[b->off + 4] << 24) | ((uint64_t)b->data[b->off + 5] << 16) |
                  ((uint64_t)b->data[b->off + 6] << 8) | (uint64_t)b->data[b->off + 7];
  b->off += 8;
  memcpy(out, &bits, sizeof(bits));
  return LC_OK;
}

static int lc_nbt_fprint_payload(FILE *f, const char *indent, lc_buf *b, int tag, int depth);
/* Good for: Pretty-print compound/list children.
 * Callers: nbt.c (same file).
 */

static int lc_nbt_fprint_named_children(FILE *f, const char *indent, lc_buf *b, int depth) {
  while (1) {
    uint8_t child;
    if (lc_buf_read_u8(b, &child) != LC_OK) {
      fprintf(f, "<truncated: missing child tag>\n");
      return -1;
    }
    if (child == TAG_END) return 0;
    uint16_t name_len;
    if (lc_buf_read_u16_be(b, &name_len) != LC_OK) {
      fprintf(f, "<truncated: missing field name>\n");
      return -1;
    }
    if ((size_t)name_len > lc_buf_remaining(b)) {
      fprintf(f, "<truncated: field name>\n");
      return -1;
    }
    const char *name = (const char *)(b->data + b->off);
    b->off += name_len;
    lc_nbt_fprint_indent(f, indent, depth);
    lc_nbt_fprint_escaped(f, name, name_len);
    fputs(": ", f);
    if (lc_nbt_fprint_payload(f, indent, b, child, depth + 1) != 0) return -1;
  }
}
/* Good for: Pretty-print NBT int array.
 * Callers: nbt.c (same file).
 */

static int lc_nbt_fprint_array_ints(FILE *f, lc_buf *b, uint32_t count, int width) {
  fputc('[', f);
  for (uint32_t i = 0; i < count; i++) {
    int32_t v;
    if (lc_buf_read_i32_be(b, &v) != LC_OK) {
      fputs(" <truncated>", f);
      return -1;
    }
    if (i) fputc(',', f);
    if (i > 0 && (i % (uint32_t)width) == 0) fputs("\n", f);
    fprintf(f, "%d", (int)v);
  }
  fputc(']', f);
  return 0;
}
/* Good for: Pretty-print NBT long array.
 * Callers: nbt.c (same file).
 */

static int lc_nbt_fprint_array_longs(FILE *f, lc_buf *b, uint32_t count, int width) {
  fputc('[', f);
  for (uint32_t i = 0; i < count; i++) {
    int64_t v;
    if (lc_buf_read_i64_be(b, &v) != LC_OK) {
      fputs(" <truncated>", f);
      return -1;
    }
    if (i) fputc(',', f);
    if (i > 0 && (i % (uint32_t)width) == 0) fputs("\n", f);
    fprintf(f, "%lld", (long long)v);
  }
  fputc(']', f);
  return 0;
}
/* Good for: Pretty-print one NBT tag payload.
 * Callers: nbt.c (same file).
 */

static int lc_nbt_fprint_payload(FILE *f, const char *indent, lc_buf *b, int tag, int depth) {
  if (depth > 48) {
    fprintf(f, "<max depth>\n");
    return -1;
  }

  switch (tag) {
    case TAG_BYTE: {
      int8_t v;
      if (lc_buf_read_i8(b, &v) != LC_OK) goto trunc;
      fprintf(f, "byte %d\n", (int)v);
      return 0;
    }
    case TAG_SHORT: {
      int16_t v;
      if (lc_buf_read_i16_be(b, &v) != LC_OK) goto trunc;
      fprintf(f, "short %d\n", (int)v);
      return 0;
    }
    case TAG_INT: {
      int32_t v;
      if (lc_buf_read_i32_be(b, &v) != LC_OK) goto trunc;
      fprintf(f, "int %d\n", (int)v);
      return 0;
    }
    case TAG_LONG: {
      int64_t v;
      if (lc_buf_read_i64_be(b, &v) != LC_OK) goto trunc;
      fprintf(f, "long %lld\n", (long long)v);
      return 0;
    }
    case TAG_FLOAT: {
      float v;
      if (lc_nbt_read_f32_be(b, &v) != LC_OK) goto trunc;
      fprintf(f, "float %g\n", (double)v);
      return 0;
    }
    case TAG_DOUBLE: {
      double v;
      if (lc_nbt_read_f64_be(b, &v) != LC_OK) goto trunc;
      fprintf(f, "double %g\n", v);
      return 0;
    }
    case TAG_BYTE_ARRAY: {
      uint32_t len;
      if (lc_buf_read_u32_be(b, &len) != LC_OK) goto trunc;
      fprintf(f, "byte[%u] ", (unsigned)len);
      if (len > lc_buf_remaining(b)) goto trunc;
      fputc('(', f);
      uint32_t show = len > 64 ? 64 : len;
      for (uint32_t i = 0; i < show; i++) {
        if (i) fputc(' ', f);
        fprintf(f, "%02x", b->data[b->off + i]);
      }
      b->off += len;
      if (len > show) fprintf(f, " …+%u", (unsigned)(len - show));
      fputs(")\n", f);
      return 0;
    }
    case TAG_STRING: {
      uint16_t len;
      if (lc_buf_read_u16_be(b, &len) != LC_OK) goto trunc;
      if ((size_t)len > lc_buf_remaining(b)) goto trunc;
      fputs("string ", f);
      lc_nbt_fprint_escaped(f, (const char *)(b->data + b->off), len);
      b->off += len;
      fputc('\n', f);
      return 0;
    }
    case TAG_LIST: {
      uint8_t elem;
      uint32_t len;
      if (lc_buf_read_u8(b, &elem) != LC_OK) goto trunc;
      if (lc_buf_read_u32_be(b, &len) != LC_OK) goto trunc;
      fprintf(f, "list<%s>[%u]\n", lc_nbt_tag_name(elem), (unsigned)len);
      for (uint32_t i = 0; i < len; i++) {
        lc_nbt_fprint_indent(f, indent, depth + 1);
        fprintf(f, "[%u]: ", (unsigned)i);
        if (lc_nbt_fprint_payload(f, indent, b, elem, depth + 2) != 0) return -1;
      }
      return 0;
    }
    case TAG_COMPOUND: {
      fputs("compound {\n", f);
      if (lc_nbt_fprint_named_children(f, indent, b, depth + 1) != 0) return -1;
      lc_nbt_fprint_indent(f, indent, depth);
      fputs("}\n", f);
      return 0;
    }
    case TAG_INT_ARRAY: {
      uint32_t len;
      if (lc_buf_read_u32_be(b, &len) != LC_OK) goto trunc;
      fprintf(f, "int[%u] ", (unsigned)len);
      if (lc_nbt_fprint_array_ints(f, b, len, 12) != 0) goto trunc;
      fputc('\n', f);
      return 0;
    }
    case TAG_LONG_ARRAY: {
      uint32_t len;
      if (lc_buf_read_u32_be(b, &len) != LC_OK) goto trunc;
      fprintf(f, "long[%u] ", (unsigned)len);
      if (lc_nbt_fprint_array_longs(f, b, len, 8) != 0) goto trunc;
      fputc('\n', f);
      return 0;
    }
    default:
      fprintf(f, "<unknown tag %d>\n", tag);
      return -1;
  }
trunc:
  fprintf(f, "<truncated>\n");
  return -1;
}

/* --- sign text extraction (block entity NBT) --- */
/* Good for: Read NBT string into malloc'd C string.
 * Callers: nbt.c (same file).
 */

static lc_status lc_nbt_read_wire_string(lc_buf *b, char **out) {
  uint16_t len;
  if (lc_buf_read_u16_be(b, &len) != LC_OK) return LC_ERR_TRUNCATED;
  if ((size_t)len > lc_buf_remaining(b)) return LC_ERR_TRUNCATED;
  char *s = (char *)malloc((size_t)len + 1);
  if (!s) return LC_ERR_OOM;
  if (len) memcpy(s, b->data + b->off, len);
  s[len] = '\0';
  b->off += len;
  *out = s;
  return LC_OK;
}
/* Good for: Append one sign line to front/back text.
 * Callers: nbt.c (same file).
 */

static void lc_sign_merge_line(char **dst, char *src) {
  if (!src || !src[0]) {
    free(src);
    return;
  }
  if (!*dst || !(*dst)[0]) {
    free(*dst);
    *dst = src;
    return;
  }
  size_t a = strlen(*dst);
  size_t c = strlen(src);
  char *merged = (char *)realloc(*dst, a + c + 1);
  if (!merged) {
    free(src);
    return;
  }
  *dst = merged;
  memcpy(*dst + a, src, c + 1);
  free(src);
}
/* Good for: Extract JSON/text from sign NBT compound.
 * Callers: nbt.c (same file).
 */

static lc_status lc_nbt_compound_take_text(lc_buf *b, char **out) {
  *out = NULL;
  while (1) {
    uint8_t tag;
    if (lc_buf_read_u8(b, &tag) != LC_OK) return LC_ERR_TRUNCATED;
    if (tag == TAG_END) return LC_OK;
    uint16_t name_len;
    if (lc_buf_read_u16_be(b, &name_len) != LC_OK) return LC_ERR_TRUNCATED;
    if ((size_t)name_len > lc_buf_remaining(b)) return LC_ERR_TRUNCATED;
    const char *name = (const char *)(b->data + b->off);
    b->off += name_len;
    if (tag == TAG_STRING && (name_len == 0 || (name_len == 4 && memcmp(name, "text", 4) == 0))) {
      char *s = NULL;
      if (lc_nbt_read_wire_string(b, &s) != LC_OK) return LC_ERR_TRUNCATED;
      lc_sign_merge_line(out, s);
      continue;
    }
    if (name_len == 4 && memcmp(name, "text", 4) == 0 && tag == TAG_COMPOUND) {
      char *s = NULL;
      if (lc_nbt_compound_take_text(b, &s) != LC_OK) return LC_ERR_TRUNCATED;
      lc_sign_merge_line(out, s);
      continue;
    }
    if (name_len == 5 && memcmp(name, "extra", 5) == 0 && tag == TAG_LIST) {
      uint8_t elem;
      uint32_t count;
      if (lc_buf_read_u8(b, &elem) != LC_OK) return LC_ERR_TRUNCATED;
      if (lc_buf_read_u32_be(b, &count) != LC_OK) return LC_ERR_TRUNCATED;
      for (uint32_t i = 0; i < count; i++) {
        if (elem == TAG_STRING) {
          char *part = NULL;
          if (lc_nbt_read_wire_string(b, &part) != LC_OK) return LC_ERR_TRUNCATED;
          lc_sign_merge_line(out, part);
          continue;
        }
        if (elem == TAG_COMPOUND) {
          char *part = NULL;
          if (lc_nbt_compound_take_text(b, &part) != LC_OK) return LC_ERR_TRUNCATED;
          lc_sign_merge_line(out, part);
          continue;
        }
        if (lc_nbt_skip_payload(b, elem) != LC_OK) return LC_ERR_TRUNCATED;
      }
      continue;
    }
    if (lc_nbt_skip_payload(b, tag) != LC_OK) return LC_ERR_TRUNCATED;
  }
}
/* Good for: Read sign messages[] into line pointers.
 * Callers: nbt.c (same file).
 */

static lc_status lc_nbt_read_messages_lines(lc_buf *b, char *lines[LC_SIGN_LINES]) {
  uint8_t elem;
  uint32_t count;
  if (lc_buf_read_u8(b, &elem) != LC_OK) return LC_ERR_TRUNCATED;
  if (lc_buf_read_u32_be(b, &count) != LC_OK) return LC_ERR_TRUNCATED;
  uint32_t n = count < LC_SIGN_LINES ? count : LC_SIGN_LINES;
  for (uint32_t i = 0; i < count; i++) {
    if (elem != TAG_COMPOUND) {
      if (lc_nbt_skip_payload(b, elem) != LC_OK) return LC_ERR_TRUNCATED;
      continue;
    }
    char *line = NULL;
    if (lc_nbt_compound_take_text(b, &line) != LC_OK) return LC_ERR_TRUNCATED;
    if (i < n) {
      free(lines[i]);
      lines[i] = line;
      line = NULL;
    } else {
      free(line);
    }
  }
  return LC_OK;
}
/* Good for: Parse front_text or back_text sign compound.
 * Callers: nbt.c (same file).
 */

static lc_status lc_sign_text_read_side_compound(lc_buf *b, char *lines[LC_SIGN_LINES], int *found) {
  while (1) {
    uint8_t tag;
    if (lc_buf_read_u8(b, &tag) != LC_OK) return LC_ERR_TRUNCATED;
    if (tag == TAG_END) return LC_OK;
    uint16_t name_len;
    if (lc_buf_read_u16_be(b, &name_len) != LC_OK) return LC_ERR_TRUNCATED;
    if ((size_t)name_len > lc_buf_remaining(b)) return LC_ERR_TRUNCATED;
    const char *name = (const char *)(b->data + b->off);
    b->off += name_len;

    if (name_len == 8 && memcmp(name, "messages", 8) == 0 && tag == TAG_LIST) {
      if (lc_nbt_read_messages_lines(b, lines) != LC_OK) return LC_ERR_TRUNCATED;
      *found = 1;
      continue;
    }
    if (name_len == 4 && memcmp(name, "text", 4) == 0 &&
        (tag == TAG_STRING || tag == TAG_COMPOUND)) {
      char *text = NULL;
      if (tag == TAG_STRING) {
        if (lc_nbt_read_wire_string(b, &text) != LC_OK) return LC_ERR_TRUNCATED;
      } else if (lc_nbt_compound_take_text(b, &text) != LC_OK) {
        return LC_ERR_TRUNCATED;
      }
      if (text && text[0]) {
        free(lines[0]);
        lines[0] = text;
        *found = 1;
      } else {
        free(text);
      }
      continue;
    }
    if (lc_nbt_skip_payload(b, tag) != LC_OK) return LC_ERR_TRUNCATED;
  }
}
/* Good for: Clear lc_sign_text line pointers.
 * Callers: nbt.c (same file).
 */

static void lc_sign_text_clear(lc_sign_text *s) {
  if (!s) return;
  for (int i = 0; i < LC_SIGN_LINES; i++) {
    free(s->front[i]);
    free(s->back[i]);
    s->front[i] = NULL;
    s->back[i] = NULL;
  }
  s->has_sign = 0;
}
/* Good for: Release heap owned by lc_sign text.
 * Callers: dump_json.c, summarize_raw_dir.c, test_packets.c.
 */

void lc_sign_text_free(lc_sign_text *s) {
  lc_sign_text_clear(s);
}
/* Good for: Walk sign root NBT compound into lc_sign_text.
 * Callers: nbt.c (same file).
 */

static lc_status lc_sign_text_walk_fields(lc_buf *b, lc_sign_text *out) {
  while (1) {
    uint8_t tag;
    if (lc_buf_read_u8(b, &tag) != LC_OK) return LC_ERR_TRUNCATED;
    if (tag == TAG_END) return LC_OK;
    uint16_t name_len;
    if (lc_buf_read_u16_be(b, &name_len) != LC_OK) return LC_ERR_TRUNCATED;
    if ((size_t)name_len > lc_buf_remaining(b)) return LC_ERR_TRUNCATED;
    const char *name = (const char *)(b->data + b->off);
    b->off += name_len;

    if (name_len == 8 && memcmp(name, "messages", 8) == 0 && tag == TAG_LIST) {
      if (lc_nbt_read_messages_lines(b, out->front) != LC_OK) return LC_ERR_TRUNCATED;
      out->has_sign = 1;
      continue;
    }
    if (name_len == 10 && memcmp(name, "front_text", 10) == 0 && tag == TAG_COMPOUND) {
      int found = 0;
      if (lc_sign_text_read_side_compound(b, out->front, &found) != LC_OK) return LC_ERR_TRUNCATED;
      if (found) out->has_sign = 1;
      continue;
    }
    if (name_len == 9 && memcmp(name, "back_text", 9) == 0 && tag == TAG_COMPOUND) {
      int found = 0;
      if (lc_sign_text_read_side_compound(b, out->back, &found) != LC_OK) return LC_ERR_TRUNCATED;
      if (found) out->has_sign = 1;
      continue;
    }
    if (lc_nbt_skip_payload(b, tag) != LC_OK) return LC_ERR_TRUNCATED;
  }
}
/* Good for: Parse sign block NBT into front/back text lines.
 * Callers: dump_json.c, summarize_raw_dir.c, test_packets.c.
 */

lc_status lc_sign_text_from_nbt(const uint8_t *data, size_t len, lc_sign_text *out) {
  if (!out) return LC_ERR_INVALID;
  lc_sign_text_clear(out);
  if (!data || len == 0) return LC_OK;

  lc_buf b;
  lc_buf_init(&b, data, len);

  size_t start = 0;
  /* Paper / relay: optional single byte before root compound tag. */
  if (len >= 2 && data[0] != TAG_COMPOUND && data[0] > 0 && data[0] < 16 && data[1] == TAG_COMPOUND) {
    start = 1;
  }

  b.off = start;
  if (lc_sign_text_walk_fields(&b, out) == LC_OK && out->has_sign) return LC_OK;
  lc_sign_text_clear(out);

  if (data[start] == TAG_COMPOUND) {
    b.off = start + 1;
    if (lc_sign_text_walk_fields(&b, out) == LC_OK && out->has_sign) return LC_OK;
    lc_sign_text_clear(out);

    if (len >= start + 3 && data[start + 1] == 0 && data[start + 2] == 0) {
      b.off = start + 3;
      if (lc_sign_text_walk_fields(&b, out) == LC_OK && out->has_sign) return LC_OK;
      lc_sign_text_clear(out);
    }
  }

  return LC_OK;
}
/* Good for: Read or print Minecraft NBT embedded in packets.
 * Callers: debug.c, slot_fprint.c.
 */

int lc_nbt_fprint_wire(FILE *f, const char *indent, const uint8_t *data, size_t len) {
  if (!f) return -1;
  if (!data || len == 0) {
    if (indent) fputs(indent, f);
    fputs("(empty)\n", f);
    return 0;
  }
  lc_buf b;
  lc_buf_init(&b, data, len);
  uint8_t tag;
  if (lc_buf_read_u8(&b, &tag) != LC_OK) {
    if (indent) fputs(indent, f);
    fputs("<truncated: missing root tag>\n", f);
    return -1;
  }
  const char *ind = indent ? indent : "";
  if (tag == TAG_COMPOUND) {
    if (ind[0]) fputs(ind, f);
    fputs("compound {\n", f);
    if (lc_nbt_fprint_named_children(f, ind, &b, 0) != 0) return -1;
    if (ind[0]) fputs(ind, f);
    fputs("}\n", f);
    return 0;
  }
  if (ind[0]) fputs(ind, f);
  fprintf(f, "%s ", lc_nbt_tag_name(tag));
  return lc_nbt_fprint_payload(f, ind, &b, tag, 0);
}
