/*
 * Rox-Filer2 technical diagnostic log.
 *
 * Disabled by default.  When enabled, the default location follows XDG:
 *   $XDG_STATE_HOME/rox-filer2/
 * or ~/.local/state/rox-filer2/ when XDG_STATE_HOME is unset.
 *
 * The default rotation keeps five files and limits each file to 2 MiB.
 */
#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "debug_log.h"

#define ROX_DEBUG_LOG_MAX_FILES 5
#define ROX_DEBUG_LOG_MAX_BYTES (2u * 1024u * 1024u)

typedef struct {
    gchar *path;
    gint64 mtime;
} RoxLogFile;

static FILE *debug_stream;
static gchar *debug_path;
static gchar *debug_directory;
static RoxDebugLevel debug_level = ROX_DEBUG_LEVEL_DEBUG;
static gsize debug_bytes;
static gboolean debug_limit_reported;
static gboolean handler_installed;
static GLogFunc previous_handler;
static gpointer previous_handler_data;
static GMutex debug_mutex;

static const gchar *level_name(RoxDebugLevel level)
{
    switch (level) {
        case ROX_DEBUG_LEVEL_ERROR: return "ERROR";
        case ROX_DEBUG_LEVEL_WARNING: return "WARNING";
        case ROX_DEBUG_LEVEL_INFO: return "INFO";
        case ROX_DEBUG_LEVEL_DEBUG: return "DEBUG";
        case ROX_DEBUG_LEVEL_TRACE: return "TRACE";
        default: return "UNKNOWN";
    }
}

static gboolean parse_level(const gchar *text, RoxDebugLevel *level)
{
    if (!text || !*text || !level)
        return FALSE;
    if (!g_ascii_strcasecmp(text, "error"))
        *level = ROX_DEBUG_LEVEL_ERROR;
    else if (!g_ascii_strcasecmp(text, "warning") ||
             !g_ascii_strcasecmp(text, "warn"))
        *level = ROX_DEBUG_LEVEL_WARNING;
    else if (!g_ascii_strcasecmp(text, "info"))
        *level = ROX_DEBUG_LEVEL_INFO;
    else if (!g_ascii_strcasecmp(text, "debug"))
        *level = ROX_DEBUG_LEVEL_DEBUG;
    else if (!g_ascii_strcasecmp(text, "trace"))
        *level = ROX_DEBUG_LEVEL_TRACE;
    else
        return FALSE;
    return TRUE;
}

static gchar *default_state_directory(void)
{
    const gchar *xdg_state = g_getenv("XDG_STATE_HOME");
    const gchar *home = g_get_home_dir();
    gchar *base;
    gchar *result;

    if (xdg_state && g_path_is_absolute(xdg_state))
        base = g_strdup(xdg_state);
    else
        base = g_build_filename(home && *home ? home : g_get_tmp_dir(),
                                ".local", "state", NULL);
    result = g_build_filename(base, "rox-filer2", NULL);
    g_free(base);
    return result;
}

static gchar *backend_name_from_argv0(const gchar *argv0)
{
    const gchar *forced = g_getenv("ROX_DESKTOP_BACKEND");
    const gchar *gdk_backend = g_getenv("GDK_BACKEND");
    gchar *base;
    gchar *result;

    if (forced && *forced)
        return g_strdup(forced);
    if (gdk_backend && *gdk_backend) {
        if (strstr(gdk_backend, "wayland"))
            return g_strdup("wayland");
        if (strstr(gdk_backend, "x11"))
            return g_strdup("x11");
    }

    base = g_path_get_basename(argv0 && *argv0 ? argv0 : "Rox-Filer2");
    if (strstr(base, "wayland"))
        result = g_strdup("wayland");
    else if (strstr(base, "x11"))
        result = g_strdup("x11");
    else
        result = g_strdup("auto");
    g_free(base);
    return result;
}

static void log_file_free(RoxLogFile *file)
{
    if (!file)
        return;
    g_free(file->path);
    g_free(file);
}

