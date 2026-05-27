#define _POSIX_C_SOURCE 200809L

#include "mc_log.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int g_color = -1;

static int use_color(void) {
  if (g_color < 0) g_color = isatty(fileno(stderr)) ? 1 : 0;
  return g_color;
}

void mc_log_set_color(int enabled) { g_color = enabled ? 1 : 0; }

static const char *level_color(mc_log_level level) {
  switch (level) {
    case MC_LOG_DEBUG:
      return "\033[90m";
    case MC_LOG_INFO:
      return "\033[36m";
    case MC_LOG_WARN:
      return "\033[33m";
    case MC_LOG_ERR:
      return "\033[31m";
    case MC_LOG_EVENT:
      return "\033[32m";
    case MC_LOG_CONN:
      return "\033[35m";
    default:
      return "";
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

void mc_log_vmsg(mc_log_level level, const char *tag, const char *fmt, va_list ap) {
  FILE *out = stderr;
  const int color = use_color();

  if (color) fputs("\033[2m", out);
  fputc('[', out);
  write_timestamp(out);
  fputc(']', out);
  if (color) fputs("\033[0m ", out);
  else fputc(' ', out);

  if (tag && tag[0]) {
    if (color) fputs("\033[1m", out);
    fputs(tag, out);
    if (color) fputs("\033[0m: ", out);
    else fputs(": ", out);
  }

  if (color) fputs(level_color(level), out);
  vfprintf(out, fmt, ap);
  if (color) fputs("\033[0m", out);
  fputc('\n', out);
}

void mc_log_msg(mc_log_level level, const char *tag, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  mc_log_vmsg(level, tag, fmt, ap);
  va_end(ap);
}

void mc_log_errno(const char *tag, const char *what) {
  mc_log_msg(MC_LOG_ERR, tag, "%s: %s", what, strerror(errno));
}
