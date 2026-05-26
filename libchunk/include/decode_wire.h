/**
 * Decode play/configuration wire (with optional leading packet id) to libchunk toString.
 */
#ifndef DECODE_WIRE_H
#define DECODE_WIRE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/** 1 if libchunk has a parser/toString for this packet name. */
int lc_packet_name_supported(const char *name);

/**
 * Parse wire bytes and write human-readable toString into out.
 * @return 1 ok, 0 unsupported (skip), -1 error
 */
int lc_decode_wire_to_string(const char *name, const uint8_t *wire, size_t wire_len, char *out,
                             size_t out_sz);

/**
 * Write full JSON decode for map_chunk (all packet fields + section state ids).
 * @return 1 ok, 0 unsupported name, -1 parse error
 */
int lc_decode_wire_map_chunk_json(const char *name, const char *file_basename,
                                  const uint8_t *wire, size_t wire_len, FILE *out);

#endif
