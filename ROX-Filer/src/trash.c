/*
 * ROX-Filer GTK3 - standard Freedesktop Trash support
 * Copyright (C) 2026 josejp2424
 * GPL-3.0-or-later
 */

#include "config.h"

#include <errno.h>
#include <string.h>
#include <glib/gstdio.h>

#include "global.h"
#include "trash.h"
#include "gui_support.h"
#include "support.h"
#include "view_iface.h"

#define TRASH_GROUP "Trash Info"

static void propagate_or_clear(GError **destination, GError *source)
{
    if (!source)
        return;
    if (destination)
        g_propagate_error(destination, source);
    else
        g_error_free(source);
}

static gboolean ensure_trash_dirs(GError **error)
{
    gchar *files = rox_trash_files_dir();
    gchar *info = rox_trash_info_dir();
    gboolean ok = TRUE;

    if (g_mkdir_with_parents(files, 0700) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    _("Unable to create the Trash directory '%s': %s"),
                    files, g_strerror(errno));
        ok = FALSE;
    } else if (g_mkdir_with_parents(info, 0700) != 0) {
        g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
                    _("Unable to create the Trash information directory '%s': %s"),
                    info, g_strerror(errno));
        ok = FALSE;
    }

    g_free(files);
    g_free(info);
    return ok;
}

gchar *rox_trash_files_dir(void)
{
    return g_build_filename(g_get_user_data_dir(), "Trash", "files", NULL);
}

gchar *rox_trash_info_dir(void)
{
    return g_build_filename(g_get_user_data_dir(), "Trash", "info", NULL);
}

static gboolean directory_has_entries(const gchar *path)
{
    GDir *dir;
    const gchar *name;

    dir = g_dir_open(path, 0, NULL);
    if (!dir)
        return FALSE;
    name = g_dir_read_name(dir);
    g_dir_close(dir);
    return name != NULL;
}

gboolean rox_trash_is_empty(void)
{
    gchar *files = rox_trash_files_dir();
    gboolean empty = !directory_has_entries(files);
    g_free(files);
    return empty;
}

const gchar *rox_trash_icon_name(void)
{
    return rox_trash_is_empty() ? "user-trash" : "user-trash-full";
}

static gchar *unique_trash_name(const gchar *files_dir,
                                const gchar *info_dir,
                                const gchar *basename)
{
    guint index = 0;

    for (;;) {
        gchar *candidate = index == 0
            ? g_strdup(basename)
            : g_strdup_printf("%s.%u", basename, index);
        gchar *file_path = g_build_filename(files_dir, candidate, NULL);
        gchar *info_leaf = g_strconcat(candidate, ".trashinfo", NULL);
        gchar *info_path = g_build_filename(info_dir, info_leaf, NULL);
        gboolean exists = g_file_test(file_path, G_FILE_TEST_EXISTS) ||
                          g_file_test(info_path, G_FILE_TEST_EXISTS);
        g_free(file_path);
        g_free(info_leaf);
        g_free(info_path);
        if (!exists)
            return candidate;
        g_free(candidate);
        index++;
    }
}

/* GIO is used first. The fallback keeps Puppy installations without a GVfs
 * trash backend compatible with the standard ~/.local/share/Trash layout. */
