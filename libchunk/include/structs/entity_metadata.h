#ifndef LIBCHUNK_STRUCT_ENTITY_METADATA_H
#define LIBCHUNK_STRUCT_ENTITY_METADATA_H

#include "../types.h"

typedef struct lc_entity_metadata {
  int32_t entity_id;
  lc_metadata_arr metadata;
} lc_entity_metadata;

#endif /* LIBCHUNK_STRUCT_ENTITY_METADATA_H */
