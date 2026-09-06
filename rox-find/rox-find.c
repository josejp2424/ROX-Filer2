/*
 * ROX File Search - native GTK3 search utility for ROX-Filer
 * Copyright (C) 2026 josejp2424
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gi18n.h>
#include <locale.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>

#ifndef GETTEXT_PACKAGE
#define GETTEXT_PACKAGE "rox-find"
#endif
#ifndef LOCALEDIR
#define LOCALEDIR "/usr/share/locale"
#endif

#define APP_ID "org.rox.sourceforge.ROXFileSearch"
#define MAX_CONTENT_BYTES_DEFAULT (20 * 1024 * 1024)

enum {
    COL_ICON,
    COL_NAME,
    COL_PARENT,
    COL_TYPE,
    COL_SIZE,
    COL_MTIME,
    COL_PATH,
    N_COLS
};

typedef enum {
    SEARCH_ANY = 0,
    SEARCH_FILES,
    SEARCH_DIRECTORIES,
    SEARCH_SYMLINKS
} SearchType;

typedef struct {
    GtkApplication *app;
    GtkWidget *window;
    GtkWidget *query_entry;
    GtkWidget *location_entry;
    GtkWidget *content_entry;
    GtkWidget *recursive_check;
    GtkWidget *hidden_check;
    GtkWidget *case_check;
    GtkWidget *content_check;
    GtkWidget *follow_check;
    GtkWidget *filesystem_check;
    GtkWidget *type_combo;
    GtkWidget *min_size_spin;
    GtkWidget *max_size_spin;
    GtkWidget *content_limit_spin;
    GtkWidget *tree;
    GtkListStore *store;
    GtkWidget *status;
    GtkWidget *spinner;
    GtkWidget *search_button;
    GtkWidget *stop_button;
    GCancellable *cancel;
    GThread *thread;
    guint result_count;
    gchar **initial_roots;
    gchar *initial_name;
    gchar *initial_content;
    gboolean initial_hidden;
    gboolean initial_recursive;
    gboolean initial_follow_links;
    gboolean initial_one_filesystem;
    guint initial_content_limit_mb;
    gboolean close_requested;
} SearchWindow;

typedef struct {
    SearchWindow *ui;
    gchar *query;
    gchar *content;
    gchar **roots;
    gboolean recursive;
    gboolean hidden;
    gboolean case_sensitive;
    gboolean search_content;
    gboolean follow_links;
    gboolean one_filesystem;
    SearchType type;
    guint64 min_size;
    guint64 max_size;
    guint64 max_content_size;
    guint result_count;
    GCancellable *cancel;
    GHashTable *visited_dirs;
} SearchJob;

typedef struct {
    SearchWindow *ui;
    gchar *path;
    gchar *name;
    gchar *parent;
    gchar *content_type;
    gchar *size_text;
    gchar *mtime_text;
    gchar *icon_name;
} SearchResult;

typedef struct {
    SearchWindow *ui;
    gchar *message;
    gboolean cancelled;
} SearchFinished;

static gchar *format_size(guint64 size)
{
    return g_format_size_full(size, G_FORMAT_SIZE_IEC_UNITS);
}

static gchar *format_mtime(guint64 seconds)
{
    GDateTime *dt = g_date_time_new_from_unix_local((gint64)seconds);
    gchar *text;
    if (!dt)
        return g_strdup("");
    text = g_date_time_format(dt, "%Y-%m-%d %H:%M");
    g_date_time_unref(dt);
    return text;
}

static gboolean text_matches(const gchar *haystack, const gchar *needle,
                             gboolean case_sensitive)
{
    gchar *a, *b;
    gboolean found;

    if (!needle || !*needle)
        return TRUE;
    if (!haystack)
        return FALSE;
    if (case_sensitive)
        return strstr(haystack, needle) != NULL;

    a = g_utf8_casefold(haystack, -1);
    b = g_utf8_casefold(needle, -1);
    found = strstr(a, b) != NULL;
    g_free(a);
    g_free(b);
    return found;
}

static gboolean filename_matches(const gchar *name, const gchar *query,
                                 gboolean case_sensitive)
{
    if (!query || !*query)
        return TRUE;
    if (strpbrk(query, "*?[") != NULL) {
        gchar *pattern = case_sensitive ? g_strdup(query) : g_utf8_casefold(query, -1);
        gchar *candidate = case_sensitive ? g_strdup(name) : g_utf8_casefold(name, -1);
        gboolean match = g_pattern_match_simple(pattern, candidate);
        g_free(pattern);
        g_free(candidate);
        return match;
    }
    return text_matches(name, query, case_sensitive);
}

static gboolean content_matches_file(GFile *file, const gchar *needle,
                                     gboolean case_sensitive,
                                     guint64 max_size,
                                     GCancellable *cancel)
{
    GFileInfo *info;
    GFileInputStream *stream;
    GByteArray *bytes;
    GError *error = NULL;
    gchar buffer[8192];
    gssize nread;
    gboolean found = FALSE;

    if (!needle || !*needle)
        return TRUE;

    info = g_file_query_info(file,
        G_FILE_ATTRIBUTE_STANDARD_SIZE "," G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
        G_FILE_QUERY_INFO_NONE, cancel, &error);
    if (!info) {
        g_clear_error(&error);
        return FALSE;
    }
    if ((guint64)g_file_info_get_size(info) > max_size) {
        g_object_unref(info);
        return FALSE;
    }
    {
        const gchar *ctype = g_file_info_get_content_type(info);
        if (ctype && !(g_str_has_prefix(ctype, "text/") ||
                       g_content_type_is_a(ctype, "application/xml") ||
                       g_content_type_is_a(ctype, "application/json") ||
                       g_content_type_is_a(ctype, "application/x-shellscript"))) {
            g_object_unref(info);
            return FALSE;
        }
    }
    g_object_unref(info);

    stream = g_file_read(file, cancel, &error);
    if (!stream) {
        g_clear_error(&error);
        return FALSE;
    }

    bytes = g_byte_array_new();
    while ((nread = g_input_stream_read(G_INPUT_STREAM(stream), buffer,
                                        sizeof(buffer), cancel, &error)) > 0) {
        g_byte_array_append(bytes, (const guint8 *)buffer, (guint)nread);
        if (bytes->len > max_size)
            break;
    }
    if (!error && bytes->len <= max_size) {
        g_byte_array_append(bytes, (const guint8 *)"", 1);
        if (g_utf8_validate((const gchar *)bytes->data, -1, NULL))
            found = text_matches((const gchar *)bytes->data, needle, case_sensitive);
    }
    g_clear_error(&error);
    g_byte_array_unref(bytes);
    g_object_unref(stream);
    return found;
}

static gboolean result_idle(gpointer data)
{
    SearchResult *r = data;
    GtkTreeIter iter;
    GIcon *gicon = NULL;

    if (!r->ui->cancel || g_cancellable_is_cancelled(r->ui->cancel))
        goto out;

    if (r->icon_name)
        gicon = g_themed_icon_new(r->icon_name);
    gtk_list_store_append(r->ui->store, &iter);
    gtk_list_store_set(r->ui->store, &iter,
        COL_ICON, gicon,
        COL_NAME, r->name,
        COL_PARENT, r->parent,
        COL_TYPE, r->content_type,
        COL_SIZE, r->size_text,
        COL_MTIME, r->mtime_text,
        COL_PATH, r->path,
        -1);
    if (gicon)
        g_object_unref(gicon);
    r->ui->result_count++;
    {
        gchar *status = g_strdup_printf(ngettext("%u result", "%u results",
                                                r->ui->result_count),
                                        r->ui->result_count);
        gtk_label_set_text(GTK_LABEL(r->ui->status), status);
        g_free(status);
    }
out:
    g_free(r->path);
    g_free(r->name);
    g_free(r->parent);
    g_free(r->content_type);
    g_free(r->size_text);
    g_free(r->mtime_text);
    g_free(r->icon_name);
    g_free(r);
    return G_SOURCE_REMOVE;
}

static gboolean finished_idle(gpointer data)
{
    SearchFinished *done = data;
    SearchWindow *ui = done->ui;

    gtk_spinner_stop(GTK_SPINNER(ui->spinner));
    gtk_widget_set_sensitive(ui->search_button, TRUE);
    gtk_widget_set_sensitive(ui->stop_button, FALSE);
    if (done->message)
        gtk_label_set_text(GTK_LABEL(ui->status), done->message);
    g_clear_object(&ui->cancel);
    ui->thread = NULL;
    g_free(done->message);
    g_free(done);
    if (ui->close_requested) {
        gtk_widget_destroy(ui->window);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_REMOVE;
}

static gboolean type_matches(SearchType type, GFileType file_type)
{
    switch (type) {
        case SEARCH_FILES: return file_type == G_FILE_TYPE_REGULAR;
        case SEARCH_DIRECTORIES: return file_type == G_FILE_TYPE_DIRECTORY;
        case SEARCH_SYMLINKS: return file_type == G_FILE_TYPE_SYMBOLIC_LINK;
        case SEARCH_ANY:
        default: return TRUE;
    }
}

static void queue_result(SearchJob *job, GFile *child, GFileInfo *info)
{
    SearchResult *r = g_new0(SearchResult, 1);
    const gchar *ctype = g_file_info_get_content_type(info);
    GIcon *icon = g_file_info_get_icon(info);
    gchar *uri;

    r->ui = job->ui;
    r->path = g_file_get_path(child);
    if (!r->path) {
        uri = g_file_get_uri(child);
        r->path = uri;
    }
    r->name = g_strdup(g_file_info_get_display_name(info));
    r->parent = g_path_get_dirname(r->path);
    r->content_type = ctype ? g_content_type_get_description(ctype) : g_strdup("");
    if (g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY)
        r->size_text = g_strdup("");
    else
        r->size_text = format_size((guint64)g_file_info_get_size(info));
    r->mtime_text = format_mtime(g_file_info_get_attribute_uint64(
        info, G_FILE_ATTRIBUTE_TIME_MODIFIED));
    if (G_IS_THEMED_ICON(icon)) {
        const gchar * const *names = g_themed_icon_get_names(G_THEMED_ICON(icon));
        if (names && names[0])
            r->icon_name = g_strdup(names[0]);
    }
    if (!r->icon_name)
        r->icon_name = g_strdup(g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY
            ? "folder" : "text-x-generic");

    job->result_count++;
    g_main_context_invoke(NULL, result_idle, r);
}

static gboolean mark_directory_visited(SearchJob *job, GFile *directory)
{
    GFileInfo *info;
    GError *error = NULL;
    const gchar *file_id;
    gchar *key;
    GFileQueryInfoFlags flags = job->follow_links
        ? G_FILE_QUERY_INFO_NONE : G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS;

    info = g_file_query_info(directory, G_FILE_ATTRIBUTE_ID_FILE, flags,
                             job->cancel, &error);
    if (info) {
        file_id = g_file_info_get_attribute_string(info, G_FILE_ATTRIBUTE_ID_FILE);
        key = file_id ? g_strdup(file_id) : g_file_get_uri(directory);
        g_object_unref(info);
    } else {
        g_clear_error(&error);
        key = g_file_get_uri(directory);
    }
    if (!key)
        return TRUE;
    if (g_hash_table_contains(job->visited_dirs, key)) {
        g_free(key);
        return FALSE;
    }
    g_hash_table_add(job->visited_dirs, key);
    return TRUE;
}

static void scan_directory(SearchJob *job, GFile *directory, guint32 root_dev)
{
    GFileEnumerator *enumerator;
    GFileInfo *info;
    GError *error = NULL;
    GFileQueryInfoFlags flags = job->follow_links
        ? G_FILE_QUERY_INFO_NONE : G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS;
    const gchar *attrs =
        G_FILE_ATTRIBUTE_STANDARD_NAME ","
        G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
        G_FILE_ATTRIBUTE_STANDARD_TYPE ","
        G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN ","
        G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK ","
        G_FILE_ATTRIBUTE_STANDARD_SIZE ","
        G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
        G_FILE_ATTRIBUTE_STANDARD_ICON ","
        G_FILE_ATTRIBUTE_TIME_MODIFIED ","
        G_FILE_ATTRIBUTE_UNIX_DEVICE;

    if (g_cancellable_is_cancelled(job->cancel) ||
        !mark_directory_visited(job, directory))
        return;

    enumerator = g_file_enumerate_children(directory, attrs, flags,
                                           job->cancel, &error);
    if (!enumerator) {
        g_clear_error(&error);
        return;
    }

    while (!g_cancellable_is_cancelled(job->cancel) &&
           (info = g_file_enumerator_next_file(enumerator, job->cancel, &error))) {
        const gchar *name = g_file_info_get_name(info);
        GFileType ftype = g_file_info_get_file_type(info);
        GFile *child;
        guint64 size;
        gboolean match;
        guint32 child_dev;

        if (!job->hidden && g_file_info_get_is_hidden(info)) {
            g_object_unref(info);
            continue;
        }

        child = g_file_get_child(directory, name);
        size = (guint64)g_file_info_get_size(info);
        match = type_matches(job->type, ftype) &&
                size >= job->min_size &&
                (job->max_size == 0 || size <= job->max_size) &&
                filename_matches(g_file_info_get_display_name(info),
                                 job->query, job->case_sensitive);

        if (match && job->search_content) {
            if (ftype != G_FILE_TYPE_REGULAR)
                match = FALSE;
            else
                match = content_matches_file(child, job->content,
                                             job->case_sensitive,
                                             job->max_content_size,
                                             job->cancel);
        }
        if (match)
            queue_result(job, child, info);

        child_dev = g_file_info_get_attribute_uint32(info,
                                                     G_FILE_ATTRIBUTE_UNIX_DEVICE);
        if (job->recursive && ftype == G_FILE_TYPE_DIRECTORY &&
            !(g_file_info_get_is_symlink(info) && !job->follow_links) &&
            (!job->one_filesystem || root_dev == 0 || child_dev == 0 ||
             child_dev == root_dev))
            scan_directory(job, child, root_dev);

        g_object_unref(child);
        g_object_unref(info);
    }
    g_clear_error(&error);
    g_object_unref(enumerator);
}

static gpointer search_thread(gpointer data)
{
    SearchJob *job = data;
    guint i;

    for (i = 0; job->roots && job->roots[i] &&
                !g_cancellable_is_cancelled(job->cancel); i++) {
        GFile *root = g_file_new_for_commandline_arg(job->roots[i]);
        GFileInfo *root_info;
        GError *error = NULL;
        guint32 root_dev = 0;

        root_info = g_file_query_info(root,
            G_FILE_ATTRIBUTE_STANDARD_TYPE ","
            G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
            G_FILE_ATTRIBUTE_STANDARD_SIZE ","
            G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
            G_FILE_ATTRIBUTE_STANDARD_ICON ","
            G_FILE_ATTRIBUTE_TIME_MODIFIED ","
            G_FILE_ATTRIBUTE_UNIX_DEVICE,
            job->follow_links ? G_FILE_QUERY_INFO_NONE : G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
            job->cancel, &error);
        if (root_info) {
            root_dev = g_file_info_get_attribute_uint32(
                root_info, G_FILE_ATTRIBUTE_UNIX_DEVICE);
            if (g_file_info_get_file_type(root_info) == G_FILE_TYPE_DIRECTORY)
                scan_directory(job, root, root_dev);
            else {
                guint64 root_size = (guint64)g_file_info_get_size(root_info);
                gboolean match =
                    type_matches(job->type, g_file_info_get_file_type(root_info)) &&
                    root_size >= job->min_size &&
                    (job->max_size == 0 || root_size <= job->max_size) &&
                    filename_matches(g_file_info_get_display_name(root_info),
                                     job->query, job->case_sensitive);
                if (match && job->search_content) {
                    if (g_file_info_get_file_type(root_info) != G_FILE_TYPE_REGULAR)
                        match = FALSE;
                    else
                        match = content_matches_file(root, job->content,
                                                     job->case_sensitive,
                                                     job->max_content_size,
                                                     job->cancel);
                }
                if (match)
                    queue_result(job, root, root_info);
            }
            g_object_unref(root_info);
        }
        g_clear_error(&error);
        g_object_unref(root);
    }

    {
        SearchFinished *done = g_new0(SearchFinished, 1);
        done->ui = job->ui;
        done->cancelled = g_cancellable_is_cancelled(job->cancel);
        done->message = done->cancelled
            ? g_strdup_printf(_("Search stopped — %u results"), job->result_count)
            : g_strdup_printf(ngettext("Search complete — %u result",
                                       "Search complete — %u results",
                                       job->result_count),
                              job->result_count);
        g_main_context_invoke(NULL, finished_idle, done);
    }

    g_free(job->query);
    g_free(job->content);
    g_strfreev(job->roots);
    g_object_unref(job->cancel);
    g_hash_table_destroy(job->visited_dirs);
    g_free(job);
    return NULL;
}

static gchar **parse_locations(const gchar *text)
{
    gchar **parts, **p;
    GPtrArray *array = g_ptr_array_new_with_free_func(g_free);

    parts = g_strsplit(text ? text : "", ";", -1);
    for (p = parts; p && *p; p++) {
        gchar *trimmed = g_strstrip(*p);
        if (*trimmed)
            g_ptr_array_add(array, g_canonicalize_filename(trimmed, NULL));
    }
    g_strfreev(parts);
    if (array->len == 0)
        g_ptr_array_add(array, g_get_current_dir());
    g_ptr_array_add(array, NULL);
    return (gchar **)g_ptr_array_free(array, FALSE);
}

static void start_search(SearchWindow *ui)
{
    SearchJob *job;
    const gchar *location;

    if (ui->thread)
        return;

    gtk_list_store_clear(ui->store);
    ui->result_count = 0;
    location = gtk_entry_get_text(GTK_ENTRY(ui->location_entry));
    job = g_new0(SearchJob, 1);
    job->ui = ui;
    job->query = g_strdup(gtk_entry_get_text(GTK_ENTRY(ui->query_entry)));
    job->content = g_strdup(gtk_entry_get_text(GTK_ENTRY(ui->content_entry)));
    job->roots = parse_locations(location);
    job->recursive = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui->recursive_check));
    job->hidden = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui->hidden_check));
    job->case_sensitive = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui->case_check));
    job->search_content = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui->content_check));
    job->follow_links = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui->follow_check));
    job->one_filesystem = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui->filesystem_check));
    job->type = (SearchType)gtk_combo_box_get_active(GTK_COMBO_BOX(ui->type_combo));
    job->min_size = (guint64)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ui->min_size_spin)) * 1024;
    job->max_size = (guint64)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ui->max_size_spin)) * 1024;
    job->max_content_size =
        (guint64)gtk_spin_button_get_value_as_int(
            GTK_SPIN_BUTTON(ui->content_limit_spin)) * 1024 * 1024;
    if (job->max_content_size == 0)
        job->max_content_size = MAX_CONTENT_BYTES_DEFAULT;
    ui->cancel = g_cancellable_new();
    job->cancel = g_object_ref(ui->cancel);
    job->visited_dirs = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    gtk_widget_set_sensitive(ui->search_button, FALSE);
    gtk_widget_set_sensitive(ui->stop_button, TRUE);
    gtk_label_set_text(GTK_LABEL(ui->status), _("Searching…"));
    gtk_spinner_start(GTK_SPINNER(ui->spinner));
    ui->thread = g_thread_new("rox-find-search", search_thread, job);
    /* The worker owns itself; ui->thread is used only as a busy marker. */
    g_thread_unref(ui->thread);
}

