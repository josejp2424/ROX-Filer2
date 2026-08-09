/*
 * ROX-Filer technical diagnostic log.
 * Added for the X11/Wayland desktop backends (2026).
 */
#ifndef ROX_DEBUG_LOG_H
#define ROX_DEBUG_LOG_H

#include <glib.h>

typedef enum {
    ROX_DEBUG_LEVEL_ERROR = 0,
    ROX_DEBUG_LEVEL_WARNING,
    ROX_DEBUG_LEVEL_INFO,
    ROX_DEBUG_LEVEL_DEBUG,
    ROX_DEBUG_LEVEL_TRACE
} RoxDebugLevel;

/* Scans the command line before gtk_init(), clears old logs when requested,
 * and opens the diagnostic file only when --debug, --log-file or --log-level
 * is present. */
gboolean rox_debug_log_preconfigure(gint argc, gchar **argv,
                                    gboolean *clear_requested,
                                    GError **error);

gboolean rox_debug_log_is_enabled(void);
const gchar *rox_debug_log_get_path(void);
const gchar *rox_debug_log_get_directory(void);
RoxDebugLevel rox_debug_log_get_level(void);

void rox_debug_log_message(RoxDebugLevel level, const gchar *category,
                           const gchar *format, ...) G_GNUC_PRINTF(3, 4);
void rox_debug_log_close(void);

gboolean rox_debug_log_clear_default(GError **error);

#define ROX_LOG_ERROR(category, ...) \
    rox_debug_log_message(ROX_DEBUG_LEVEL_ERROR, category, __VA_ARGS__)
#define ROX_LOG_WARNING(category, ...) \
    rox_debug_log_message(ROX_DEBUG_LEVEL_WARNING, category, __VA_ARGS__)
#define ROX_LOG_INFO(category, ...) \
    rox_debug_log_message(ROX_DEBUG_LEVEL_INFO, category, __VA_ARGS__)
#define ROX_LOG_DEBUG(category, ...) \
    rox_debug_log_message(ROX_DEBUG_LEVEL_DEBUG, category, __VA_ARGS__)
#define ROX_LOG_TRACE(category, ...) \
    rox_debug_log_message(ROX_DEBUG_LEVEL_TRACE, category, __VA_ARGS__)

#endif
