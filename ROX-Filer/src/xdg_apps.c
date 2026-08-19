/*
 * ROX-Filer GTK3 - modern XDG/GIO application associations.
 * Copyright (C) 2026 josejp2424.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This module deliberately ignores the historical ROX OpenWith directories.
 * Applications are read from the standard XDG application database and user
 * applications are created below g_get_user_data_dir().
 */

#include "config.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gio/gdesktopappinfo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gstdio.h>

#include "global.h"
#include "xdg_apps.h"
#include "options.h"
#include "support.h"
#include "gui_support.h"
#include "run.h"
#include "filer.h"
#include "main.h"
#include "type.h"
#include "custom_actions.h"
#include "debug_log.h"

#define CUSTOM_APP_PREFIX "rox-user-"
#define CUSTOM_APP_MARKER "X-ROX-Filer-Created"

enum {
    APP_COL_NAME = 0,
    APP_COL_COMMAND,
    APP_COL_MIME,
    APP_COL_PATH,
    APP_N_COLUMNS
};

typedef struct {
    GAppInfo *app;
    GList *paths;
    GtkWindow *parent;
} AppLaunchData;

typedef struct {
    GList *paths;
    GtkWindow *parent;
    gchar *mime_type;
    gchar *desktop_path;
    gboolean launch_after_save;
} CustomDialogData;

typedef struct {
    GtkWidget *entry;
    GtkWidget *image;
    GtkWindow *parent;
} IconButtonData;

static GList *build_file_association_tools(Option *option, xmlNode *node,
                                            guchar *label);
static void add_application_clicked(GtkButton *button, gpointer data);
static void show_manager_clicked(GtkButton *button, gpointer data);
static void open_user_apps_clicked(GtkButton *button, gpointer data);
static void open_mimeapps_clicked(GtkButton *button, gpointer data);
static void manager_add(GtkButton *button, gpointer data);
static void manager_edit(GtkButton *button, gpointer data);
static void manager_remove(GtkButton *button, gpointer data);
static void remove_desktop_from_mimeapps(const gchar *desktop_id);

/* Build a semicolon-terminated desktop-id list with desktop_id first and all
 * previous entries preserved in their original order. */
static gchar *mimeapps_prepend_id(const gchar *value, const gchar *desktop_id)
{
    GString *result;
    gchar **ids;
    gint i;

    result = g_string_new(NULL);
    if (desktop_id && *desktop_id) {
        g_string_append(result, desktop_id);
        g_string_append_c(result, ';');
    }

    ids = g_strsplit(value ? value : "", ";", -1);
    for (i = 0; ids[i]; i++) {
        gchar *id = g_strstrip(ids[i]);
        if (!*id || g_strcmp0(id, desktop_id) == 0)
            continue;
        g_string_append(result, id);
        g_string_append_c(result, ';');
    }
    g_strfreev(ids);
    return g_string_free(result, FALSE);
}

/* Remove only desktop_id from one semicolon-separated association list. */
static gchar *mimeapps_remove_id(const gchar *value, const gchar *desktop_id)
{
    GString *result;
    gchar **ids;
    gint i;

    result = g_string_new(NULL);
    ids = g_strsplit(value ? value : "", ";", -1);
    for (i = 0; ids[i]; i++) {
        gchar *id = g_strstrip(ids[i]);
        if (!*id || g_strcmp0(id, desktop_id) == 0)
            continue;
        g_string_append(result, id);
        g_string_append_c(result, ';');
    }
    g_strfreev(ids);
    return g_string_free(result, FALSE);
}

static gboolean mimeapps_write_atomic(GKeyFile *key, const gchar *path,
                                      GError **error)
{
    gchar *data = NULL;
    gchar *tmp = NULL;
    gsize length = 0;
    gsize offset = 0;
    gint fd = -1;
    mode_t mode = 0600;
    struct stat st;
    gboolean ok = FALSE;

    data = g_key_file_to_data(key, &length, error);
    if (!data)
        goto out;

    if (stat(path, &st) == 0)
        mode = st.st_mode & 0777;

    tmp = g_strconcat(path, ".tmp.XXXXXX", NULL);
    fd = g_mkstemp(tmp);
    if (fd < 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "%s", g_strerror(errno));
        goto out;
    }

    if (fchmod(fd, mode) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "%s", g_strerror(errno));
        goto out;
    }

    while (offset < length) {
        ssize_t written = write(fd, data + offset, length - offset);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                        "%s", g_strerror(errno));
            goto out;
        }
        if (written == 0) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_FAILED,
                                _("Unknown error"));
            goto out;
        }
        offset += (gsize) written;
    }

    if (fsync(fd) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "%s", g_strerror(errno));
        goto out;
    }

    if (close(fd) != 0) {
        fd = -1;
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "%s", g_strerror(errno));
        goto out;
    }
    fd = -1;

    if (g_rename(tmp, path) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "%s", g_strerror(errno));
        goto out;
    }

    ok = TRUE;
out:
    if (fd >= 0)
        close(fd);
    if (!ok && tmp)
        unlink(tmp);
    g_free(tmp);
    g_free(data);
    return ok;
}

/* Rox-Filer2 must never ask GIO to regenerate the user's mimeapps.list when
 * changing one association. Puppy installations in particular may carry
 * hand-maintained associations that must survive. Load the existing file,
 * update only the requested MIME key and write it back atomically. */
gboolean xdg_apps_set_mime_association(GAppInfo *app,
                                        const gchar *mime_type,
                                        gboolean set_default,
                                        GError **error)
{
    const gchar *desktop_id;
    gchar *desktop_id_owned = NULL;
    gchar *path = NULL;
    GKeyFile *key = NULL;
    gchar *old_value = NULL;
    gchar *new_value = NULL;
    gboolean exists;
    gboolean ok = FALSE;

    g_return_val_if_fail(G_IS_APP_INFO(app), FALSE);
    g_return_val_if_fail(mime_type != NULL && *mime_type, FALSE);

    desktop_id = g_app_info_get_id(app);
    if ((!desktop_id || !*desktop_id) && G_IS_DESKTOP_APP_INFO(app)) {
        const gchar *filename = g_desktop_app_info_get_filename(
            G_DESKTOP_APP_INFO(app));
        if (filename && *filename) {
            desktop_id_owned = g_path_get_basename(filename);
            desktop_id = desktop_id_owned;
        }
    }
    if (!desktop_id || !*desktop_id) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                            _("Invalid command."));
        g_free(desktop_id_owned);
        return FALSE;
    }

    if (g_mkdir_with_parents(g_get_user_config_dir(), 0700) != 0 &&
        errno != EEXIST) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    "%s", g_strerror(errno));
        goto out;
    }

    path = g_build_filename(g_get_user_config_dir(), "mimeapps.list", NULL);
    key = g_key_file_new();
    exists = g_file_test(path, G_FILE_TEST_EXISTS);
    if (exists && !g_key_file_load_from_file(key, path,
            G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, error)) {
        /* Never replace an existing file that we could not parse. */
        goto out;
    }

    if (set_default) {
        old_value = g_key_file_get_string(key, "Default Applications",
                                          mime_type, NULL);
        new_value = mimeapps_prepend_id(old_value, desktop_id);
        g_key_file_set_string(key, "Default Applications", mime_type,
                              new_value);
        g_clear_pointer(&old_value, g_free);
        g_clear_pointer(&new_value, g_free);
    }

    old_value = g_key_file_get_string(key, "Added Associations",
                                      mime_type, NULL);
    new_value = mimeapps_prepend_id(old_value, desktop_id);
    g_key_file_set_string(key, "Added Associations", mime_type, new_value);
    g_clear_pointer(&old_value, g_free);
    g_clear_pointer(&new_value, g_free);

    /* The same desktop ID cannot validly be both added and removed for one
     * MIME type. Remove only this ID and preserve every other removal. */
    old_value = g_key_file_get_string(key, "Removed Associations",
                                      mime_type, NULL);
    if (old_value) {
        new_value = mimeapps_remove_id(old_value, desktop_id);
        if (new_value && *new_value)
            g_key_file_set_string(key, "Removed Associations", mime_type,
                                  new_value);
        else
            g_key_file_remove_key(key, "Removed Associations", mime_type,
                                  NULL);
        g_clear_pointer(&old_value, g_free);
        g_clear_pointer(&new_value, g_free);
    }

    ok = mimeapps_write_atomic(key, path, error);
out:
    g_clear_pointer(&old_value, g_free);
    g_clear_pointer(&new_value, g_free);
    if (key)
        g_key_file_unref(key);
    g_free(path);
    g_free(desktop_id_owned);
    return ok;
}

static GList *copy_paths(GList *paths)
{
    GList *copy = NULL;
    GList *node;

    for (node = paths; node; node = node->next)
        copy = g_list_append(copy, g_strdup((const gchar *) node->data));

    return copy;
}

static void free_launch_data(gpointer data)
{
    AppLaunchData *launch = data;

    if (!launch)
        return;
    if (launch->app)
        g_object_unref(launch->app);
    destroy_glist(&launch->paths);
    if (launch->parent)
        g_object_unref(launch->parent);
    g_free(launch);
}

