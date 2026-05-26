#define _POSIX_C_SOURCE 200809L

/**
 * Batch-decode sniffer raw chunk captures (binary wire) with libchunk.
 * map_chunk → full JSON; other packets → one-line toString .txt.
 *
 * Input files: chunkLogDir/<server>/<ms>-<packet_name> [optional -<seq>]
 * Output files: <out_dir>/<same_basename>.txt (or .json for map_chunk full dump), and the
 * same file under <out_dir>/<packet_name>/ (temporary layout for browsing by type).
 *
 * Usage: decode_raw_dir <input_dir> <output_dir> [png_dir]
 */
#include "decode_wire.h" /* libchunk/include via -Iinclude */
#include "libchunk.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define LINE_MAX 8192
/** Worst case: full section (4096 blocks) × ~48 chars per change. */
#define MBC_LINE_MAX (128 + 4096 * 48)
#define WIRE_MAX (16 * 1024 * 1024)
#define LC_PATH_MAX 8192

static const char *PACKET_NAMES[] = {
    "multi_block_change",
    "initialize_world_border",
    "sync_entity_position",
    "entity_head_rotation",
    "entity_update_attributes",
    "entity_move_look",
    "entity_equipment",
    "entity_metadata",
    "rel_entity_move",
    "entity_destroy",
    "set_passengers",
    "entity_velocity",
    "registry_data",
    "block_change",
    "tile_entity_data",
    "update_light",
    "spawn_entity",
    "map_chunk",
    "unload_chunk",
    "position",
    "respawn",
    NULL,
};

static int parse_packet_name(const char *basename, char *name, size_t namesz) {
  const char *p = basename;
  while (*p >= '0' && *p <= '9') p++;
  if (*p != '-') return -1;
  p++;

  for (size_t i = 0; PACKET_NAMES[i]; i++) {
    const char *pkt = PACKET_NAMES[i];
    size_t nlen = strlen(pkt);
    if (strncmp(p, pkt, nlen) != 0) continue;
    if (p[nlen] == '\0') {
      snprintf(name, namesz, "%s", pkt);
      return 0;
    }
    if (p[nlen] == '-') {
      const char *q = p + nlen + 1;
      if (*q == '\0') return -1;
      for (; *q; q++) {
        if (*q < '0' || *q > '9') return -1;
      }
      snprintf(name, namesz, "%s", pkt);
      return 0;
    }
  }
  return -1;
}

/** snprintf with an explicit overflow check (silences -Wformat-truncation). */
static int lc_path_fmt(char *out, size_t outsz, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(out, outsz, fmt, ap);
  va_end(ap);
  if (n < 0 || (size_t)n >= outsz) return -1;
  return 0;
}

static int lc_path_out_file(char *out, size_t outsz, const char *out_dir, const char *basename,
                            const char *ext) {
  size_t d = strlen(out_dir), b = strlen(basename), e = strlen(ext);
  if (d + 1 + b + 1 + e + 1 > outsz) return -1;
  return lc_path_fmt(out, outsz, "%s/%s.%s", out_dir, basename, ext);
}

static int lc_path_out_pkt_dir(char *out, size_t outsz, const char *out_dir, const char *pkt_name) {
  size_t d = strlen(out_dir), p = strlen(pkt_name);
  if (d + 1 + p + 1 > outsz) return -1;
  return lc_path_fmt(out, outsz, "%s/%s", out_dir, pkt_name);
}

static int lc_path_out_pkt_file(char *out, size_t outsz, const char *out_dir, const char *pkt_name,
                                const char *basename, const char *ext) {
  size_t d = strlen(out_dir), p = strlen(pkt_name), b = strlen(basename), e = strlen(ext);
  if (d + 1 + p + 1 + b + 1 + e + 1 > outsz) return -1;
  return lc_path_fmt(out, outsz, "%s/%s/%s.%s", out_dir, pkt_name, basename, ext);
}

static int mkdir_p(const char *dir) {
  char tmp[LC_PATH_MAX];
  if (lc_path_fmt(tmp, sizeof tmp, "%s", dir) != 0) return -1;
  size_t len = strlen(tmp);
  if (len == 0) return -1;
  if (tmp[len - 1] == '/') tmp[len - 1] = '\0';
  for (char *p = tmp + 1; *p; p++) {
    if (*p != '/') continue;
    *p = '\0';
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    *p = '/';
  }
  if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
  return 0;
}

