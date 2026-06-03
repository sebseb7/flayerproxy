/**
 * Decode Minecraft play/configuration packets to libchunk toString / JSON.
 *
 * - lc_decode_payload_to_string: packet body only (no leading packet-id varint).
 * - lc_decode_wire_to_string: sniffer .wire capture (leading packet-id varint, then body).
 */
#ifndef DECODE_WIRE_H
#define DECODE_WIRE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/** 1 if libchunk has a parser/toString for this packet name. */
int lc_packet_name_supported(const char *name);

/**
 * Parse packet payload and write human-readable toString into out.
 * @return 1 ok, 0 unsupported (skip), -1 error
 */
int lc_decode_payload_to_string(const char *name, const uint8_t *payload, size_t payload_len,
                                char *out, size_t out_sz);

/**
 * Parse sniffer wire (packet-id varint + payload); delegates to lc_decode_payload_to_string.
 * @return 1 ok, 0 unsupported (skip), -1 error
 */
int lc_decode_wire_to_string(const char *name, const uint8_t *wire, size_t wire_len, char *out,
                             size_t out_sz);

/**
 * Write full JSON decode for map_chunk payload.
 * @return 1 ok, 0 unsupported name, -1 parse error
 */
int lc_decode_payload_map_chunk_json(const char *name, const char *file_basename,
                                     const uint8_t *payload, size_t payload_len, FILE *out);

/**
 * Write full JSON decode for map_chunk sniffer wire (id + payload).
 * @return 1 ok, 0 unsupported name, -1 parse error
 */
int lc_decode_wire_map_chunk_json(const char *name, const char *file_basename,
                                  const uint8_t *wire, size_t wire_len, FILE *out);

#endif
