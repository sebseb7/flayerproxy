#ifndef LIBCHUNK_STRUCT_REGISTRY_DATA_H
#define LIBCHUNK_STRUCT_REGISTRY_DATA_H

#include "../types.h"

typedef struct lc_registry_data {
  char *id;
  lc_registry_entry_arr entries;
} lc_registry_data;

#endif /* LIBCHUNK_STRUCT_REGISTRY_DATA_H */
