#ifndef MC_LOG_H
#define MC_LOG_H

typedef enum mc_log_level {
  MC_LOG_DEBUG,
  MC_LOG_INFO,
  MC_LOG_WARN,
  MC_LOG_ERR,
  MC_LOG_EVENT,
  MC_LOG_CONN,
} mc_log_level;

/** Force color on (1) or off (0). Default: auto from isatty(stderr). */
void mc_log_set_color(int enabled);

void mc_log_msg(mc_log_level level, const char *tag, const char *fmt, ...);
void mc_log_errno(const char *tag, const char *what);

#define MC_LOGD(tag, ...) mc_log_msg(MC_LOG_DEBUG, tag, __VA_ARGS__)
#define MC_LOGI(tag, ...) mc_log_msg(MC_LOG_INFO, tag, __VA_ARGS__)
#define MC_LOGW(tag, ...) mc_log_msg(MC_LOG_WARN, tag, __VA_ARGS__)
#define MC_LOGE(tag, ...) mc_log_msg(MC_LOG_ERR, tag, __VA_ARGS__)
#define MC_LOGEV(tag, ...) mc_log_msg(MC_LOG_EVENT, tag, __VA_ARGS__)
#define MC_LOGCN(tag, ...) mc_log_msg(MC_LOG_CONN, tag, __VA_ARGS__)

#endif
