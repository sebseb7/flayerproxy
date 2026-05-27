#include "mc_auth.h"

#include "mc_packet_ids.h"
#include "mc_wire.h"

#include <string.h>

static int offline_login_start(mc_client *cli, const uint8_t *payload, size_t len) {
  if (mc_parse_login_start(payload, len, cli) != 0) return -1;
  mc_offline_uuid(cli->username, cli->uuid);
  cli->auth_state = MC_AUTH_OFFLINE;
  return 0;
}

static int offline_send_login_success(mc_client *cli) {
  mc_buf p;
  memset(&p, 0, sizeof p);
  if (mc_buf_uuid(&p, cli->uuid) != LC_OK) return -1;
  if (mc_buf_string(&p, cli->username) != LC_OK) return -1;
  if (mc_buf_varint(&p, 0) != LC_OK) return -1;
  if (mc_send_frame(cli->fd, MC_PKT_LOGIN_SUCCESS, p.data, p.len) != 0) {
    mc_buf_free(&p);
    return -1;
  }
  mc_buf_free(&p);
  /* Omit Set Compression when disabled; sending id 0x03 after config switch
   * is misread as finish_configuration (5-byte threshold varint). */
  cli->auth_state = MC_AUTH_OK;
  return 0;
}

const mc_auth_ops mc_auth_offline = {
    .on_login_start = offline_login_start,
    .send_login_success = offline_send_login_success,
};