static void stop_search(SearchWindow *ui)
{
    if (ui->cancel)
        g_cancellable_cancel(ui->cancel);
}

static gchar *selected_path(SearchWindow *ui)
{
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(ui->tree));
    GtkTreeModel *model;
    GtkTreeIter iter;
    gchar *path = NULL;
    if (gtk_tree_selection_get_selected(selection, &model, &iter))
        gtk_tree_model_get(model, &iter, COL_PATH, &path, -1);
    return path;
}

static void open_path(const gchar *path)
{
    GFile *file;
    gchar *uri;
    GError *error = NULL;
    if (!path)
        return;
    file = g_file_new_for_commandline_arg(path);
    uri = g_file_get_uri(file);
    if (!g_app_info_launch_default_for_uri(uri, NULL, &error)) {
        GtkWidget *d = gtk_message_dialog_new(NULL, GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", _("Unable to open the selected item"));
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s",
            error ? error->message : _("Unknown error"));
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
    }
    g_clear_error(&error);
    g_free(uri);
    g_object_unref(file);
}

static void show_in_rox(const gchar *path)
{
    gchar *argv[] = { (gchar *)"ROX-Filer", (gchar *)"-s", (gchar *)path, NULL };
    GError *error = NULL;
    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
        g_warning("Unable to run ROX-Filer: %s", error->message);
        g_clear_error(&error);
    }
}

