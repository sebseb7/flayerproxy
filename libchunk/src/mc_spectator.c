/**
 * Minecraft 1.21.10 spectator server in C (offline auth).
 * Replays registry/login/terrain wires captured from chunkStream; grass placeholder otherwise.
 */
#define _POSIX_C_SOURCE 200809L

#include "mc_log.h"
#include "mc_spectator.h"

#include "internal.h"
#include "libchunk.h"
#include "mc_auth.h"
#include "mc_packet_ids.h"
#include "mc_server_common.h"
#include "mc_wire_templates.h"
#include "mc_wire.h"

#include <math.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MC_CACHE_CONFIG_MAX 512
#define MC_CACHE_CHUNKS_MAX 512
#define MC_CACHE_WIRE_MAX (4 * 1024 * 1024)
#define MC_GRASS_RADIUS 2
#define MC_SPAWN_X 8.0
#define MC_SPAWN_Y 64.0
#define MC_SPAWN_Z 8.0

typedef struct mc_wire_copy {
  uint8_t *data;
  size_t len;
} mc_wire_copy;

static pthread_mutex_t g_cache_mu = PTHREAD_MUTEX_INITIALIZER;
static int g_stream_active;
static mc_wire_copy g_config[MC_CACHE_CONFIG_MAX];
static size_t g_config_count;
static mc_wire_copy g_login;
static mc_wire_copy g_position;
static mc_wire_copy g_chunks[MC_CACHE_CHUNKS_MAX];
static size_t g_chunks_count;

static int g_listen_fd = -1;
static pthread_t g_accept_tid;
static volatile int g_run_accept;

static uint8_t *g_grass_chunk_wire;
static size_t g_grass_chunk_wire_len;
/* Good for: Free one cached wire blob in spectator stream.
 * Callers: mc_spectator.c (same file).
 */

static void wire_copy_free(mc_wire_copy *w) {
  free(w->data);
  w->data = NULL;
  w->len = 0;
}
/* Good for: Copy wire bytes into spectator stream cache entry.
 * Callers: mc_spectator.c (same file).
 */

static int wire_copy_set(mc_wire_copy *w, const uint8_t *data, size_t len) {
  wire_copy_free(w);
  if (!data || len == 0) return 0;
  if (len > MC_CACHE_WIRE_MAX) len = MC_CACHE_WIRE_MAX;
  w->data = (uint8_t *)malloc(len);
  if (!w->data) return -1;
  memcpy(w->data, data, len);
  w->len = len;
  return 0;
}
/* Good for: Clear ingested config/login/chunk wire cache.
 * Callers: chunk_stream_receiver.c.
 */

void mc_stream_cache_reset(void) {
  pthread_mutex_lock(&g_cache_mu);
  g_stream_active = 0;
  for (size_t i = 0; i < g_config_count; i++) wire_copy_free(&g_config[i]);
  g_config_count = 0;
  wire_copy_free(&g_login);
  wire_copy_free(&g_position);
  for (size_t i = 0; i < g_chunks_count; i++) wire_copy_free(&g_chunks[i]);
  g_chunks_count = 0;
  pthread_mutex_unlock(&g_cache_mu);
}
/* Good for: True if sniffer packet name is configuration phase.
 * Callers: mc_spectator.c (same file).
 */

static int is_config_stream_packet(const char *name) {
  return strcmp(name, "registry_data") == 0 || strcmp(name, "feature_flags") == 0 ||
         strcmp(name, "tags") == 0 || strcmp(name, "custom_payload") == 0 ||
         strcmp(name, "reset_chat") == 0 || strcmp(name, "select_known_packs") == 0 ||
         strcmp(name, "server_links") == 0 || strcmp(name, "code_of_conduct") == 0;
}
/* Good for: Store captured wire by packet name for replay.
 * Callers: chunk_stream_receiver.c.
 */

