#ifndef LIBCHUNK_STRUCT_SET_PASSENGERS_H
#define LIBCHUNK_STRUCT_SET_PASSENGERS_H

#include "../types.h"

typedef struct lc_set_passengers {
  int32_t entity_id;
  int32_t *passengers;
  size_t passenger_count;
} lc_set_passengers;

#endif /* LIBCHUNK_STRUCT_SET_PASSENGERS_H */
