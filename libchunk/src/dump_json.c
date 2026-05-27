#include "internal.h"

#include <stdio.h>
#include <string.h>

static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
/* Good for: Escape string for JSON output.
 * Callers: dump_json.c (same file).
 */

static void json_escape(FILE *f, const char *s) {
  fputc('"', f);
  if (!s) {
    fputc('"', f);
    return;
  }
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    if (*p == '"' || *p == '\\') fputc('\\', f);
    fputc((int)*p, f);
  }
  fputc('"', f);
}
/* Good for: Write base64-encoded field to JSON.
 * Callers: dump_json.c (same file).
 */

static int json_write_base64(FILE *f, const uint8_t *data, size_t len) {
  if (!len) {
    fputs("\"\"", f);
    return 0;
  }
  fputc('"', f);
  size_t i = 0;
  for (; i + 2 < len; i += 3) {
    uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | data[i + 2];
    fputc(B64[(n >> 18) & 63], f);
    fputc(B64[(n >> 12) & 63], f);
    fputc(B64[(n >> 6) & 63], f);
    fputc(B64[n & 63], f);
  }
  if (i < len) {
    uint32_t n = (uint32_t)data[i] << 16;
    fputc(B64[(n >> 18) & 63], f);
    if (i + 1 < len) {
      n |= (uint32_t)data[i + 1] << 8;
      fputc(B64[(n >> 12) & 63], f);
      fputc(B64[(n >> 6) & 63], f);
      fputc('=', f);
    } else {
      fputc(B64[(n >> 12) & 63], f);
      fputc('=', f);
      fputc('=', f);
    }
  }
  fputc('"', f);
  return 0;
}
/* Good for: Write byte buffer as JSON object with base64.
 * Callers: dump_json.c (same file).
 */

static void json_buffer_object(FILE *f, const char *indent, const lc_byte_buf *b) {
  fprintf(f, "%s{\n", indent);
  fprintf(f, "%s  \"_type\": \"Buffer\",\n", indent);
  fprintf(f, "%s  \"encoding\": \"base64\",\n", indent);
  fprintf(f, "%s  \"length\": %zu,\n", indent, b->len);
  fprintf(f, "%s  \"data\": ", indent);
  json_write_base64(f, b->data, b->len);
  fputc('\n', f);
  fprintf(f, "%s}", indent);
}
/* Good for: Write long array as JSON.
 * Callers: dump_json.c (same file).
 */

static void json_i64_array(FILE *f, const lc_i64_arr *a) {
  fputc('[', f);
  for (size_t i = 0; i < a->count; i++) {
    if (i) fputc(',', f);
    fprintf(f, "%lld", (long long)a->values[i]);
  }
  fputc(']', f);
}
/* Good for: Write light grid as JSON.
 * Callers: dump_json.c (same file).
 */

static void json_u8_grid(FILE *f, const lc_u8_grid *g) {
  fputc('[', f);
  for (size_t r = 0; r < g->row_count; r++) {
    if (r) fputc(',', f);
    fputc('[', f);
    for (size_t c = 0; c < g->row_lens[r]; c++) {
      if (c) fputc(',', f);
      fprintf(f, "%u", (unsigned)g->rows[r][c]);
    }
    fputc(']', f);
  }
  fputc(']', f);
}
/* Good for: Write heightmaps as JSON.
 * Callers: dump_json.c (same file).
 */

static void json_heightmaps(FILE *f, const lc_heightmap_arr *hm) {
  fputc('[', f);
  for (size_t i = 0; i < hm->count; i++) {
    if (i) fputc(',', f);
    const lc_heightmap *h = &hm->items[i];
    fputs("\n    {\n      \"typeId\": ", f);
    fprintf(f, "%d,\n      \"type\": ", h->type_id);
    json_escape(f, h->type_name ? h->type_name : "");
    fputs(",\n      \"data\": ", f);
    json_i64_array(f, &h->data);
    fputs("\n    }", f);
  }
  fputc('\n', f);
  fputc(']', f);
}
/* Good for: Write sign text fields as JSON.
 * Callers: dump_json.c (same file).
 */

static void json_sign_object(FILE *f, const lc_sign_text *sign) {
  fputs(",\n      \"sign\": {\n        \"front\": [", f);
  for (int i = 0; i < LC_SIGN_LINES; i++) {
    if (i) fputc(',', f);
    json_escape(f, sign->front[i] ? sign->front[i] : "");
  }
  fputs("],\n        \"back\": [", f);
  for (int i = 0; i < LC_SIGN_LINES; i++) {
    if (i) fputc(',', f);
    json_escape(f, sign->back[i] ? sign->back[i] : "");
  }
  fputs("]\n      }", f);
}

