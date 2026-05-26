#ifndef PLAY_STREAM_H
#define PLAY_STREAM_H

#include <stddef.h>
#include <stdint.h>

/** 1 if this play packet is handled by play_stream decoders. */
int lc_play_stream_packet_supported(const char *name);

/**
 * Decode payload (no packet id) to human-readable summary.
 * @return 1 ok, -1 parse error
 */
int lc_decode_play_stream_to_string(const char *name, const uint8_t *payload, size_t payload_len,
                                    char *out, size_t out_sz);

#endif
