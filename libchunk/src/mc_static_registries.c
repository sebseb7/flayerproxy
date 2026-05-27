#include "mc_log.h"
#include "mc_static_registries.h"

#include "internal.h"
#include "libchunk.h"
#include "mc_packet_ids.h"
#include "mc_server_common.h"
#include "mc_static_registries_data.h"
#include "mc_wire.h"
#include "packets_write.h"

#include <stdlib.h>
#include <string.h>

static lc_registry_data *g_registries;
static size_t g_registry_count;
static lc_update_tags g_tags;
static int g_tags_loaded;
/* Good for: Send one registry_data blob on wire.
 * Callers: mc_static_registries.c (same file).
 */

static int send_registry_blob(int fd, const mc_static_blob *blob) {
  return mc_send_frame(fd, MC_PKT_CFG_REGISTRY_DATA, blob->data, blob->len);
}
/* Good for: Send update_tags blob on wire.
 * Callers: mc_static_registries.c (same file).
 */

static int send_tags_blob(int fd) {
  return mc_send_frame(fd, MC_PKT_COMMON_UPDATE_TAGS, mc_static_tags.data, mc_static_tags.len);
}
/* Good for: Send reset_chat configuration packet.
 * Callers: mc_static_registries.c (same file).
 */

static int send_reset_chat(int fd) {
  return mc_send_frame(fd, MC_PKT_CFG_RESET_CHAT, NULL, 0);
}
/* Good for: Reference static Minecraft server: config / registries / grass world.
 * Callers: mc_wire_templates.c.
 */

int mc_static_registries_init(void) {
  mc_static_registries_free();

  g_registry_count = mc_static_registry_blob_count;
  g_registries = (lc_registry_data *)calloc(g_registry_count, sizeof(lc_registry_data));
  if (!g_registries) return -1;

  for (size_t i = 0; i < g_registry_count; i++) {
    const mc_static_blob *blob = &mc_static_registry_blobs[i];
    if (lc_parse_registry_data(blob->data, blob->len, &g_registries[i]) != LC_OK) {
      MC_LOGE("static_server", "failed to parse registry %s", blob->label);
      mc_static_registries_free();
      return -1;
    }
  }

  memset(&g_tags, 0, sizeof g_tags);
  if (lc_parse_update_tags(mc_static_tags.data, mc_static_tags.len, &g_tags) != LC_OK) {
    MC_LOGE("static_server", "failed to parse update_tags");
    mc_static_registries_free();
    return -1;
  }
  g_tags_loaded = 1;
  return 0;
}
/* Good for: Reference static Minecraft server: config / registries / grass world.
 * Callers: mc_static_registries.c (same file), mc_wire_templates.c.
 */

void mc_static_registries_free(void) {
  if (g_registries) {
    for (size_t i = 0; i < g_registry_count; i++) lc_registry_data_free(&g_registries[i]);
    free(g_registries);
    g_registries = NULL;
  }
  g_registry_count = 0;
  if (g_tags_loaded) lc_update_tags_free(&g_tags);
  g_tags_loaded = 0;
}
/* Good for: Reference static Minecraft server: config / registries / grass world.
 * Callers: mc_static_server.c.
 */

int mc_static_send_registry_sync(int fd) {
  if (!g_registries || !g_tags_loaded) return -1;

  for (size_t i = 0; i < g_registry_count; i++) {
    if (i == 12) {
      if (send_tags_blob(fd) != 0) return -1;
    }
    if (send_registry_blob(fd, &mc_static_registry_blobs[i]) != 0) return -1;
  }
  if (g_registry_count <= 12) {
    if (send_tags_blob(fd) != 0) return -1;
  }
  if (send_reset_chat(fd) != 0) return -1;
  return mc_send_frame(fd, MC_PKT_CFG_FINISH, NULL, 0);
}