void mc_stream_cache_ingest(const char *pkt_name, const uint8_t *wire, size_t wire_len) {
  if (!pkt_name || !wire || wire_len == 0) return;
  pthread_mutex_lock(&g_cache_mu);
  g_stream_active = 1;
  if (is_config_stream_packet(pkt_name) && g_config_count < MC_CACHE_CONFIG_MAX) {
    if (wire_copy_set(&g_config[g_config_count], wire, wire_len) == 0) g_config_count++;
  } else if (strcmp(pkt_name, "login") == 0) {
    (void)wire_copy_set(&g_login, wire, wire_len);
  } else if (strcmp(pkt_name, "position") == 0) {
    (void)wire_copy_set(&g_position, wire, wire_len);
  } else if (strcmp(pkt_name, "map_chunk") == 0 && g_chunks_count < MC_CACHE_CHUNKS_MAX) {
    if (wire_copy_set(&g_chunks[g_chunks_count], wire, wire_len) == 0) g_chunks_count++;
  }
  pthread_mutex_unlock(&g_cache_mu);
}
/* Good for: True if cache has registry/config packets for replay.
 * Callers: mc_spectator.c (same file).
 */

static int cache_has_config(void) {
  pthread_mutex_lock(&g_cache_mu);
  int v = g_config_count > 0;
  pthread_mutex_unlock(&g_cache_mu);
  return v;
}
/* Good for: True if any stream data was ingested.
 * Callers: libchunk.h (public API, no .c callers in tree).
 */

int mc_stream_cache_has_stream(void) {
  pthread_mutex_lock(&g_cache_mu);
  int v = g_stream_active;
  pthread_mutex_unlock(&g_cache_mu);
  return v;
}
/* Good for: Send minimal login when no capture cached.
 * Callers: mc_spectator.c (same file).
 */

static int send_play_login_minimal(mc_client *cli) {
  pthread_mutex_lock(&g_cache_mu);
  if (g_login.len > 0) {
    int rc = mc_send_wire_framed(cli->fd, g_login.data, g_login.len);
    pthread_mutex_unlock(&g_cache_mu);
    return rc;
  }
  pthread_mutex_unlock(&g_cache_mu);
  mc_buf p;
  memset(&p, 0, sizeof p);
  if (mc_buf_i32_be(&p, cli->entity_id) != LC_OK) return -1;
  if (mc_buf_u8(&p, 0) != LC_OK) return -1;
  if (mc_buf_varint(&p, 1) != LC_OK) return -1;
  const char *wn[] = {"minecraft:overworld"};
  if (mc_buf_varint(&p, 1) != LC_OK) return -1;
  if (mc_buf_string(&p, wn[0]) != LC_OK) return -1;
  if (mc_buf_varint(&p, 20) != LC_OK) return -1;
  if (mc_buf_varint(&p, 10) != LC_OK) return -1;
  if (mc_buf_varint(&p, 10) != LC_OK) return -1;
  if (mc_buf_u8(&p, 0) != LC_OK) return -1;
  if (mc_buf_u8(&p, 1) != LC_OK) return -1;
  if (mc_buf_u8(&p, 0) != LC_OK) return -1;
  if (mc_buf_u8(&p, 0) != LC_OK) return -1;
  return mc_send_frame(cli->fd, MC_PKT_PLAY_LOGIN, p.data, p.len);
}
/* Good for: Force spectator gamemode on client.
 * Callers: mc_spectator.c (same file).
 */

static int send_spectator_gamemode(mc_client *cli) {
  mc_buf p;
  memset(&p, 0, sizeof p);
  if (mc_buf_u8(&p, 3) != LC_OK) return -1;
  if (mc_buf_f32_be(&p, 3.0f) != LC_OK) return -1;
  return mc_send_frame(cli->fd, MC_PKT_PLAY_GAME_STATE_CHANGE, p.data, p.len);
}
/* Good for: Send view position for chunk streaming.
 * Callers: mc_spectator.c (same file).
 */