gboolean rox_trash_file(GFile *file, GError **error)
{
    GError *local_error = NULL;
    gchar *source_path;
    gchar *files_dir;
    gchar *info_dir;
    gchar *basename;
    gchar *trash_name;
    gchar *dest_path;
    gchar *info_leaf;
    gchar *info_path;
    gchar *escaped;
    GDateTime *now;
    gchar *date;
    gchar *contents;
    GFile *dest;
    gboolean moved;

    g_return_val_if_fail(G_IS_FILE(file), FALSE);

    if (g_file_trash(file, NULL, &local_error))
        return TRUE;

    if (local_error &&
        !g_error_matches(local_error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED) &&
        !g_error_matches(local_error, G_IO_ERROR, G_IO_ERROR_FAILED)) {
        propagate_or_clear(error, local_error);
        return FALSE;
    }
    g_clear_error(&local_error);

    source_path = g_file_get_path(file);
    if (!source_path) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                            _("Only local files can be moved to this Trash."));
        return FALSE;
    }
    if (!ensure_trash_dirs(error)) {
        g_free(source_path);
        return FALSE;
    }

    files_dir = rox_trash_files_dir();
    info_dir = rox_trash_info_dir();
    basename = g_path_get_basename(source_path);
    trash_name = unique_trash_name(files_dir, info_dir, basename);
    dest_path = g_build_filename(files_dir, trash_name, NULL);
    info_leaf = g_strconcat(trash_name, ".trashinfo", NULL);
    info_path = g_build_filename(info_dir, info_leaf, NULL);

    escaped = g_uri_escape_string(source_path, "/", FALSE);
    now = g_date_time_new_now_local();
    date = g_date_time_format(now, "%Y-%m-%dT%H:%M:%S");
    contents = g_strdup_printf("[%s]\nPath=%s\nDeletionDate=%s\n",
                               TRASH_GROUP, escaped, date);

    if (!g_file_set_contents(info_path, contents, -1, &local_error)) {
        propagate_or_clear(error, local_error);
        moved = FALSE;
    } else {
        dest = g_file_new_for_path(dest_path);
        moved = g_file_move(file, dest, G_FILE_COPY_NONE, NULL,
                            NULL, NULL, &local_error);
        g_object_unref(dest);
        if (!moved) {
            g_unlink(info_path);
            propagate_or_clear(error, local_error);
        }
    }

    g_free(contents);
    g_free(date);
    g_date_time_unref(now);
    g_free(escaped);
    g_free(info_path);
    g_free(info_leaf);
    g_free(dest_path);
    g_free(trash_name);
    g_free(basename);
    g_free(info_dir);
    g_free(files_dir);
    g_free(source_path);
    return moved;
}

void rox_trash_open(FilerWindow *source_window)
{
    gchar *files = rox_trash_files_dir();
    GError *error = NULL;

    if (!ensure_trash_dirs(&error)) {
        report_error("%s", error->message);
        g_clear_error(&error);
        g_free(files);
        return;
    }
    filer_opendir(files, source_window, NULL);
    g_free(files);
}

gboolean rox_trash_filer_is_trash(FilerWindow *filer_window)
{
    gchar *files;
    gchar *canonical_files;
    gchar *canonical_current;
    gboolean same;

    if (!filer_window || !filer_window->sym_path)
        return FALSE;
    files = rox_trash_files_dir();
    canonical_files = g_canonicalize_filename(files, NULL);
    canonical_current = g_canonicalize_filename(filer_window->sym_path, NULL);
    same = g_strcmp0(canonical_files, canonical_current) == 0;
    g_free(canonical_current);
    g_free(canonical_files);
    g_free(files);
    return same;
}

static gchar *original_path_for_name(const gchar *name, GError **error)
{
    gchar *info_dir = rox_trash_info_dir();
    gchar *leaf = g_strconcat(name, ".trashinfo", NULL);
    gchar *path = g_build_filename(info_dir, leaf, NULL);
    GKeyFile *key = g_key_file_new();
    gchar *encoded = NULL;
    gchar *decoded = NULL;

    if (!g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, error))
        goto out;
    encoded = g_key_file_get_string(key, TRASH_GROUP, "Path", error);
    if (!encoded)
        goto out;
    decoded = g_uri_unescape_string(encoded, NULL);
    if (!decoded || !g_path_is_absolute(decoded)) {
        g_clear_pointer(&decoded, g_free);
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    _("The Trash information for '%s' has no valid original path."),
                    name);
    }
out:
    g_free(encoded);
    g_key_file_unref(key);
    g_free(path);
    g_free(leaf);
    g_free(info_dir);
    return decoded;
}

