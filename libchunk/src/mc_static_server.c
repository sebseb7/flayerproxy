#define _POSIX_C_SOURCE 200809L

#include "mc_chunk_stream.h"
#include "mc_log.h"
#include "mc_static_server.h"

#include "internal.h"
#include "libchunk.h"
#include "mc_c2s_log.h"
#include "mc_packet_ids.h"
#include "mc_static_registries.h"
#include "mc_wire.h"
#include "mc_wire_templates.h"

#include <math.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

typedef struct mc_client_thread_ctx {
  int fd;
  char peer_addr[INET_ADDRSTRLEN];
  uint16_t peer_port;
} mc_client_thread_ctx;

/** Global block state ids (1.21.10 registry). */
#define MC_BLOCK_STATE_AIR 0
#define MC_BLOCK_STATE_REDSTONE_BLOCK 11109

/** Demo block at spawn: y=64 (one above grass at y=63), z=spawn+1. */
#define MC_DEMO_BLOCK_X 10
#define MC_DEMO_BLOCK_Y 64
#define MC_DEMO_BLOCK_Z 9
#define MC_DEMO_BLOCK_TOGGLE_MS 1000
/* Good for: Monotonic clock in ms for demo block toggle.
 * Callers: mc_static_server.c (same file).
 */

static int64_t mc_monotonic_ms(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return (int64_t)time(NULL) * 1000;
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
/* Good for: Send Block Change for demo redstone block.
 * Callers: mc_static_server.c (same file).
 */

static int send_block_change(int fd, int32_t x, int32_t y, int32_t z, int32_t state_id) {
  mc_buf body;
  memset(&body, 0, sizeof body);
  lc_block_pos pos = {.x = x, .y = y, .z = z};
  if (mc_buf_block_pos(&body, &pos) != LC_OK || mc_buf_varint(&body, state_id) != LC_OK) {
    mc_buf_free(&body);
    return -1;
  }
  int rc = mc_send_frame(fd, MC_PKT_PLAY_BLOCK_CHANGE, body.data, body.len);
  mc_buf_free(&body);
  return rc;
}
/* Good for: Chunk coords containing demo block.
 * Callers: mc_static_server.c (same file).
 */

static int demo_block_chunk(int32_t block_coord) {
  return (int32_t)floor((double)block_coord / 16.0);
}

static void tick_demo_block(mc_client *cli, const mc_chunk_stream *chunks, int *show_redstone,
                            int64_t *last_toggle_ms) {
  if (!cli->play_ready) return;
  int32_t demo_cx = demo_block_chunk(MC_DEMO_BLOCK_X);
  int32_t demo_cz = demo_block_chunk(MC_DEMO_BLOCK_Z);
  if (!mc_chunk_stream_has_chunk(chunks, demo_cx, demo_cz)) return;

  int64_t now = mc_monotonic_ms();
  if (*last_toggle_ms != 0 && now - *last_toggle_ms < MC_DEMO_BLOCK_TOGGLE_MS) return;
  *last_toggle_ms = now;
  int32_t state = *show_redstone ? MC_BLOCK_STATE_REDSTONE_BLOCK : MC_BLOCK_STATE_AIR;
  if (send_block_change(cli->fd, MC_DEMO_BLOCK_X, MC_DEMO_BLOCK_Y, MC_DEMO_BLOCK_Z, state) != 0) {
    MC_LOGW("static_server", "block_change failed for %s", cli->username[0] ? cli->username : "?");
    return;
  }
  *show_redstone = !*show_redstone;
  MC_LOGI("static_server", "block_change (%d,%d,%d) -> %s", MC_DEMO_BLOCK_X, MC_DEMO_BLOCK_Y,
          MC_DEMO_BLOCK_Z, state == MC_BLOCK_STATE_AIR ? "air" : "redstone_block");
}

static mc_static_server_opts g_opts;
static int g_listen_fd = -1;
static pthread_t g_accept_tid;
static volatile int g_run_accept;
static _Atomic int32_t g_next_entity_id = 1;
/* Good for: Build world/spawn context for packet templates.
 * Callers: mc_static_server.c (same file).
 */

static mc_patch_ctx make_patch_ctx(const mc_client *cli) {
  const mc_server_world *w = mc_templates_world();
  mc_patch_ctx ctx;
  memset(&ctx, 0, sizeof ctx);
  ctx.entity_id = cli->entity_id;
  ctx.uuid = cli->uuid;
  ctx.username = cli->username;
  ctx.gamemode = cli->gamemode;
  ctx.teleport_id = cli->teleport_id;
  ctx.spawn_x = w ? w->spawn_x : 8.0;
  ctx.spawn_y = w ? w->spawn_y : 64.0;
  ctx.spawn_z = w ? w->spawn_z : 8.0;
  ctx.chunk_x = w ? w->spawn_chunk_x : 0;
  ctx.chunk_z = w ? w->spawn_chunk_z : 0;
  return ctx;
}
/* Good for: Send play join templates and mark initial chunk grid.
 * Callers: mc_spectator.c, mc_static_server.c (same file).
 */

static int enter_play(mc_client *cli, mc_chunk_stream *chunks) {
  mc_patch_ctx ctx = make_patch_ctx(cli);
  if (mc_template_send_play_join(cli->fd, &ctx) != 0) return -1;
  if (mc_static_chunks_upstream()) {
    if (mc_template_send_upstream_world(cli->fd, &ctx) != 0) {
      MC_LOGW("static_server", "upstream chunk send failed (wire error); continuing play join");
    }
  } else if (mc_template_send_grass_world(cli->fd, &ctx) != 0) {
    return -1;
  }
  const mc_server_world *w = mc_templates_world();
  int32_t cx = w->spawn_chunk_x ? w->spawn_chunk_x : (int32_t)floor(ctx.spawn_x / 16.0);
  int32_t cz = w->spawn_chunk_z ? w->spawn_chunk_z : (int32_t)floor(ctx.spawn_z / 16.0);
  mc_chunk_stream_init(chunks, w->view_radius);
  if (mc_static_chunks_upstream()) {
    mc_chunk_stream_mark_cached_grid(chunks, cx, cz);
  } else {
    mc_chunk_stream_mark_grid(chunks, cx, cz);
  }
  chunks->pos_x = ctx.spawn_x;
  chunks->pos_y = ctx.spawn_y;
  chunks->pos_z = ctx.spawn_z;
  chunks->has_pos = 1;
  return 0;
}
/* Good for: Apply C2S move and refresh chunk stream.
 * Callers: mc_static_server.c (same file).
 */

static int handle_player_move(mc_client *cli, mc_chunk_stream *chunks, double x, double y, double z) {
  return mc_chunk_stream_on_move(chunks, cli->fd, x, y, z);
}

static const char *cli_state_name(mc_cli_state state) {
  switch (state) {
    case MC_CLI_HANDSHAKE: return "handshake";
    case MC_CLI_STATUS: return "status";
    case MC_CLI_LOGIN: return "login";
    case MC_CLI_CONFIG: return "configuration";
    case MC_CLI_PLAY: return "play";
    default: return "?";
  }
}
/* Good for: Warn on unhandled client play packet id.
 * Callers: mc_static_server.c (same file).
 */

static void log_unhandled_c2s(const mc_client *cli, int32_t pkt_id, size_t payload_len) {
  const char *who = cli->username[0] ? cli->username : "?";
  MC_LOGI("static_server", "unhandled C2S %s 0x%02x len=%zu (%s)", cli_state_name(cli->state), pkt_id,
          payload_len, who);
}
/* Good for: Log unhandled C2S only when handler returned 0.
 * Callers: mc_static_server.c (same file).
 */

static void log_unhandled_if(int handled, const mc_client *cli, int32_t pkt_id, size_t payload_len) {
  if (!handled) log_unhandled_c2s(cli, pkt_id, payload_len);
}
/* Good for: True if play packet id is intentionally ignored.
 * Callers: mc_static_server.c (same file).
 */

static int play_packet_noop(int32_t pkt_id) {
  switch (pkt_id) {
    case MC_PKT_C2S_PONG:
    case MC_PKT_C2S_TICK_END:
    case MC_PKT_C2S_CHAT_COMMAND:
    case MC_PKT_C2S_CHAT:
    case MC_PKT_C2S_MOVE_ROT:
    case MC_PKT_C2S_MOVE_STATUS:
    case MC_PKT_C2S_CUSTOM_PAYLOAD:
    case MC_PKT_C2S_PLAYER_ACTION:
    case MC_PKT_C2S_PLAYER_INPUT:
    case MC_PKT_C2S_SWING:
    case MC_PKT_C2S_USE_ITEM_ON:
    case MC_PKT_C2S_USE_ITEM:
    case MC_PKT_C2S_CHUNK_BATCH_RECEIVED:
    case MC_PKT_C2S_SET_CARRIED_ITEM:
      return 1;
    default:
      return 0;
  }
}
/* Good for: poll() timeout for static server accept loop.
 * Callers: mc_static_server.c (same file).
 */

static int poll_timeout_ms(const mc_client *cli) {
  return cli->state == MC_CLI_PLAY ? 1000 : -1;
}
/* Good for: Per-client thread: handshake, config, play loop.
 * Callers: mc_spectator.c, mc_static_server.c (same file).
 */

static void handle_client(int fd) {
  mc_client cli;
  memset(&cli, 0, sizeof cli);
  cli.fd = fd;
  cli.state = MC_CLI_HANDSHAKE;
  cli.entity_id = atomic_fetch_add(&g_next_entity_id, 1);
  cli.gamemode = g_opts.gamemode;
  cli.teleport_id = 0;
  cli.auth_state = MC_AUTH_NONE;
  int config_sync_sent = 0;
  mc_chunk_stream chunks;

  const mc_auth_ops *auth = g_opts.auth ? g_opts.auth : &mc_auth_offline;
  int demo_show_redstone = 1;
  int64_t demo_last_toggle_ms = 0;

  for (;;) {
    if (mc_client_tick_keep_alive(&cli) != 0) {
      MC_LOGW("static_server", "%s timed out (keep_alive)", cli.username[0] ? cli.username : "?");
      break;
    }

    if (cli.state == MC_CLI_PLAY) tick_demo_block(&cli, &chunks, &demo_show_redstone, &demo_last_toggle_ms);

    struct pollfd pfd = {.fd = fd, .events = POLLIN};
    int pr = poll(&pfd, 1, poll_timeout_ms(&cli));
    if (pr < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (pr == 0) continue;

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
      cli.state = (next == MC_HS_LOGIN)   ? MC_CLI_LOGIN
                  : (next == MC_HS_STATUS) ? MC_CLI_STATUS
                                           : MC_CLI_HANDSHAKE;
      if (next == MC_HS_STATUS) {
        MC_LOGI("static_server", "status ping (server list)");
      } else if (next == MC_HS_LOGIN) {
        MC_LOGI("static_server", "login handshake");
      }
      free(packet);
      continue;
    }

    if (cli.state == MC_CLI_STATUS) {
      int handled = 0;
      if (pkt_id == MC_PKT_C2S_STATUS_REQUEST) {
        handled = 1;
        if (mc_send_status_response(fd) != 0) {
          free(packet);
          break;
        }
        MC_LOGI("static_server", "status response sent");
      } else if (pkt_id == MC_PKT_C2S_STATUS_PING) {
        handled = 1;
        lc_buf pb;
        lc_buf_init(&pb, payload, payload_len);
        int64_t ping_time;
        if (lc_buf_read_i64_be(&pb, &ping_time) != LC_OK) {
          free(packet);
          break;
        }
        if (mc_send_status_pong(fd, ping_time) != 0) {
          free(packet);
          break;
        }
        MC_LOGI("static_server", "status pong sent");
        free(packet);
        break;
      }
      log_unhandled_if(handled, &cli, pkt_id, payload_len);
      free(packet);
      continue;
    }

    if (cli.state == MC_CLI_LOGIN) {
      int handled = 0;
      if (pkt_id == MC_PKT_C2S_LOGIN_START) {
        handled = 1;
        if (auth->on_login_start(&cli, payload, payload_len) != 0 ||
            auth->send_login_success(&cli) != 0) {
          free(packet);
          break;
        }
        MC_LOGEV("static_server", "login success -> %s", cli.username);
      } else if (pkt_id == MC_PKT_C2S_LOGIN_ACKNOWLEDGED) {
        handled = 1;
        if (g_opts.registry_from_enabled) {
          mc_static_registries_start_fetch_on_login_acknowledged();
        }
        cli.state = MC_CLI_CONFIG;
        MC_LOGEV("static_server", "entering configuration");
        mc_patch_ctx ctx = make_patch_ctx(&cli);
        if (mc_template_send_config_sequence(cli.fd, &ctx) != 0) {
          free(packet);
          break;
        }
        if (mc_client_send_keep_alive(&cli) != 0) {
          free(packet);
          break;
        }
      }
      log_unhandled_if(handled, &cli, pkt_id, payload_len);
      free(packet);
      continue;
    }

    if (cli.state == MC_CLI_CONFIG) {
      int handled = 0;
      if (pkt_id == MC_PKT_C2S_CFG_PONG) {
        handled = 1;
      } else if (pkt_id == MC_PKT_C2S_CFG_KEEP_ALIVE) {
        handled = 1;
        mc_client_handle_keep_alive(&cli, payload, payload_len);
      } else if (pkt_id == MC_PKT_C2S_CFG_SETTINGS || pkt_id == MC_PKT_C2S_CFG_CUSTOM_PAYLOAD) {
        handled = 1;
      } else if (!config_sync_sent && pkt_id == MC_PKT_C2S_CFG_SELECT_KNOWN_PACKS) {
        handled = 1;
        if (mc_static_send_registry_sync(cli.fd, payload, payload_len) != 0) {
          free(packet);
          break;
        }
        config_sync_sent = 1;
        MC_LOGEV("static_server", "registry sync sent (%zu registries + tags)", mc_static_registry_count());
      } else if (pkt_id == MC_PKT_C2S_CFG_FINISH) {
        handled = 1;
        cli.state = MC_CLI_PLAY;
        MC_LOGEV("static_server", "entering play");
        if (enter_play(&cli, &chunks) != 0) {
          free(packet);
          break;
        }
        cli.play_ready = 1;
        if (mc_client_send_keep_alive(&cli) != 0) {
          free(packet);
          break;
        }
        MC_LOGI("static_server", "play ready for %s (entity=%d)", cli.username, cli.entity_id);
      }
      log_unhandled_if(handled, &cli, pkt_id, payload_len);
      free(packet);
      continue;
    }

    if (cli.state == MC_CLI_PLAY) {
      mc_log_c2s_play(cli.username, pkt_id, payload, payload_len);

      int handled = 0;
      if (pkt_id == MC_PKT_C2S_KEEP_ALIVE) {
        handled = 1;
        mc_client_handle_keep_alive(&cli, payload, payload_len);
      } else if (pkt_id == MC_PKT_C2S_PLAYER_LOADED) {
        handled = 1;
        MC_LOGI("static_server", "%s player_loaded (world ready)", cli.username);
      } else if (pkt_id == MC_PKT_C2S_TELEPORT_CONFIRM) {
        handled = 1;
        lc_c2s_teleport_confirm tc;
        if (lc_parse_c2s_teleport_confirm(payload, payload_len, &tc) == LC_OK) {
          cli.teleport_id = tc.teleport_id;
        }
      } else if (pkt_id == MC_PKT_C2S_POSITION) {
        handled = 1;
        lc_c2s_position pos;
        if (lc_parse_c2s_position(payload, payload_len, &pos) == LC_OK) {
          if (handle_player_move(&cli, &chunks, pos.x, pos.y, pos.z) != 0) {
            free(packet);
            break;
          }
        }
      } else if (pkt_id == MC_PKT_C2S_POSITION_LOOK) {
        handled = 1;
        lc_c2s_position_look pos;
        if (lc_parse_c2s_position_look(payload, payload_len, &pos) == LC_OK) {
          if (handle_player_move(&cli, &chunks, pos.x, pos.y, pos.z) != 0) {
            free(packet);
            break;
          }
        }
      } else if (play_packet_noop(pkt_id)) {
        handled = 1;
      }
      log_unhandled_if(handled, &cli, pkt_id, payload_len);
      free(packet);
      continue;
    }

    free(packet);
  }
  close(fd);
}

static void *client_thread(void *arg) {
  mc_client_thread_ctx *ctx = (mc_client_thread_ctx *)arg;
  int fd = ctx->fd;
  MC_LOGCN("static_server", "incoming connection from %s:%u", ctx->peer_addr, (unsigned)ctx->peer_port);
  handle_client(fd);
  MC_LOGCN("static_server", "connection closed from %s:%u", ctx->peer_addr, (unsigned)ctx->peer_port);
  free(ctx);
  return NULL;
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
      mc_log_errno("static_server", "accept");
      continue;
    }
    int nodelay = 1;
    setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof nodelay);

    mc_client_thread_ctx *ctx = (mc_client_thread_ctx *)malloc(sizeof *ctx);
    if (!ctx) {
      close(cfd);
      continue;
    }
    ctx->fd = cfd;
    ctx->peer_port = ntohs(peer.sin_port);
    if (!inet_ntop(AF_INET, &peer.sin_addr, ctx->peer_addr, sizeof ctx->peer_addr)) {
      snprintf(ctx->peer_addr, sizeof ctx->peer_addr, "?");
    }

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&tid, &attr, client_thread, ctx) != 0) {
      mc_log_errno("static_server", "pthread_create(client)");
      pthread_attr_destroy(&attr);
      free(ctx);
      close(cfd);
      continue;
    }
    pthread_attr_destroy(&attr);
  }
  return NULL;
}
/* Good for: Reference static Minecraft server: config / registries / grass world.
 * Callers: mc_static_server.c.
 */

