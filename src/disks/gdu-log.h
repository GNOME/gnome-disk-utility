/* gdu-log.h
 *
 * Copyright 2024 Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * Author(s):
 *   Mohammed Sadiq <sadiq@sadiqpk.org>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <glib.h>

#ifndef GDU_LOG_LEVEL_TRACE
# define GDU_LOG_LEVEL_TRACE ((GLogLevelFlags)(1 << G_LOG_LEVEL_USER_SHIFT))
# define GDU_LOG_DETAILED ((GLogLevelFlags)(1 << (G_LOG_LEVEL_USER_SHIFT + 1)))
#endif

#define GDU_TRACE_MSG(fmt, ...)                         \
  gdu_log (G_LOG_DOMAIN, GDU_LOG_LEVEL_TRACE,           \
           NULL, __FILE__, G_STRINGIFY (__LINE__),      \
           G_STRFUNC, fmt, ##__VA_ARGS__)
#define GDU_TRACE(value, fmt, ...)                      \
  gdu_log (G_LOG_DOMAIN,                                \
           GDU_LOG_LEVEL_TRACE,                         \
           value, __FILE__, G_STRINGIFY (__LINE__),     \
           G_STRFUNC, fmt, ##__VA_ARGS__)
#define GDU_TRACE_DETAILED(value, fmt, ...)             \
  gdu_log (G_LOG_DOMAIN,                                \
           GDU_LOG_LEVEL_TRACE | GDU_LOG_DETAILED,      \
           value, __FILE__, G_STRINGIFY (__LINE__),     \
           G_STRFUNC, fmt, ##__VA_ARGS__)
#define GDU_DEBUG_MSG(fmt, ...)                         \
  gdu_log (G_LOG_DOMAIN,                                \
           G_LOG_LEVEL_DEBUG | GDU_LOG_DETAILED,        \
           NULL, __FILE__, G_STRINGIFY (__LINE__),      \
           G_STRFUNC, fmt, ##__VA_ARGS__)
#define GDU_DEBUG(value, fmt, ...)                      \
  gdu_log (G_LOG_DOMAIN,                                \
           G_LOG_LEVEL_DEBUG | GDU_LOG_DETAILED,        \
           value, __FILE__, G_STRINGIFY (__LINE__),     \
           G_STRFUNC, fmt, ##__VA_ARGS__)
#define GDU_WARNING(value, fmt, ...)                    \
  gdu_log (G_LOG_DOMAIN,                                \
           G_LOG_LEVEL_WARNING | GDU_LOG_DETAILED,      \
           value, __FILE__, G_STRINGIFY (__LINE__),     \
           G_STRFUNC, fmt, ##__VA_ARGS__)

void         gdu_log_init               (void);
void         gdu_log_increase_verbosity (void);
int          gdu_log_get_verbosity      (void);
void         gdu_log_to_file            (const char     *file_path,
                                         gboolean        append);
const char  *gdu_log_bool_str           (gboolean        value,
                                         gboolean        use_success);
void         gdu_log                    (const char     *domain,
                                         GLogLevelFlags  log_level,
                                         const char     *value,
                                         const char     *file,
                                         const char     *line,
                                         const char     *func,
                                         const char     *message_format,
                                         ...) G_GNUC_PRINTF (7, 8);
void         gdu_log_anonymize_value    (GString        *str,
                                         const char     *value);
