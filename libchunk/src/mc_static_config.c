#include "mc_static_config.h"

#include "mc_packet_ids.h"
#include "mc_server_common.h"
#include "mc_wire.h"

#include <string.h>

#define MC_PKT_COMMON_CUSTOM_PAYLOAD 0x01

static int send_custom_payload_brand(int fd) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_string(&b, "minecraft:brand") != LC_OK) return -1;
  const char brand[] = "Flayer (static)";
  if (mc_buf_varint(&b, (int32_t)sizeof brand - 1) != LC_OK) return -1;
  if (mc_buf_write(&b, (const uint8_t *)brand, sizeof brand - 1) != LC_OK) return -1;
  int rc = mc_send_frame(fd, MC_PKT_COMMON_CUSTOM_PAYLOAD, b.data, b.len);
  mc_buf_free(&b);
  return rc;
}

static int send_feature_flags(int fd) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_varint(&b, 1) != LC_OK) return -1;
  if (mc_buf_string(&b, "minecraft:vanilla") != LC_OK) return -1;
  int rc = mc_send_frame(fd, MC_PKT_CFG_FEATURE_FLAGS, b.data, b.len);
  mc_buf_free(&b);
  return rc;
}

static int send_select_known_packs(int fd) {
  mc_buf b;
  memset(&b, 0, sizeof b);
  if (mc_buf_varint(&b, 1) != LC_OK) return -1;
  if (mc_buf_string(&b, "minecraft") != LC_OK) return -1;
  if (mc_buf_string(&b, "core") != LC_OK) return -1;
  if (mc_buf_string(&b, "1.21.4") != LC_OK) return -1;
  int rc = mc_send_frame(fd, MC_PKT_CFG_SELECT_KNOWN_PACKS, b.data, b.len);
  mc_buf_free(&b);
  return rc;
}

int mc_static_send_config_preamble(int fd) {
  if (send_custom_payload_brand(fd) != 0) return -1;
  if (send_feature_flags(fd) != 0) return -1;
  return send_select_known_packs(fd);
}