static void open_parent(const gchar *path)
{
    gchar *parent;
    gchar *argv[] = { (gchar *)"ROX-Filer", NULL, NULL };
    GError *error = NULL;
    if (!path)
        return;
    parent = g_path_get_dirname(path);
    argv[1] = parent;
    if (!g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
        g_warning("Unable to run ROX-Filer: %s", error->message);
        g_clear_error(&error);
    }
    g_free(parent);
}

static void trash_path(SearchWindow *ui, const gchar *path)
{
    GFile *file;
    GError *error = NULL;
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter iter;

    if (!path)
        return;
    {
        GtkWidget *confirm = gtk_message_dialog_new(GTK_WINDOW(ui->window),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
            "%s", _("Move the selected item to Trash?"));
        gtk_dialog_add_buttons(GTK_DIALOG(confirm),
            _("Cancel"), GTK_RESPONSE_CANCEL,
            _("Move to Trash"), GTK_RESPONSE_ACCEPT, NULL);
        gtk_dialog_set_default_response(GTK_DIALOG(confirm), GTK_RESPONSE_CANCEL);
        gtk_window_set_position(GTK_WINDOW(confirm), GTK_WIN_POS_CENTER_ON_PARENT);
        if (gtk_dialog_run(GTK_DIALOG(confirm)) != GTK_RESPONSE_ACCEPT) {
            gtk_widget_destroy(confirm);
            return;
        }
        gtk_widget_destroy(confirm);
    }
    file = g_file_new_for_commandline_arg(path);
    if (!g_file_trash(file, NULL, &error)) {
        GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(ui->window), GTK_DIALOG_MODAL,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", _("Unable to move the item to Trash"));
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(d), "%s",
            error ? error->message : _("Unknown error"));
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
        g_clear_error(&error);
    } else {
        selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(ui->tree));
        if (gtk_tree_selection_get_selected(selection, &model, &iter))
            gtk_list_store_remove(ui->store, &iter);
    }
    g_object_unref(file);
}

