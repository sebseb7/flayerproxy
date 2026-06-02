#define _POSIX_C_SOURCE 200809L

/**
 * Summarize sniffer raw map_chunk captures with libchunk.
 *
 * Counts blocks by global state id (sorted by count), block entities by type id,
 * per-type coordinate lists under <out>/block-entities/<type>/coordinates.txt,
 * and sign text with world coordinates.
 *
 * Usage: summarize_raw_dir <input_dir> <output_dir>
 */
#include "libchunk.h"
#include "map_colors.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define WIRE_MAX (16 * 1024 * 1024)
#define LC_PATH_MAX 8192
#define LC_STATE_HIST_SIZE (LC_STATE_MAP_MAX + 1)
#define LC_BE_TYPE_MAX 256

typedef struct lc_block_type_entry {
  char name[96];
  uint64_t count;
} lc_block_type_entry;

typedef struct lc_be_hist_entry {
  int32_t type_id;
  uint64_t count;
} lc_be_hist_entry;

typedef struct lc_sign_row {
  int32_t wx;
  int32_t wz;
  int16_t wy;
  char side[8];
  char *text;
} lc_sign_row;

typedef struct lc_sign_buf {
  lc_sign_row *rows;
  size_t count;
  size_t cap;
} lc_sign_buf;

typedef struct lc_be_coord_row {
  char *file;
  int32_t wx;
  int32_t wz;
  int16_t wy;
} lc_be_coord_row;

typedef struct lc_be_coord_buf {
  lc_be_coord_row *rows;
  size_t count;
  size_t cap;
} lc_be_coord_buf;

static uint64_t g_state_counts[LC_STATE_HIST_SIZE];
/** Topmost non-air block per column (same scan as map PNG surface). */
static uint64_t g_surface_counts[LC_STATE_HIST_SIZE];
/** Grass (state 8/9) with block directly above: vanilla snowy only when snow covers. */
static uint64_t g_grass_above_air[2];   /* [0]=sid8 [1]=sid9 */
static uint64_t g_grass_above_snow[2];
static uint64_t g_grass_above_other[2];
static uint64_t g_grass_surface_above_air[2];
static uint64_t g_grass_surface_above_snow[2];
static uint64_t g_be_counts[LC_BE_TYPE_MAX];
static lc_be_coord_buf g_be_coords[LC_BE_TYPE_MAX];

typedef struct lc_chunk_key {
  int32_t x;
  int32_t z;
} lc_chunk_key;

static lc_chunk_key *g_seen_chunks = NULL;
static size_t g_seen_count = 0;
static size_t g_seen_cap = 0;

static void lc_seen_chunks_free(void) {
  free(g_seen_chunks);
  g_seen_chunks = NULL;
  g_seen_count = g_seen_cap = 0;
}

static int lc_chunk_already_seen(int32_t x, int32_t z) {
  for (size_t i = 0; i < g_seen_count; i++) {
    if (g_seen_chunks[i].x == x && g_seen_chunks[i].z == z) return 1;
  }
  return 0;
}

static int lc_chunk_mark_seen(int32_t x, int32_t z) {
  if (g_seen_count == g_seen_cap) {
    size_t ncap = g_seen_cap ? g_seen_cap * 2 : 256;
    lc_chunk_key *n = (lc_chunk_key *)realloc(g_seen_chunks, ncap * sizeof(lc_chunk_key));
    if (!n) return -1;
    g_seen_chunks = n;
    g_seen_cap = ncap;
  }
  g_seen_chunks[g_seen_count].x = x;
  g_seen_chunks[g_seen_count].z = z;
  g_seen_count++;
  return 0;
}