static int send_update_view(mc_client *cli, int32_t cx, int32_t cz) {
  mc_buf p;
  memset(&p, 0, sizeof p);
  if (mc_buf_varint(&p, cx) != LC_OK) return -1;
  if (mc_buf_varint(&p, cz) != LC_OK) return -1;
  return mc_send_frame(cli->fd, MC_PKT_PLAY_UPDATE_VIEW_POSITION, p.data, p.len);
}
/* Good for: Rewrite map_chunk x/z in cached wire to new chunk.
 * Callers: mc_spectator.c (same file).
 */

static int patch_chunk_coords(uint8_t *wire, size_t wire_len, int32_t cx, int32_t cz) {
  lc_buf b;
  lc_buf_init(&b, wire, wire_len);
  int32_t id;
  if (lc_buf_read_varint(&b, &id) != LC_OK) return -1;
  if (id != MC_PKT_PLAY_MAP_CHUNK) return -1;
  if (lc_buf_remaining(&b) < 8) return -1;
  size_t off = b.off;
  wire[off] = (uint8_t)((uint32_t)cx >> 24);
  wire[off + 1] = (uint8_t)((uint32_t)cx >> 16);
  wire[off + 2] = (uint8_t)((uint32_t)cx >> 8);
  wire[off + 3] = (uint8_t)cx;
  wire[off + 4] = (uint8_t)((uint32_t)cz >> 24);
  wire[off + 5] = (uint8_t)((uint32_t)cz >> 16);
  wire[off + 6] = (uint8_t)((uint32_t)cz >> 8);
  wire[off + 7] = (uint8_t)cz;
  return 0;
}
/* Good for: Send grass placeholder chunks around spawn.
 * Callers: mc_spectator.c (same file).
 */

static int send_grass_world(mc_client *cli) {
  if (!g_grass_chunk_wire || g_grass_chunk_wire_len == 0) return -1;
  int32_t cx = (int32_t)floor(MC_SPAWN_X / 16.0);
  int32_t cz = (int32_t)floor(MC_SPAWN_Z / 16.0);
  if (send_update_view(cli, cx, cz) != 0) return -1;
  for (int dx = -MC_GRASS_RADIUS; dx <= MC_GRASS_RADIUS; dx++) {
    for (int dz = -MC_GRASS_RADIUS; dz <= MC_GRASS_RADIUS; dz++) {
      uint8_t *copy = (uint8_t *)malloc(g_grass_chunk_wire_len);
      if (!copy) return -1;
      memcpy(copy, g_grass_chunk_wire, g_grass_chunk_wire_len);
      if (patch_chunk_coords(copy, g_grass_chunk_wire_len, cx + dx, cz + dz) != 0) {
        free(copy);
        return -1;
      }
      int rc = mc_send_wire_framed(cli->fd, copy, g_grass_chunk_wire_len);
      free(copy);
      if (rc != 0) return -1;
    }
  }
  pthread_mutex_lock(&g_cache_mu);
  if (g_position.len > 0) {
    int rc = mc_send_wire_framed(cli->fd, g_position.data, g_position.len);
    pthread_mutex_unlock(&g_cache_mu);
    return rc;
  }
  pthread_mutex_unlock(&g_cache_mu);
  mc_buf p;
  memset(&p, 0, sizeof p);
  if (mc_buf_varint(&p, 0) != LC_OK) return -1;
  if (mc_buf_f64_be(&p, MC_SPAWN_X) != LC_OK) return -1;
  if (mc_buf_f64_be(&p, MC_SPAWN_Y) != LC_OK) return -1;
  if (mc_buf_f64_be(&p, MC_SPAWN_Z) != LC_OK) return -1;
  if (mc_buf_f64_be(&p, 0.0) != LC_OK) return -1;
  if (mc_buf_f64_be(&p, 0.0) != LC_OK) return -1;
  if (mc_buf_f64_be(&p, 0.0) != LC_OK) return -1;
  if (mc_buf_f32_be(&p, 0.0f) != LC_OK) return -1;
  if (mc_buf_f32_be(&p, 0.0f) != LC_OK) return -1;
  if (mc_buf_u8(&p, 0) != LC_OK) return -1;
  return mc_send_frame(cli->fd, MC_PKT_PLAY_POSITION, p.data, p.len);
}
/* Good for: Replay cached configuration/registry wires.
 * Callers: mc_spectator.c (same file).
 */