static void copy_path_to_clipboard(const gchar *path)
{
    GtkClipboard *clipboard;
    if (!path)
        return;
    clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard, path, -1);
}

static void result_activated(GtkTreeView *tree, GtkTreePath *tree_path,
                             GtkTreeViewColumn *column, gpointer data)
{
    SearchWindow *ui = data;
    gchar *path = selected_path(ui);
    (void)tree; (void)tree_path; (void)column;
    open_path(path);
    g_free(path);
}

static void menu_open(GtkMenuItem *item, gpointer data)
{
    SearchWindow *ui = data; gchar *path = selected_path(ui);
    (void)item; open_path(path); g_free(path);
}
static void menu_show(GtkMenuItem *item, gpointer data)
{
    SearchWindow *ui = data; gchar *path = selected_path(ui);
    (void)item; show_in_rox(path); g_free(path);
}
static void menu_parent(GtkMenuItem *item, gpointer data)
{
    SearchWindow *ui = data; gchar *path = selected_path(ui);
    (void)item; open_parent(path); g_free(path);
}
static void menu_copy(GtkMenuItem *item, gpointer data)
{
    SearchWindow *ui = data; gchar *path = selected_path(ui);
    (void)item; copy_path_to_clipboard(path); g_free(path);
}
static void menu_trash(GtkMenuItem *item, gpointer data)
{
    SearchWindow *ui = data; gchar *path = selected_path(ui);
    (void)item; trash_path(ui, path); g_free(path);
}

