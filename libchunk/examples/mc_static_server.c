/**
 * Standalone Minecraft 1.21.10 static grass-world server (offline auth).
 */
#include "mc_log.h"
#include "mc_static_server.h"

#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *argv0) {
  MC_LOGI("static_server", "Usage: %s [--host HOST] [--port PORT] [--gamemode survival|spectator]", argv0);
}

static mc_gamemode parse_gamemode(const char *s) {
  if (!s || strcmp(s, "survival") == 0) return MC_GM_SURVIVAL;
  if (strcmp(s, "spectator") == 0) return MC_GM_SPECTATOR;
  if (strcmp(s, "creative") == 0) return MC_GM_CREATIVE;
  if (strcmp(s, "adventure") == 0) return MC_GM_ADVENTURE;
  return MC_GM_SURVIVAL;
}

int main(int argc, char **argv) {
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);

  mc_static_server_opts opts;
  memset(&opts, 0, sizeof opts);
  opts.host = "0.0.0.0";
  opts.port = 25565;
  opts.gamemode = MC_GM_SURVIVAL;
  opts.auth = &mc_auth_offline;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
      opts.host = argv[++i];
    } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
      opts.port = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--gamemode") == 0 && i + 1 < argc) {
      opts.gamemode = parse_gamemode(argv[++i]);
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      usage(argv[0]);
      return 0;
    } else {
      usage(argv[0]);
      return 1;
    }
  }

  if (mc_static_server_start(&opts) != 0) return 1;
  for (;;) pause();
  return 0;
}