static gchar *mime_type_for_paths(GList *paths)
{
    GList *node;
    gchar *common = NULL;

    for (node = paths; node; node = node->next) {
        const gchar *path = node->data;
        MIME_type *type = NULL;
        gchar *name;

        /* Do not ask the filename/content MIME detector to guess folders.
         * A real directory always uses the exact Freedesktop type, including
         * symbolic links to directories. */
        if (g_file_test(path, G_FILE_TEST_IS_DIR))
            name = g_strdup("inode/directory");
        else {
            type = type_from_path(path);
            if (!type)
                return NULL;
            name = g_strconcat(type->media_type, "/", type->subtype, NULL);
        }
        if (!common)
            common = name;
        else if (g_strcmp0(common, name) != 0) {
            g_free(name);
            g_free(common);
            return NULL;
        } else {
            g_free(name);
        }
    }

    return common;
}

/* Lanzar primero mediante GIO con un contexto gráfico real. Esto respeta la
 * especificación Desktop Entry, la activación D-Bus y la notificación de inicio.
 * Si GIO informa un error, conservar la expansión directa de Exec= como respaldo
 * para distribuciones ligeras con una base XDG incompleta. */
gboolean xdg_apps_launch_app_info(GAppInfo *app, GList *paths,
                                   GtkWindow *parent)
{
    GList *files = NULL;
    GList *node;
    GError *error = NULL;
    gboolean launched = FALSE;
    GAppLaunchContext *context = NULL;
    GdkDisplay *display;

    g_return_val_if_fail(app != NULL, FALSE);

    /* Use the standard GIO launcher first. It handles Desktop Entry field
     * codes, startup notification, DBus activation and application-specific
     * launch rules more accurately than a hand-written Exec= parser. */
    display = gdk_display_get_default();
    if (display) {
        GdkAppLaunchContext *gdk_context =
            gdk_display_get_app_launch_context(display);
        if (gdk_context) {
            gdk_app_launch_context_set_timestamp(gdk_context,
                                                  gtk_get_current_event_time());
            if (parent) {
                GdkScreen *screen = gtk_window_get_screen(parent);
                if (screen)
                    gdk_app_launch_context_set_screen(gdk_context, screen);
            }
            context = G_APP_LAUNCH_CONTEXT(gdk_context);
        }
    }

    rox_debug_log("XDG", "launch name=%s id=%s command=%s desktop=%s",
        g_app_info_get_display_name(app) ? g_app_info_get_display_name(app) : "",
        g_app_info_get_id(app) ? g_app_info_get_id(app) : "",
        g_app_info_get_commandline(app) ? g_app_info_get_commandline(app) : "",
        G_IS_DESKTOP_APP_INFO(app) &&
        g_desktop_app_info_get_filename(G_DESKTOP_APP_INFO(app))
            ? g_desktop_app_info_get_filename(G_DESKTOP_APP_INFO(app)) : "");
    for (node = paths; node; node = node->next) {
        rox_debug_log("XDG", "path=%s", (const gchar *) node->data);
        files = g_list_append(files, g_file_new_for_path(node->data));
    }

    launched = g_app_info_launch(app, files, context, &error);
    rox_debug_log("XDG", "gio-result=%s error=%s",
        launched ? "ok" : "failed", error ? error->message : "");
    g_list_free_full(files, g_object_unref);
    files = NULL;
    if (context)
        g_object_unref(context);

    /* Some minimal desktops have an incomplete GIO activation environment.
     * Only when GIO reports an actual failure, retry the local Desktop Entry
     * by expanding Exec= directly. This avoids swallowing useful GIO behavior
     * while preserving a fallback for Puppy/Essora-style systems. */
    if (!launched && G_IS_DESKTOP_APP_INFO(app)) {
        const gchar *desktop_file =
            g_desktop_app_info_get_filename(G_DESKTOP_APP_INFO(app));

        if (desktop_file && *desktop_file) {
            GPtrArray *args = g_ptr_array_new();
            gchar *working_dir = NULL;

            g_clear_error(&error);
            for (node = paths; node; node = node->next)
                g_ptr_array_add(args, node->data);
            g_ptr_array_add(args, NULL);

            if (paths && paths->data) {
                const gchar *first_path = paths->data;
                if (g_file_test(first_path, G_FILE_TEST_IS_DIR))
                    working_dir = g_strdup(first_path);
                else
                    working_dir = g_path_get_dirname(first_path);
            }

            launched = run_desktop_entry(desktop_file,
                (const gchar **) args->pdata,
                working_dir && *working_dir ? working_dir : g_get_home_dir());
            rox_debug_log("XDG", "desktop-fallback=%s file=%s cwd=%s",
                launched ? "ok" : "failed", desktop_file,
                working_dir && *working_dir ? working_dir : g_get_home_dir());
            g_free(working_dir);
            g_ptr_array_free(args, TRUE);
        }
    }

    if (!launched) {
        report_error(_("Unable to launch %s: %s"),
                     g_app_info_get_display_name(app),
                     error ? error->message : _("Unknown error"));
    }

    g_clear_error(&error);
    return launched;
}

gboolean xdg_apps_diagnose_launch(const gchar *desktop_file,
                                   const gchar *path)
{
    GDesktopAppInfo *desktop;
    GList paths = {0};
    gboolean launched;

    g_return_val_if_fail(desktop_file != NULL, FALSE);
    g_return_val_if_fail(path != NULL, FALSE);

    desktop = g_desktop_app_info_new_from_filename(desktop_file);
    if (!desktop) {
        g_printerr("DIAG_OPEN_WITH_ERROR=invalid-desktop-file:%s\n",
                   desktop_file);
        return FALSE;
    }

    paths.data = (gpointer) path;
    g_print("DIAG_OPEN_WITH_DESKTOP=%s\n", desktop_file);
    g_print("DIAG_OPEN_WITH_PATH=%s\n", path);
    g_print("DIAG_OPEN_WITH_NAME=%s\n",
            g_app_info_get_display_name(G_APP_INFO(desktop)));
    {
        const gchar *command = g_app_info_get_commandline(G_APP_INFO(desktop));
        g_print("DIAG_OPEN_WITH_COMMAND=%s\n", command ? command : "");
    }

    launched = xdg_apps_launch_app_info(G_APP_INFO(desktop), &paths, NULL);
    g_print("DIAG_OPEN_WITH_RESULT=%s\n", launched ? "ok" : "failed");
    g_object_unref(desktop);
    return launched;
}

static void app_item_activate(GtkMenuItem *item, gpointer data)
{
    AppLaunchData *launch = data;
    (void) item;

    if (launch)
        xdg_apps_launch_app_info(launch->app, launch->paths, launch->parent);
}

static GtkWidget *app_menu_item_new(GAppInfo *app)
{
    const gchar *name = g_app_info_get_display_name(app);
    GIcon *icon = g_app_info_get_icon(app);
    GtkWidget *item;

    item = gtk_image_menu_item_new_with_label(name && *name
                                              ? name
                                              : _("Unnamed Application"));
    if (icon) {
        GtkWidget *image = gtk_image_new_from_gicon(icon, GTK_ICON_SIZE_MENU);
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), image);
        gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(item), TRUE);
    }
    return item;
}

static gchar *app_key(GAppInfo *app)
{
    const gchar *id = g_app_info_get_id(app);
    const gchar *exec = g_app_info_get_executable(app);
    const gchar *name = g_app_info_get_display_name(app);

    if (id && *id)
        return g_strdup(id);
    if (exec && *exec)
        return g_strdup(exec);
    return g_strdup(name ? name : "application");
}

static gboolean mime_pattern_matches(const gchar *pattern,
                                     const gchar *mime_type)
{
    const gchar *slash;

    if (!pattern || !*pattern || !mime_type || !*mime_type)
        return FALSE;
    if (g_strcmp0(pattern, mime_type) == 0)
        return TRUE;
    slash = strchr(pattern, '/');
    if (slash && slash[1] == '*' && slash[2] == '\0') {
        gchar *prefix = g_strndup(pattern, (slash - pattern) + 1);
        gboolean matches = g_str_has_prefix(mime_type, prefix);
        g_free(prefix);
        return matches;
    }

    /* inode/directory must be declared by the application itself. Do not
     * inherit unrelated parent/content types, otherwise editors such as
     * Geany can leak into a folder's MIME menu through stale GIO caches. */
    if (g_strcmp0(mime_type, "inode/directory") == 0)
        return FALSE;

    return g_content_type_is_a(mime_type, pattern);
}

static gboolean desktop_file_supports_mime(const gchar *path,
                                           const gchar *mime_type)
{
    GKeyFile *key = g_key_file_new();
    gchar *entry_type = NULL;
    gchar **mime_types = NULL;
    gsize count = 0;
    gsize i;
    gboolean matches = FALSE;

    if (!g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, NULL))
        goto out;
    if (g_key_file_get_boolean(key, "Desktop Entry", "Hidden", NULL))
        goto out;

    entry_type = g_key_file_get_string(key, "Desktop Entry", "Type", NULL);
    if (g_strcmp0(entry_type, "Application") != 0)
        goto out;

    /* A NULL MIME type is used by the application chooser to include every
     * installed desktop application. The normal contextual menu still passes
     * an exact MIME type and therefore remains filtered. */
    if (!mime_type || !*mime_type) {
        matches = TRUE;
        goto out;
    }

    mime_types = g_key_file_get_string_list(key, "Desktop Entry", "MimeType",
                                             &count, NULL);
    for (i = 0; mime_types && i < count; i++) {
        if (mime_pattern_matches(mime_types[i], mime_type)) {
            matches = TRUE;
            break;
        }
    }