static gint compare_log_files(gconstpointer a, gconstpointer b)
{
    const RoxLogFile *left = *(RoxLogFile * const *)a;
    const RoxLogFile *right = *(RoxLogFile * const *)b;
    if (left->mtime < right->mtime)
        return -1;
    if (left->mtime > right->mtime)
        return 1;
    return g_strcmp0(left->path, right->path);
}

static gboolean is_managed_log_name(const gchar *name)
{
    return name && g_str_has_prefix(name, "rox-") && g_str_has_suffix(name, ".log");
}

static gboolean rotate_default_logs(const gchar *directory, GError **error)
{
    GDir *dir;
    const gchar *name;
    GPtrArray *files;

    dir = g_dir_open(directory, 0, error);
    if (!dir)
        return FALSE;

    files = g_ptr_array_new_with_free_func((GDestroyNotify)log_file_free);
    while ((name = g_dir_read_name(dir)) != NULL) {
        gchar *path;
        GStatBuf st;
        RoxLogFile *file;

        if (!is_managed_log_name(name))
            continue;
        path = g_build_filename(directory, name, NULL);
        if (g_stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            g_free(path);
            continue;
        }
        file = g_new0(RoxLogFile, 1);
        file->path = path;
        file->mtime = (gint64)st.st_mtime;
        g_ptr_array_add(files, file);
    }
    g_dir_close(dir);

    g_ptr_array_sort(files, compare_log_files);
    while (files->len >= ROX_DEBUG_LOG_MAX_FILES) {
        RoxLogFile *file = g_ptr_array_index(files, 0);
        if (g_unlink(file->path) != 0 && errno != ENOENT) {
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                        "Unable to remove old Rox-Filer2 log '%s': %s",
                        file->path, g_strerror(errno));
            g_ptr_array_unref(files);
            return FALSE;
        }
        g_ptr_array_remove_index(files, 0);
    }
    g_ptr_array_unref(files);
    return TRUE;
}

static gchar *make_default_log_path(const gchar *argv0, GError **error)
{
    GDateTime *now;
    gchar *stamp;
    gchar *backend;
    gchar *filename;
    gchar *path;

    debug_directory = default_state_directory();
    if (g_mkdir_with_parents(debug_directory, 0700) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "Unable to create ROX log directory '%s': %s",
                    debug_directory, g_strerror(errno));
        return NULL;
    }
    if (!rotate_default_logs(debug_directory, error))
        return NULL;

    now = g_date_time_new_now_local();
    stamp = g_date_time_format(now, "%Y%m%d-%H%M%S");
    backend = backend_name_from_argv0(argv0);
    filename = g_strdup_printf("rox-%s-%s-%ld.log", backend, stamp,
                               (long)getpid());
    path = g_build_filename(debug_directory, filename, NULL);
    g_free(filename);
    g_free(backend);
    g_free(stamp);
    g_date_time_unref(now);
    return path;
}

static gboolean open_log_file(const gchar *requested_path, const gchar *argv0,
                              GError **error)
{
    gchar *parent;
    gint fd;

    if (requested_path && *requested_path) {
        debug_path = g_canonicalize_filename(requested_path, NULL);
        parent = g_path_get_dirname(debug_path);
        if (g_mkdir_with_parents(parent, 0700) != 0) {
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                        "Unable to create log directory '%s': %s",
                        parent, g_strerror(errno));
            g_free(parent);
            return FALSE;
        }
        debug_directory = g_strdup(parent);
        g_free(parent);
    } else {
        debug_path = make_default_log_path(argv0, error);
        if (!debug_path)
            return FALSE;
    }

#ifdef O_CLOEXEC
    fd = g_open(debug_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
#else
    fd = g_open(debug_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
#endif
    if (fd < 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "Unable to open ROX log '%s': %s", debug_path,
                    g_strerror(errno));
        return FALSE;
    }
#ifndef O_CLOEXEC
    {
        gint flags = fcntl(fd, F_GETFD, 0);
        if (flags >= 0)
            fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    }
#endif
    debug_stream = fdopen(fd, "w");
    if (!debug_stream) {
        gint saved_errno = errno;
        close(fd);
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(saved_errno),
                    "Unable to create stream for ROX log '%s': %s",
                    debug_path, g_strerror(saved_errno));
        return FALSE;
    }
    setvbuf(debug_stream, NULL, _IOLBF, 0);
    debug_bytes = 0;
    debug_limit_reported = FALSE;
    return TRUE;
}

