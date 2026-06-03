#ifndef MC_CONN_STATE_H
#define MC_CONN_STATE_H

/** Registry fetch (upstream Minecraft server) vs client to static server. */
typedef enum mc_conn_link {
  MC_CONN_LINK_UP = 0,
  MC_CONN_LINK_DOWN = 1,
} mc_conn_link;

/**
 * Same meaning on UP and DOWN (Minecraft connection lifecycle):
 *   INITIATED    — link open; configuration not finished yet
 *   CONFIGURED   — configuration phase finished (registries/tags + finish_configuration)
 *   PLAYING      — play phase active on this link
 *   DISCONNECTED — link closed
 */
typedef enum mc_conn_state {
  MC_CONN_STATE_INITIATED = 0,
  MC_CONN_STATE_CONFIGURED = 1,
  MC_CONN_STATE_PLAYING = 2,
  MC_CONN_STATE_DISCONNECTED = 3,
} mc_conn_state;

typedef struct mc_conn_state_tracker {
  mc_conn_state state;
  int has_state;
} mc_conn_state_tracker;

void mc_conn_state_tracker_init(mc_conn_state_tracker *tracker);

/** Log a colored state transition when `new_state` differs from the tracker. */
void mc_conn_state_transition(mc_conn_link link, mc_conn_state_tracker *tracker, mc_conn_state new_state,
                              const char *detail);

void mc_conn_state_upstream_reset(void);
void mc_conn_state_upstream(mc_conn_state state, const char *detail);

const char *mc_conn_link_name(mc_conn_link link);
const char *mc_conn_state_name(mc_conn_state state);

#endif
