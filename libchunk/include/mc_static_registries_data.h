#ifndef MC_STATIC_REGISTRIES_DATA_H
#define MC_STATIC_REGISTRIES_DATA_H

#include <stddef.h>
#include <stdint.h>

typedef struct mc_static_blob {
  const char *label;
  const uint8_t *data;
  size_t len;
} mc_static_blob;

extern const mc_static_blob mc_static_registry_blobs[];
extern const size_t mc_static_registry_blob_count;
extern const mc_static_blob mc_static_tags;

#endif