static int replay_config_registry(mc_client *cli) {
  pthread_mutex_lock(&g_cache_mu);
  for (size_t i = 0; i < g_config_count; i++) {
    if (mc_send_wire_framed(cli->fd, g_config[i].data, g_config[i].len) != 0) {
      pthread_mutex_unlock(&g_cache_mu);
      return -1;
    }
  }
  pthread_mutex_unlock(&g_cache_mu);
  return mc_send_config_finish(cli);
}
/* Good for: Replay cached map_chunk wires with coord patch.
 * Callers: mc_spectator.c (same file).
 */

static int replay_cached_chunks(mc_client *cli) {
  pthread_mutex_lock(&g_cache_mu);
  for (size_t i = 0; i < g_chunks_count; i++) {
    if (mc_send_wire_framed(cli->fd, g_chunks[i].data, g_chunks[i].len) != 0) {
      pthread_mutex_unlock(&g_cache_mu);
      return -1;
    }
  }
  if (g_position.len > 0) {
    int rc = mc_send_wire_framed(cli->fd, g_position.data, g_position.len);
    pthread_mutex_unlock(&g_cache_mu);
    return rc;
  }
  pthread_mutex_unlock(&g_cache_mu);
  return 0;
}
/* Good for: Replay cached stream or grass world; enter play state.
 * Callers: mc_spectator.c (same file), mc_static_server.c.
 */

static int enter_play(mc_client *cli) {
  if (send_play_login_minimal(cli) != 0) return -1;
  if (send_spectator_gamemode(cli) != 0) return -1;
  if (cache_has_config()) return replay_cached_chunks(cli);
  return send_grass_world(cli);
}
/* Good for: Per-client thread: handshake, config, play loop.
 * Callers: mc_spectator.c (same file), mc_static_server.c.
 */

static void handle_client(int fd) {
  mc_client cli;
  memset(&cli, 0, sizeof cli);
  cli.fd = fd;
  cli.state = MC_CLI_HANDSHAKE;
  cli.entity_id = 1;
  cli.gamemode = MC_GM_SPECTATOR;

  for (;;) {
    uint8_t *packet = NULL;
    size_t packet_len = 0;
    int32_t pkt_id = -1;
    if (mc_read_packet(fd, &packet, &packet_len, &pkt_id) != 0) break;
    lc_buf b;
    lc_buf_init(&b, packet, packet_len);
    int32_t id_check;
    if (lc_buf_read_varint(&b, &id_check) != LC_OK) {
      free(packet);
      break;
    }
    pkt_id = id_check;
    const uint8_t *payload = packet + b.off;
    size_t payload_len = packet_len - b.off;

    if (cli.state == MC_CLI_HANDSHAKE) {
      int32_t proto, next;
      char *host = NULL;
      uint16_t port;
      lc_buf hb;
      lc_buf_init(&hb, payload, payload_len);
      if (lc_buf_read_varint(&hb, &proto) != LC_OK || lc_buf_read_string(&hb, &host) != LC_OK ||
/* Good for: Read u16_be from packet cursor lc_buf (all parsers).
 * Callers: nbt.c.
 */
          lc_buf_read_u16_be(&hb, &port) != LC_OK || lc_buf_read_varint(&hb, &next) != LC_OK) {
        free(host);
        free(packet);
        break;
      }
      free(host);
      (void)proto;
      (void)port;
      cli.state = (next == MC_HS_LOGIN) ? MC_CLI_LOGIN : MC_CLI_HANDSHAKE;
      free(packet);
      continue;
    }
    if (cli.state == MC_CLI_LOGIN) {
      if (pkt_id == MC_PKT_C2S_LOGIN_START) {
        if (mc_auth_offline.on_login_start(&cli, payload, payload_len) != 0 ||
            mc_auth_offline.send_login_success(&cli) != 0) {
          free(packet);
          break;
        }
      } else if (pkt_id == MC_PKT_C2S_LOGIN_ACKNOWLEDGED) {
        cli.state = MC_CLI_CONFIG;
        if (replay_config_registry(&cli) != 0) {
          free(packet);
          break;
        }
      }
      free(packet);
      continue;
    }
    if (cli.state == MC_CLI_CONFIG) {
      if (pkt_id == MC_PKT_C2S_CFG_FINISH) {
        cli.state = MC_CLI_PLAY;
        if (enter_play(&cli) != 0) {
          free(packet);
          break;
        }
        cli.play_ready = 1;
      }
      free(packet);
      continue;
    }
    if (cli.state == MC_CLI_PLAY) {
      if (pkt_id == MC_PKT_C2S_KEEP_ALIVE) {
        mc_client_handle_keep_alive(&cli, payload, payload_len);
      } else if (pkt_id == MC_PKT_C2S_PLAYER_LOADED) {
        /* empty payload */
      }
      free(packet);
      continue;
    }
    free(packet);
  }
  close(fd);
}