static GtkWidget *menu_item_with_icon(const gchar *label, const gchar *icon_name)
{
    /* 2.12.2-82: GtkImageMenuItem esta obsoleto desde GTK 3.10. */
    GtkWidget *item = gtk_menu_item_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *image = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_MENU);
    GtkWidget *text = gtk_label_new(label);

    gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
    gtk_label_set_xalign(GTK_LABEL(text), 0.0);
    gtk_box_pack_start(GTK_BOX(box), text, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(item), box);
    gtk_widget_show_all(box);
    return item;
}

static gboolean result_button(GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    SearchWindow *ui = data;
    GtkWidget *menu, *item;
    GtkTreePath *path = NULL;
    (void)widget;
    if (event->type != GDK_BUTTON_PRESS || event->button != 3)
        return FALSE;
    if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(ui->tree),
                                     (gint)event->x, (gint)event->y,
                                     &path, NULL, NULL, NULL)) {
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(ui->tree), path, NULL, FALSE);
        gtk_tree_path_free(path);
    }
    {
        gchar *selected = selected_path(ui);
        if (!selected)
            return FALSE;
        g_free(selected);
    }

    menu = gtk_menu_new();
    item = menu_item_with_icon(_("Open"), "document-open");
    g_signal_connect(item, "activate", G_CALLBACK(menu_open), ui);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    item = menu_item_with_icon(_("Show in ROX-Filer"), "system-file-manager");
    g_signal_connect(item, "activate", G_CALLBACK(menu_show), ui);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    item = menu_item_with_icon(_("Open Parent Folder"), "folder-open");
    g_signal_connect(item, "activate", G_CALLBACK(menu_parent), ui);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    item = menu_item_with_icon(_("Copy Path"), "edit-copy");
    g_signal_connect(item, "activate", G_CALLBACK(menu_copy), ui);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    item = menu_item_with_icon(_("Move to Trash"), "user-trash");
    g_signal_connect(item, "activate", G_CALLBACK(menu_trash), ui);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
    return TRUE;
}