/** chunk_stream_receiver: x<world>_z<world>[.<pkt>].wire (and x_y_z variants) */
static int parse_chunk_coord_wire_basename(const char *basename) {
  if (basename[0] != 'x') return 0;
  size_t len = strlen(basename);
  if (len < 8 || strcmp(basename + len - 5, ".wire") != 0) return 0;

  const char *coord_end = basename + len - 5;
  const char *pkt_dot = strrchr(basename, '.');
  if (pkt_dot && pkt_dot < coord_end && pkt_dot > basename) coord_end = pkt_dot;

  char *end = NULL;
  long wx = strtol(basename + 1, &end, 10);
  if (end == basename + 1 || end[0] != '_') return 0;
  if (end[1] == 'z') {
    const char *zstart = end + 2;
    long wz = strtol(zstart, &end, 10);
    if (end == zstart || end != coord_end) return 0;
    if ((wx % 16) != 0 || (wz % 16) != 0) return 0;
    return 1;
  }
  if (end[1] != 'y') return 0;
  const char *ystart = end + 2;
  long wy = strtol(ystart, &end, 10);
  if (end == ystart || end[0] != '_' || end[1] != 'z') return 0;
  (void)wy;
  const char *zstart = end + 2;
  (void)strtol(zstart, &end, 10);
  if (end == zstart || end != coord_end) return 0;
  return 1;
}

/** Legacy sniffer flat captures: <ms>-map_chunk[...] */
static int parse_map_chunk_basename(const char *basename) {
  if (parse_chunk_coord_wire_basename(basename)) return 1;
  const char *p = basename;
  while (*p >= '0' && *p <= '9') p++;
  if (*p != '-') return 0;
  p++;
  if (strncmp(p, "map_chunk", 9) != 0) return 0;
  if (p[9] == '\0') return 1;
  if (p[9] == '-') {
    for (p += 10; *p; p++) {
      if (*p < '0' || *p > '9') return 0;
    }
    return 1;
  }
  return 0;
}

/** snprintf into buf; fail if result would be truncated. */
static int lc_path_fmt(char *buf, size_t buflen, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, buflen, fmt, ap);
  va_end(ap);
  if (n < 0 || (size_t)n >= buflen) {
    fprintf(stderr, "path too long (need %d bytes, have %zu)\n", n, buflen);
    return -1;
  }
  return 0;
}

static int walk_map_chunk_wires(const char *dir, char ***paths, size_t *n, size_t *cap) {
  DIR *d = opendir(dir);
  if (!d) return -1;

  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    if (ent->d_name[0] == '.') continue;

    char path[LC_PATH_MAX];
    if (lc_path_fmt(path, sizeof path, "%s/%s", dir, ent->d_name) != 0) continue;

    struct stat st;
    if (stat(path, &st) != 0) continue;

    if (S_ISDIR(st.st_mode)) {
      if (walk_map_chunk_wires(path, paths, n, cap) < 0) {
        closedir(d);
        return -1;
      }
      continue;
    }
    if (!S_ISREG(st.st_mode)) continue;
    if (!parse_map_chunk_basename(ent->d_name)) continue;

    if (*n == *cap) {
      size_t ncap = *cap ? *cap * 2 : 256;
      char **next = (char **)realloc(*paths, ncap * sizeof(char *));
      if (!next) {
        closedir(d);
        return -1;
      }
      *paths = next;
      *cap = ncap;
    }
    (*paths)[(*n)++] = strdup(path);
  }
  closedir(d);
  return 0;
}

