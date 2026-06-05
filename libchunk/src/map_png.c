#include "internal.h"
#include "map_colors.h"

#include <stdio.h>
#include <string.h>

#define CHUNK_DIM 16
#define PNG_SCALE LC_MAP_CHUNK_PNG_SCALE
#define PNG_W LC_MAP_CHUNK_PNG_SIZE
#define PNG_H LC_MAP_CHUNK_PNG_SIZE
/* Good for: Section-local block index (same layout as chunk.c).
 * Callers: map_png.c (same file).
 */

static int lc_block_index_local(int lx, int ly, int lz) { return (ly << 8) | (lz << 4) | lx; }

static const lc_chunk_section *lc_find_section(const lc_chunk *c, int32_t section_y) {
  for (size_t i = 0; i < c->section_count; i++) {
    if (c->sections[i].section_y == section_y) return &c->sections[i];
  }
  return NULL;
}
/* Good for: Block state at column for map coloring.
 * Callers: map_png.c (same file).
 */

static int32_t lc_chunk_state_at(const lc_chunk *c, int lx, int32_t world_y, int lz) {
  int32_t sec_y = world_y >> 4;
  const lc_chunk_section *sec = lc_find_section(c, sec_y);
  if (!sec) return 0;
  int ly = world_y - (sec_y << 4);
  if (ly < 0 || ly > 15) return 0;
  return sec->state_ids[lc_block_index_local(lx, ly, lz)];
}

/** Shallow water tint (#3F76E4). Deep columns lerp toward a darker blue. */
/* Good for: Tint RGB for underwater column by depth.
 * Callers: map_png.c (same file).
 */
static void lc_water_overlay_rgb(int depth, uint8_t *r, uint8_t *g, uint8_t *b) {
  const uint8_t sr = 0x3f, sg = 0x76, sb = 0xe4;
  const uint8_t dr = 0x14, dg = 0x32, db = 0x5c;
  if (depth <= 2) {
    *r = sr;
    *g = sg;
    *b = sb;
    return;
  }
  int t = depth >= 18 ? 255 : ((depth - 2) * 255) / 16;
  *r = (uint8_t)((sr * (255 - t) + dr * t) / 255);
  *g = (uint8_t)((sg * (255 - t) + dg * t) / 255);
  *b = (uint8_t)((sb * (255 - t) + db * t) / 255);
}

/**
 * Water overlay alpha vs column depth. Shallow is fairly blue; depth ramps to a
 * near-opaque dark tint so deep ocean reads clearly darker than coastlines.
 */
/* Good for: Alpha for water depth shading on map.
 * Callers: map_png.c (same file).
 */
static uint8_t lc_water_depth_alpha(int depth) {
  if (depth <= 0) return 0;
  int alpha = 100 + (depth - 1) * 9;
  if (alpha > 220) alpha = 220;
  return (uint8_t)alpha;
}

static void lc_blend_rgb(uint8_t br, uint8_t bg, uint8_t bb, uint8_t wr, uint8_t wg, uint8_t wb,
                         uint8_t alpha, uint8_t *r, uint8_t *g, uint8_t *b) {
  int inv = 255 - (int)alpha;
  *r = (uint8_t)((br * inv + wr * alpha) / 255);
  *g = (uint8_t)((bg * inv + wg * alpha) / 255);
  *b = (uint8_t)((bb * inv + wb * alpha) / 255);
}

/**
 * Column color: top-face texture RGB from minecraft-assets. Water is skipped for the
 * bed; shallow water is mostly transparent so the block below shows through.
 *
 * Nether special case: instead of the topmost non-air block (always the bedrock ceiling),
 * find the topmost block that has at least 3 consecutive air blocks above it.  This skips
 * the bedrock roof and renders the actual nether terrain below.
 */
#define NETHER_MIN_AIR_ABOVE 3

