#define _POSIX_C_SOURCE 200809L

#include "mc_conn_state.h"

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static mc_conn_state_tracker g_upstream_tracker;

static int g_color = -1;

static int use_color(void) {
  if (g_color < 0) g_color = isatty(fileno(stderr)) ? 1 : 0;
  return g_color;
}

const char *mc_conn_link_name(mc_conn_link link) {
  return link == MC_CONN_LINK_UP ? "UP" : "DOWN";
}

const char *mc_conn_state_name(mc_conn_state state) {
  switch (state) {
    case MC_CONN_STATE_INITIATED:
      return "INITIATED";
    case MC_CONN_STATE_CONFIGURED:
      return "CONFIGURED";
    case MC_CONN_STATE_PLAYING:
      return "PLAYING";
    case MC_CONN_STATE_DISCONNECTED:
      return "DISCONNECTED";
    default:
      return "?";
  }
}

void mc_conn_state_tracker_init(mc_conn_state_tracker *tracker) {
  if (!tracker) return;
  tracker->state = MC_CONN_STATE_DISCONNECTED;
  tracker->has_state = 0;
}

void mc_conn_state_upstream_reset(void) { mc_conn_state_tracker_init(&g_upstream_tracker); }

void mc_conn_state_upstream(mc_conn_state state, const char *detail) {
  mc_conn_state_transition(MC_CONN_LINK_UP, &g_upstream_tracker, state, detail);
}

static const char *link_badge_color(mc_conn_link link) {
  return link == MC_CONN_LINK_UP ? "\033[1;95m" : "\033[1;94m";
}

static const char *state_color(mc_conn_state state) {
  switch (state) {
    case MC_CONN_STATE_INITIATED:
      return "\033[1;97m";
    case MC_CONN_STATE_CONFIGURED:
      return "\033[1;93m";
    case MC_CONN_STATE_PLAYING:
      return "\033[1;92m";
    case MC_CONN_STATE_DISCONNECTED:
      return "\033[1;91m";
    default:
      return "\033[1m";
  }
}

static void write_timestamp(FILE *out) {
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    fputs("??:??:??.???", out);
    return;
  }
  struct tm tm;
  if (!localtime_r(&ts.tv_sec, &tm)) {
    fputs("??:??:??.???", out);
    return;
  }
  char buf[16];
  if (strftime(buf, sizeof buf, "%H:%M:%S", &tm) == 0) {
    fputs("??:??:??.???", out);
    return;
  }
  fprintf(out, "%s.%03ld", buf, ts.tv_nsec / 1000000L);
}

void mc_conn_state_transition(mc_conn_link link, mc_conn_state_tracker *tracker, mc_conn_state new_state,
                              const char *detail) {
  if (!tracker) return;
  if (tracker->has_state && tracker->state == new_state) return;

  const mc_conn_state prev = tracker->has_state ? tracker->state : new_state;
  const int first = !tracker->has_state;
  tracker->state = new_state;
  tracker->has_state = 1;

  FILE *out = stderr;
  const int color = use_color();

  if (color) fputs("\033[2m", out);
  fputc('[', out);
  write_timestamp(out);
  fputc(']', out);
  if (color) fputs("\033[0m ", out);
  else fputc(' ', out);

  if (color) fputs("\033[1m", out);
  fputs("static_server", out);
  if (color) fputs("\033[0m: ", out);
  else fputs(": ", out);

  if (color) fputs("\033[2m", out);
  fputs("conn ", out);
  if (color) fputs("\033[0m", out);

  if (color) fputs(link_badge_color(link), out);
  fputs(mc_conn_link_name(link), out);
  if (color) fputs("\033[0m", out);

  if (detail && detail[0]) {
    fputs(" ", out);
    if (color) fputs("\033[2m", out);
    fputs("(", out);
    if (color) fputs("\033[0m", out);
    fputs(detail, out);
    if (color) fputs("\033[2m", out);
    fputs(")", out);
    if (color) fputs("\033[0m", out);
  }

  fputs("  ", out);

  if (!first) {
    if (color) fputs(state_color(prev), out);
    fputs(mc_conn_state_name(prev), out);
    if (color) fputs("\033[0m", out);
    fputs(" ", out);
    if (color) fputs("\033[1;96m", out);
    fputs("-> ", out);
    if (color) fputs("\033[0m", out);
  }

  if (color) fputs(state_color(new_state), out);
  fputs(mc_conn_state_name(new_state), out);
  if (color) fputs("\033[0m\n", out);
  else fputc('\n', out);
}