static int write_text(const char *path, const char *text) {
  FILE *f = fopen(path, "wb");
  if (!f) return -1;
  size_t n = strlen(text);
  if (fwrite(text, 1, n, f) != n) {
    fclose(f);
    return -1;
  }
  return fclose(f) == 0 ? 0 : -1;
}

static int copy_file_path(const char *src, const char *dst) {
  FILE *in = fopen(src, "rb");
  if (!in) return -1;
  FILE *out = fopen(dst, "wb");
  if (!out) {
    fclose(in);
    return -1;
  }
  char buf[65536];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
    if (fwrite(buf, 1, n, out) != n) {
      fclose(in);
      fclose(out);
      return -1;
    }
  }
  int err = ferror(in) ? -1 : 0;
  if (fclose(in) != 0) err = -1;
  if (fclose(out) != 0) err = -1;
  return err;
}

/** Also write under <out_dir>/<pkt_name>/ (same basename + ext). */
static int mirror_to_pkt_subdir(const char *out_dir, const char *pkt_name, const char *basename,
                                const char *ext) {
  char flat[LC_PATH_MAX], subdir[LC_PATH_MAX], sub[LC_PATH_MAX];
  if (lc_path_out_file(flat, sizeof flat, out_dir, basename, ext) != 0) return -1;
  if (lc_path_out_pkt_dir(subdir, sizeof subdir, out_dir, pkt_name) != 0) return -1;
  if (lc_path_out_pkt_file(sub, sizeof sub, out_dir, pkt_name, basename, ext) != 0) return -1;
  if (mkdir_p(subdir) != 0) return -1;
  return copy_file_path(flat, sub);
}

static int write_text_dual(const char *out_dir, const char *pkt_name, const char *basename,
                           const char *ext, const char *text) {
  char flat[LC_PATH_MAX], subdir[LC_PATH_MAX], sub[LC_PATH_MAX];
  if (lc_path_out_file(flat, sizeof flat, out_dir, basename, ext) != 0) return -1;
  if (lc_path_out_pkt_dir(subdir, sizeof subdir, out_dir, pkt_name) != 0) return -1;
  if (lc_path_out_pkt_file(sub, sizeof sub, out_dir, pkt_name, basename, ext) != 0) return -1;
  if (mkdir_p(subdir) != 0) return -1;
  if (write_text(flat, text) != 0) return -1;
  if (write_text(sub, text) != 0) return -1;
  return 0;
}

static int write_parse_error(const char *out_dir, const char *pkt_name, const char *basename) {
  char msg[512];
  snprintf(msg, sizeof msg, "# %s (%s)\nparse error\n", basename, pkt_name);
  return write_text_dual(out_dir, pkt_name, basename, "err", msg);
}

typedef struct lc_png_coord_tracker {
  int32_t *wx;
  int32_t *wz;
  size_t count;
  size_t cap;
} lc_png_coord_tracker;

static void lc_png_coord_tracker_free(lc_png_coord_tracker *t) {
  free(t->wx);
  free(t->wz);
  t->wx = NULL;
  t->wz = NULL;
  t->count = t->cap = 0;
}

static int lc_png_coord_seen(const lc_png_coord_tracker *t, int32_t wx, int32_t wz) {
  for (size_t i = 0; i < t->count; i++) {
    if (t->wx[i] == wx && t->wz[i] == wz) return 1;
  }
  return 0;
}

static int lc_png_coord_tracker_add(lc_png_coord_tracker *t, int32_t wx, int32_t wz) {
  if (lc_png_coord_seen(t, wx, wz)) return 0;
  if (t->count == t->cap) {
    size_t ncap = t->cap ? t->cap * 2 : 64;
    int32_t *nwx = (int32_t *)realloc(t->wx, ncap * sizeof(int32_t));
    int32_t *nwz = (int32_t *)realloc(t->wz, ncap * sizeof(int32_t));
    if (!nwx || !nwz) {
      free(nwx);
      free(nwz);
      return -1;
    }
    t->wx = nwx;
    t->wz = nwz;
    t->cap = ncap;
  }
  t->wx[t->count] = wx;
  t->wz[t->count] = wz;
  t->count++;
  return 1;
}

