#ifndef LIBCHUNK_STRUCT_ENTITY_DESTROY_H
#define LIBCHUNK_STRUCT_ENTITY_DESTROY_H

#include "../types.h"

typedef struct lc_entity_destroy {
  int32_t *entity_ids;
  size_t count;
} lc_entity_destroy;

#endif /* LIBCHUNK_STRUCT_ENTITY_DESTROY_H */
