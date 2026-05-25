#ifndef LIBCHUNK_STRUCT_MULTI_BLOCK_CHANGE_H
#define LIBCHUNK_STRUCT_MULTI_BLOCK_CHANGE_H

#include "../types.h"

typedef struct lc_multi_block_change {
  lc_chunk_section_pos chunk_coordinates;
  int32_t *records;
  size_t record_count;
} lc_multi_block_change;

#endif /* LIBCHUNK_STRUCT_MULTI_BLOCK_CHANGE_H */