static int mkdir_p(const char *dir) {
  char tmp[LC_PATH_MAX];
  snprintf(tmp, sizeof tmp, "%s", dir);
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

static int sign_lines_nonempty(char *lines[LC_SIGN_LINES]) {
  for (int i = 0; i < LC_SIGN_LINES; i++) {
    if (lines[i] && lines[i][0]) return 1;
  }
  return 0;
}

static char *join_sign_lines(char *lines[LC_SIGN_LINES]) {
  size_t cap = 64;
  char *out = (char *)malloc(cap);
  if (!out) return NULL;
  out[0] = '\0';
  for (int i = 0; i < LC_SIGN_LINES; i++) {
    const char *line = lines[i] ? lines[i] : "";
    if (i) {
      size_t need = strlen(out) + 3;
      if (need + 1 > cap) {
        cap = need + 64;
        char *n = (char *)realloc(out, cap);
        if (!n) {
          free(out);
          return NULL;
        }
        out = n;
      }
      strcat(out, " | ");
    }
    size_t need = strlen(out) + strlen(line) + 1;
    if (need > cap) {
      cap = need + 64;
      char *n = (char *)realloc(out, cap);
      if (!n) {
        free(out);
        return NULL;
      }
      out = n;
    }
    strcat(out, line);
  }
  return out;
}

static int lc_sign_buf_push(lc_sign_buf *buf, int32_t wx, int16_t wy, int32_t wz, const char *side,
                            char *lines[LC_SIGN_LINES]) {
  if (!sign_lines_nonempty(lines)) return 0;
  char *text = join_sign_lines(lines);
  if (!text) return -1;
  if (buf->count == buf->cap) {
    size_t ncap = buf->cap ? buf->cap * 2 : 64;
    lc_sign_row *nrows = (lc_sign_row *)realloc(buf->rows, ncap * sizeof(lc_sign_row));
    if (!nrows) {
      free(text);
      return -1;
    }
    buf->rows = nrows;
    buf->cap = ncap;
  }
  lc_sign_row *r = &buf->rows[buf->count++];
  r->wx = wx;
  r->wy = wy;
  r->wz = wz;
  snprintf(r->side, sizeof r->side, "%s", side);
  r->text = text;
  return 0;
}

static void lc_sign_buf_free(lc_sign_buf *buf) {
  for (size_t i = 0; i < buf->count; i++) free(buf->rows[i].text);
  free(buf->rows);
  buf->rows = NULL;
  buf->count = buf->cap = 0;
}

static int lc_be_coord_buf_push(lc_be_coord_buf *buf, const char *file, int32_t wx, int16_t wy,
                                int32_t wz) {
  char *file_copy = strdup(file ? file : "");
  if (!file_copy) return -1;
  if (buf->count == buf->cap) {
    size_t ncap = buf->cap ? buf->cap * 2 : 32;
    lc_be_coord_row *nrows = (lc_be_coord_row *)realloc(buf->rows, ncap * sizeof(lc_be_coord_row));
    if (!nrows) {
      free(file_copy);
      return -1;
    }
    buf->rows = nrows;
    buf->cap = ncap;
  }
  lc_be_coord_row *r = &buf->rows[buf->count++];
  r->file = file_copy;
  r->wx = wx;
  r->wy = wy;
  r->wz = wz;
  return 0;
}

static void lc_be_coord_bufs_free(void) {
  for (int t = 0; t < LC_BE_TYPE_MAX; t++) {
    for (size_t i = 0; i < g_be_coords[t].count; i++) free(g_be_coords[t].rows[i].file);
    free(g_be_coords[t].rows);
    g_be_coords[t].rows = NULL;
    g_be_coords[t].count = g_be_coords[t].cap = 0;
  }
}

/** "minecraft:chest" -> "minecraft-chest" for directory names. */
static void lc_be_type_dirname(int32_t type_id, char *out, size_t outlen) {
  const char *name = lc_block_entity_type_name(type_id);
  if (!name || !name[0]) {
    snprintf(out, outlen, "type-%d-unknown", type_id);
    return;
  }
  snprintf(out, outlen, "%02d-", type_id);
  size_t off = strlen(out);
  for (const char *p = name; *p && off + 1 < outlen; p++) {
    char c = *p;
    if (c == ':')
      c = '-';
    else if (c < '0' || (c > '9' && c < 'A') || (c > 'Z' && c < 'a') || c > 'z')
      c = '_';
    out[off++] = c;
  }
  out[off] = '\0';
}

static int cmp_be_coord_row(const void *a, const void *b) {
  const lc_be_coord_row *ra = (const lc_be_coord_row *)a;
  const lc_be_coord_row *rb = (const lc_be_coord_row *)b;
  int fc = strcmp(ra->file ? ra->file : "", rb->file ? rb->file : "");
  if (fc != 0) return fc;
  if (ra->wx != rb->wx) return (ra->wx < rb->wx) ? -1 : 1;
  if (ra->wz != rb->wz) return (ra->wz < rb->wz) ? -1 : 1;
  if (ra->wy != rb->wy) return (ra->wy < rb->wy) ? -1 : 1;
  return 0;
}

static const lc_chunk_section *lc_find_section_sum(const lc_chunk *c, int32_t section_y) {
  for (size_t i = 0; i < c->section_count; i++) {
    if (c->sections[i].section_y == section_y) return &c->sections[i];
  }
  return NULL;
}

static int32_t lc_chunk_state_at_sum(const lc_chunk *c, int lx, int32_t wy, int lz) {
  int32_t sec_y = wy >> 4;
  const lc_chunk_section *sec = lc_find_section_sum(c, sec_y);
  if (!sec) return 0;
  int ly = wy - (sec_y << 4);
  if (ly < 0 || ly > 15) return 0;
  return sec->state_ids[(ly << 8) | (lz << 4) | lx];
}

static void accumulate_chunk_blocks(const lc_chunk *c) {
  for (size_t s = 0; s < c->section_count; s++) {
    const lc_chunk_section *sec = &c->sections[s];
    for (int i = 0; i < LC_BLOCK_VOLUME; i++) {
      int32_t sid = sec->state_ids[i];
      if (sid < 0 || sid > LC_STATE_MAP_MAX) continue;
      g_state_counts[(size_t)sid]++;
    }
  }
}

static int lc_state_is_grass(int32_t sid) { return sid == 8 || sid == 9; }

static int lc_grass_snowy_index(int32_t sid) { return sid == 9 ? 1 : 0; }

/** snow layer, snow_block, powder_snow (vanilla snowy grass triggers). */
static int lc_state_is_snow_cover(int32_t sid) {
  const char *n = lc_state_id_block_name(sid);
  if (!n || n[0] == '?') return 0;
  return strncmp(n, "minecraft:snow", 14) == 0 || strncmp(n, "minecraft:powder_snow", 21) == 0;
}

static void lc_grass_audit_above(const lc_chunk *c, int lx, int32_t wy, int lz, int32_t sid,
                                 uint64_t *air, uint64_t *snow, uint64_t *other) {
  int gi = lc_grass_snowy_index(sid);
  int32_t above = lc_chunk_state_at_sum(c, lx, wy + 1, lz);
  if (above == 0)
    air[gi]++;
  else if (lc_state_is_snow_cover(above))
    snow[gi]++;
  else
    other[gi]++;
}

static void accumulate_chunk_grass_snowy_audit(const lc_chunk *c) {
  for (size_t s = 0; s < c->section_count; s++) {
    const lc_chunk_section *sec = &c->sections[s];
    int32_t base_y = sec->section_y << 4;
    for (int i = 0; i < LC_BLOCK_VOLUME; i++) {
      int32_t sid = sec->state_ids[i];
      if (!lc_state_is_grass(sid)) continue;
      int ly = i >> 8;
      int lz = (i >> 4) & 15;
      int lx = i & 15;
      int32_t wy = base_y + ly;
      lc_grass_audit_above(c, lx, wy, lz, sid, g_grass_above_air, g_grass_above_snow,
                           g_grass_above_other);
    }
  }
}

static void accumulate_chunk_surface(const lc_chunk *c) {
  int32_t y_max = c->min_y + c->world_height - 1;
  for (int lz = 0; lz < 16; lz++) {
    for (int lx = 0; lx < 16; lx++) {
      int32_t top_sid = 0;
      int32_t top_wy = 0;
      for (int32_t wy = y_max; wy >= c->min_y; wy--) {
        int32_t sid = lc_chunk_state_at_sum(c, lx, wy, lz);
        if (sid != 0) {
          top_sid = sid;
          top_wy = wy;
          if (sid >= 0 && sid <= LC_STATE_MAP_MAX) g_surface_counts[(size_t)sid]++;
          break;
        }
      }
      if (lc_state_is_grass(top_sid)) {
        static uint64_t surface_other[2];
        lc_grass_audit_above(c, lx, top_wy, lz, top_sid, g_grass_surface_above_air,
                             g_grass_surface_above_snow, surface_other);
      }
    }
  }
}

static void accumulate_block_entities(const lc_map_chunk *mc, const char *file, lc_sign_buf *signs) {
  for (size_t i = 0; i < mc->block_entities.count; i++) {
    const lc_block_entity *e = &mc->block_entities.items[i];
    int32_t wx = mc->x * 16 + (int)e->x;
    int32_t wz = mc->z * 16 + (int)e->z;

    if (e->type >= 0 && e->type < LC_BE_TYPE_MAX) {
      g_be_counts[(size_t)e->type]++;
      if (lc_be_coord_buf_push(&g_be_coords[e->type], file, wx, e->y, wz) != 0) return;
    }

    if (!e->nbt.len) continue;
    lc_sign_text st;
    memset(&st, 0, sizeof st);
    if (lc_sign_text_from_nbt(e->nbt.data, e->nbt.len, &st) != LC_OK || !st.has_sign) continue;

    if (lc_sign_buf_push(signs, wx, e->y, wz, "front", st.front) != 0) {
      lc_sign_text_free(&st);
      return;
    }
    if (lc_sign_buf_push(signs, wx, e->y, wz, "back", st.back) != 0) {
      lc_sign_text_free(&st);
      return;
    }
    lc_sign_text_free(&st);
  }
}

/** "minecraft:oak_stairs[half=bottom,...]" -> "minecraft:oak_stairs" */
static void lc_block_type_key(int32_t state_id, char *out, size_t outlen) {
  const char *full = lc_state_id_block_name(state_id);
  if (!full || !full[0]) {
    snprintf(out, outlen, "?");
    return;
  }
  size_t i = 0;
  for (; full[i] && full[i] != '[' && i + 1 < outlen; i++) out[i] = full[i];
  out[i] = '\0';
}

static int lc_block_type_add(lc_block_type_entry **arr, size_t *n, size_t *cap, const char *name,
                             uint64_t add) {
  for (size_t i = 0; i < *n; i++) {
    if (strcmp((*arr)[i].name, name) == 0) {
      (*arr)[i].count += add;
      return 0;
    }
  }
  if (*n == *cap) {
    size_t ncap = *cap ? *cap * 2 : 256;
    lc_block_type_entry *next =
        (lc_block_type_entry *)realloc(*arr, ncap * sizeof(lc_block_type_entry));
    if (!next) return -1;
    *arr = next;
    *cap = ncap;
  }
  lc_block_type_entry *e = &(*arr)[(*n)++];
  snprintf(e->name, sizeof e->name, "%s", name);
  e->count = add;
  return 0;
}

static int cmp_block_type_desc(const void *a, const void *b) {
  const lc_block_type_entry *ea = (const lc_block_type_entry *)a;
  const lc_block_type_entry *eb = (const lc_block_type_entry *)b;
  if (ea->count != eb->count) return (ea->count > eb->count) ? -1 : 1;
  return strcmp(ea->name, eb->name);
}

static int cmp_be_hist_desc(const void *a, const void *b) {
  const lc_be_hist_entry *ea = (const lc_be_hist_entry *)a;
  const lc_be_hist_entry *eb = (const lc_be_hist_entry *)b;
  if (ea->count != eb->count) return (ea->count > eb->count) ? -1 : 1;
  return (ea->type_id < eb->type_id) ? -1 : (ea->type_id > eb->type_id);
}

static int cmp_sign_row(const void *a, const void *b) {
  const lc_sign_row *ra = (const lc_sign_row *)a;
  const lc_sign_row *rb = (const lc_sign_row *)b;
  if (ra->wx != rb->wx) return (ra->wx < rb->wx) ? -1 : 1;
  if (ra->wz != rb->wz) return (ra->wz < rb->wz) ? -1 : 1;
  if (ra->wy != rb->wy) return (ra->wy < rb->wy) ? -1 : 1;
  return strcmp(ra->side, rb->side);
}

static int write_state_histogram(const uint64_t *counts, const char *path, const char *comment) {
  lc_block_type_entry *entries = NULL;
  size_t n = 0, cap = 0;
  char key[96];

  for (int32_t sid = 0; sid <= LC_STATE_MAP_MAX; sid++) {
    uint64_t c = counts[(size_t)sid];
    if (!c) continue;
    lc_block_type_key(sid, key, sizeof key);
    if (lc_block_type_add(&entries, &n, &cap, key, c) != 0) {
      free(entries);
      return -1;
    }
  }
  qsort(entries, n, sizeof(lc_block_type_entry), cmp_block_type_desc);

  FILE *f = fopen(path, "w");
  if (!f) {
    free(entries);
    return -1;
  }
  fprintf(f, "# %s\n", comment);
  fputs("# blockName\tcount\n", f);
  uint64_t total = 0;
  for (size_t i = 0; i < n; i++) {
    total += entries[i].count;
    fprintf(f, "%s\t%llu\n", entries[i].name, (unsigned long long)entries[i].count);
  }
  fclose(f);
  free(entries);
  fprintf(stderr, "%s: %llu columns, %zu types -> %s\n", comment, (unsigned long long)total, n,
          path);
  return 0;
}

static int write_blocks_histogram(const char *path) {
  return write_state_histogram(
      g_state_counts, path,
      "minecraft-data 1.21.10; volume (all blocks in sections); grouped by block type");
}

static uint64_t surface_count_type(const char *block_type) {
  char key[96];
  uint64_t sum = 0;
  for (int32_t sid = 0; sid <= LC_STATE_MAP_MAX; sid++) {
    lc_block_type_key(sid, key, sizeof key);
    if (strcmp(key, block_type) != 0) continue;
    sum += g_surface_counts[(size_t)sid];
  }
  return sum;
}

static int write_be_histogram(const char *path) {
  lc_be_hist_entry *entries = NULL;
  size_t n = 0;
  for (int32_t tid = 0; tid < LC_BE_TYPE_MAX; tid++) {
    if (g_be_counts[(size_t)tid] == 0) continue;
    entries = (lc_be_hist_entry *)realloc(entries, (n + 1) * sizeof(lc_be_hist_entry));
    if (!entries) return -1;
    entries[n].type_id = tid;
    entries[n].count = g_be_counts[(size_t)tid];
    n++;
  }
  qsort(entries, n, sizeof(lc_be_hist_entry), cmp_be_hist_desc);

  FILE *f = fopen(path, "w");
  if (!f) {
    free(entries);
    return -1;
  }
  fputs("# typeId\ttypeName\tcount\n", f);
  uint64_t total = 0;
  for (size_t i = 0; i < n; i++) {
    int32_t tid = entries[i].type_id;
    uint64_t c = entries[i].count;
    total += c;
    const char *name = lc_block_entity_type_name(tid);
    fprintf(f, "%d\t%s\t%llu\n", tid, name ? name : "?", (unsigned long long)c);
  }
  fclose(f);
  free(entries);
  fprintf(stderr, "block_entities: %llu -> %s\n", (unsigned long long)total, path);
  return 0;
}

static int write_be_coord_dirs(const char *out_dir) {
  char root[LC_PATH_MAX];
  if (lc_path_fmt(root, sizeof root, "%s/block-entities", out_dir) != 0) return -1;
  if (mkdir_p(root) != 0) return -1;

  size_t dirs = 0;
  for (int32_t tid = 0; tid < LC_BE_TYPE_MAX; tid++) {
    lc_be_coord_buf *buf = &g_be_coords[tid];
    if (!g_be_counts[tid] || !buf->count) continue;

    char subdir[256];
    lc_be_type_dirname(tid, subdir, sizeof subdir);

    char dirpath[LC_PATH_MAX];
    if (lc_path_fmt(dirpath, sizeof dirpath, "%s/block-entities/%s", out_dir, subdir) != 0) return -1;
    if (mkdir_p(dirpath) != 0) return -1;

    qsort(buf->rows, buf->count, sizeof(lc_be_coord_row), cmp_be_coord_row);

    char coordpath[LC_PATH_MAX];
    if (lc_path_fmt(coordpath, sizeof coordpath, "%s/block-entities/%s/coordinates.txt", out_dir,
                    subdir) != 0)
      return -1;
    FILE *f = fopen(coordpath, "w");
    if (!f) return -1;

    const char *name = lc_block_entity_type_name(tid);
    fprintf(f, "# typeId=%d\ttypeName=%s\tcount=%zu\n", tid, name ? name : "?", buf->count);
    fputs("# file\tworldX\tworldY\tworldZ\n", f);
    for (size_t i = 0; i < buf->count; i++) {
      const lc_be_coord_row *r = &buf->rows[i];
      fprintf(f, "%s\t%d\t%d\t%d\n", r->file ? r->file : "", r->wx, (int)r->wy, r->wz);
    }
    fclose(f);
    dirs++;
  }

  fprintf(stderr, "block entity coord dirs: %zu -> %s/\n", dirs, root);
  return 0;
}

static int write_signs(const char *path, lc_sign_buf *buf) {
  qsort(buf->rows, buf->count, sizeof(lc_sign_row), cmp_sign_row);
  FILE *f = fopen(path, "w");
  if (!f) return -1;
  fputs("# worldX\tworldY\tworldZ\tside\ttext\n", f);
  for (size_t i = 0; i < buf->count; i++) {
    const lc_sign_row *r = &buf->rows[i];
    fprintf(f, "%d\t%d\t%d\t%s\t%s\n", r->wx, (int)r->wy, r->wz, r->side, r->text);
  }
  fclose(f);
  fprintf(stderr, "sign sides: %zu -> %s\n", buf->count, path);
  return 0;
}

/** @return 2 decoded, 1 skipped duplicate chunk, 0 not map_chunk, -1 I/O */
static int process_file(const char *path, lc_sign_buf *signs, int *decoded, int *skipped_dup,
                        int *parse_err) {
  const char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  if (!parse_map_chunk_basename(base)) return 0;

  FILE *f = fopen(path, "rb");
  if (!f) {
    perror(path);
    return -1;
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0 || sz > (long)WIRE_MAX) {
    fprintf(stderr, "%s: invalid size %ld\n", path, sz);
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

  const uint8_t *payload = wire;
  size_t payload_len = (size_t)sz;
  if (lc_skip_packet_id(wire, (size_t)sz, &payload, &payload_len) != LC_OK) {
    free(wire);
    (*parse_err)++;
    return 0;
  }

  lc_map_chunk mc;
  memset(&mc, 0, sizeof mc);
  if (lc_parse_map_chunk(payload, payload_len, &mc) != LC_OK) {
    free(wire);
    (*parse_err)++;
    return 0;
  }

  if (lc_chunk_already_seen(mc.x, mc.z)) {
    lc_map_chunk_free(&mc);
    free(wire);
    (*skipped_dup)++;
    return 1;
  }
  if (lc_chunk_mark_seen(mc.x, mc.z) != 0) {
    lc_map_chunk_free(&mc);
    free(wire);
    return -1;
  }

  lc_chunk chunk;
  lc_chunk_init(&chunk);
  if (lc_chunk_from_map_chunk(&mc, &chunk) == LC_OK) {
    accumulate_chunk_blocks(&chunk);
    accumulate_chunk_surface(&chunk);
    accumulate_chunk_grass_snowy_audit(&chunk);
  } else {
    fprintf(stderr, "sections decode failed: %s\n", base);
  }
  lc_chunk_free(&chunk);

  accumulate_block_entities(&mc, base, signs);
  lc_map_chunk_free(&mc);
  free(wire);
  (*decoded)++;
  return 0;
}

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <input_dir> <output_dir>\n", argv[0]);
    fprintf(stderr,
            "Summarize raw map_chunk wire: block counts by stateId, block entities by type, "
            "sign text.\n");
    return 1;
  }

  const char *in_dir = argv[1];
  const char *out_dir = argv[2];
  if (mkdir_p(out_dir) != 0) {
    perror(out_dir);
    return 1;
  }

  memset(g_state_counts, 0, sizeof g_state_counts);
  memset(g_be_counts, 0, sizeof g_be_counts);

  lc_sign_buf signs;
  memset(&signs, 0, sizeof signs);

  char **paths = NULL;
  size_t n = 0, cap = 0;
  if (walk_map_chunk_wires(in_dir, &paths, &n, &cap) != 0) {
    perror(in_dir);
    return 1;
  }

  int decoded = 0, skipped_dup = 0, parse_err = 0, io_err = 0;
  for (size_t i = 0; i < n; i++) {
    int r = process_file(paths[i], &signs, &decoded, &skipped_dup, &parse_err);
    if (r < 0) io_err = 1;
    free(paths[i]);
  }
  free(paths);
  lc_seen_chunks_free();

  char blocks_path[LC_PATH_MAX], be_path[LC_PATH_MAX], signs_path[LC_PATH_MAX];
  if (lc_path_fmt(blocks_path, sizeof blocks_path, "%s/blocks-by-type.txt", out_dir) != 0)
    io_err = 1;
  if (lc_path_fmt(be_path, sizeof be_path, "%s/block-entities-by-type.txt", out_dir) != 0) io_err = 1;
  if (lc_path_fmt(signs_path, sizeof signs_path, "%s/signs.txt", out_dir) != 0) io_err = 1;

  if (write_blocks_histogram(blocks_path) != 0) io_err = 1;
  char surface_path[LC_PATH_MAX];
  if (lc_path_fmt(surface_path, sizeof surface_path, "%s/map-surface-by-type.txt", out_dir) != 0)
    io_err = 1;
  else if (write_state_histogram(g_surface_counts, surface_path,
                                 "map surface: topmost non-air per 16x16 column (map PNG logic)") !=
           0)
    io_err = 1;
  else {
    /* Wire ids 8/9: snowy bool is inverted vs old minecraft-data labels (see generate script). */
    uint64_t grass_snowy_false = g_surface_counts[9];
    uint64_t grass_snowy_true = g_surface_counts[8];
    uint64_t snow_layers = surface_count_type("minecraft:snow");
    uint64_t snow_block = surface_count_type("minecraft:snow_block");
    uint64_t powder_snow = surface_count_type("minecraft:powder_snow");
    fprintf(stderr,
            "map surface grass wire snowy=false %llu  snowy=true %llu\n",
            (unsigned long long)grass_snowy_false, (unsigned long long)grass_snowy_true);
    fprintf(stderr, "map surface snow layers %llu  snow_block %llu  powder_snow %llu\n",
            (unsigned long long)snow_layers, (unsigned long long)snow_block,
            (unsigned long long)powder_snow);
    fprintf(stderr,
            "surface grass + air above (vanilla expects snowy=false): wire8=%llu wire9=%llu\n",
            (unsigned long long)g_grass_surface_above_air[0],
            (unsigned long long)g_grass_surface_above_air[1]);
    fprintf(stderr,
            "surface grass + snow cover above (vanilla expects snowy=true): wire8=%llu wire9=%llu\n",
            (unsigned long long)g_grass_surface_above_snow[0],
            (unsigned long long)g_grass_surface_above_snow[1]);
    fprintf(stderr,
            "all grass + air above: wire8=%llu wire9=%llu  + snow above: wire8=%llu wire9=%llu  "
            "+ other above: wire8=%llu wire9=%llu\n",
            (unsigned long long)g_grass_above_air[0], (unsigned long long)g_grass_above_air[1],
            (unsigned long long)g_grass_above_snow[0], (unsigned long long)g_grass_above_snow[1],
            (unsigned long long)g_grass_above_other[0], (unsigned long long)g_grass_above_other[1]);
    fprintf(stderr,
            "wire label mismatch (if non-zero, snowy bool still wrong on id): air above but "
            "id8 %llu  |  snow above but id9 %llu\n",
            (unsigned long long)g_grass_above_air[0], (unsigned long long)g_grass_above_snow[1]);
  }
  if (write_be_histogram(be_path) != 0) io_err = 1;
  if (write_be_coord_dirs(out_dir) != 0) io_err = 1;
  if (write_signs(signs_path, &signs) != 0) io_err = 1;

  lc_sign_buf_free(&signs);
  lc_be_coord_bufs_free();

  fprintf(stderr, "chunks used: %d (latest per column), skipped older duplicates: %d, parse "
                  "errors: %d\n",
          decoded, skipped_dup, parse_err);
  return (parse_err || io_err) ? 1 : 0;
}