static gboolean restore_one(const gchar *trashed_path, GError **error)
{
    gchar *name = g_path_get_basename(trashed_path);
    gchar *original = original_path_for_name(name, error);
    gchar *parent = NULL;
    gchar *info_dir = NULL;
    gchar *info_leaf = NULL;
    gchar *info_path = NULL;
    GFile *source = NULL;
    GFile *dest = NULL;
    gboolean ok = FALSE;

    if (!original)
        goto out;
    if (g_file_test(original, G_FILE_TEST_EXISTS)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                    _("Cannot restore '%s' because '%s' already exists."),
                    name, original);
        goto out;
    }
    parent = g_path_get_dirname(original);
    if (!g_file_test(parent, G_FILE_TEST_IS_DIR)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                    _("Cannot restore '%s' because its original folder no longer exists: %s"),
                    name, parent);
        goto out;
    }

    source = g_file_new_for_path(trashed_path);
    dest = g_file_new_for_path(original);
    ok = g_file_move(source, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, error);
    if (ok) {
        info_dir = rox_trash_info_dir();
        info_leaf = g_strconcat(name, ".trashinfo", NULL);
        info_path = g_build_filename(info_dir, info_leaf, NULL);
        if (g_unlink(info_path) != 0 && errno != ENOENT)
            g_warning("Unable to remove trash information '%s': %s",
                      info_path, g_strerror(errno));
    }
out:
    g_clear_object(&dest);
    g_clear_object(&source);
    g_free(info_path);
    g_free(info_leaf);
    g_free(info_dir);
    g_free(parent);
    g_free(original);
    g_free(name);
    return ok;
}

void rox_trash_restore_selected(FilerWindow *filer_window)
{
    GList *paths;
    GList *node;
    GString *errors;
    guint restored = 0;

    if (!rox_trash_filer_is_trash(filer_window)) {
        delayed_error(_("Open the Trash and select the items you want to restore."));
        return;
    }
    paths = filer_selected_items(filer_window);
    if (!paths) {
        delayed_error(_("Select one or more items to restore."));
        return;
    }

    errors = g_string_new(NULL);
    for (node = paths; node; node = node->next) {
        GError *error = NULL;
        if (restore_one(node->data, &error)) {
            restored++;
        } else {
            if (errors->len)
                g_string_append_c(errors, '\n');
            g_string_append_printf(errors, "%s: %s", (gchar *)node->data,
                                   error ? error->message : _("Unknown error"));
            g_clear_error(&error);
        }
    }
    destroy_glist(&paths);
    filer_refresh(filer_window);
    filer_update_all();

    if (errors->len)
        report_error("%s", errors->str);
    else if (restored == 1)
        info_message(_("One item was restored."));
    else if (restored > 1)
        info_message(_("%u items were restored."), restored);
    g_string_free(errors, TRUE);
}

static gboolean delete_recursively(GFile *file, GError **error)
{
    GFileType type;

    type = g_file_query_file_type(file, G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                  NULL);
    if (type == G_FILE_TYPE_DIRECTORY) {
        GFileEnumerator *en;
        GFileInfo *info;

        en = g_file_enumerate_children(file,
            G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE,
            G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, error);
        if (!en)
            return FALSE;
        while ((info = g_file_enumerator_next_file(en, NULL, error))) {
            GFile *child = g_file_get_child(file, g_file_info_get_name(info));
            gboolean ok = delete_recursively(child, error);
            g_object_unref(child);
            g_object_unref(info);
            if (!ok) {
                g_object_unref(en);
                return FALSE;
            }
        }
        if (error && *error) {
            g_object_unref(en);
            return FALSE;
        }
        g_object_unref(en);
    }
    return g_file_delete(file, NULL, error);
}

static void empty_directory(const gchar *path, GString *errors)
{
    GDir *dir = g_dir_open(path, 0, NULL);
    const gchar *name;

    if (!dir)
        return;
    while ((name = g_dir_read_name(dir))) {
        gchar *child_path = g_build_filename(path, name, NULL);
        GFile *child = g_file_new_for_path(child_path);
        GError *error = NULL;
        if (!delete_recursively(child, &error)) {
            if (errors->len)
                g_string_append_c(errors, '\n');
            g_string_append_printf(errors, "%s: %s", child_path,
                error ? error->message : _("Unknown error"));
            g_clear_error(&error);
        }
        g_object_unref(child);
        g_free(child_path);
    }
    g_dir_close(dir);
}