static void *accept_thread(void *arg) {
  (void)arg;
  while (g_run_accept) {
    struct sockaddr_in peer;
    socklen_t plen = sizeof peer;
    int cfd = accept(g_listen_fd, (struct sockaddr *)&peer, &plen);
    if (cfd < 0) {
      if (errno == EINTR) continue;
      if (!g_run_accept) break;
      mc_log_errno("spectator", "accept");
      continue;
    }
    char peer_addr[INET_ADDRSTRLEN];
    uint16_t peer_port = ntohs(peer.sin_port);
    if (!inet_ntop(AF_INET, &peer.sin_addr, peer_addr, sizeof peer_addr)) {
      snprintf(peer_addr, sizeof peer_addr, "?");
    }
    MC_LOGCN("spectator", "incoming connection from %s:%u", peer_addr, (unsigned)peer_port);
    handle_client(cfd);
    MC_LOGCN("spectator", "connection closed from %s:%u", peer_addr, (unsigned)peer_port);
  }
  return NULL;
}
/* Good for: Load grass map_chunk template wire into memory.
 * Callers: mc_spectator.c (same file).
 */

static int load_grass_template(void) {
  if (mc_templates_init() != 0) return -1;
  return mc_templates_grass_packet_wire(&g_grass_chunk_wire, &g_grass_chunk_wire_len);
}
/* Good for: Start spectator TCP server (chunk_stream_receiver).
 * Callers: chunk_stream_receiver.c.
 */

int mc_spectator_start(const char *host, int port) {
  if (g_listen_fd >= 0) return 0;
  if (load_grass_template() != 0) {
    MC_LOGE("spectator", "failed to load grass chunk template");
    return -1;
  }
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    mc_log_errno("spectator", "socket");
    return -1;
  }
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)port);
  if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
    MC_LOGE("spectator", "invalid host %s", host);
    close(fd);
    return -1;
  }
  if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
    mc_log_errno("spectator", "bind");
    close(fd);
    return -1;
  }
  if (listen(fd, 8) != 0) {
    mc_log_errno("spectator", "listen");
    close(fd);
    return -1;
  }
  g_listen_fd = fd;
  g_run_accept = 1;
  if (pthread_create(&g_accept_tid, NULL, accept_thread, NULL) != 0) {
    mc_log_errno("spectator", "pthread_create");
    close(g_listen_fd);
    g_listen_fd = -1;
    return -1;
  }
  MC_LOGI("spectator", "listening on %s:%d (C, offline auth)", host, port);
  return 0;
}
/* Good for: Stop spectator server and join accept thread.
 * Callers: libchunk.h (public API, no .c callers in tree).
 */

void mc_spectator_stop(void) {
  if (g_listen_fd < 0) return;
  g_run_accept = 0;
  shutdown(g_listen_fd, SHUT_RDWR);
  pthread_join(g_accept_tid, NULL);
  close(g_listen_fd);
  g_listen_fd = -1;
  free(g_grass_chunk_wire);
  g_grass_chunk_wire = NULL;
  g_grass_chunk_wire_len = 0;
  mc_templates_free();
}