out:
    g_strfreev(mime_types);
    g_free(entry_type);
    g_key_file_unref(key);
    return matches;
}

static void collect_app_info(GPtrArray *apps, GHashTable *seen, GAppInfo *app)
{
    gchar *key;

    if (!app)
        return;
    key = app_key(app);
    if (g_hash_table_contains(seen, key)) {
        g_free(key);
        return;
    }
    g_hash_table_add(seen, key);
    g_ptr_array_add(apps, g_object_ref(app));
}

/* GIO can return an application from mimeapps.list or a stale mimeinfo.cache
 * even when its current .desktop file does not advertise inode/directory.
 * For folders, accept only applications that explicitly declare support. */
static void collect_app_info_for_mime(GPtrArray *apps, GHashTable *seen,
                                      GAppInfo *app, const gchar *mime_type)
{
    if (!app)
        return;

    if (g_strcmp0(mime_type, "inode/directory") == 0) {
        const gchar *filename = NULL;

        if (G_IS_DESKTOP_APP_INFO(app))
            filename = g_desktop_app_info_get_filename(G_DESKTOP_APP_INFO(app));
        if (!filename || !desktop_file_supports_mime(filename, mime_type))
            return;
    }

    collect_app_info(apps, seen, app);
}

static void collect_apps_from_directory(const gchar *directory,
                                        const gchar *mime_type,
                                        GPtrArray *apps,
                                        GHashTable *seen,
                                        guint depth)
{
    GDir *dir;
    const gchar *leaf;

    if (!directory || depth > 4)
        return;
    dir = g_dir_open(directory, 0, NULL);
    if (!dir)
        return;

    while ((leaf = g_dir_read_name(dir)) != NULL) {
        gchar *path;

        if (leaf[0] == '.')
            continue;
        path = g_build_filename(directory, leaf, NULL);
        if (g_file_test(path, G_FILE_TEST_IS_DIR) &&
            !g_file_test(path, G_FILE_TEST_IS_SYMLINK)) {
            collect_apps_from_directory(path, mime_type, apps, seen, depth + 1);
        } else if (g_str_has_suffix(leaf, ".desktop") &&
                   desktop_file_supports_mime(path, mime_type)) {
            GDesktopAppInfo *desktop = g_desktop_app_info_new_from_filename(path);
            if (desktop) {
                collect_app_info(apps, seen, G_APP_INFO(desktop));
                g_object_unref(desktop);
            }
        }
        g_free(path);
    }
    g_dir_close(dir);
}

static GPtrArray *collect_apps_for_mime(const gchar *mime_type)
{
    GPtrArray *apps = g_ptr_array_new_with_free_func(g_object_unref);
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal,
                                             g_free, NULL);
    GAppInfo *default_app;
    GList *list;
    GList *node;
    gchar *directory;
    const gchar * const *system_dirs;
    gint i;

    default_app = g_app_info_get_default_for_type(mime_type, FALSE);
    if (default_app) {
        collect_app_info_for_mime(apps, seen, default_app, mime_type);
        g_object_unref(default_app);
    }

    list = g_app_info_get_recommended_for_type(mime_type);
    for (node = list; node; node = node->next)
        collect_app_info_for_mime(apps, seen, G_APP_INFO(node->data), mime_type);
    g_list_free_full(list, g_object_unref);

    list = g_app_info_get_all_for_type(mime_type);
    for (node = list; node; node = node->next)
        collect_app_info_for_mime(apps, seen, G_APP_INFO(node->data), mime_type);
    g_list_free_full(list, g_object_unref);

    list = g_app_info_get_fallback_for_type(mime_type);
    for (node = list; node; node = node->next)
        collect_app_info_for_mime(apps, seen, G_APP_INFO(node->data), mime_type);
    g_list_free_full(list, g_object_unref);

    /* Puppy and other small distributions do not always ship an up-to-date
     * mimeinfo.cache. Scan the actual desktop files as a reliable fallback. */
    directory = g_build_filename(g_get_user_data_dir(), "applications", NULL);
    collect_apps_from_directory(directory, mime_type, apps, seen, 0);
    g_free(directory);

    system_dirs = g_get_system_data_dirs();
    for (i = 0; system_dirs && system_dirs[i]; i++) {
        directory = g_build_filename(system_dirs[i], "applications", NULL);
        collect_apps_from_directory(directory, mime_type, apps, seen, 0);
        g_free(directory);
    }

    g_hash_table_destroy(seen);
    return apps;
}

static GPtrArray *collect_apps_for_chooser(const gchar *mime_type)
{
    GPtrArray *apps = g_ptr_array_new_with_free_func(g_object_unref);
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal,
                                             g_free, NULL);
    GPtrArray *recommended;
    GList *list;
    GList *node;
    gchar *directory;
    const gchar * const *system_dirs;
    guint i;
    gint dir_index;

    /* Keep MIME-compatible applications first. */
    recommended = collect_apps_for_mime(mime_type);
    for (i = 0; i < recommended->len; i++)
        collect_app_info(apps, seen,
                         G_APP_INFO(g_ptr_array_index(recommended, i)));
    g_ptr_array_unref(recommended);

    /* Then offer every application known by GIO. This is important on Puppy
     * and other small systems where mimeinfo.cache can be incomplete. */
    list = g_app_info_get_all();
    for (node = list; node; node = node->next)
        collect_app_info(apps, seen, G_APP_INFO(node->data));
    g_list_free_full(list, g_object_unref);

    /* Finally scan the real XDG application directories, so the chooser also
     * works when the desktop database has not been refreshed yet. */
    directory = g_build_filename(g_get_user_data_dir(), "applications", NULL);
    collect_apps_from_directory(directory, NULL, apps, seen, 0);
    g_free(directory);

    system_dirs = g_get_system_data_dirs();
    for (dir_index = 0; system_dirs && system_dirs[dir_index]; dir_index++) {
        directory = g_build_filename(system_dirs[dir_index], "applications", NULL);
        collect_apps_from_directory(directory, NULL, apps, seen, 0);
        g_free(directory);
    }

    g_hash_table_destroy(seen);
    return apps;
}

static GtkWidget *app_menu_item_new_direct(GAppInfo *app)
{
    const gchar *name = g_app_info_get_display_name(app);
    gchar *label = g_strdup_printf(_("Open with %s"),
                                   name && *name ? name : _("Unnamed Application"));
    GtkWidget *item = gtk_image_menu_item_new_with_label(label);
    GIcon *icon = g_app_info_get_icon(app);

    g_free(label);
    if (icon) {
        GtkWidget *image = gtk_image_new_from_gicon(icon, GTK_ICON_SIZE_MENU);
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), image);
        gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(item), TRUE);
    }
    return item;
}

static void bind_app_item(GtkWidget *item, GAppInfo *app, GList *paths,
                          GtkWindow *parent)
{
    AppLaunchData *launch = g_new0(AppLaunchData, 1);

    launch->app = g_object_ref(app);
    launch->paths = copy_paths(paths);
    launch->parent = parent ? g_object_ref(parent) : NULL;
    g_signal_connect(item, "activate", G_CALLBACK(app_item_activate), launch);
    g_object_set_data_full(G_OBJECT(item), "rox-xdg-launch-data",
                           launch, free_launch_data);
}

static gboolean append_app_to_menu(GtkWidget *menu, GAppInfo *app,
                                   GList *paths, GtkWindow *parent)
{
    GtkWidget *item;

    if (!app)
        return FALSE;
    item = app_menu_item_new(app);
    bind_app_item(item, app, paths, parent);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    return TRUE;
}

static void choose_another_activate(GtkMenuItem *item, gpointer data)
{
    CustomDialogData *ctx = data;
    (void) item;

    if (ctx)
        xdg_apps_choose_for_paths(ctx->paths, ctx->parent, FALSE);
}

static void add_custom_activate(GtkMenuItem *item, gpointer data)
{
    CustomDialogData *ctx = data;
    (void) item;

    if (ctx)
        xdg_apps_add_custom_for_paths(ctx->paths, ctx->parent);
}

static void add_file_action_activate(GtkMenuItem *item, gpointer data)
{
    CustomDialogData *ctx = data;
    (void) item;

    if (ctx)
        custom_actions_add_for_paths(ctx->paths, ctx->parent);
}

static void manage_apps_activate(GtkMenuItem *item, gpointer data)
{
    CustomDialogData *ctx = data;
    (void) item;

    if (ctx)
        xdg_apps_show_manager(ctx->parent);
}

static void manage_actions_activate(GtkMenuItem *item, gpointer data)
{
    CustomDialogData *ctx = data;
    (void) item;

    if (ctx)
        custom_actions_show_manager(ctx->parent);
}

static void custom_dialog_data_free(gpointer data)
{
    CustomDialogData *ctx = data;

    if (!ctx)
        return;
    destroy_glist(&ctx->paths);
    if (ctx->parent)
        g_object_unref(ctx->parent);
    g_free(ctx->mime_type);
    g_free(ctx->desktop_path);
    g_free(ctx);
}

static CustomDialogData *custom_dialog_data_new(GList *paths, GtkWindow *parent,
                                                 const gchar *mime_type)
{
    CustomDialogData *ctx = g_new0(CustomDialogData, 1);
    ctx->paths = copy_paths(paths);
    ctx->parent = parent ? g_object_ref(parent) : NULL;
    ctx->mime_type = g_strdup(mime_type);
    return ctx;
}