static void lc_column_map_rgb(const lc_chunk *c, int lx, int lz, int32_t *out_sid,
                              int32_t *out_world_y, uint8_t *r, uint8_t *g, uint8_t *b) {
  int32_t y_max = c->min_y + c->world_height - 1;
  int32_t top_sid = 0;
  int32_t top_y = LC_MAP_SURFACE_NO_Y;
  int32_t bed_sid = 0;
  int water_depth = 0;

  int is_nether = (c->min_y == 0 && c->world_height == 128);

  if (is_nether) {
    int air_count = 0;
    for (int32_t wy = y_max; wy >= c->min_y; wy--) {
      int32_t sid = lc_chunk_state_at(c, lx, wy, lz);
      if (sid == 0) {
        air_count++;
      } else {
        if (air_count >= NETHER_MIN_AIR_ABOVE) {
          top_y = wy;
          top_sid = sid;
          bed_sid = sid;
          break;
        }
        air_count = 0;
      }
    }
  } else {
    for (int32_t wy = y_max; wy >= c->min_y; wy--) {
      int32_t sid = lc_chunk_state_at(c, lx, wy, lz);
      if (sid == 0) continue;
      if (top_y == LC_MAP_SURFACE_NO_Y) {
        top_y = wy;
        top_sid = sid;
      }
      if (lc_state_id_is_water(sid)) {
        water_depth++;
        continue;
      }
      bed_sid = sid;
      break;
    }
  }

  if (out_world_y) *out_world_y = top_y;
  if (out_sid) *out_sid = top_sid;

  if (water_depth > 0 && bed_sid != 0) {
    uint8_t br, bg, bb, wr, wg, wb;
    lc_state_id_top_rgb(bed_sid, &br, &bg, &bb);
    lc_water_overlay_rgb(water_depth, &wr, &wg, &wb);
    lc_blend_rgb(br, bg, bb, wr, wg, wb, lc_water_depth_alpha(water_depth), r, g, b);
    return;
  }
  if (water_depth > 0) {
    uint8_t wr, wg, wb;
    lc_water_overlay_rgb(water_depth, &wr, &wg, &wb);
    lc_blend_rgb(0x0c, 0x0c, 0x30, wr, wg, wb, lc_water_depth_alpha(water_depth), r, g, b);
    return;
  }
  if (top_sid != 0) {
    lc_state_id_top_rgb(top_sid, r, g, b);
    if (top_y != LC_MAP_SURFACE_NO_Y) {
      int32_t biome_id = lc_chunk_biome_at(c, lx, top_y, lz);
      lc_map_rgb_apply_biome_tint(top_sid, biome_id, r, g, b);
    }
    return;
  }
  *r = *g = *b = 0;
}
/* Good for: CRC32 for PNG chunk trailer.
 * Callers: map_png.c (same file).
 */

static uint32_t lc_crc32(const uint8_t *buf, size_t len) {
  static uint32_t table[256];
  static int init;
  if (!init) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t c = i;
      for (int k = 0; k < 8; k++) c = (c & 1) ? (0xedb88320u ^ (c >> 1)) : (c >> 1);
      table[i] = c;
    }
    init = 1;
  }
  uint32_t c = 0xffffffffu;
  for (size_t i = 0; i < len; i++) c = table[(c ^ buf[i]) & 0xff] ^ (c >> 8);
  return c ^ 0xffffffffu;
}
/* Good for: Write big-endian u32 to PNG file.
 * Callers: map_png.c (same file).
 */