static void json_block_entities(FILE *f, const lc_block_entity_arr *be, int32_t chunk_x,
                                int32_t chunk_z) {
  fputc('[', f);
  for (size_t i = 0; i < be->count; i++) {
    if (i) fputc(',', f);
    const lc_block_entity *e = &be->items[i];
    const char *type_name = lc_block_entity_type_name(e->type);
    fprintf(f,
            "\n    {\n"
            "      \"chunkX\": %d,\n"
            "      \"chunkZ\": %d,\n"
            "      \"localX\": %u,\n"
            "      \"localZ\": %u,\n"
            "      \"worldX\": %d,\n"
            "      \"worldZ\": %d,\n"
            "      \"y\": %d,\n"
            "      \"type\": %d,\n"
            "      \"typeName\": ",
            chunk_x, chunk_z, (unsigned)e->x, (unsigned)e->z, chunk_x * 16 + (int)e->x,
            chunk_z * 16 + (int)e->z, (int)e->y, e->type);
    if (type_name) {
      json_escape(f, type_name);
    } else {
      fputs("null", f);
    }
    if (e->nbt.len) {
      fputs(",\n      \"nbt\": ", f);
      json_buffer_object(f, "      ", &e->nbt);
      lc_sign_text sign;
      memset(&sign, 0, sizeof sign);
      if (lc_sign_text_from_nbt(e->nbt.data, e->nbt.len, &sign) == LC_OK && sign.has_sign) {
        json_sign_object(f, &sign);
      }
      lc_sign_text_free(&sign);
    } else {
      fputs(",\n      \"nbt\": null", f);
    }
    fputs("\n    }", f);
  }
  fputc('\n', f);
  fputc(']', f);
}
/* Good for: Write decoded chunk sections as JSON.
 * Callers: dump_json.c (same file).
 */

static void json_sections(FILE *f, const lc_chunk *c) {
  fputc('[', f);
  for (size_t i = 0; i < c->section_count; i++) {
    if (i) fputc(',', f);
    const lc_chunk_section *sec = &c->sections[i];
    fprintf(f,
            "\n    {\n"
            "      \"sectionY\": %d,\n"
            "      \"solidBlockCount\": %d,\n"
            "      \"stateIds\": [",
            sec->section_y, (int)sec->solid_block_count);
    for (int j = 0; j < LC_BLOCK_VOLUME; j++) {
      if (j) fputc(',', f);
      if (j % 32 == 0) fputc('\n', f);
      fprintf(f, "%d", sec->state_ids[j]);
    }
    fputs("\n      ]\n    }", f);
  }
  fputc('\n', f);
  fputc(']', f);
}

static const char *lc_status_name(lc_status st) {
  switch (st) {
    case LC_OK:
      return "ok";
    case LC_ERR_TRUNCATED:
      return "truncated";
    case LC_ERR_INVALID:
      return "invalid";
    case LC_ERR_OOM:
      return "oom";
    default:
      return "unknown";
  }
}

int lc_map_chunk_dump_json(FILE *f, const char *file_basename, const uint8_t *wire,
                           size_t wire_len, const lc_map_chunk *mc) {
  if (!f || !mc) return -1;

  fputs("{\n", f);
  fputs("  \"name\": \"map_chunk\",\n", f);
  fputs("  \"file\": ", f);
  json_escape(f, file_basename ? file_basename : "");
  fputs(",\n", f);
  (void)wire;
  (void)wire_len;

  fputs("  \"params\": {\n", f);
  fprintf(f, "    \"x\": %d,\n", mc->x);
  fprintf(f, "    \"z\": %d,\n", mc->z);
  fputs("    \"heightmaps\": ", f);
  json_heightmaps(f, &mc->heightmaps);
  fputs(",\n", f);
  fputs("    \"blockEntities\": ", f);
  json_block_entities(f, &mc->block_entities, mc->x, mc->z);
  fputs(",\n", f);
  fputs("    \"skyLightMask\": ", f);
  json_i64_array(f, &mc->sky_light_mask);
  fputs(",\n", f);
  fputs("    \"blockLightMask\": ", f);
  json_i64_array(f, &mc->block_light_mask);
  fputs(",\n", f);
  fputs("    \"emptySkyLightMask\": ", f);
  json_i64_array(f, &mc->empty_sky_light_mask);
  fputs(",\n", f);
  fputs("    \"emptyBlockLightMask\": ", f);
  json_i64_array(f, &mc->empty_block_light_mask);
  fputs(",\n", f);
  fputs("    \"skyLight\": ", f);
  json_u8_grid(f, &mc->sky_light);
  fputs(",\n", f);
  fputs("    \"blockLight\": ", f);
  json_u8_grid(f, &mc->block_light);

  lc_chunk decoded;
  lc_chunk_init(&decoded);
  lc_status dst = lc_chunk_from_map_chunk(mc, &decoded);
  if (dst == LC_OK) {
    fputs(",\n    \"minY\": ", f);
    fprintf(f, "%d,\n", decoded.min_y);
    fputs("    \"worldHeight\": ", f);
    fprintf(f, "%d,\n", decoded.world_height);
    fputs("    \"sections\": ", f);
    json_sections(f, &decoded);
  } else {
    fprintf(f,
            ",\n    \"sectionsDecodeError\": \"%s\",\n"
            "    \"sections\": []",
            lc_status_name(dst));
  }
  lc_chunk_free(&decoded);

  fputs("\n  }\n}\n", f);
  return 0;
}