static void write_line_unlocked(RoxDebugLevel level, const gchar *category,
                                const gchar *message)
{
    GDateTime *now;
    gchar *stamp;
    gchar *line;
    gsize length;

    if (!debug_stream || level > debug_level)
        return;

    now = g_date_time_new_now_local();
    stamp = g_date_time_format(now, "%Y-%m-%dT%H:%M:%S.%f%z");
    line = g_strdup_printf("%s | pid=%ld | %s | %s | %s\n",
                           stamp, (long)getpid(), level_name(level),
                           category && *category ? category : "general",
                           message ? message : "");
    length = strlen(line);

    if (debug_bytes + length > ROX_DEBUG_LOG_MAX_BYTES) {
        if (!debug_limit_reported) {
            static const gchar notice[] =
                "ROX diagnostic log reached the 2 MiB limit; further messages are suppressed.\n";
            fwrite(notice, 1, sizeof(notice) - 1, debug_stream);
            fflush(debug_stream);
            debug_limit_reported = TRUE;
        }
        g_free(line);
        g_free(stamp);
        g_date_time_unref(now);
        return;
    }

    fwrite(line, 1, length, debug_stream);
    fflush(debug_stream);
    debug_bytes += length;
    g_free(line);
    g_free(stamp);
    g_date_time_unref(now);
}

static RoxDebugLevel glib_level_to_rox(GLogLevelFlags flags)
{
    if (flags & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL))
        return ROX_DEBUG_LEVEL_ERROR;
    if (flags & G_LOG_LEVEL_WARNING)
        return ROX_DEBUG_LEVEL_WARNING;
    if (flags & (G_LOG_LEVEL_MESSAGE | G_LOG_LEVEL_INFO))
        return ROX_DEBUG_LEVEL_INFO;
    return ROX_DEBUG_LEVEL_DEBUG;
}

static void debug_glib_handler(const gchar *domain, GLogLevelFlags flags,
                               const gchar *message, gpointer user_data)
{
    RoxDebugLevel level = glib_level_to_rox(flags);
    gchar *category;
    (void)user_data;

    /* GdkPixbuf emits several lines for every builtin 14x14 menu icon when
     * G_MESSAGES_DEBUG is active. They hide useful MIME/backend events and
     * can consume the whole 2 MiB log without indicating an error. */
    if (domain && !g_strcmp0(domain, "GdkPixbuf") &&
        !(flags & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL |
                   G_LOG_LEVEL_WARNING)))
        return;

    category = g_strdup_printf("glib%s%s", domain && *domain ? ":" : "",
                               domain && *domain ? domain : "");
    rox_debug_log_message(level, category, "%s", message ? message : "");
    g_free(category);

    if (previous_handler)
        previous_handler(domain, flags, message, previous_handler_data);
    else
        g_log_default_handler(domain, flags, message, NULL);
}

gboolean rox_debug_log_clear_default(GError **error)
{
    gchar *directory = default_state_directory();
    GDir *dir;
    const gchar *name;

    dir = g_dir_open(directory, 0, NULL);
    if (!dir) {
        g_free(directory);
        return TRUE;
    }

    while ((name = g_dir_read_name(dir)) != NULL) {
        gchar *path;
        if (!is_managed_log_name(name))
            continue;
        path = g_build_filename(directory, name, NULL);
        if (g_unlink(path) != 0 && errno != ENOENT) {
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                        "Unable to remove ROX log '%s': %s", path,
                        g_strerror(errno));
            g_free(path);
            g_dir_close(dir);
            g_free(directory);
            return FALSE;
        }
        g_free(path);
    }
    g_dir_close(dir);
    g_rmdir(directory); /* Ignore ENOTEMPTY: non-log files are preserved. */
    g_free(directory);
    return TRUE;
}