void rox_trash_empty(GtkWindow *parent)
{
    gchar *files;
    gchar *info;
    GString *errors;
    gboolean accepted;
    (void)parent;

    if (rox_trash_is_empty()) {
        info_message(_("The Trash is already empty."));
        return;
    }
    accepted = confirm(_("Permanently delete every item in the Trash?\n\nThis action cannot be undone."),
                       ROX_ICON_TRASH, _("_Empty Trash"));
    if (!accepted)
        return;

    files = rox_trash_files_dir();
    info = rox_trash_info_dir();
    errors = g_string_new(NULL);
    empty_directory(files, errors);
    if (!directory_has_entries(files))
        empty_directory(info, errors);
    filer_update_all();

    if (errors->len)
        report_error("%s", errors->str);
    g_string_free(errors, TRUE);
    g_free(info);
    g_free(files);
}

static void toolbar_trash_open(GtkWidget *widget, gpointer data)
{
    (void)widget;
    rox_trash_open((FilerWindow *)data);
}

static void toolbar_trash_restore(GtkMenuItem *item, gpointer data)
{
    (void)item;
    rox_trash_restore_selected((FilerWindow *)data);
}

static void toolbar_trash_empty(GtkMenuItem *item, gpointer data)
{
    FilerWindow *filer_window = data;
    (void)item;
    rox_trash_empty(filer_window && filer_window->window
                    ? GTK_WINDOW(filer_window->window) : NULL);
}

static void toolbar_trash_show_menu(GtkMenuToolButton *button, gpointer data)
{
    FilerWindow *filer_window = data;
    GtkWidget *menu = gtk_menu_tool_button_get_menu(button);
    GtkWidget *restore = g_object_get_data(G_OBJECT(menu), "restore-item");
    GtkWidget *empty = g_object_get_data(G_OBJECT(menu), "empty-item");

    gtk_widget_set_sensitive(restore,
        rox_trash_filer_is_trash(filer_window) &&
        view_count_selected(filer_window->view) > 0);
    gtk_widget_set_sensitive(empty, !rox_trash_is_empty());
}

GtkToolItem *rox_trash_toolbar_button_new(FilerWindow *filer_window)
{
    GtkWidget *icon;
    GtkToolItem *button;
    GtkWidget *menu;
    GtkWidget *item;
    GtkWidget *restore_item;
    GtkWidget *empty_item;

    icon = image_new_icon(rox_trash_icon_name(), GTK_ICON_SIZE_LARGE_TOOLBAR);
    button = gtk_menu_tool_button_new(icon, _("Trash"));
    gtk_tool_item_set_homogeneous(button, FALSE);
    gtk_tool_item_set_tooltip_text(button,
        _("Open the Trash; use the arrow to restore selected items or empty it"));

    menu = rox_menu_new();
    item = menu_item_new_with_icon(_("Open Trash"), ROX_ICON_TRASH);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect(item, "activate", G_CALLBACK(toolbar_trash_open), filer_window);

    restore_item = menu_item_new_with_icon(_("Restore Selected"), ROX_ICON_UNDO);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), restore_item);
    g_signal_connect(restore_item, "activate",
                     G_CALLBACK(toolbar_trash_restore), filer_window);

    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    empty_item = menu_item_new_with_icon(_("Empty Trash"), ROX_ICON_DELETE);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), empty_item);
    g_signal_connect(empty_item, "activate",
                     G_CALLBACK(toolbar_trash_empty), filer_window);

    g_object_set_data(G_OBJECT(menu), "restore-item", restore_item);
    g_object_set_data(G_OBJECT(menu), "empty-item", empty_item);
    gtk_menu_tool_button_set_menu(GTK_MENU_TOOL_BUTTON(button), menu);
    g_signal_connect(button, "clicked", G_CALLBACK(toolbar_trash_open), filer_window);
    g_signal_connect(button, "show-menu",
                     G_CALLBACK(toolbar_trash_show_menu), filer_window);
    return button;
}

GFileMonitor *rox_trash_monitor_new(GError **error)
{
    gchar *files = rox_trash_files_dir();
    GFile *dir;
    GFileMonitor *monitor;

    if (!ensure_trash_dirs(error)) {
        g_free(files);
        return NULL;
    }
    dir = g_file_new_for_path(files);
    monitor = g_file_monitor_directory(dir, G_FILE_MONITOR_NONE, NULL, error);
    g_object_unref(dir);
    g_free(files);
    return monitor;
}
