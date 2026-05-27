#ifndef MC_SPECTATOR_H
#define MC_SPECTATOR_H

#include <stddef.h>
#include <stdint.h>

/** Reset cached S2C wires from the sniffer stream. */
void mc_stream_cache_reset(void);

/** Store a copy of a framed wire packet (id + payload) from chunkStream. */
void mc_stream_cache_ingest(const char *pkt_name, const uint8_t *wire, size_t wire_len);

/** @return 1 if sniffer stream has sent at least one packet since reset. */
int mc_stream_cache_has_stream(void);

/**
 * Start spectator Minecraft server (TCP, offline auth, C implementation).
 * @return 0 on success
 */
int mc_spectator_start(const char *host, int port);

void mc_spectator_stop(void);

#endif
