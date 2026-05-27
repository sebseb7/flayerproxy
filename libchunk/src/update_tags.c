#include "libchunk.h"

#include "internal.h"

#include <stdlib.h>
#include <string.h>
/* Good for: Release heap owned by lc_tag group entry arr.
 * Callers: update_tags.c (same file).
 */

void lc_tag_group_entry_arr_free(lc_tag_group_entry *items, size_t count) {
  if (!items) return;
  for (size_t i = 0; i < count; i++) {
    free(items[i].name);
    free(items[i].ids);
  }
  free(items);
}
/* Good for: Release heap owned by lc_update tags.
 * Callers: mc_static_registries.c, update_tags.c (same file).
 */

void lc_update_tags_free(lc_update_tags *p) {
  if (!p) return;
  for (size_t i = 0; i < p->group_count; i++) {
    free(p->groups[i].registry_id);
    lc_tag_group_entry_arr_free(p->groups[i].tags, p->groups[i].tag_count);
  }
  free(p->groups);
  memset(p, 0, sizeof *p);
}
/* Good for: Decode Minecraft wire payload for update tags into a struct.
 * Callers: mc_static_registries.c.
 */

lc_status lc_parse_update_tags(const uint8_t *data, size_t len, lc_update_tags *out) {
  memset(out, 0, sizeof *out);
  lc_buf b;
  lc_buf_init(&b, data, len);
  int32_t group_count;
  if (lc_buf_read_varint(&b, &group_count) != LC_OK) return LC_ERR_TRUNCATED;
  if (group_count < 0) return LC_ERR_INVALID;
  out->group_count = (size_t)group_count;
  if (group_count == 0) return LC_OK;
  out->groups = (lc_tag_group *)calloc((size_t)group_count, sizeof(lc_tag_group));
  if (!out->groups) return LC_ERR_OOM;
  for (int32_t gi = 0; gi < group_count; gi++) {
    lc_tag_group *g = &out->groups[gi];
    if (lc_buf_read_string(&b, &g->registry_id) != LC_OK) goto fail;
    int32_t tag_count;
    if (lc_buf_read_varint(&b, &tag_count) != LC_OK) goto fail;
    if (tag_count < 0) goto fail;
    g->tag_count = (size_t)tag_count;
    if (tag_count == 0) continue;
    g->tags = (lc_tag_group_entry *)calloc((size_t)tag_count, sizeof(lc_tag_group_entry));
    if (!g->tags) goto fail;
    for (int32_t ti = 0; ti < tag_count; ti++) {
      lc_tag_group_entry *t = &g->tags[ti];
      if (lc_buf_read_string(&b, &t->name) != LC_OK) goto fail;
      int32_t id_count;
      if (lc_buf_read_varint(&b, &id_count) != LC_OK) goto fail;
      if (id_count < 0) goto fail;
      t->id_count = (size_t)id_count;
      if (id_count == 0) continue;
      t->ids = (int32_t *)calloc((size_t)id_count, sizeof(int32_t));
      if (!t->ids) goto fail;
      for (int32_t ii = 0; ii < id_count; ii++) {
        if (lc_buf_read_varint(&b, &t->ids[ii]) != LC_OK) goto fail;
      }
    }
  }
  return LC_OK;
fail:
  lc_update_tags_free(out);
  return LC_ERR_TRUNCATED;
}
