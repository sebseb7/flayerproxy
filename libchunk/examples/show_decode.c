/**
 * Print libchunk toString() for sniffer decoded JSON dumps.
 * Usage: show_decode <decoded.json> [more.json ...]
 *    or: show_decode --dir <decoded-directory> [max=N]
 */
#include "decode_wire.h"
#include "libchunk.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_MAX 65536

static int decode_wire_b64(const char *b64, size_t b64_len, const char *name) {
  static const char b64tab[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  uint8_t rev[256];
  memset(rev, 0xff, sizeof rev);
  for (int i = 0; i < 64; i++) rev[(unsigned char)b64tab[i]] = (uint8_t)i;

  size_t cap = b64_len;
  uint8_t *raw = (uint8_t *)malloc(cap);
  if (!raw) return -1;
  size_t n = 0;
  int pad = 0;
  uint32_t acc = 0;
  int bits = 0;

  for (size_t i = 0; i < b64_len; i++) {
    char c = b64[i];
    if (c == '=') {
      pad++;
      continue;
    }
    if (c == '\n' || c == '\r' || c == ' ') continue;
    if (rev[(unsigned char)c] == 0xff) {
      free(raw);
      return -1;
    }
    acc = (acc << 6) | rev[(unsigned char)c];
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      raw[n++] = (uint8_t)((acc >> bits) & 0xff);
    }
  }

  char line[8192];
  int rc = lc_decode_wire_to_string(name, raw, n, line, sizeof line);
  free(raw);
  if (rc == 0) return 0;
  if (rc < 0) {
    fprintf(stderr, "%s: parse error (wire %zu bytes)\n", name, n);
    return -1;
  }
  printf("%s\n", line);
  return 0;
}

static const char *extract_json_string(const char *json, const char *key, char *buf, size_t bufsz) {
  char pat[64];
  snprintf(pat, sizeof pat, "\"%s\":", key);
  const char *p = strstr(json, pat);
  if (!p) return NULL;
  p += strlen(pat);
  while (*p == ' ' || *p == '\t') p++;
  if (*p != '"') return NULL;
  p++;
  size_t i = 0;
  while (*p && *p != '"' && i + 1 < bufsz) {
    if (*p == '\\' && p[1]) {
      p++;
    }
    buf[i++] = *p++;
  }
  buf[i] = '\0';
  return buf;
}

static int process_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    perror(path);
    return -1;
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0 || sz > 50 * 1024 * 1024) {
    fclose(f);
    return -1;
  }
  char *json = (char *)malloc((size_t)sz + 1);
  if (!json) {
    fclose(f);
    return -1;
  }
  if (fread(json, 1, (size_t)sz, f) != (size_t)sz) {
    free(json);
    fclose(f);
    return -1;
  }
  json[sz] = '\0';
  fclose(f);

  char name[128];
  char b64[LINE_MAX];
  if (!extract_json_string(json, "name", name, sizeof name)) {
    free(json);
    return -1;
  }
  const char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  printf("--- %s (%s) ---\n", base, name);

  const char *wp = strstr(json, "\"wire\"");
  if (wp && extract_json_string(wp, "data", b64, sizeof b64)) {
    free(json);
    return decode_wire_b64(b64, strlen(b64), name);
  }

  if (strcmp(name, "map_chunk") == 0) {
    char xs[32] = "?";
    char zs[32] = "?";
    const char *pp = strstr(json, "\"params\"");
    if (pp) {
      extract_json_string(pp, "x", xs, sizeof xs);
      extract_json_string(pp, "z", zs, sizeof zs);
    }
    size_t sections = 0;
    for (const char *sp = json; (sp = strstr(sp, "\"sectionY\"")) != NULL; sp++) sections++;
    free(json);
    printf("map_chunk{x=%s,z=%s,sections=%zu} (decoded JSON — use raw capture to re-parse wire)\n",
           xs, zs, sections);
    return 0;
  }

  free(json);
  fprintf(stderr, "%s: no wire field (only map_chunk JSON dumps are supported without wire)\n", path);
  return -1;
}

static int cmp_str(const void *a, const void *b) {
  return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int process_dir(const char *dir, int max_files) {
  DIR *d = opendir(dir);
  if (!d) {
    perror(dir);
    return -1;
  }
  char **paths = NULL;
  size_t n = 0, cap = 0;
  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    if (ent->d_name[0] == '.') continue;
    size_t len = strlen(ent->d_name);
    if (len < 6 || strcmp(ent->d_name + len - 5, ".json") != 0) continue;
    if (n >= (size_t)max_files && max_files > 0) continue;
    if (n == cap) {
      cap = cap ? cap * 2 : 256;
      paths = (char **)realloc(paths, cap * sizeof(char *));
    }
    size_t plen = strlen(dir) + len + 2;
    paths[n] = (char *)malloc(plen);
    snprintf(paths[n], plen, "%s/%s", dir, ent->d_name);
    n++;
  }
  closedir(d);
  qsort(paths, n, sizeof(char *), cmp_str);

  for (size_t i = 0; i < n; i++) {
    const char *supported =
        "map_chunk\0update_light\0block_change\0multi_block_change\0spawn_entity\0"
        "entity_metadata\0entity_equipment\0entity_destroy\0set_passengers\0"
        "rel_entity_move\0entity_move_look\0sync_entity_position\0entity_velocity\0"
        "entity_head_rotation\0position\0respawn\0initialize_world_border\0"
        "registry_data\0";
    int ok = 0;
    for (const char *p = supported; *p; p += strlen(p) + 1) {
      if (strstr(paths[i], p)) {
        ok = 1;
        break;
      }
    }
    if (!ok) {
      free(paths[i]);
      continue;
    }
    process_file(paths[i]);
  }
  free(paths);
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <file.json> ...\n", argv[0]);
    fprintf(stderr, "       %s --dir <decoded/> [max=N]\n", argv[0]);
    return 1;
  }
  if (strcmp(argv[1], "--dir") == 0 && argc >= 3) {
    int max = 40;
    if (argc >= 4 && strncmp(argv[3], "max=", 4) == 0) max = atoi(argv[3] + 4);
    return process_dir(argv[2], max);
  }
  int err = 0;
  for (int i = 1; i < argc; i++) {
    if (process_file(argv[i]) != 0) err = 1;
  }
  return err;
}