int mc_static_server_start(const mc_static_server_opts *opts) {
  if (!opts) return -1;
  if (g_listen_fd >= 0) return 0;

  g_opts = *opts;
  if (!g_opts.auth) g_opts.auth = &mc_auth_offline;
  if (g_opts.port <= 0) g_opts.port = 25565;
  if (!g_opts.host) g_opts.host = "0.0.0.0";

  if (opts->registry_from_enabled) {
    mc_static_registries_set_fetch(&opts->registry_from);
  } else {
    mc_static_registries_set_fetch(NULL);
  }

  if (mc_templates_init() != 0) {
    MC_LOGE("static_server", "failed to init packet builders");
    return -1;
  }

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    mc_log_errno("static_server", "socket");
    mc_templates_free();
    return -1;
  }
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)g_opts.port);
  if (inet_pton(AF_INET, g_opts.host, &addr.sin_addr) != 1) {
    MC_LOGE("static_server", "invalid host %s", g_opts.host);
    close(fd);
    mc_templates_free();
    return -1;
  }
  if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
    mc_log_errno("static_server", "bind");
    close(fd);
    mc_templates_free();
    return -1;
  }
  if (listen(fd, 128) != 0) {
    mc_log_errno("static_server", "listen");
    close(fd);
    mc_templates_free();
    return -1;
  }
  g_listen_fd = fd;
  g_run_accept = 1;
  if (pthread_create(&g_accept_tid, NULL, accept_thread, NULL) != 0) {
    mc_log_errno("static_server", "pthread_create");
    close(g_listen_fd);
    g_listen_fd = -1;
    mc_templates_free();
    return -1;
  }
  if (opts->registry_from_enabled) {
    MC_LOGI("static_server", "registry source: remote %s:%d (user=%s, load on first join)",
            opts->registry_from.host, opts->registry_from.port,
            opts->registry_from.username ? opts->registry_from.username : "FlayerBot");
    MC_LOGI("static_server", "chunk source: upstream map_chunk cache (no local generation)");
  } else {
    MC_LOGI("static_server", "registry source: embedded (load on first join)");
  }
  MC_LOGI("static_server", "listening on %s:%d gamemode=%d", g_opts.host, g_opts.port, (int)g_opts.gamemode);
  return 0;
}
/* Good for: Reference static Minecraft server: config / registries / grass world.
 * Callers: libchunk.h (public API, no .c callers in tree).
 */

void mc_static_server_stop(void) {
  if (g_listen_fd < 0) return;
  g_run_accept = 0;
  shutdown(g_listen_fd, SHUT_RDWR);
  pthread_join(g_accept_tid, NULL);
  close(g_listen_fd);
  g_listen_fd = -1;
  mc_templates_free();
}