static void append_context_item(GtkWidget *menu, const gchar *label,
                                const gchar *icon_name, GCallback callback,
                                GList *paths, GtkWindow *parent,
                                const gchar *mime_type, const gchar *data_key)
{
    GtkWidget *item = gtk_image_menu_item_new_with_label(label);
    CustomDialogData *ctx = custom_dialog_data_new(paths, parent, mime_type);

    if (icon_name && *icon_name) {
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
            gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_MENU));
        gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(item), TRUE);
    }
    g_signal_connect(item, "activate", callback, ctx);
    g_object_set_data_full(G_OBJECT(item), data_key, ctx,
                           custom_dialog_data_free);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}

GtkWidget *xdg_apps_create_open_with_menu(GList *paths, GtkWindow *parent)
{
    GtkWidget *menu;
    GPtrArray *apps;
    gchar *mime_type;
    guint i;

    mime_type = mime_type_for_paths(paths);
    if (!mime_type)
        return NULL;
    /* Directories are valid MIME objects too (normally inode/directory).
     * Keep Open With available for them so file managers and user commands can
     * be selected in exactly the same way as for regular files. */
    menu = rox_menu_new();
    apps = collect_apps_for_mime(mime_type);
    for (i = 0; i < apps->len; i++)
        append_app_to_menu(menu, G_APP_INFO(g_ptr_array_index(apps, i)),
                           paths, parent);

    if (apps->len > 0)
        gtk_menu_shell_append(GTK_MENU_SHELL(menu),
                              gtk_separator_menu_item_new());

    append_context_item(menu, _("Choose Another Application..."),
                        "application-x-executable",
                        G_CALLBACK(choose_another_activate), paths, parent,
                        mime_type, "rox-choose-app-data");
    append_context_item(menu, _("Add Custom Application..."), "list-add",
                        G_CALLBACK(add_custom_activate), paths, parent,
                        mime_type, "rox-add-app-data");
    append_context_item(menu, _("Add File Action..."), "system-run",
                        G_CALLBACK(add_file_action_activate), paths, parent,
                        mime_type, "rox-add-action-data");

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    append_context_item(menu, _("Manage Applications..."),
                        "application-x-executable",
                        G_CALLBACK(manage_apps_activate), paths, parent,
                        mime_type, "rox-manage-apps-data");
    append_context_item(menu, _("Manage File Actions..."),
                        "preferences-system",
                        G_CALLBACK(manage_actions_activate), paths, parent,
                        mime_type, "rox-manage-actions-data");

    gtk_widget_show_all(menu);
    g_ptr_array_unref(apps);
    g_free(mime_type);
    return menu;
}

static GList *current_mime_items = NULL;

void xdg_apps_remove_mime_tools(void)
{
    GList *node;

    for (node = current_mime_items; node; node = node->next)
        gtk_widget_destroy(GTK_WIDGET(node->data));
    g_list_free(current_mime_items);
    current_mime_items = NULL;
}

static void insert_current_mime_item(GtkWidget *menu, GtkWidget *item,
                                     gint position)
{
    gtk_menu_shell_insert(GTK_MENU_SHELL(menu), item, position);
    gtk_widget_show(item);
    current_mime_items = g_list_append(current_mime_items, item);
}

int xdg_apps_add_mime_tools(GtkWidget *menu, GList *paths, GtkWindow *parent)
{
    gchar *mime_type;
    GList *node;
    GPtrArray *apps = NULL;
    GList *actions;
    gint position = 0;
    guint i;

    xdg_apps_remove_mime_tools();
    if (!menu || !paths)
        return 0;

    mime_type = mime_type_for_paths(paths);

    /* A directory normally has the MIME type inode/directory. Show matching
     * applications and actions in the foreground instead of treating it as
     * an object without a MIME type. */
    if (mime_type) {
        apps = collect_apps_for_mime(mime_type);
        for (i = 0; i < apps->len; i++) {
            GAppInfo *app = G_APP_INFO(g_ptr_array_index(apps, i));
            GtkWidget *item = app_menu_item_new_direct(app);
            bind_app_item(item, app, paths, parent);
            insert_current_mime_item(menu, item, position++);
        }
    }

    actions = custom_actions_create_items(paths, parent);
    for (node = actions; node; node = node->next)
        insert_current_mime_item(menu, GTK_WIDGET(node->data), position++);
    g_list_free(actions);

    if (position > 0)
        insert_current_mime_item(menu, gtk_separator_menu_item_new(), position++);

    if (apps)
        g_ptr_array_unref(apps);
    g_free(mime_type);
    return position;
}


enum {
    CHOOSER_COL_ICON = 0,
    CHOOSER_COL_NAME,
    CHOOSER_COL_COMMAND,
    CHOOSER_COL_INDEX,
    CHOOSER_N_COLUMNS
};

static void chooser_row_activated(GtkTreeView *tree, GtkTreePath *path,
                                  GtkTreeViewColumn *column, gpointer data)
{
    (void) tree;
    (void) path;
    (void) column;
    gtk_dialog_response(GTK_DIALOG(data), GTK_RESPONSE_OK);
}

void xdg_apps_choose_for_paths(GList *paths, GtkWindow *parent,
                               gboolean default_remember)
{
    gchar *mime_type = mime_type_for_paths(paths);
    GPtrArray *apps;
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *type_label;
    GtkWidget *scroller;
    GtkWidget *tree;
    GtkWidget *remember;
    GtkListStore *store;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeSelection *selection;
    guint i;
    gint response;

    if (!mime_type) {
        report_error("%s", _("The selected items do not share one MIME type."));
        return;
    }

    apps = collect_apps_for_chooser(mime_type);
    dialog = gtk_dialog_new_with_buttons(_("Choose Another Application"),
        parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        _("Cancel"), GTK_RESPONSE_CANCEL,
        _("Add Custom Application..."), GTK_RESPONSE_APPLY,
        _("Open"), GTK_RESPONSE_OK,
        NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 620, 440);
    gtk_window_set_position(GTK_WINDOW(dialog), parent
        ? GTK_WIN_POS_CENTER_ON_PARENT : GTK_WIN_POS_CENTER);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);

    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 10);
    gtk_box_set_spacing(GTK_BOX(content), 8);

    {
        gchar *text = g_strdup_printf(_("Applications for %s"), mime_type);
        type_label = gtk_label_new(text);
        g_free(text);
    }
    gtk_label_set_xalign(GTK_LABEL(type_label), 0.0);
    gtk_box_pack_start(GTK_BOX(content), type_label, FALSE, FALSE, 0);

    store = gtk_list_store_new(CHOOSER_N_COLUMNS,
                               G_TYPE_ICON,
                               G_TYPE_STRING,
                               G_TYPE_STRING,
                               G_TYPE_UINT);
    for (i = 0; i < apps->len; i++) {
        GAppInfo *app = G_APP_INFO(g_ptr_array_index(apps, i));
        GtkTreeIter iter;
        const gchar *name = g_app_info_get_display_name(app);
        const gchar *command = g_app_info_get_commandline(app);
        GIcon *icon = g_app_info_get_icon(app);

        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
                           CHOOSER_COL_ICON, icon,
                           CHOOSER_COL_NAME,
                           name && *name ? name : _("Unnamed Application"),
                           CHOOSER_COL_COMMAND, command ? command : "",
                           CHOOSER_COL_INDEX, i,
                           -1);
    }

    tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), TRUE);
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, _("Application"));
    renderer = gtk_cell_renderer_pixbuf_new();
    g_object_set(renderer, "stock-size", GTK_ICON_SIZE_MENU, NULL);
    gtk_tree_view_column_pack_start(column, renderer, FALSE);
    gtk_tree_view_column_add_attribute(column, renderer, "gicon",
                                       CHOOSER_COL_ICON);
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_add_attribute(column, renderer, "text",
                                       CHOOSER_COL_NAME);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes(_("Command"), renderer,
                                                       "text",
                                                       CHOOSER_COL_COMMAND,
                                                       NULL);
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

    scroller = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroller), tree);
    gtk_box_pack_start(GTK_BOX(content), scroller, TRUE, TRUE, 0);

    if (apps->len == 0) {
        GtkWidget *empty = gtk_label_new(
            _("No installed applications were found for this type. Add a custom application or command."));
        gtk_label_set_line_wrap(GTK_LABEL(empty), TRUE);
        gtk_label_set_xalign(GTK_LABEL(empty), 0.0);
        gtk_box_pack_start(GTK_BOX(content), empty, FALSE, FALSE, 0);
        gtk_widget_set_sensitive(tree, FALSE);
    } else {
        GtkTreePath *first = gtk_tree_path_new_first();
        gtk_tree_selection_select_path(selection, first);
        gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(tree), first, NULL,
                                     FALSE, 0.0, 0.0);
        gtk_tree_path_free(first);
    }

    remember = gtk_check_button_new_with_label(
        _("Remember this application for this file type"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(remember), default_remember);
    gtk_box_pack_start(GTK_BOX(content), remember, FALSE, FALSE, 0);

    g_signal_connect(tree, "row-activated",
                     G_CALLBACK(chooser_row_activated), dialog);
    gtk_widget_show_all(content);

    for (;;) {
        response = gtk_dialog_run(GTK_DIALOG(dialog));
        if (response == GTK_RESPONSE_APPLY) {
            gtk_widget_hide(dialog);
            xdg_apps_add_custom_for_paths(paths, parent);
            break;
        }
        if (response == GTK_RESPONSE_OK) {
            GtkTreeModel *model;
            GtkTreeIter iter;
            guint index = G_MAXUINT;

            if (!gtk_tree_selection_get_selected(selection, &model, &iter)) {
                report_error("%s", _("Select an application or add a custom command."));
                continue;
            }
            gtk_tree_model_get(model, &iter, CHOOSER_COL_INDEX, &index, -1);
            if (index < apps->len) {
                GAppInfo *app = G_APP_INFO(g_ptr_array_index(apps, index));
                GError *error = NULL;

                if (!xdg_apps_set_mime_association(
                        app, mime_type,
                        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remember)),
                        &error)) {
                    report_error(_("Unable to set the default application for %s: %s"),
                                 mime_type,
                                 error ? error->message : _("Unknown error"));
                    g_clear_error(&error);
                }
                xdg_apps_launch_app_info(app, paths, parent);
            }
        }
        break;
    }

    gtk_widget_destroy(dialog);
    g_object_unref(store);
    g_ptr_array_unref(apps);
    g_free(mime_type);
}

