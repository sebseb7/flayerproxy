#ifndef LIBCHUNK_STRUCT_BLOCK_CHANGE_H
#define LIBCHUNK_STRUCT_BLOCK_CHANGE_H

#include "../types.h"

typedef struct lc_block_change {
  lc_block_pos location;
  int32_t type;
} lc_block_change;

#endif /* LIBCHUNK_STRUCT_BLOCK_CHANGE_H */