/** Top-left world block coordinate (chunk corner × 16). */
static void lc_png_path_for_chunk(const char *png_dir, int32_t cx, int32_t cz, char *out,
                                  size_t outlen) {
  int32_t rx = cx >> 5;
  int32_t rz = cz >> 5;
  int32_t wx = cx * 16;
  int32_t wz = cz * 16;
  snprintf(out, outlen, "%s/rx%d/rz%d/cx%d/cz%d/x%d_z%d.png", png_dir, rx, rz, cx, cz, wx, wz);
}

/** @return 2 decoded, 1 skipped, 0 parse error, -1 I/O */
static int process_file(const char *in_path, const char *basename, const char *out_dir,
                        const char *png_dir, lc_png_coord_tracker *png_coords) {
  char pkt_name[128];
  if (parse_packet_name(basename, pkt_name, sizeof pkt_name) != 0) {
    fprintf(stderr, "skip (bad name): %s\n", basename);
    return 1;
  }
  if (!lc_packet_name_supported(pkt_name)) {
    fprintf(stderr, "skip (unsupported): %s (%s)\n", basename, pkt_name);
    return 1;
  }

  FILE *f = fopen(in_path, "rb");
  if (!f) {
    perror(in_path);
    return -1;
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0 || sz > (long)WIRE_MAX) {
    fprintf(stderr, "%s: invalid size %ld\n", in_path, sz);
    fclose(f);
    return -1;
  }
  uint8_t *wire = (uint8_t *)malloc((size_t)sz);
  if (!wire) {
    fclose(f);
    return -1;
  }
  if (fread(wire, 1, (size_t)sz, f) != (size_t)sz) {
    free(wire);
    fclose(f);
    return -1;
  }
  fclose(f);

  char out_path[4096];

  if (strcmp(pkt_name, "map_chunk") == 0) {
    const uint8_t *payload = wire;
    size_t payload_len = (size_t)sz;
    if (lc_skip_packet_id(wire, (size_t)sz, &payload, &payload_len) != LC_OK) {
      free(wire);
      if (write_parse_error(out_dir, pkt_name, basename) != 0) perror(out_dir);
      fprintf(stderr, "parse error: %s (%s)\n", basename, pkt_name);
      return 0;
    }

    lc_map_chunk mc;
    memset(&mc, 0, sizeof mc);
    lc_status pst = lc_parse_map_chunk(payload, payload_len, &mc);
    if (pst != LC_OK) {
      free(wire);
      if (write_parse_error(out_dir, pkt_name, basename) != 0) perror(out_dir);
      fprintf(stderr, "parse error: %s (%s)\n", basename, pkt_name);
      return 0;
    }

    snprintf(out_path, sizeof out_path, "%s/%s.json", out_dir, basename);
    FILE *outf = fopen(out_path, "w");
    if (!outf) {
      perror(out_path);
      lc_map_chunk_free(&mc);
      free(wire);
      return -1;
    }
    if (lc_map_chunk_dump_json(outf, basename, wire, (size_t)sz, &mc) != 0) {
      fclose(outf);
      lc_map_chunk_free(&mc);
      free(wire);
      return -1;
    }
    fclose(outf);
    if (mirror_to_pkt_subdir(out_dir, pkt_name, basename, "json") != 0) {
      perror(out_dir);
      lc_map_chunk_free(&mc);
      free(wire);
      return -1;
    }

    if (png_dir) {
      int32_t wx = mc.x * 16;
      int32_t wz = mc.z * 16;
      if (!lc_png_coord_seen(png_coords, wx, wz)) {
        if (lc_png_coord_tracker_add(png_coords, wx, wz) < 0) {
          fprintf(stderr, "png coord tracker oom\n");
          lc_map_chunk_free(&mc);
          free(wire);
          return -1;
        }
        lc_png_path_for_chunk(png_dir, mc.x, mc.z, out_path, sizeof out_path);
        {
          char dir[LC_PATH_MAX];
          snprintf(dir, sizeof dir, "%s", out_path);
          char *slash = strrchr(dir, '/');
          if (!slash) {
            fprintf(stderr, "png path error: %s\n", out_path);
            lc_map_chunk_free(&mc);
            free(wire);
            return -1;
          }
          *slash = '\0';
          if (mkdir_p(dir) != 0) {
            fprintf(stderr, "png mkdir error: %s\n", out_path);
            lc_map_chunk_free(&mc);
            free(wire);
            return -1;
          }
        }
        if (lc_map_chunk_write_top_png(&mc, out_path) != LC_OK) {
          fprintf(stderr, "png error: %s\n", out_path);
          lc_map_chunk_free(&mc);
          free(wire);
          return -1;
        }
      }
    }

    lc_map_chunk_free(&mc);
    free(wire);
    return 2;
  }

  if (strcmp(pkt_name, "registry_data") == 0) {
    const uint8_t *payload = wire;
    size_t payload_len = (size_t)sz;
    if (lc_skip_packet_id(wire, (size_t)sz, &payload, &payload_len) != LC_OK) {
      free(wire);
      if (write_parse_error(out_dir, pkt_name, basename) != 0) perror(out_dir);
      fprintf(stderr, "parse error: %s (%s)\n", basename, pkt_name);
      return 0;
    }

    lc_registry_data rd;
    memset(&rd, 0, sizeof rd);
    lc_status pst = lc_parse_registry_data(payload, payload_len, &rd);
    if (pst != LC_OK) {
      free(wire);
      if (write_parse_error(out_dir, pkt_name, basename) != 0) perror(out_dir);
      fprintf(stderr, "parse error: %s (%s)\n", basename, pkt_name);
      return 0;
    }

    snprintf(out_path, sizeof out_path, "%s/%s.txt", out_dir, basename);
    FILE *outf = fopen(out_path, "w");
    if (!outf) {
      perror(out_path);
      lc_registry_data_free(&rd);
      free(wire);
      return -1;
    }
    fprintf(outf, "# %s (%s)\n\n", basename, pkt_name);
    lc_registry_data_fprint(outf, &rd);
    fclose(outf);
    if (mirror_to_pkt_subdir(out_dir, pkt_name, basename, "txt") != 0) {
      perror(out_dir);
      lc_registry_data_free(&rd);
      free(wire);
      return -1;
    }
    lc_registry_data_free(&rd);
    free(wire);
    return 2;
  }

  if (strcmp(pkt_name, "tile_entity_data") == 0) {
    const uint8_t *payload = wire;
    size_t payload_len = (size_t)sz;
    if (lc_skip_packet_id(wire, (size_t)sz, &payload, &payload_len) != LC_OK) {
      free(wire);
      if (write_parse_error(out_dir, pkt_name, basename) != 0) perror(out_dir);
      fprintf(stderr, "parse error: %s (%s)\n", basename, pkt_name);
      return 0;
    }

    lc_tile_entity_data ted;
    memset(&ted, 0, sizeof ted);
    lc_status pst = lc_parse_tile_entity_data(payload, payload_len, &ted);
    if (pst != LC_OK) {
      free(wire);
      if (write_parse_error(out_dir, pkt_name, basename) != 0) perror(out_dir);
      fprintf(stderr, "parse error: %s (%s)\n", basename, pkt_name);
      return 0;
    }

    snprintf(out_path, sizeof out_path, "%s/%s.txt", out_dir, basename);
    FILE *outf = fopen(out_path, "w");
    if (!outf) {
      perror(out_path);
      lc_tile_entity_data_free(&ted);
      free(wire);
      return -1;
    }
    fprintf(outf, "# %s (%s)\n\n", basename, pkt_name);
    lc_tile_entity_data_fprint(outf, &ted);
    fclose(outf);
    if (mirror_to_pkt_subdir(out_dir, pkt_name, basename, "txt") != 0) {
      perror(out_dir);
      lc_tile_entity_data_free(&ted);
      free(wire);
      return -1;
    }
    lc_tile_entity_data_free(&ted);
    free(wire);
    return 2;
  }

  if (strcmp(pkt_name, "entity_update_attributes") == 0) {
    const uint8_t *payload = wire;
    size_t payload_len = (size_t)sz;
    if (lc_skip_packet_id(wire, (size_t)sz, &payload, &payload_len) != LC_OK) {
      free(wire);
      if (write_parse_error(out_dir, pkt_name, basename) != 0) perror(out_dir);
      fprintf(stderr, "parse error: %s (%s)\n", basename, pkt_name);
      return 0;
    }

    lc_entity_update_attributes eua;
    memset(&eua, 0, sizeof eua);
    lc_status pst = lc_parse_entity_update_attributes(payload, payload_len, &eua);
    if (pst != LC_OK) {
      free(wire);
      if (write_parse_error(out_dir, pkt_name, basename) != 0) perror(out_dir);
      fprintf(stderr, "parse error: %s (%s)\n", basename, pkt_name);
      return 0;
    }

    snprintf(out_path, sizeof out_path, "%s/%s.txt", out_dir, basename);
    FILE *outf = fopen(out_path, "w");
    if (!outf) {
      perror(out_path);
      lc_entity_update_attributes_free(&eua);
      free(wire);
      return -1;
    }
    fprintf(outf, "# %s (%s)\n\n", basename, pkt_name);
    lc_entity_update_attributes_fprint(outf, &eua);
    fclose(outf);
    if (mirror_to_pkt_subdir(out_dir, pkt_name, basename, "txt") != 0) {
      perror(out_dir);
      lc_entity_update_attributes_free(&eua);
      free(wire);
      return -1;
    }
    lc_entity_update_attributes_free(&eua);
    free(wire);
    return 2;
  }

  if (strcmp(pkt_name, "update_light") == 0) {
    const uint8_t *payload = wire;
    size_t payload_len = (size_t)sz;
    if (lc_skip_packet_id(wire, (size_t)sz, &payload, &payload_len) != LC_OK) {
      free(wire);
      if (write_parse_error(out_dir, pkt_name, basename) != 0) perror(out_dir);
      fprintf(stderr, "parse error: %s (%s)\n", basename, pkt_name);
      return 0;
    }

    lc_update_light ul;
    memset(&ul, 0, sizeof ul);
    lc_status pst = lc_parse_update_light(payload, payload_len, &ul);
    if (pst != LC_OK) {
      free(wire);
      if (write_parse_error(out_dir, pkt_name, basename) != 0) perror(out_dir);
      fprintf(stderr, "parse error: %s (%s)\n", basename, pkt_name);
      return 0;
    }

    snprintf(out_path, sizeof out_path, "%s/%s.txt", out_dir, basename);
    FILE *outf = fopen(out_path, "w");
    if (!outf) {
      perror(out_path);
      lc_update_light_free(&ul);
      free(wire);
      return -1;
    }
    fprintf(outf, "# %s (%s)\n\n", basename, pkt_name);
    lc_update_light_fprint(outf, &ul);
    fclose(outf);
    if (mirror_to_pkt_subdir(out_dir, pkt_name, basename, "txt") != 0) {
      perror(out_dir);
      lc_update_light_free(&ul);
      free(wire);
      return -1;
    }
    lc_update_light_free(&ul);
    free(wire);
    return 2;
  }

  if (strcmp(pkt_name, "entity_equipment") == 0) {
    const uint8_t *payload = wire;
    size_t payload_len = (size_t)sz;
    if (lc_skip_packet_id(wire, (size_t)sz, &payload, &payload_len) != LC_OK) {
      free(wire);
      if (write_parse_error(out_dir, pkt_name, basename) != 0) perror(out_dir);
      fprintf(stderr, "parse error: %s (%s)\n", basename, pkt_name);
      return 0;
    }

    lc_entity_equipment ee;
    memset(&ee, 0, sizeof ee);
    lc_status pst = lc_parse_entity_equipment(payload, payload_len, &ee);
    if (pst != LC_OK) {
      free(wire);
      if (write_parse_error(out_dir, pkt_name, basename) != 0) perror(out_dir);
      fprintf(stderr, "parse error: %s (%s)\n", basename, pkt_name);
      return 0;
    }

    snprintf(out_path, sizeof out_path, "%s/%s.txt", out_dir, basename);
    FILE *outf = fopen(out_path, "w");
    if (!outf) {
      perror(out_path);
      lc_entity_equipment_free(&ee);
      free(wire);
      return -1;
    }
    fprintf(outf, "# %s (%s)\n\n", basename, pkt_name);
    lc_entity_equipment_fprint(outf, &ee);
    fclose(outf);
    if (mirror_to_pkt_subdir(out_dir, pkt_name, basename, "txt") != 0) {
      perror(out_dir);
      lc_entity_equipment_free(&ee);
      free(wire);
      return -1;
    }
    lc_entity_equipment_free(&ee);
    free(wire);
    return 2;
  }

  const int is_mbc = strcmp(pkt_name, "multi_block_change") == 0;
  size_t line_cap = is_mbc ? MBC_LINE_MAX : LINE_MAX;
  char *line = is_mbc ? (char *)malloc(line_cap) : NULL;
  char line_stack[LINE_MAX];
  if (!line) line = line_stack;
  int rc = lc_decode_wire_to_string(pkt_name, wire, (size_t)sz, line, line_cap);
  free(wire);

  if (rc < 0) {
    if (line != line_stack) free(line);
    if (write_parse_error(out_dir, pkt_name, basename) != 0) {
      perror(out_dir);
      return -1;
    }
    fprintf(stderr, "parse error: %s (%s)\n", basename, pkt_name);
    return 0;
  }

  size_t body_cap = line_cap + 256;
  char *body = (char *)malloc(body_cap);
  if (!body) {
    if (line != line_stack) free(line);
    return -1;
  }
  snprintf(body, body_cap, "# %s (%s)\n%s\n", basename, pkt_name, line);
  if (line != line_stack) free(line);
  if (write_text_dual(out_dir, pkt_name, basename, "txt", body) != 0) {
    free(body);
    perror(out_dir);
    return -1;
  }
  free(body);
  return 2;
}