static gchar *sanitize_id(const gchar *name, const gchar *prefix)
{
    GString *out = g_string_new(prefix ? prefix : "");
    const gchar *p;
    gboolean dash = FALSE;

    for (p = name ? name : "application"; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (c < 128 && g_ascii_isalnum((gchar) c)) {
            g_string_append_c(out, g_ascii_tolower((gchar) c));
            dash = FALSE;
        } else if (!dash) {
            g_string_append_c(out, '-');
            dash = TRUE;
        }
    }

    while (out->len > 0 && out->str[out->len - 1] == '-')
        g_string_truncate(out, out->len - 1);
    if (out->len == 0 || (prefix && out->len == strlen(prefix)))
        g_string_append(out, "application");
    return g_string_free(out, FALSE);
}

static gchar *command_with_field_code(const gchar *command)
{
    if (strstr(command, "%f") || strstr(command, "%F") ||
        strstr(command, "%u") || strstr(command, "%U"))
        return g_strdup(command);
    return g_strconcat(command, " %F", NULL);
}

static gboolean validate_command(const gchar *command, GError **error)
{
    gchar **argv = NULL;
    gint argc = 0;
    gchar *program = NULL;
    gboolean valid = FALSE;

    if (!command || !*command) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "%s", _("The command is empty."));
        return FALSE;
    }

    if (!g_shell_parse_argv(command, &argc, &argv, error))
        return FALSE;

    if (argc > 0) {
        gchar *clean = g_strdup(argv[0]);
        gchar *percent = strchr(clean, '%');
        if (percent)
            *percent = '\0';
        if (g_path_is_absolute(clean))
            valid = g_file_test(clean, G_FILE_TEST_IS_EXECUTABLE);
        else {
            program = g_find_program_in_path(clean);
            valid = program != NULL;
        }
        if (!valid)
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                        _("Command '%s' was not found."), clean);
        g_free(clean);
    }

    g_free(program);
    g_strfreev(argv);
    return valid;
}

static void icon_preview_update(GtkFileChooser *chooser, gpointer data)
{
    GtkWidget *preview = data;
    gchar *filename = gtk_file_chooser_get_preview_filename(chooser);
    GError *error = NULL;
    GdkPixbuf *pixbuf = NULL;

    if (filename)
        pixbuf = gdk_pixbuf_new_from_file_at_scale(filename, 128, 128, TRUE, &error);

    if (pixbuf) {
        gtk_image_set_from_pixbuf(GTK_IMAGE(preview), pixbuf);
        gtk_file_chooser_set_preview_widget_active(chooser, TRUE);
        g_object_unref(pixbuf);
    } else {
        gtk_file_chooser_set_preview_widget_active(chooser, FALSE);
        g_clear_error(&error);
    }
    g_free(filename);
}

gchar *xdg_apps_choose_icon(GtkWindow *parent, const gchar *initial)
{
    GtkWidget *dialog;
    GtkWidget *preview;
    GtkFileFilter *filter;
    gchar *result = NULL;

    dialog = gtk_file_chooser_dialog_new(_("Choose Icon"), parent,
        GTK_FILE_CHOOSER_ACTION_OPEN,
        _("Cancel"), GTK_RESPONSE_CANCEL,
        _("Choose"), GTK_RESPONSE_ACCEPT,
        NULL);
    gtk_window_set_position(GTK_WINDOW(dialog), parent
        ? GTK_WIN_POS_CENTER_ON_PARENT : GTK_WIN_POS_CENTER);

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, _("Image files"));
    gtk_file_filter_add_pixbuf_formats(filter);
    gtk_file_filter_add_pattern(filter, "*.svg");
    gtk_file_filter_add_pattern(filter, "*.SVG");
    gtk_file_filter_add_pattern(filter, "*.xpm");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

    if (initial && g_path_is_absolute(initial) &&
        g_file_test(initial, G_FILE_TEST_EXISTS))
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(dialog), initial);
    else if (g_file_test("/usr/share/pixmaps", G_FILE_TEST_IS_DIR))
        gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog),
                                            "/usr/share/pixmaps");

    preview = gtk_image_new();
    gtk_widget_set_size_request(preview, 144, 144);
    gtk_file_chooser_set_preview_widget(GTK_FILE_CHOOSER(dialog), preview);
    g_signal_connect(dialog, "update-preview",
                     G_CALLBACK(icon_preview_update), preview);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
        result = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

    gtk_widget_destroy(dialog);
    return result;
}

static void choose_icon_clicked(GtkButton *button, gpointer data)
{
    IconButtonData *icon_data = data;
    const gchar *current;
    gchar *selected;
    GdkPixbuf *pixbuf;
    GError *error = NULL;

    (void) button;
    current = gtk_entry_get_text(GTK_ENTRY(icon_data->entry));
    selected = xdg_apps_choose_icon(icon_data->parent, current);
    if (!selected)
        return;

    gtk_entry_set_text(GTK_ENTRY(icon_data->entry), selected);
    pixbuf = gdk_pixbuf_new_from_file_at_scale(selected, 64, 64, TRUE, &error);
    if (pixbuf) {
        gtk_image_set_from_pixbuf(GTK_IMAGE(icon_data->image), pixbuf);
        g_object_unref(pixbuf);
    } else {
        gtk_image_set_from_icon_name(GTK_IMAGE(icon_data->image),
                                     "image-missing", GTK_ICON_SIZE_DIALOG);
        g_clear_error(&error);
    }
    g_free(selected);
}

static gchar *path_extension_lower(const gchar *path)
{
    const gchar *dot = strrchr(path, '.');
    return dot ? g_ascii_strdown(dot, -1) : g_strdup("");
}

gchar *xdg_apps_install_user_icon(const gchar *source_or_name,
                                   const gchar *desktop_id,
                                   GError **error)
{
    gchar *safe_id;
    gchar *extension;
    gchar *directory;
    gchar *destination;
    gchar *icon_name;

    if (!source_or_name || !*source_or_name)
        return g_strdup("application-x-executable");

    if (!g_path_is_absolute(source_or_name))
        return g_strdup(source_or_name);

    if (!g_file_test(source_or_name, G_FILE_TEST_IS_REGULAR)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                    _("Icon file '%s' does not exist."), source_or_name);
        return NULL;
    }

    safe_id = sanitize_id(desktop_id, NULL);
    extension = path_extension_lower(source_or_name);
    icon_name = g_strdup(safe_id);

    if (g_strcmp0(extension, ".svg") == 0 ||
        g_strcmp0(extension, ".svgz") == 0) {
        GFile *source;
        GFile *target;
        directory = g_build_filename(g_get_user_data_dir(), "icons", "hicolor",
                                     "scalable", "apps", NULL);
        if (g_mkdir_with_parents(directory, 0755) != 0 && errno != EEXIST) {
            g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                        _("Unable to create icon directory '%s': %s"),
                        directory, g_strerror(errno));
            g_free(directory);
            g_free(extension);
            g_free(icon_name);
            g_free(safe_id);
            return NULL;
        }
        {
            const gchar *svg_extension = g_strcmp0(extension, ".svgz") == 0
                ? ".svgz" : ".svg";
            gchar *leaf = g_strconcat(safe_id, svg_extension, NULL);
            destination = g_build_filename(directory, leaf, NULL);
            g_free(leaf);
        }
        source = g_file_new_for_path(source_or_name);
        target = g_file_new_for_path(destination);
        if (!g_file_copy(source, target, G_FILE_COPY_OVERWRITE, NULL,
                         NULL, NULL, error)) {
            g_object_unref(source);
            g_object_unref(target);
            g_free(destination);
            g_free(directory);
            g_free(extension);
            g_free(icon_name);
            g_free(safe_id);
            return NULL;
        }
        g_object_unref(source);
        g_object_unref(target);
    } else {
        GdkPixbuf *pixbuf;
        GdkPixbuf *scaled;
        gint width;
        gint height;
        gint new_width;
        gint new_height;

        directory = g_build_filename(g_get_user_data_dir(), "icons", "hicolor",
                                     "128x128", "apps", NULL);
        if (g_mkdir_with_parents(directory, 0755) != 0 && errno != EEXIST) {
            g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                        _("Unable to create icon directory '%s': %s"),
                        directory, g_strerror(errno));
            g_free(directory);
            g_free(extension);
            g_free(icon_name);
            g_free(safe_id);
            return NULL;
        }
        {
            gchar *leaf = g_strconcat(safe_id, ".png", NULL);
            destination = g_build_filename(directory, leaf, NULL);
            g_free(leaf);
        }
        pixbuf = gdk_pixbuf_new_from_file(source_or_name, error);
        if (!pixbuf) {
            g_free(destination);
            g_free(directory);
            g_free(extension);
            g_free(icon_name);
            g_free(safe_id);
            return NULL;
        }
        width = gdk_pixbuf_get_width(pixbuf);
        height = gdk_pixbuf_get_height(pixbuf);
        if (width >= height) {
            new_width = 128;
            new_height = MAX(1, (height * 128) / MAX(width, 1));
        } else {
            new_height = 128;
            new_width = MAX(1, (width * 128) / MAX(height, 1));
        }
        scaled = gdk_pixbuf_scale_simple(pixbuf, new_width, new_height,
                                         GDK_INTERP_BILINEAR);
        g_object_unref(pixbuf);
        if (!scaled || !gdk_pixbuf_save(scaled, destination, "png", error, NULL)) {
            if (scaled)
                g_object_unref(scaled);
            g_free(destination);
            g_free(directory);
            g_free(extension);
            g_free(icon_name);
            g_free(safe_id);
            return NULL;
        }
        g_object_unref(scaled);
    }

    g_free(destination);
    g_free(directory);
    g_free(extension);
    g_free(safe_id);
    if (gtk_icon_theme_get_default())
        gtk_icon_theme_rescan_if_needed(gtk_icon_theme_get_default());
    return icon_name;
}

