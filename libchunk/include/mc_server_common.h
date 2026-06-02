#ifndef MC_SERVER_COMMON_H
#define MC_SERVER_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

#include "mc_packet_ids.h"

#define MC_USERNAME_MAX 64
#define MC_WIRE_MAX (4 * 1024 * 1024)
#define MC_KEEP_ALIVE_INTERVAL_MS 15000

typedef enum {
  MC_CLI_HANDSHAKE,
  MC_CLI_STATUS,
  MC_CLI_LOGIN,
  MC_CLI_CONFIG,
  MC_CLI_PLAY,
} mc_cli_state;

typedef enum {
  MC_AUTH_NONE,
  MC_AUTH_OFFLINE,
  MC_AUTH_PENDING_CHAT,
  MC_AUTH_OK,
} mc_auth_state;

typedef enum {
  MC_GM_SURVIVAL = 0,
  MC_GM_CREATIVE = 1,
  MC_GM_ADVENTURE = 2,
  MC_GM_SPECTATOR = 3,
} mc_gamemode;

typedef struct mc_client {
  int fd;
  mc_cli_state state;
  char username[MC_USERNAME_MAX];
  uint8_t uuid[16];
  int32_t entity_id;
  int play_ready;
  mc_auth_state auth_state;
  int chat_authenticated;
  mc_gamemode gamemode;
  int32_t teleport_id;
  int64_t keep_alive_challenge;
  int64_t keep_alive_sent_ms;
  int keep_alive_pending;
} mc_client;

ssize_t mc_send_all(int fd, const void *buf, size_t len);
int mc_send_frame(int fd, int32_t pkt_id, const uint8_t *payload, size_t payload_len);
/** Like mc_send_frame; on failure logs context, packet id, sizes, and send errno / short write. */
int mc_send_frame_logged(int fd, int32_t pkt_id, const uint8_t *payload, size_t payload_len, const char *context);
int mc_send_wire_framed(int fd, const uint8_t *wire, size_t wire_len);
int mc_read_packet(int fd, uint8_t **out, size_t *out_len, int32_t *pkt_id);

int mc_parse_login_start(const uint8_t *payload, size_t len, mc_client *cli);
int mc_send_compress_disable(mc_client *cli);
int mc_send_config_finish(mc_client *cli);

int mc_send_status_response(int fd);
int mc_send_status_pong(int fd, int64_t ping_time);
int mc_send_keep_alive(int fd, int32_t pkt_id, int64_t id);
int mc_client_send_keep_alive(mc_client *cli);
int mc_client_handle_keep_alive(mc_client *cli, const uint8_t *payload, size_t payload_len);
/** Call periodically (~1s). Sends keep-alive every 15s; returns -1 if client timed out. */
int mc_client_tick_keep_alive(mc_client *cli);

void mc_offline_uuid(const char *username, uint8_t uuid[16]);

#endif