gboolean rox_debug_log_preconfigure(gint argc, gchar **argv,
                                    gboolean *clear_requested,
                                    GError **error)
{
    gboolean enabled = FALSE;
    gboolean clear = FALSE;
    const gchar *requested_path = NULL;
    RoxDebugLevel requested_level = ROX_DEBUG_LEVEL_DEBUG;
    gint i;

    if (clear_requested)
        *clear_requested = FALSE;

    for (i = 1; i < argc; i++) {
        const gchar *arg = argv[i];
        const gchar *value;

        if (!g_strcmp0(arg, "--debug")) {
            enabled = TRUE;
            requested_level = ROX_DEBUG_LEVEL_DEBUG;
        } else if (!g_strcmp0(arg, "--clear-logs")) {
            clear = TRUE;
        } else if (g_str_has_prefix(arg, "--log-file=")) {
            enabled = TRUE;
            requested_path = arg + strlen("--log-file=");
        } else if (!g_strcmp0(arg, "--log-file") && i + 1 < argc) {
            enabled = TRUE;
            requested_path = argv[++i];
        } else if (g_str_has_prefix(arg, "--log-level=")) {
            enabled = TRUE;
            value = arg + strlen("--log-level=");
            if (!parse_level(value, &requested_level)) {
                g_set_error(error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                            "Unknown ROX log level '%s'", value);
                return FALSE;
            }
        } else if (!g_strcmp0(arg, "--log-level") && i + 1 < argc) {
            enabled = TRUE;
            value = argv[++i];
            if (!parse_level(value, &requested_level)) {
                g_set_error(error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                            "Unknown ROX log level '%s'", value);
                return FALSE;
            }
        }
    }

    if (clear && !rox_debug_log_clear_default(error))
        return FALSE;
    if (clear_requested)
        *clear_requested = clear;
    if (!enabled)
        return TRUE;

    debug_level = requested_level;
    if (!open_log_file(requested_path, argc > 0 ? argv[0] : NULL, error))
        return FALSE;

    previous_handler = g_log_set_default_handler(debug_glib_handler, NULL);
    previous_handler_data = NULL;
    handler_installed = TRUE;
    atexit(rox_debug_log_close);

    rox_debug_log_message(ROX_DEBUG_LEVEL_INFO, "startup",
                          "technical log enabled; level=%s; file=%s",
                          level_name(debug_level), debug_path);
    for (i = 0; i < argc; i++) {
        gchar *quoted = g_shell_quote(argv[i] ? argv[i] : "");
        rox_debug_log_message(ROX_DEBUG_LEVEL_DEBUG, "argv",
                              "argv[%d]=%s", i, quoted);
        g_free(quoted);
    }
    g_printerr("Rox-Filer2: diagnostic log: %s\n", debug_path);
    return TRUE;
}

gboolean rox_debug_log_is_enabled(void)
{
    return debug_stream != NULL;
}

const gchar *rox_debug_log_get_path(void)
{
    return debug_path;
}

const gchar *rox_debug_log_get_directory(void)
{
    return debug_directory;
}

RoxDebugLevel rox_debug_log_get_level(void)
{
    return debug_level;
}

void rox_debug_log_message(RoxDebugLevel level, const gchar *category,
                           const gchar *format, ...)
{
    va_list args;
    gchar *message;

    if (!debug_stream || level > debug_level)
        return;
    va_start(args, format);
    message = g_strdup_vprintf(format, args);
    va_end(args);

    g_mutex_lock(&debug_mutex);
    write_line_unlocked(level, category, message);
    g_mutex_unlock(&debug_mutex);
    g_free(message);
}

void rox_debug_log_close(void)
{
    g_mutex_lock(&debug_mutex);
    if (debug_stream) {
        write_line_unlocked(ROX_DEBUG_LEVEL_INFO, "shutdown",
                            "Rox-Filer2 diagnostic log closed");
        fclose(debug_stream);
        debug_stream = NULL;
    }
    g_mutex_unlock(&debug_mutex);

    if (handler_installed) {
        g_log_set_default_handler(previous_handler ? previous_handler
                                                   : g_log_default_handler,
                                  previous_handler_data);
        handler_installed = FALSE;
    }
    g_clear_pointer(&debug_path, g_free);
    g_clear_pointer(&debug_directory, g_free);
}