static void choose_location(GtkButton *button, gpointer data)
{
    SearchWindow *ui = data;
    GtkWidget *dialog = gtk_file_chooser_dialog_new(_("Choose Search Folder"),
        GTK_WINDOW(ui->window), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        _("Cancel"), GTK_RESPONSE_CANCEL, _("Choose"), GTK_RESPONSE_ACCEPT, NULL);
    (void)button;
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER_ON_PARENT);
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar *folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_entry_set_text(GTK_ENTRY(ui->location_entry), folder);
        g_free(folder);
    }
    gtk_widget_destroy(dialog);
}

static gboolean request_window_close(GtkWidget *widget, GdkEvent *event,
                                     gpointer data)
{
    SearchWindow *ui = data;
    (void)widget;
    (void)event;
    if (ui->thread) {
        ui->close_requested = TRUE;
        stop_search(ui);
        gtk_label_set_text(GTK_LABEL(ui->status),
                           _("Stopping search…"));
        return TRUE;
    }
    return FALSE;
}

static void close_button_clicked(GtkButton *button, gpointer data)
{
    SearchWindow *ui = data;
    (void)button;
    if (ui->thread) {
        ui->close_requested = TRUE;
        stop_search(ui);
        gtk_label_set_text(GTK_LABEL(ui->status),
                           _("Stopping search…"));
        return;
    }
    gtk_widget_destroy(ui->window);
}

static void content_toggled(GtkToggleButton *toggle, gpointer data)
{
    SearchWindow *ui = data;
    gtk_widget_set_sensitive(ui->content_entry,
        gtk_toggle_button_get_active(toggle));
}

static void about_clicked(GtkButton *button, gpointer data)
{
    SearchWindow *ui = data;
    const gchar *authors[] = { "josejp2424", NULL };
    (void)button;
    gtk_show_about_dialog(GTK_WINDOW(ui->window),
        "program-name", _("ROX File Search"),
        "version", "1.0.0",
        "comments", _("Search for files and folders with ROX-Filer integration."),
        "authors", authors,
        "license-type", GTK_LICENSE_GPL_3_0,
        "logo-icon-name", "rox-find",
        NULL);
}

static GtkWidget *labelled_row(const gchar *label_text, GtkWidget *child)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *label = gtk_label_new(label_text);
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_size_request(label, 90, -1);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), child, TRUE, TRUE, 0);
    return box;
}

static void add_text_column(GtkTreeView *tree, const gchar *title, gint column,
                            gint min_width, gboolean expand)
{
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *view_col = gtk_tree_view_column_new_with_attributes(
        title, renderer, "text", column, NULL);
    gtk_tree_view_column_set_resizable(view_col, TRUE);
    gtk_tree_view_column_set_min_width(view_col, min_width);
    gtk_tree_view_column_set_expand(view_col, expand);
    gtk_tree_view_append_column(tree, view_col);
}