static gchar **split_mime_types(const gchar *text, gsize *length)
{
    gchar **raw;
    GPtrArray *values;
    gint i;

    values = g_ptr_array_new_with_free_func(g_free);
    raw = g_strsplit_set(text ? text : "", ";, \t\r\n", -1);
    for (i = 0; raw[i]; i++) {
        gchar *value = g_strstrip(raw[i]);
        if (*value && strchr(value, '/'))
            g_ptr_array_add(values, g_strdup(value));
    }
    g_strfreev(raw);
    g_ptr_array_add(values, NULL);
    if (length)
        *length = values->len - 1;
    return (gchar **) g_ptr_array_free(values, FALSE);
}

static void update_desktop_database(void)
{
    gchar *program = g_find_program_in_path("update-desktop-database");
    gchar *directory;
    gchar *argv[3];

    if (!program)
        return;
    directory = g_build_filename(g_get_user_data_dir(), "applications", NULL);
    argv[0] = program;
    argv[1] = directory;
    argv[2] = NULL;
    g_spawn_async(NULL, argv, NULL, G_SPAWN_STDOUT_TO_DEV_NULL |
                  G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, NULL);
    g_free(directory);
    g_free(program);
}

static gboolean save_custom_application(GtkWindow *parent,
                                        const gchar *existing_path,
                                        const gchar *name,
                                        const gchar *command,
                                        const gchar *icon_source,
                                        const gchar *mime_text,
                                        gboolean terminal,
                                        gboolean show_in_menu,
                                        gboolean set_default,
                                        gchar **saved_path,
                                        GAppInfo **saved_app)
{
    gchar *applications_dir;
    gchar *desktop_id;
    gchar *desktop_path;
    gchar *desktop_base;
    gchar *exec;
    gchar *icon_name;
    gchar **mime_types;
    gsize mime_count;
    GKeyFile *key_file;
    gchar *contents;
    gsize contents_len;
    GError *error = NULL;
    GDesktopAppInfo *desktop_app;
    gsize i;

    (void) parent;
    if (!validate_command(command, &error)) {
        report_error("%s", error ? error->message : _("Invalid command."));
        g_clear_error(&error);
        return FALSE;
    }

    mime_types = split_mime_types(mime_text, &mime_count);
    if (mime_count == 0) {
        report_error("%s", _("Enter at least one valid MIME type."));
        g_strfreev(mime_types);
        return FALSE;
    }

    applications_dir = g_build_filename(g_get_user_data_dir(),
                                         "applications", NULL);
    if (g_mkdir_with_parents(applications_dir, 0755) != 0 && errno != EEXIST) {
        report_error(_("Unable to create applications directory '%s': %s"),
                     applications_dir, g_strerror(errno));
        g_free(applications_dir);
        g_strfreev(mime_types);
        return FALSE;
    }

    if (existing_path) {
        desktop_path = g_strdup(existing_path);
        desktop_base = g_path_get_basename(existing_path);
        desktop_id = g_strndup(desktop_base,
                     strlen(desktop_base) - (g_str_has_suffix(desktop_base, ".desktop") ? 8 : 0));
        g_free(desktop_base);
    } else {
        desktop_id = sanitize_id(name, CUSTOM_APP_PREFIX);
        desktop_base = g_strconcat(desktop_id, ".desktop", NULL);
        desktop_path = g_build_filename(applications_dir, desktop_base, NULL);
        g_free(desktop_base);
    }

    icon_name = xdg_apps_install_user_icon(icon_source, desktop_id, &error);
    if (!icon_name) {
        report_error("%s", error ? error->message : _("Unable to install icon."));
        g_clear_error(&error);
        g_free(desktop_path);
        g_free(desktop_id);
        g_free(applications_dir);
        g_strfreev(mime_types);
        return FALSE;
    }

    exec = command_with_field_code(command);
    key_file = g_key_file_new();
    g_key_file_set_string(key_file, "Desktop Entry", "Type", "Application");
    g_key_file_set_string(key_file, "Desktop Entry", "Name", name);
    g_key_file_set_string(key_file, "Desktop Entry", "Exec", exec);
    g_key_file_set_string(key_file, "Desktop Entry", "Icon", icon_name);
    g_key_file_set_boolean(key_file, "Desktop Entry", "Terminal", terminal);
    g_key_file_set_boolean(key_file, "Desktop Entry", "NoDisplay", !show_in_menu);
    g_key_file_set_boolean(key_file, "Desktop Entry", "StartupNotify", TRUE);
    if (show_in_menu)
        g_key_file_set_string(key_file, "Desktop Entry", "Categories",
                              "Utility;FileTools;");
    g_key_file_set_string_list(key_file, "Desktop Entry", "MimeType",
                               (const gchar * const *) mime_types, mime_count);
    g_key_file_set_boolean(key_file, "Desktop Entry", CUSTOM_APP_MARKER, TRUE);

    contents = g_key_file_to_data(key_file, &contents_len, &error);
    g_key_file_unref(key_file);
    if (!contents || !g_file_set_contents(desktop_path, contents,
                                          contents_len, &error)) {
        report_error(_("Unable to save '%s': %s"), desktop_path,
                     error ? error->message : _("Unknown error"));
        g_clear_error(&error);
        g_free(contents);
        g_free(exec);
        g_free(icon_name);
        g_free(desktop_path);
        g_free(desktop_id);
        g_free(applications_dir);
        g_strfreev(mime_types);
        return FALSE;
    }
    chmod(desktop_path, 0644);
    g_free(contents);

    desktop_app = g_desktop_app_info_new_from_filename(desktop_path);
    if (!desktop_app) {
        report_error(_("The new desktop file '%s' could not be loaded."),
                     desktop_path);
        g_free(exec);
        g_free(icon_name);
        g_free(desktop_path);
        g_free(desktop_id);
        g_free(applications_dir);
        g_strfreev(mime_types);
        return FALSE;
    }

    if (existing_path) {
        gchar *desktop_filename = g_path_get_basename(desktop_path);
        remove_desktop_from_mimeapps(desktop_filename);
        g_free(desktop_filename);
    }

    for (i = 0; i < mime_count; i++) {
        GError *assoc_error = NULL;
        if (!xdg_apps_set_mime_association(G_APP_INFO(desktop_app),
                                           mime_types[i], set_default,
                                           &assoc_error)) {
            report_error(_("Unable to set the default application for %s: %s"),
                         mime_types[i],
                         assoc_error ? assoc_error->message : _("Unknown error"));
            g_clear_error(&assoc_error);
        }
    }

    update_desktop_database();
    if (saved_path)
        *saved_path = g_strdup(desktop_path);
    if (saved_app)
        *saved_app = G_APP_INFO(g_object_ref(desktop_app));

    g_object_unref(desktop_app);
    g_free(exec);
    g_free(icon_name);
    g_free(desktop_path);
    g_free(desktop_id);
    g_free(applications_dir);
    g_strfreev(mime_types);
    return TRUE;
}