static void lc_png_write_u32_be(FILE *f, uint32_t v) {
  uint8_t b[4] = {(uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v};
  fwrite(b, 1, 4, f);
}
/* Good for: Adler32 checksum for zlib payload.
 * Callers: map_png.c (same file).
 */

static uint32_t lc_adler32(const uint8_t *data, size_t len) {
  uint32_t a = 1, b = 0;
  for (size_t i = 0; i < len; i++) {
    a = (a + data[i]) % 65521u;
    b = (b + a) % 65521u;
  }
  return (b << 16) | a;
}

/** Zlib wrapper around deflate stored blocks (no external zlib). */
/* Good for: Wrap raw bytes in zlib stored blocks (no libz).
 * Callers: map_png.c (same file).
 */
static lc_status lc_zlib_store(const uint8_t *raw, size_t raw_len, uint8_t **out, size_t *out_len) {
  size_t blocks = raw_len ? 1 + raw_len / 65535 : 1;
  size_t cap = 2 + raw_len + blocks * 5 + 4;
  uint8_t *buf = (uint8_t *)malloc(cap);
  if (!buf) return LC_ERR_OOM;

  size_t pos = 0;
  buf[pos++] = 0x78;
  buf[pos++] = 0x01;

  size_t off = 0;
  while (off < raw_len || (raw_len == 0 && off == 0)) {
    size_t chunk = raw_len - off;
    if (chunk > 65535) chunk = 65535;
    int final = (off + chunk >= raw_len);
    buf[pos++] = (uint8_t)(final ? 1 : 0);
    buf[pos++] = (uint8_t)(chunk & 0xff);
    buf[pos++] = (uint8_t)((chunk >> 8) & 0xff);
    uint16_t nlen = (uint16_t)(~chunk);
    buf[pos++] = (uint8_t)(nlen & 0xff);
    buf[pos++] = (uint8_t)(nlen >> 8);
    if (chunk) {
      memcpy(buf + pos, raw + off, chunk);
      pos += chunk;
      off += chunk;
    } else {
      break;
    }
  }

  uint32_t adler = lc_adler32(raw, raw_len);
  buf[pos++] = (uint8_t)(adler >> 24);
  buf[pos++] = (uint8_t)(adler >> 16);
  buf[pos++] = (uint8_t)(adler >> 8);
  buf[pos++] = (uint8_t)adler;

  *out = buf;
  *out_len = pos;
  return LC_OK;
}

/** Inflate zlib stream produced by lc_zlib_store (CMF 0x78 FLG 0x01, stored blocks only). */
/* Good for: Inflate zlib stored blocks from PNG IDAT.
 * Callers: map_png.c (same file).
 */
static lc_status lc_zlib_unstore(const uint8_t *in, size_t in_len, uint8_t **raw, size_t *raw_len) {
  if (in_len < 6 || in[0] != 0x78) return LC_ERR_INVALID;
  size_t cap = 65536;
  size_t len = 0;
  uint8_t *out = (uint8_t *)malloc(cap);
  if (!out) return LC_ERR_OOM;

  size_t pos = 2;
  while (pos < in_len) {
    if (pos + 5 > in_len) {
      free(out);
      return LC_ERR_INVALID;
    }
    uint8_t hdr = in[pos++];
    int final = hdr & 1;
    uint16_t nlen = (uint16_t)in[pos] | ((uint16_t)in[pos + 1] << 8);
    pos += 2;
    uint16_t nlen_inv = (uint16_t)in[pos] | ((uint16_t)in[pos + 1] << 8);
    pos += 2;
    if ((uint16_t)~nlen != nlen_inv) {
      free(out);
      return LC_ERR_INVALID;
    }
    if (pos + nlen > in_len) {
      free(out);
      return LC_ERR_INVALID;
    }
    if (len + nlen > cap) {
      size_t ncap = cap;
      while (ncap < len + nlen) {
        if (ncap > SIZE_MAX / 2) {
          free(out);
          return LC_ERR_OOM;
        }
        ncap *= 2;
      }
      uint8_t *p = (uint8_t *)realloc(out, ncap);
      if (!p) {
        free(out);
        return LC_ERR_OOM;
      }
      out = p;
      cap = ncap;
    }
    if (nlen) memcpy(out + len, in + pos, nlen);
    len += nlen;
    pos += nlen;
    if (final) break;
  }
  if (pos + 4 > in_len) {
    free(out);
    return LC_ERR_INVALID;
  }
  uint32_t adler = lc_adler32(out, len);
  uint32_t wire = ((uint32_t)in[pos] << 24) | ((uint32_t)in[pos + 1] << 16) |
                  ((uint32_t)in[pos + 2] << 8) | (uint32_t)in[pos + 3];
  if (adler != wire) {
    free(out);
    return LC_ERR_INVALID;
  }
  *raw = out;
  *raw_len = len;
  return LC_OK;
}
/* Good for: Write one PNG chunk (length, type, data, CRC).
 * Callers: map_png.c (same file).
 */

static void lc_png_chunk_write(FILE *f, const char type[4], const uint8_t *data, size_t len) {
  lc_png_write_u32_be(f, (uint32_t)len);
  fwrite(type, 1, 4, f);
  if (len) fwrite(data, 1, len, f);
  uint8_t *buf = (uint8_t *)malloc(len + 4);
  if (!buf) return;
  memcpy(buf, type, 4);
  if (len) memcpy(buf + 4, data, len);
  uint32_t crc = lc_crc32(buf, len + 4);
  free(buf);
  lc_png_write_u32_be(f, crc);
}
/* Good for: Encode rgb png packet payload bytes for outbound wire (static server / templates).
 * Callers: map_png.c (same file).
 */

lc_status lc_write_rgb_png(const char *path, const uint8_t *rgb, int w, int h) {
  size_t raw_row = (size_t)(1 + w * 3);
  size_t raw_len = raw_row * (size_t)h;
  uint8_t *raw = (uint8_t *)malloc(raw_len);
  if (!raw) return LC_ERR_OOM;
  for (int y = 0; y < h; y++) {
    uint8_t *row = raw + (size_t)y * raw_row;
    row[0] = 0;
    memcpy(row + 1, rgb + (size_t)y * (size_t)w * 3, (size_t)w * 3);
  }

  uint8_t *comp = NULL;
  size_t comp_len = 0;
  lc_status zst = lc_zlib_store(raw, raw_len, &comp, &comp_len);
  free(raw);
  if (zst != LC_OK) return zst;

  FILE *f = fopen(path, "wb");
  if (!f) {
    free(comp);
    return LC_ERR_INVALID;
  }

  static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  fwrite(sig, 1, 8, f);

  uint8_t ihdr[13];
  ihdr[0] = (uint8_t)(w >> 24);
  ihdr[1] = (uint8_t)(w >> 16);
  ihdr[2] = (uint8_t)(w >> 8);
  ihdr[3] = (uint8_t)w;
  ihdr[4] = (uint8_t)(h >> 24);
  ihdr[5] = (uint8_t)(h >> 16);
  ihdr[6] = (uint8_t)(h >> 8);
  ihdr[7] = (uint8_t)h;
  ihdr[8] = 8;
  ihdr[9] = 2;
  ihdr[10] = 0;
  ihdr[11] = 0;
  ihdr[12] = 0;
  lc_png_chunk_write(f, "IHDR", ihdr, sizeof ihdr);
  lc_png_chunk_write(f, "IDAT", comp, comp_len);
  lc_png_chunk_write(f, "IEND", NULL, 0);

  fclose(f);
  free(comp);
  return LC_OK;
}
/* Good for: PNG/AVIF RGB buffer I/O for map tiles.
 * Callers: stitch_megatiles.c.
 */

lc_status lc_read_rgb_png(const char *path, uint8_t **rgb, int *w, int *h) {
  if (!path || !rgb || !w || !h) return LC_ERR_INVALID;

  FILE *f = fopen(path, "rb");
  if (!f) return LC_ERR_INVALID;

  uint8_t sig[8];
  if (fread(sig, 1, 8, f) != 8 || memcmp(sig, "\x89PNG\r\n\x1a\n", 8) != 0) {
    fclose(f);
    return LC_ERR_INVALID;
  }

  int img_w = 0, img_h = 0;
  uint8_t bit_depth = 0, color_type = 255;
  uint8_t *idat = NULL;
  size_t idat_len = 0, idat_cap = 0;

  while (1) {
    uint8_t len_b[4];
    if (fread(len_b, 1, 4, f) != 4) break;
    uint32_t clen = ((uint32_t)len_b[0] << 24) | ((uint32_t)len_b[1] << 16) |
                    ((uint32_t)len_b[2] << 8) | (uint32_t)len_b[3];
    char type[4];
    if (fread(type, 1, 4, f) != 4) break;

    if (strncmp(type, "IHDR", 4) == 0 && clen >= 13) {
      uint8_t ihdr[13];
      if (fread(ihdr, 1, 13, f) != 13) break;
      img_w = (int)(((uint32_t)ihdr[0] << 24) | ((uint32_t)ihdr[1] << 16) |
                    ((uint32_t)ihdr[2] << 8) | (uint32_t)ihdr[3]);
      img_h = (int)(((uint32_t)ihdr[4] << 24) | ((uint32_t)ihdr[5] << 16) |
                    ((uint32_t)ihdr[6] << 8) | (uint32_t)ihdr[7]);
      bit_depth = ihdr[8];
      color_type = ihdr[9];
      fseek(f, 4, SEEK_CUR);
    } else if (strncmp(type, "IDAT", 4) == 0) {
      if (clen) {
        if (idat_len + clen > idat_cap) {
          size_t ncap = idat_cap ? idat_cap : 4096;
          while (ncap < idat_len + clen) {
            if (ncap > SIZE_MAX / 2) {
              free(idat);
              fclose(f);
              return LC_ERR_OOM;
            }
            ncap *= 2;
          }
          uint8_t *p = (uint8_t *)realloc(idat, ncap);
          if (!p) {
            free(idat);
            fclose(f);
            return LC_ERR_OOM;
          }
          idat = p;
          idat_cap = ncap;
        }
        if (fread(idat + idat_len, 1, clen, f) != clen) break;
        idat_len += clen;
      }
      fseek(f, 4, SEEK_CUR);
    } else if (strncmp(type, "IEND", 4) == 0) {
      fseek(f, (long)clen + 4, SEEK_CUR);
      break;
    } else {
      fseek(f, (long)clen + 4, SEEK_CUR);
    }
  }
  fclose(f);

  if (!img_w || !img_h || bit_depth != 8 || color_type != 2 || !idat) {
    free(idat);
    return LC_ERR_INVALID;
  }

  uint8_t *raw = NULL;
  size_t raw_len = 0;
  lc_status zst = lc_zlib_unstore(idat, idat_len, &raw, &raw_len);
  free(idat);
  if (zst != LC_OK) return zst;

  size_t row_bytes = (size_t)(1 + img_w * 3);
  if (raw_len < row_bytes * (size_t)img_h) {
    free(raw);
    return LC_ERR_INVALID;
  }

  uint8_t *out = (uint8_t *)malloc((size_t)img_w * (size_t)img_h * 3);
  if (!out) {
    free(raw);
    return LC_ERR_OOM;
  }

  for (int y = 0; y < img_h; y++) {
    const uint8_t *row = raw + (size_t)y * row_bytes;
    if (row[0] != 0) {
      free(out);
      free(raw);
      return LC_ERR_INVALID;
    }
    memcpy(out + (size_t)y * (size_t)img_w * 3, row + 1, (size_t)img_w * 3);
  }
  free(raw);

  *rgb = out;
  *w = img_w;
  *h = img_h;
  return LC_OK;
}
/* Good for: Top-down map PNG / surface column export.
 * Callers: map_png.c (same file).
 */

lc_status lc_chunk_fill_map_surface(const lc_chunk *c, lc_map_surface_cell *out) {
  if (!c || !out) return LC_ERR_INVALID;

  int32_t wx0 = c->x * 16;
  int32_t wz0 = c->z * 16;
  size_t i = 0;
  for (int lz = 0; lz < CHUNK_DIM; lz++) {
    for (int lx = 0; lx < CHUNK_DIM; lx++) {
      lc_map_surface_cell *cell = &out[i++];
      int32_t wy = LC_MAP_SURFACE_NO_Y;
      int32_t sid = 0;
      uint8_t r, g, b;
      uint8_t pid;
      lc_column_map_rgb(c, lx, lz, &sid, &wy, &r, &g, &b);
      pid = lc_state_id_map_protocol(sid);
      cell->local_x = (uint8_t)lx;
      cell->local_z = (uint8_t)lz;
      cell->world_x = wx0 + lx;
      cell->world_z = wz0 + lz;
      cell->world_y = wy;
      cell->state_id = sid;
      cell->map_protocol_id = pid;
      cell->r = r;
      cell->g = g;
      cell->b = b;
    }
  }
  return LC_OK;
}
/* Good for: map_chunk packet helper: fill map surface.
 * Callers: map_png.c (same file).
 */

lc_status lc_map_chunk_fill_map_surface(const lc_map_chunk *mc, lc_map_surface_cell *out) {
  if (!mc || !out) return LC_ERR_INVALID;
  lc_chunk c;
  lc_chunk_init(&c);
  lc_status st = lc_chunk_from_map_chunk(mc, &c);
  if (st != LC_OK) {
    lc_chunk_free(&c);
    return st;
  }
  st = lc_chunk_fill_map_surface(&c, out);
  lc_chunk_free(&c);
  return st;
}
/* Good for: map_chunk packet helper: fprint map surface.
 * Callers: list_map_surface.c.
 */

int lc_map_chunk_fprint_map_surface(FILE *f, const lc_map_chunk *mc) {
  if (!f || !mc) return -1;

  lc_map_surface_cell cells[LC_MAP_SURFACE_COLUMNS];
  if (lc_map_chunk_fill_map_surface(mc, cells) != LC_OK) return -1;

  int32_t wx0 = mc->x * 16;
  int32_t wz0 = mc->z * 16;
  fprintf(f, "chunk (%d, %d)  world X %d..%d  Z %d..%d\n", mc->x, mc->z, wx0, wx0 + 15, wz0,
          wz0 + 15);
  fprintf(f, "map PNG: %dx%d px (%d px/block), top texture colors (water blends with depth)\n",
          LC_MAP_CHUNK_PNG_SIZE, LC_MAP_CHUNK_PNG_SIZE, PNG_SCALE);
  fputs("lx lz | worldX worldZ |   Y | stateId | mapId |   R   G   B\n", f);
  fputs("------+--------------+-----+---------+-------+------------\n", f);

  for (size_t i = 0; i < LC_MAP_SURFACE_COLUMNS; i++) {
    const lc_map_surface_cell *c = &cells[i];
    fprintf(f, "%2u %2u | %6d %6d | ", (unsigned)c->local_x, (unsigned)c->local_z, c->world_x,
            c->world_z);
    if (c->world_y == LC_MAP_SURFACE_NO_Y)
      fputs("  - ", f);
    else
      fprintf(f, "%4d", c->world_y);
    fprintf(f, " | %7d | %5u | %3u %3u %3u\n", c->state_id, (unsigned)c->map_protocol_id, c->r,
            c->g, c->b);
  }

  int32_t uniq_sid[LC_MAP_SURFACE_COLUMNS];
  int uniq_count[LC_MAP_SURFACE_COLUMNS];
  size_t n_uniq = 0;
  for (size_t i = 0; i < LC_MAP_SURFACE_COLUMNS; i++) {
    int32_t sid = cells[i].state_id;
    size_t u;
    for (u = 0; u < n_uniq; u++) {
      if (uniq_sid[u] == sid) break;
    }
    if (u == n_uniq) {
      uniq_sid[n_uniq] = sid;
      uniq_count[n_uniq] = 1;
      n_uniq++;
    } else {
      uniq_count[u]++;
    }
  }
  fputs("\nSummary by stateId:\n", f);
  for (size_t u = 0; u < n_uniq; u++) {
    int32_t sid = uniq_sid[u];
    uint8_t pid = lc_state_id_map_protocol(sid);
    uint8_t r, g, b;
    lc_state_id_top_rgb(sid, &r, &g, &b);
    fprintf(f, "  %3d columns  stateId=%d  mapId=%u  rgb=%u,%u,%u\n", uniq_count[u], sid,
            (unsigned)pid, r, g, b);
  }
  return 0;
}
/* Good for: Top-down map PNG / surface column export.
 * Callers: map_png.c (same file), map_tile.c.
 */

lc_status lc_chunk_render_top_rgb(const lc_chunk *c, uint8_t *rgb) {
  if (!c || !rgb) return LC_ERR_INVALID;

  for (int lz = 0; lz < CHUNK_DIM; lz++) {
    for (int lx = 0; lx < CHUNK_DIM; lx++) {
      int32_t sid = 0;
      uint8_t r, g, b;
      lc_column_map_rgb(c, lx, lz, &sid, NULL, &r, &g, &b);
      for (int sy = 0; sy < PNG_SCALE; sy++) {
        for (int sx = 0; sx < PNG_SCALE; sx++) {
          int px_x = lx * PNG_SCALE + sx;
          int px_y = lz * PNG_SCALE + sy;
          size_t off = ((size_t)px_y * (size_t)PNG_W + (size_t)px_x) * 3;
          rgb[off] = r;
          rgb[off + 1] = g;
          rgb[off + 2] = b;
        }
      }
    }
  }
  return LC_OK;
}
/* Good for: Top-down map PNG / surface column export.
 * Callers: map_png.c (same file).
 */

lc_status lc_chunk_write_top_png(const lc_chunk *c, const char *path) {
  if (!c || !path) return LC_ERR_INVALID;

  size_t px = (size_t)PNG_W * (size_t)PNG_H * 3;
  uint8_t *rgb = (uint8_t *)calloc(px, 1);
  if (!rgb) return LC_ERR_OOM;

  lc_status st = lc_chunk_render_top_rgb(c, rgb);
  if (st == LC_OK) st = lc_write_rgb_png(path, rgb, PNG_W, PNG_H);
  free(rgb);
  return st;
}
/* Good for: map_chunk packet helper: write top png.
 * Callers: chunk_stream_receiver.c, decode_raw_dir.c.
 */

lc_status lc_map_chunk_write_top_png(const lc_map_chunk *mc, const char *path,
                                     const char *dimension) {
  if (!mc || !path) return LC_ERR_INVALID;
  lc_chunk c;
  lc_chunk_init(&c);
  lc_status st = lc_chunk_from_map_chunk(mc, &c);
  if (st != LC_OK) {
    lc_chunk_free(&c);
    return st;
  }

  /* Fix up min_y / world_height based on the dimension string.
   * lc_chunk_from_map_chunk always uses Overworld defaults (-64 / 384), so
   * non-Overworld chunks have wrong section Y offsets and Y scan range.
   * We must also shift section_y values so lc_chunk_state_at() finds them. */
  if (dimension) {
    int32_t new_min_y = c.min_y;
    int32_t new_world_height = c.world_height;
    if (strstr(dimension, "nether")) {
      new_min_y = 0;
      new_world_height = 128;
    } else if (strstr(dimension, "the_end")) {
      new_min_y = 0;
      new_world_height = 256;
    }
    if (new_min_y != c.min_y || new_world_height != c.world_height) {
      int32_t shift = (new_min_y >> 4) - (c.min_y >> 4);
      for (size_t i = 0; i < c.section_count; i++) {
        c.sections[i].section_y += shift;
      }
      c.min_y = new_min_y;
      c.world_height = new_world_height;
    }
  }

  st = lc_chunk_write_top_png(&c, path);
  lc_chunk_free(&c);
  return st;
}