static void build_window(GtkApplication *app, SearchWindow *ui)
{
    GtkWidget *main_box, *search_frame, *grid, *row, *button, *advanced;
    GtkWidget *scrolled, *bottom, *header;
    GtkCellRenderer *pix;
    GtkTreeViewColumn *icon_col;
    guint i;

    ui->app = app;
    ui->window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(ui->window), _("ROX File Search"));
    g_signal_connect(ui->window, "delete-event",
                     G_CALLBACK(request_window_close), ui);
    gtk_window_set_default_size(GTK_WINDOW(ui->window), 820, 560);
    gtk_window_set_position(GTK_WINDOW(ui->window), GTK_WIN_POS_CENTER);
    gtk_window_set_icon_name(GTK_WINDOW(ui->window), "rox-find");

    main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(main_box), 10);
    gtk_container_add(GTK_CONTAINER(ui->window), main_box);

    header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    {
        GtkWidget *logo = gtk_image_new_from_icon_name("rox-find", GTK_ICON_SIZE_DIALOG);
        GtkWidget *title = gtk_label_new(NULL);
        gtk_label_set_markup(GTK_LABEL(title), _("<b>Search for files and folders</b>"));
        gtk_widget_set_halign(title, GTK_ALIGN_START);
        gtk_box_pack_start(GTK_BOX(header), logo, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(header), title, TRUE, TRUE, 0);
        button = gtk_button_new_from_icon_name("help-about", GTK_ICON_SIZE_BUTTON);
        gtk_widget_set_tooltip_text(button, _("About ROX File Search"));
        g_signal_connect(button, "clicked", G_CALLBACK(about_clicked), ui);
        gtk_box_pack_end(GTK_BOX(header), button, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(main_box), header, FALSE, FALSE, 0);

    search_frame = gtk_frame_new(NULL);
    grid = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 8);
    gtk_container_add(GTK_CONTAINER(search_frame), grid);

    ui->query_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ui->query_entry), _("Name or pattern, for example *.svg"));
    row = labelled_row(_("Name:"), ui->query_entry);
    gtk_box_pack_start(GTK_BOX(grid), row, FALSE, FALSE, 0);

    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    ui->location_entry = gtk_entry_new();
    gtk_widget_set_hexpand(ui->location_entry, TRUE);
    button = gtk_button_new_with_label(_("Choose…"));
    g_signal_connect(button, "clicked", G_CALLBACK(choose_location), ui);
    gtk_box_pack_start(GTK_BOX(row), ui->location_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row), button, FALSE, FALSE, 0);
    row = labelled_row(_("Location:"), row);
    gtk_box_pack_start(GTK_BOX(grid), row, FALSE, FALSE, 0);

    ui->content_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(ui->content_entry), _("Text contained in files"));
    gtk_widget_set_sensitive(ui->content_entry, FALSE);
    row = labelled_row(_("Content:"), ui->content_entry);
    gtk_box_pack_start(GTK_BOX(grid), row, FALSE, FALSE, 0);

    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    ui->recursive_check = gtk_check_button_new_with_label(_("Include subdirectories"));
    ui->hidden_check = gtk_check_button_new_with_label(_("Include hidden items"));
    ui->case_check = gtk_check_button_new_with_label(_("Case sensitive"));
    ui->content_check = gtk_check_button_new_with_label(_("Search file contents"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->recursive_check), TRUE);
    g_signal_connect(ui->content_check, "toggled", G_CALLBACK(content_toggled), ui);
    gtk_box_pack_start(GTK_BOX(row), ui->recursive_check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), ui->hidden_check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), ui->case_check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), ui->content_check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(grid), row, FALSE, FALSE, 0);

    advanced = gtk_expander_new(_("Advanced options"));
    {
        GtkWidget *abox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        GtkWidget *line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        ui->type_combo = gtk_combo_box_text_new();
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui->type_combo), _("All items"));
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui->type_combo), _("Files only"));
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui->type_combo), _("Directories only"));
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(ui->type_combo), _("Symbolic links only"));
        gtk_combo_box_set_active(GTK_COMBO_BOX(ui->type_combo), 0);
        gtk_box_pack_start(GTK_BOX(line), gtk_label_new(_("Type:")), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(line), ui->type_combo, FALSE, FALSE, 0);
        ui->min_size_spin = gtk_spin_button_new_with_range(0, 10485760, 1);
        ui->max_size_spin = gtk_spin_button_new_with_range(0, 10485760, 1);
        gtk_box_pack_start(GTK_BOX(line), gtk_label_new(_("Minimum KB:")), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(line), ui->min_size_spin, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(line), gtk_label_new(_("Maximum KB (0 = unlimited):")), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(line), ui->max_size_spin, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(abox), line, FALSE, FALSE, 0);
        line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        ui->content_limit_spin = gtk_spin_button_new_with_range(1, 1024, 1);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->content_limit_spin),
                                  ui->initial_content_limit_mb > 0
                                  ? ui->initial_content_limit_mb : 20);
        gtk_box_pack_start(GTK_BOX(line),
                           gtk_label_new(_("Maximum file size for content search:")),
                           FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(line), ui->content_limit_spin,
                           FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(line), gtk_label_new(_("MB")),
                           FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(abox), line, FALSE, FALSE, 0);
        line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        ui->follow_check = gtk_check_button_new_with_label(_("Follow symbolic links"));
        ui->filesystem_check = gtk_check_button_new_with_label(_("Stay on the selected filesystem"));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->filesystem_check), TRUE);
        gtk_box_pack_start(GTK_BOX(line), ui->follow_check, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(line), ui->filesystem_check, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(abox), line, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(advanced), abox);
    }
    gtk_box_pack_start(GTK_BOX(grid), advanced, FALSE, FALSE, 0);

    row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    ui->search_button = gtk_button_new_with_label(_("Search"));
    gtk_button_set_image(GTK_BUTTON(ui->search_button),
                         gtk_image_new_from_icon_name("system-search", GTK_ICON_SIZE_BUTTON));
    ui->stop_button = gtk_button_new_with_label(_("Stop"));
    gtk_button_set_image(GTK_BUTTON(ui->stop_button),
                         gtk_image_new_from_icon_name("process-stop", GTK_ICON_SIZE_BUTTON));
    gtk_widget_set_sensitive(ui->stop_button, FALSE);
    g_signal_connect_swapped(ui->search_button, "clicked", G_CALLBACK(start_search), ui);
    g_signal_connect_swapped(ui->stop_button, "clicked", G_CALLBACK(stop_search), ui);
    g_signal_connect_swapped(ui->query_entry, "activate", G_CALLBACK(start_search), ui);
    gtk_box_pack_end(GTK_BOX(row), ui->search_button, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(row), ui->stop_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(grid), row, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(main_box), search_frame, FALSE, FALSE, 0);

    ui->store = gtk_list_store_new(N_COLS, G_TYPE_ICON, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    ui->tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(ui->store));
    gtk_tree_view_set_headers_clickable(GTK_TREE_VIEW(ui->tree), TRUE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(ui->tree), TRUE);
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(ui->tree), COL_NAME);
    pix = gtk_cell_renderer_pixbuf_new();
    icon_col = gtk_tree_view_column_new_with_attributes("", pix, "gicon", COL_ICON, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(ui->tree), icon_col);
    add_text_column(GTK_TREE_VIEW(ui->tree), _("Name"), COL_NAME, 150, TRUE);
    add_text_column(GTK_TREE_VIEW(ui->tree), _("Location"), COL_PARENT, 220, TRUE);
    add_text_column(GTK_TREE_VIEW(ui->tree), _("Type"), COL_TYPE, 120, FALSE);
    add_text_column(GTK_TREE_VIEW(ui->tree), _("Size"), COL_SIZE, 80, FALSE);
    add_text_column(GTK_TREE_VIEW(ui->tree), _("Modified"), COL_MTIME, 120, FALSE);
    for (i = 0; i < N_COLS - 1; i++) {
        GtkTreeViewColumn *col = gtk_tree_view_get_column(GTK_TREE_VIEW(ui->tree), i);
        if (col)
            gtk_tree_view_column_set_sort_column_id(col, i == 0 ? COL_NAME : (gint)i);
    }
    g_signal_connect(ui->tree, "row-activated", G_CALLBACK(result_activated), ui);
    g_signal_connect(ui->tree, "button-press-event", G_CALLBACK(result_button), ui);

    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scrolled), ui->tree);
    gtk_box_pack_start(GTK_BOX(main_box), scrolled, TRUE, TRUE, 0);

    bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    ui->spinner = gtk_spinner_new();
    ui->status = gtk_label_new(_("Ready"));
    gtk_widget_set_halign(ui->status, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(bottom), ui->spinner, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bottom), ui->status, TRUE, TRUE, 0);
    button = gtk_button_new_with_label(_("Close"));
    g_signal_connect(button, "clicked", G_CALLBACK(close_button_clicked), ui);
    gtk_box_pack_end(GTK_BOX(bottom), button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_box), bottom, FALSE, FALSE, 0);

    if (ui->initial_roots && ui->initial_roots[0]) {
        GString *locations = g_string_new(NULL);
        for (i = 0; ui->initial_roots[i]; i++) {
            if (i)
                g_string_append(locations, "; ");
            g_string_append(locations, ui->initial_roots[i]);
        }
        gtk_entry_set_text(GTK_ENTRY(ui->location_entry), locations->str);
        g_string_free(locations, TRUE);
    } else {
        gchar *cwd = g_get_current_dir();
        gtk_entry_set_text(GTK_ENTRY(ui->location_entry), cwd);
        g_free(cwd);
    }

    if (ui->initial_name)
        gtk_entry_set_text(GTK_ENTRY(ui->query_entry), ui->initial_name);
    if (ui->initial_content) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->content_check), TRUE);
        gtk_entry_set_text(GTK_ENTRY(ui->content_entry), ui->initial_content);
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->hidden_check), ui->initial_hidden);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->recursive_check), ui->initial_recursive);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->follow_check), ui->initial_follow_links);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->filesystem_check), ui->initial_one_filesystem);

    gtk_widget_show_all(ui->window);
    gtk_widget_grab_focus(ui->query_entry);
}