static void load_existing_custom(const gchar *path,
                                 GtkEntry *name_entry,
                                 GtkEntry *command_entry,
                                 GtkEntry *icon_entry,
                                 GtkEntry *mime_entry,
                                 GtkToggleButton *terminal,
                                 GtkToggleButton *show_menu)
{
    GKeyFile *key = g_key_file_new();
    gchar *value;
    gchar **mime;
    gsize count;
    GError *error = NULL;

    if (!path || !g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, &error)) {
        g_clear_error(&error);
        g_key_file_unref(key);
        return;
    }

    value = g_key_file_get_locale_string(key, "Desktop Entry", "Name", NULL, NULL);
    if (value) { gtk_entry_set_text(name_entry, value); g_free(value); }
    value = g_key_file_get_string(key, "Desktop Entry", "Exec", NULL);
    if (value) { gtk_entry_set_text(command_entry, value); g_free(value); }
    value = g_key_file_get_string(key, "Desktop Entry", "Icon", NULL);
    if (value) { gtk_entry_set_text(icon_entry, value); g_free(value); }
    mime = g_key_file_get_string_list(key, "Desktop Entry", "MimeType", &count, NULL);
    if (mime) {
        value = g_strjoinv(";", mime);
        gtk_entry_set_text(mime_entry, value);
        g_free(value);
        g_strfreev(mime);
    }
    gtk_toggle_button_set_active(terminal,
        g_key_file_get_boolean(key, "Desktop Entry", "Terminal", NULL));
    gtk_toggle_button_set_active(show_menu,
        !g_key_file_get_boolean(key, "Desktop Entry", "NoDisplay", NULL));
    g_key_file_unref(key);
}

static gboolean run_custom_application_dialog(CustomDialogData *ctx)
{
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *grid;
    GtkWidget *name_entry;
    GtkWidget *command_entry;
    GtkWidget *icon_entry;
    GtkWidget *mime_entry;
    GtkWidget *icon_image;
    GtkWidget *choose_icon;
    GtkWidget *terminal;
    GtkWidget *show_menu;
    GtkWidget *set_default;
    GtkWidget *open_now;
    IconButtonData icon_data;
    gboolean saved = FALSE;

    dialog = gtk_dialog_new_with_buttons(ctx->desktop_path
            ? _("Edit Custom Application") : _("Add Custom Application"),
        ctx->parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        _("Cancel"), GTK_RESPONSE_CANCEL,
        _("Save"), GTK_RESPONSE_ACCEPT,
        NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 560, 360);
    gtk_window_set_position(GTK_WINDOW(dialog), ctx->parent
        ? GTK_WIN_POS_CENTER_ON_PARENT : GTK_WIN_POS_CENTER);

    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);

    name_entry = gtk_entry_new();
    command_entry = gtk_entry_new();
    icon_entry = gtk_entry_new();
    mime_entry = gtk_entry_new();
    icon_image = gtk_image_new_from_icon_name("application-x-executable",
                                               GTK_ICON_SIZE_DIALOG);
    choose_icon = gtk_button_new_with_label(_("Choose..."));
    terminal = gtk_check_button_new_with_label(_("Run in a terminal"));
    show_menu = gtk_check_button_new_with_label(_("Show in the applications menu"));
    set_default = gtk_check_button_new_with_label(_("Set as default for these MIME types"));
    open_now = gtk_check_button_new_with_label(_("Open the selected files after saving"));

    gtk_entry_set_placeholder_text(GTK_ENTRY(command_entry), "program %F");
    gtk_entry_set_placeholder_text(GTK_ENTRY(mime_entry), "image/png;image/jpeg");
    if (ctx->mime_type)
        gtk_entry_set_text(GTK_ENTRY(mime_entry), ctx->mime_type);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(open_now),
                                 ctx->launch_after_save && ctx->paths != NULL);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("Name:")), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), name_entry, 1, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("Command:")), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), command_entry, 1, 1, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("Icon:")), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), icon_entry, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), choose_icon, 2, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), icon_image, 3, 0, 1, 3);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("MIME types:")), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), mime_entry, 1, 3, 3, 1);
    gtk_grid_attach(GTK_GRID(grid), terminal, 1, 4, 3, 1);
    gtk_grid_attach(GTK_GRID(grid), show_menu, 1, 5, 3, 1);
    gtk_grid_attach(GTK_GRID(grid), set_default, 1, 6, 3, 1);
    if (ctx->paths)
        gtk_grid_attach(GTK_GRID(grid), open_now, 1, 7, 3, 1);

    icon_data.entry = icon_entry;
    icon_data.image = icon_image;
    icon_data.parent = GTK_WINDOW(dialog);
    g_signal_connect(choose_icon, "clicked", G_CALLBACK(choose_icon_clicked),
                     &icon_data);

    if (ctx->desktop_path)
        load_existing_custom(ctx->desktop_path,
            GTK_ENTRY(name_entry), GTK_ENTRY(command_entry),
            GTK_ENTRY(icon_entry), GTK_ENTRY(mime_entry),
            GTK_TOGGLE_BUTTON(terminal), GTK_TOGGLE_BUTTON(show_menu));

    gtk_widget_show_all(content);
    while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const gchar *name = gtk_entry_get_text(GTK_ENTRY(name_entry));
        const gchar *command = gtk_entry_get_text(GTK_ENTRY(command_entry));
        const gchar *icon = gtk_entry_get_text(GTK_ENTRY(icon_entry));
        const gchar *mime = gtk_entry_get_text(GTK_ENTRY(mime_entry));
        GAppInfo *app = NULL;

        if (!name || !*name) {
            report_error("%s", _("Enter a name for the application."));
            continue;
        }

        if (save_custom_application(GTK_WINDOW(dialog), ctx->desktop_path,
                name, command, icon, mime,
                gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(terminal)),
                gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(show_menu)),
                gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(set_default)),
                NULL, &app)) {
            saved = TRUE;
            if (app && ctx->paths &&
                gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(open_now)))
                xdg_apps_launch_app_info(app, ctx->paths, ctx->parent);
            if (app)
                g_object_unref(app);
            break;
        }
    }

    gtk_widget_destroy(dialog);
    return saved;
}

void xdg_apps_add_custom_for_paths(GList *paths, GtkWindow *parent)
{
    CustomDialogData ctx = {0};
    ctx.paths = copy_paths(paths);
    ctx.parent = parent;
    ctx.mime_type = mime_type_for_paths(paths);
    ctx.launch_after_save = TRUE;
    run_custom_application_dialog(&ctx);
    destroy_glist(&ctx.paths);
    g_free(ctx.mime_type);
}

static gboolean custom_desktop_is_owned(const gchar *path)
{
    GKeyFile *key = g_key_file_new();
    gboolean owned = FALSE;

    if (g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, NULL))
        owned = g_key_file_get_boolean(key, "Desktop Entry",
                                       CUSTOM_APP_MARKER, NULL);
    g_key_file_unref(key);
    return owned;
}

static void populate_custom_store(GtkListStore *store)
{
    gchar *directory = g_build_filename(g_get_user_data_dir(),
                                         "applications", NULL);
    GDir *dir = g_dir_open(directory, 0, NULL);
    const gchar *leaf;

    gtk_list_store_clear(store);
    if (!dir) {
        g_free(directory);
        return;
    }

    while ((leaf = g_dir_read_name(dir)) != NULL) {
        gchar *path;
        GKeyFile *key;
        gchar *name;
        gchar *command;
        gchar **mime;
        gsize count;
        gchar *mime_text = NULL;
        GtkTreeIter iter;

        if (!g_str_has_suffix(leaf, ".desktop"))
            continue;
        path = g_build_filename(directory, leaf, NULL);
        if (!custom_desktop_is_owned(path)) {
            g_free(path);
            continue;
        }
        key = g_key_file_new();
        if (!g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, NULL)) {
            g_key_file_unref(key);
            g_free(path);
            continue;
        }
        name = g_key_file_get_locale_string(key, "Desktop Entry", "Name", NULL, NULL);
        command = g_key_file_get_string(key, "Desktop Entry", "Exec", NULL);
        mime = g_key_file_get_string_list(key, "Desktop Entry", "MimeType", &count, NULL);
        if (mime) {
            mime_text = g_strjoinv(";", mime);
            g_strfreev(mime);
        }
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
            APP_COL_NAME, name ? name : leaf,
            APP_COL_COMMAND, command ? command : "",
            APP_COL_MIME, mime_text ? mime_text : "",
            APP_COL_PATH, path,
            -1);
        g_free(name);
        g_free(command);
        g_free(mime_text);
        g_key_file_unref(key);
        g_free(path);
    }
    g_dir_close(dir);
    g_free(directory);
}

static gchar *manager_selected_path(GtkTreeView *tree)
{
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree);
    GtkTreeModel *model;
    GtkTreeIter iter;
    gchar *path = NULL;

    if (gtk_tree_selection_get_selected(selection, &model, &iter))
        gtk_tree_model_get(model, &iter, APP_COL_PATH, &path, -1);
    return path;
}

static void remove_desktop_from_mimeapps(const gchar *desktop_id)
{
    gchar *path = g_build_filename(g_get_user_config_dir(),
                                   "mimeapps.list", NULL);
    GKeyFile *key = g_key_file_new();
    const gchar *groups[] = {
        "Default Applications",
        "Added Associations",
        "Removed Associations",
        NULL
    };
    gint g;

    if (!g_key_file_load_from_file(key, path,
            G_KEY_FILE_KEEP_COMMENTS | G_KEY_FILE_KEEP_TRANSLATIONS, NULL)) {
        g_key_file_unref(key);
        g_free(path);
        return;
    }

    for (g = 0; groups[g]; g++) {
        gsize key_count = 0;
        gchar **keys = g_key_file_get_keys(key, groups[g], &key_count, NULL);
        gsize i;
        for (i = 0; keys && i < key_count; i++) {
            gchar *value = g_key_file_get_string(key, groups[g], keys[i], NULL);
            gchar *new_value = mimeapps_remove_id(value, desktop_id);
            if (new_value && *new_value)
                g_key_file_set_string(key, groups[g], keys[i], new_value);
            else
                g_key_file_remove_key(key, groups[g], keys[i], NULL);
            g_free(new_value);
            g_free(value);
        }
        g_strfreev(keys);
    }

    if (!mimeapps_write_atomic(key, path, NULL))
        ROX_LOG_WARNING("mimeapps", "unable to update %s while removing %s",
                        path, desktop_id ? desktop_id : "(null)");

    g_key_file_unref(key);
    g_free(path);
}

