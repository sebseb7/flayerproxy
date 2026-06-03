#define _POSIX_C_SOURCE 200809L

#include "mc_upstream_bridge.h"

#include "mc_conn.h"
#include "mc_conn_state.h"
#include "mc_log.h"
#include "mc_packet_ids.h"

#include "libchunk.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define BRIDGE_Q_MAX 64
#define BRIDGE_PAYLOAD_MAX (1024 * 1024)

typedef struct bridge_queued {
  int32_t pkt_id;
  size_t len;
  uint8_t *data;
} bridge_queued;

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static mc_conn *g_conn;
static int g_active;
static bridge_queued g_q[BRIDGE_Q_MAX];
static int g_q_count;

void mc_upstream_bridge_lock(void) { pthread_mutex_lock(&g_mu); }

void mc_upstream_bridge_unlock(void) { pthread_mutex_unlock(&g_mu); }

void mc_upstream_bridge_enable(mc_conn *conn) {
  if (!conn) return;
  pthread_mutex_lock(&g_mu);
  g_conn = conn;
  g_active = 1;
  g_q_count = 0;
  pthread_mutex_unlock(&g_mu);
  MC_LOGI("static_server", "upstream bridge: enabled (C2S relay when UP+DOWN are PLAYING)");
}

void mc_upstream_bridge_disable(void) {
  pthread_mutex_lock(&g_mu);
  g_active = 0;
  g_conn = NULL;
  for (int i = 0; i < g_q_count; i++) free(g_q[i].data);
  g_q_count = 0;
  pthread_mutex_unlock(&g_mu);
}

int mc_upstream_bridge_active(void) {
  pthread_mutex_lock(&g_mu);
  int ok = g_active;
  pthread_mutex_unlock(&g_mu);
  return ok;
}

static int queue_push(int32_t pkt_id, const uint8_t *payload, size_t len) {
  if (g_q_count >= BRIDGE_Q_MAX || len > BRIDGE_PAYLOAD_MAX) return -1;
  uint8_t *copy = (uint8_t *)malloc(len ? len : 1);
  if (!copy) return -1;
  if (len) memcpy(copy, payload, len);
  g_q[g_q_count].pkt_id = pkt_id;
  g_q[g_q_count].len = len;
  g_q[g_q_count].data = copy;
  g_q_count++;
  return 0;
}

void mc_upstream_bridge_flush(mc_conn *conn) {
  if (!conn) return;
  while (g_q_count > 0) {
    bridge_queued q = g_q[0];
    for (int i = 1; i < g_q_count; i++) g_q[i - 1] = g_q[i];
    g_q_count--;
    if (mc_conn_send_frame(conn, q.pkt_id, q.data, q.len) != 0) {
      MC_LOGEV("static_server", "upstream bridge: queued C2S 0x%02x send dropped", (unsigned)(q.pkt_id & 0xff));
      free(q.data);
      break;
    }
    free(q.data);
  }
}

/** Play C2S that belong only on the client↔static link or only on the bot↔upstream link. */
static int bridge_c2s_not_relayed(int32_t pkt_id) {
  switch (pkt_id) {
  case MC_PKT_C2S_KEEP_ALIVE:
    /* DOWN: mc_client_* keep-alive. UP: handle_play_response on S2C keep_alive. */
    return 1;
  case MC_PKT_C2S_TICK_END:
    /* Client tick vs bot tick_end after map_chunk on upstream. */
    return 1;
  case MC_PKT_C2S_PLAYER_LOADED:
    /* Bot sends when ready on upstream; client signals static server separately. */
    return 1;
  default:
    return 0;
  }
}

int mc_upstream_bridge_forward_c2s(int32_t pkt_id, const uint8_t *payload, size_t payload_len) {
  if (!payload && payload_len > 0) return -1;
  if (payload_len > BRIDGE_PAYLOAD_MAX) return -1;
  if (bridge_c2s_not_relayed(pkt_id)) return 0;

  pthread_mutex_lock(&g_mu);
  if (!g_active || mc_conn_state_upstream_get() != MC_CONN_STATE_PLAYING) {
    pthread_mutex_unlock(&g_mu);
    return -1;
  }
  if (!g_conn || g_conn->fd < 0) {
    pthread_mutex_unlock(&g_mu);
    return -1;
  }

  mc_upstream_bridge_flush(g_conn);

  if (mc_conn_send_frame(g_conn, pkt_id, payload, payload_len) != 0) {
    if (queue_push(pkt_id, payload, payload_len) != 0) {
      pthread_mutex_unlock(&g_mu);
      return -1;
    }
    pthread_mutex_unlock(&g_mu);
    return 0;
  }

  MC_LOGEV("static_server", "upstream bridge: C2S 0x%02x (%zu B) -> upstream", (unsigned)(pkt_id & 0xff),
           payload_len);
  pthread_mutex_unlock(&g_mu);
  return 0;
}
