#ifndef MC_UPSTREAM_BRIDGE_H
#define MC_UPSTREAM_BRIDGE_H

#include <stddef.h>
#include <stdint.h>

struct mc_conn;

/** Hold upstream play socket for C2S relay (registry fetch thread keeps reading S2C). */
void mc_upstream_bridge_enable(struct mc_conn *conn);
void mc_upstream_bridge_disable(void);

/** 1 when upstream play link is held for bridging. */
int mc_upstream_bridge_active(void);

/**
 * Forward client play C2S to upstream (same frame id + payload).
 * keep_alive, tick_end, and player_loaded are never relayed — each TCP
 * connection (client↔static, bot↔upstream) has its own keep-alive and lifecycle.
 * Requires upstream bridge active and UP conn state PLAYING.
 */
int mc_upstream_bridge_forward_c2s(int32_t pkt_id, const uint8_t *payload, size_t payload_len);

/** Drain pending client C2S forwards (call from registry fetch thread before read). */
void mc_upstream_bridge_flush(struct mc_conn *conn);

void mc_upstream_bridge_lock(void);
void mc_upstream_bridge_unlock(void);

#endif