static int cmp_str(const void *a, const void *b) {
  return strcmp(*(const char *const *)a, *(const char *const *)b);
}

int main(int argc, char **argv) {
  if (argc < 3 || argc > 4) {
    fprintf(stderr, "Usage: %s <input_dir> <output_dir> [png_dir]\n", argv[0]);
    fprintf(stderr,
            "Decode sniffer raw wire files. map_chunk → JSON; optional png_dir → per-chunk "
            "top-surface PNGs.\n");
    return 1;
  }

  const char *in_dir = argv[1];
  const char *out_dir = argv[2];
  const char *png_dir = argc == 4 ? argv[3] : NULL;

  if (mkdir_p(out_dir) != 0) {
    perror(out_dir);
    return 1;
  }
  if (png_dir && mkdir_p(png_dir) != 0) {
    perror(png_dir);
    return 1;
  }

  DIR *d = opendir(in_dir);
  if (!d) {
    perror(in_dir);
    return 1;
  }

  char **names = NULL;
  size_t n = 0, cap = 0;
  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    if (ent->d_name[0] == '.') continue;
    if (n == cap) {
      cap = cap ? cap * 2 : 256;
      names = (char **)realloc(names, cap * sizeof(char *));
      if (!names) {
        closedir(d);
        return 1;
      }
    }
    names[n++] = strdup(ent->d_name);
  }
  closedir(d);
  qsort(names, n, sizeof(char *), cmp_str);

  lc_png_coord_tracker png_coords;
  memset(&png_coords, 0, sizeof png_coords);

  int decoded = 0, skipped = 0, parse_err = 0, io_err = 0;
  for (size_t i = 0; i < n; i++) {
    char in_path[4096];
    snprintf(in_path, sizeof in_path, "%s/%s", in_dir, names[i]);
    struct stat st;
    if (stat(in_path, &st) != 0 || !S_ISREG(st.st_mode)) {
      free(names[i]);
      continue;
    }
    int r = process_file(in_path, names[i], out_dir, png_dir, &png_coords);
    if (r == 2)
      decoded++;
    else if (r == 1)
      skipped++;
    else if (r == 0)
      parse_err++;
    else
      io_err++;
    free(names[i]);
  }
  free(names);
  lc_png_coord_tracker_free(&png_coords);

  fprintf(stderr, "decoded %d, skipped %d, parse errors %d, I/O errors %d -> %s", decoded,
          skipped, parse_err, io_err, out_dir);
  if (png_dir) fprintf(stderr, " (png -> %s)", png_dir);
  fputc('\n', stderr);
  return (parse_err || io_err) ? 1 : 0;
}