static void remove_owned_application(const gchar *path)
{
    gchar *desktop_id = g_path_get_basename(path);
    GKeyFile *key = g_key_file_new();
    gchar *icon = NULL;

    if (g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, NULL))
        icon = g_key_file_get_string(key, "Desktop Entry", "Icon", NULL);
    g_key_file_unref(key);

    remove_desktop_from_mimeapps(desktop_id);
    unlink(path);

    if (icon && g_str_has_prefix(icon, CUSTOM_APP_PREFIX)) {
        gchar *svg_leaf = g_strconcat(icon, ".svg", NULL);
        gchar *svgz_leaf = g_strconcat(icon, ".svgz", NULL);
        gchar *png_leaf = g_strconcat(icon, ".png", NULL);
        gchar *svg = g_build_filename(g_get_user_data_dir(), "icons", "hicolor",
                                      "scalable", "apps", svg_leaf, NULL);
        gchar *svgz = g_build_filename(g_get_user_data_dir(), "icons", "hicolor",
                                       "scalable", "apps", svgz_leaf, NULL);
        gchar *png = g_build_filename(g_get_user_data_dir(), "icons", "hicolor",
                                      "128x128", "apps", png_leaf, NULL);
        unlink(svg);
        unlink(svgz);
        unlink(png);
        g_free(svg);
        g_free(svgz);
        g_free(png);
        g_free(svg_leaf);
        g_free(svgz_leaf);
        g_free(png_leaf);
    }
    g_free(icon);
    g_free(desktop_id);
    update_desktop_database();
}

void xdg_apps_show_manager(GtkWindow *parent)
{
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *box;
    GtkWidget *scroll;
    GtkWidget *tree;
    GtkWidget *buttons;
    GtkWidget *add;
    GtkWidget *edit;
    GtkWidget *remove;
    GtkWidget *open_folder;
    GtkListStore *store;
    GtkCellRenderer *renderer;
    gint response;

    dialog = gtk_dialog_new_with_buttons(_("Custom Applications"), parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        _("Close"), GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 760, 430);
    gtk_window_set_position(GTK_WINDOW(dialog), parent
        ? GTK_WIN_POS_CENTER_ON_PARENT : GTK_WIN_POS_CENTER);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    gtk_box_pack_start(GTK_BOX(content), box, TRUE, TRUE, 0);

    store = gtk_list_store_new(APP_N_COLUMNS,
                               G_TYPE_STRING, G_TYPE_STRING,
                               G_TYPE_STRING, G_TYPE_STRING);
    tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
        gtk_tree_view_column_new_with_attributes(_("Name"), renderer,
                                                  "text", APP_COL_NAME, NULL));
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
        gtk_tree_view_column_new_with_attributes(_("Command"), renderer,
                                                  "text", APP_COL_COMMAND, NULL));
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
        gtk_tree_view_column_new_with_attributes(_("MIME Types"), renderer,
                                                  "text", APP_COL_MIME, NULL));
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), tree);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

    buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_START);
    add = gtk_button_new_with_label(_("Add"));
    edit = gtk_button_new_with_label(_("Edit"));
    remove = gtk_button_new_with_label(_("Remove"));
    open_folder = gtk_button_new_with_label(_("Open Applications Folder"));
    gtk_container_add(GTK_CONTAINER(buttons), add);
    gtk_container_add(GTK_CONTAINER(buttons), edit);
    gtk_container_add(GTK_CONTAINER(buttons), remove);
    gtk_container_add(GTK_CONTAINER(buttons), open_folder);
    gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);

    populate_custom_store(store);
    gtk_widget_show_all(content);

    g_signal_connect(add, "clicked", G_CALLBACK(manager_add), tree);
    g_signal_connect(edit, "clicked", G_CALLBACK(manager_edit), tree);
    g_signal_connect(remove, "clicked", G_CALLBACK(manager_remove), tree);
    g_signal_connect(open_folder, "clicked",
                     G_CALLBACK(open_user_apps_clicked), NULL);

    while ((response = gtk_dialog_run(GTK_DIALOG(dialog))) != GTK_RESPONSE_CLOSE &&
           response != GTK_RESPONSE_DELETE_EVENT) {
        (void) response;
    }

    gtk_widget_destroy(dialog);
    g_object_unref(store);
}

/* The manager buttons are connected after creation using these callbacks. */
static void manager_add(GtkButton *button, gpointer data)
{
    GtkWidget *tree = data;
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
    CustomDialogData ctx = {0};
    ctx.parent = parent;
    ctx.launch_after_save = FALSE;
    if (run_custom_application_dialog(&ctx))
        populate_custom_store(GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(tree))));
}

static void manager_edit(GtkButton *button, gpointer data)
{
    GtkWidget *tree = data;
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
    gchar *path = manager_selected_path(GTK_TREE_VIEW(tree));
    CustomDialogData ctx = {0};

    if (!path)
        return;
    ctx.parent = parent;
    ctx.desktop_path = path;
    ctx.launch_after_save = FALSE;
    if (run_custom_application_dialog(&ctx))
        populate_custom_store(GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(tree))));
    g_free(path);
}

static void manager_remove(GtkButton *button, gpointer data)
{
    GtkWidget *tree = data;
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
    gchar *path = manager_selected_path(GTK_TREE_VIEW(tree));
    GtkWidget *confirm;

    if (!path)
        return;
    confirm = gtk_message_dialog_new(parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
        "%s", _("Remove the selected custom application?"));
    gtk_dialog_add_button(GTK_DIALOG(confirm), _("Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(confirm), _("Remove"), GTK_RESPONSE_ACCEPT);
    gtk_window_set_position(GTK_WINDOW(confirm), GTK_WIN_POS_CENTER_ON_PARENT);
    if (gtk_dialog_run(GTK_DIALOG(confirm)) == GTK_RESPONSE_ACCEPT) {
        remove_owned_application(path);
        populate_custom_store(GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(tree))));
    }
    gtk_widget_destroy(confirm);
    g_free(path);
}

static void add_application_clicked(GtkButton *button, gpointer data)
{
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
    CustomDialogData ctx = {0};

    (void) data;
    ctx.parent = parent;
    ctx.launch_after_save = FALSE;
    run_custom_application_dialog(&ctx);
}

static void show_manager_clicked(GtkButton *button, gpointer data)
{
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
    (void) data;
    xdg_apps_show_manager(parent);
}

static void open_user_apps_clicked(GtkButton *button, gpointer data)
{
    gchar *directory = g_build_filename(g_get_user_data_dir(),
                                         "applications", NULL);
    (void) button;
    (void) data;
    g_mkdir_with_parents(directory, 0755);
    filer_opendir(directory, NULL, NULL);
    g_free(directory);
}

static void open_mimeapps_clicked(GtkButton *button, gpointer data)
{
    gchar *path = g_build_filename(g_get_user_config_dir(),
                                   "mimeapps.list", NULL);
    (void) button;
    (void) data;
    if (g_file_test(path, G_FILE_TEST_EXISTS))
        open_to_show(path);
    else
        filer_opendir(g_get_user_config_dir(), NULL, NULL);
    g_free(path);
}

static GList *build_file_association_tools(Option *option, xmlNode *node,
                                            guchar *label)
{
    GtkWidget *box;
    GtkWidget *add;
    GtkWidget *manage;
    GtkWidget *apps;
    GtkWidget *mimeapps;

    (void) option;
    (void) node;
    (void) label;
    box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(box), GTK_BUTTONBOX_START);
    add = gtk_button_new_with_label(_("Add Custom Application..."));
    manage = gtk_button_new_with_label(_("Manage Custom Applications..."));
    apps = gtk_button_new_with_label(_("Open User Applications Folder"));
    mimeapps = gtk_button_new_with_label(_("Show mimeapps.list"));
    gtk_container_add(GTK_CONTAINER(box), add);
    gtk_container_add(GTK_CONTAINER(box), manage);
    gtk_container_add(GTK_CONTAINER(box), apps);
    gtk_container_add(GTK_CONTAINER(box), mimeapps);
    g_signal_connect(add, "clicked", G_CALLBACK(add_application_clicked), NULL);
    g_signal_connect(manage, "clicked", G_CALLBACK(show_manager_clicked), NULL);
    g_signal_connect(apps, "clicked", G_CALLBACK(open_user_apps_clicked), NULL);
    g_signal_connect(mimeapps, "clicked", G_CALLBACK(open_mimeapps_clicked), NULL);
    return g_list_append(NULL, box);
}

void xdg_apps_init(void)
{
    option_register_widget("file-association-tools",
                           build_file_association_tools);
}