static void window_destroyed(GtkWidget *widget, gpointer data)
{
    SearchWindow *ui = data;
    (void)widget;
    if (ui->cancel)
        g_cancellable_cancel(ui->cancel);
    g_strfreev(ui->initial_roots);
    g_free(ui->initial_name);
    g_free(ui->initial_content);
    g_free(ui);
}

static void app_activate(GtkApplication *app, gpointer user_data)
{
    SearchWindow *ui = user_data;
    build_window(app, ui);
    g_signal_connect(ui->window, "destroy", G_CALLBACK(window_destroyed), ui);
}

int main(int argc, char **argv)
{
    GtkApplication *app;
    SearchWindow *ui;
    GOptionContext *context;
    GError *error = NULL;
    gchar *name_prefill = NULL;
    gchar *content_prefill = NULL;
    gboolean include_hidden = FALSE;
    gboolean recursive = TRUE;
    gboolean follow_links = FALSE;
    gboolean one_filesystem = TRUE;
    gint content_limit_mb = 20;
    GOptionEntry entries[] = {
        { "name", 'n', 0, G_OPTION_ARG_STRING, &name_prefill,
          "Prefill the file-name query", "PATTERN" },
        { "content", 'c', 0, G_OPTION_ARG_STRING, &content_prefill,
          "Prefill the content query", "TEXT" },
        { "hidden", 0, 0, G_OPTION_ARG_NONE, &include_hidden,
          "Include hidden files", NULL },
        { "no-recursive", 0, G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &recursive,
          "Do not include subdirectories", NULL },
        { "follow-links", 0, 0, G_OPTION_ARG_NONE, &follow_links,
          "Follow symbolic links", NULL },
        { "cross-filesystems", 0, G_OPTION_FLAG_REVERSE, G_OPTION_ARG_NONE, &one_filesystem,
          "Allow recursion across filesystem boundaries", NULL },
        { "max-content-mb", 0, 0, G_OPTION_ARG_INT, &content_limit_mb,
          "Maximum file size for content search in MB", "MB" },
        { NULL }
    };
    int status;

    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);

    context = g_option_context_new(_("[FOLDER…]"));
    g_option_context_add_main_entries(context, entries, GETTEXT_PACKAGE);
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        g_printerr("%s\n", error->message);
        g_clear_error(&error);
        g_option_context_free(context);
        return EXIT_FAILURE;
    }
    g_option_context_free(context);

    ui = g_new0(SearchWindow, 1);
    if (argc > 1)
        ui->initial_roots = g_strdupv(&argv[1]);
    ui->initial_name = g_strdup(name_prefill);
    ui->initial_content = g_strdup(content_prefill);
    ui->initial_hidden = include_hidden;
    ui->initial_recursive = recursive;
    ui->initial_follow_links = follow_links;
    ui->initial_one_filesystem = one_filesystem;
    ui->initial_content_limit_mb = (guint)CLAMP(content_limit_mb, 1, 1024);

    app = gtk_application_new(APP_ID, G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(app_activate), ui);
    /* Folder arguments were parsed above and belong to the search engine,
     * not to GtkApplication's file-opening command line. */
    status = g_application_run(G_APPLICATION(app), 1, argv);

    g_object_unref(app);
    g_free(name_prefill);
    g_free(content_prefill);
    return status;
}
