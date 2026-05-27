#ifndef LC_STRUCTS_UPDATE_TAGS_H
#define LC_STRUCTS_UPDATE_TAGS_H

#include "types.h"

typedef struct lc_tag_group_entry {
  char *name;
  int32_t *ids;
  size_t id_count;
} lc_tag_group_entry;

typedef struct lc_tag_group {
  char *registry_id;
  lc_tag_group_entry *tags;
  size_t tag_count;
} lc_tag_group;

typedef struct lc_update_tags {
  lc_tag_group *groups;
  size_t group_count;
} lc_update_tags;

#endif
