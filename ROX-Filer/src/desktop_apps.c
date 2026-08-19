/*
 * Agregado por josejp2424 (2026): administrador GTK3 de aplicaciones para
 * ROX Desktop, inspirado en el gestor de escritorio de EssoraWM.
 *
 * Escanea los directorios XDG de aplicaciones, respeta los lanzadores que
 * deben mostrarse y copia el archivo .desktop seleccionado al verdadero
 * XDG_DESKTOP_DIR. No usa PuppyPin ni scripts externos.
 */
#include "config.h"

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gstdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>

/* Modificado por josejp2424 (2026): global.h define los tipos de
 * compatibilidad GTK3 usados por i18n.h, incluido RoxItemFactoryEntry. */
#include "global.h"
#include "desktop_apps.h"
#include "desktop_dropdown.h"
#include "i18n.h"

#define APP_ICON_SIZE 36

enum {
    APP_COL_ICON,
    APP_COL_NAME,
    APP_COL_DESCRIPTION,
    APP_COL_SOURCE,
    APP_COL_BASENAME,
    APP_COL_ON_DESKTOP,
    APP_N_COLS
};

typedef struct {
    GtkWidget *dialog;
    GtkWidget *tree;
    GtkWidget *search;
    GtkWidget *status;
    GtkWidget *add_button;
    GtkWidget *remove_button;
    GtkWidget *icon_size_combo;
    GtkWidget *single_click_radio;
    GtkWidget *double_click_radio;
    GtkListStore *store;
    GtkTreeModelFilter *filter;
    gchar *desktop_dir;
} DesktopAppsDialog;

static GdkPixbuf *desktop_apps_icon_pixbuf(GIcon *icon)
{
    GtkIconInfo *info;
    GdkPixbuf *pixbuf = NULL;

    if (icon) {
        info = gtk_icon_theme_lookup_by_gicon(gtk_icon_theme_get_default(),
                                              icon, APP_ICON_SIZE,
                                              GTK_ICON_LOOKUP_FORCE_SIZE);
        if (info) {
            pixbuf = gtk_icon_info_load_icon(info, NULL);
            g_object_unref(info);
        }
    }

    if (!pixbuf)
        pixbuf = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(),
                                          "application-x-executable",
                                          APP_ICON_SIZE,
                                          GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
    return pixbuf;
}

static gboolean desktop_apps_is_desktop_file(const gchar *name)
{
    return name && g_str_has_suffix(name, ".desktop");
}

static gchar *desktop_apps_destination(const DesktopAppsDialog *state,
                                       const gchar *basename)
{
    return g_build_filename(state->desktop_dir, basename, NULL);
}

static void desktop_apps_set_status(DesktopAppsDialog *state,
                                    const gchar *message)
{
    gtk_label_set_text(GTK_LABEL(state->status), message ? message : "");
}

static gboolean desktop_apps_get_selected(DesktopAppsDialog *state,
                                          GtkTreeIter *child_iter,
                                          gchar **source,
                                          gchar **basename,
                                          gboolean *on_desktop)
{
    GtkTreeSelection *selection;
    GtkTreeModel *model;
    GtkTreeIter filter_iter;

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(state->tree));
    if (!gtk_tree_selection_get_selected(selection, &model, &filter_iter))
        return FALSE;

    gtk_tree_model_filter_convert_iter_to_child_iter(state->filter,
                                                      child_iter,
                                                      &filter_iter);
    gtk_tree_model_get(GTK_TREE_MODEL(state->store), child_iter,
                       APP_COL_SOURCE, source,
                       APP_COL_BASENAME, basename,
                       APP_COL_ON_DESKTOP, on_desktop,
                       -1);
    return TRUE;
}

static void desktop_apps_update_buttons(DesktopAppsDialog *state)
{
    GtkTreeIter iter;
    gchar *source = NULL;
    gchar *basename = NULL;
    gboolean on_desktop = FALSE;
    gboolean selected;

    selected = desktop_apps_get_selected(state, &iter, &source, &basename,
                                         &on_desktop);
    gtk_widget_set_sensitive(state->add_button, selected && !on_desktop);
    gtk_widget_set_sensitive(state->remove_button, selected && on_desktop);
    g_free(source);
    g_free(basename);
}

static void desktop_apps_selection_changed(GtkTreeSelection *selection,
                                           gpointer data)
{
    (void)selection;
    desktop_apps_update_buttons(data);
}

static gboolean desktop_apps_filter_visible(GtkTreeModel *model,
                                            GtkTreeIter *iter,
                                            gpointer data)
{
    DesktopAppsDialog *state = data;
    const gchar *needle;
    gchar *name = NULL;
    gchar *description = NULL;
    gchar *name_folded;
    gchar *description_folded;
    gchar *needle_folded;
    gboolean visible;

    needle = gtk_entry_get_text(GTK_ENTRY(state->search));
    if (!needle || !*needle)
        return TRUE;

    gtk_tree_model_get(model, iter,
                       APP_COL_NAME, &name,
                       APP_COL_DESCRIPTION, &description,
                       -1);
    name_folded = g_utf8_casefold(name ? name : "", -1);
    description_folded = g_utf8_casefold(description ? description : "", -1);
    needle_folded = g_utf8_casefold(needle, -1);
    visible = strstr(name_folded, needle_folded) != NULL ||
              strstr(description_folded, needle_folded) != NULL;
    g_free(name_folded);
    g_free(description_folded);
    g_free(needle_folded);
    g_free(name);
    g_free(description);
    return visible;
}

static void desktop_apps_search_changed(GtkEditable *editable, gpointer data)
{
    DesktopAppsDialog *state = data;
    (void)editable;
    gtk_tree_model_filter_refilter(state->filter);
    desktop_apps_update_buttons(state);
}

static void desktop_apps_add_directory(DesktopAppsDialog *state,
                                       const gchar *applications_dir,
                                       GHashTable *seen)
{
    GDir *dir;
    const gchar *name;

    if (!applications_dir || !g_file_test(applications_dir, G_FILE_TEST_IS_DIR))
        return;

    dir = g_dir_open(applications_dir, 0, NULL);
    if (!dir)
        return;

    while ((name = g_dir_read_name(dir))) {
        gchar *source;
        GDesktopAppInfo *app;
        const gchar *display_name;
        const gchar *description;
        GIcon *icon;
        GdkPixbuf *pixbuf;
        gchar *destination;
        gboolean on_desktop;
        GtkTreeIter iter;

        if (!desktop_apps_is_desktop_file(name) ||
            g_hash_table_contains(seen, name))
            continue;

        source = g_build_filename(applications_dir, name, NULL);
        app = g_desktop_app_info_new_from_filename(source);
        if (!app) {
            g_free(source);
            continue;
        }

        if (!g_app_info_should_show(G_APP_INFO(app))) {
            g_object_unref(app);
            g_free(source);
            continue;
        }

        display_name = g_app_info_get_display_name(G_APP_INFO(app));
        if (!display_name || !*display_name) {
            g_object_unref(app);
            g_free(source);
            continue;
        }

        description = g_app_info_get_description(G_APP_INFO(app));
        icon = g_app_info_get_icon(G_APP_INFO(app));
        pixbuf = desktop_apps_icon_pixbuf(icon);
        destination = desktop_apps_destination(state, name);
        on_desktop = g_file_test(destination, G_FILE_TEST_EXISTS);

        gtk_list_store_append(state->store, &iter);
        gtk_list_store_set(state->store, &iter,
                           APP_COL_ICON, pixbuf,
                           APP_COL_NAME, display_name,
                           APP_COL_DESCRIPTION, description ? description : "",
                           APP_COL_SOURCE, source,
                           APP_COL_BASENAME, name,
                           APP_COL_ON_DESKTOP, on_desktop,
                           -1);

        g_hash_table_add(seen, g_strdup(name));
        g_clear_object(&pixbuf);
        g_free(destination);
        g_object_unref(app);
        g_free(source);
    }

    g_dir_close(dir);
}

static void desktop_apps_scan(DesktopAppsDialog *state)
{
    GHashTable *seen;
    gchar *directory;
    const gchar * const *system_dirs;
    guint i;

    seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    /* El directorio del usuario tiene prioridad sobre los lanzadores del
     * sistema con el mismo desktop-id. */
    directory = g_build_filename(g_get_user_data_dir(), "applications", NULL);
    desktop_apps_add_directory(state, directory, seen);
    g_free(directory);

    system_dirs = g_get_system_data_dirs();
    for (i = 0; system_dirs && system_dirs[i]; i++) {
        directory = g_build_filename(system_dirs[i], "applications", NULL);
        desktop_apps_add_directory(state, directory, seen);
        g_free(directory);
    }

    g_hash_table_unref(seen);
}

static void desktop_apps_mark_state(DesktopAppsDialog *state,
                                    GtkTreeIter *iter,
                                    gboolean on_desktop)
{
    gtk_list_store_set(state->store, iter,
                       APP_COL_ON_DESKTOP, on_desktop, -1);
    desktop_apps_update_buttons(state);
}

static void desktop_apps_add_selected(DesktopAppsDialog *state)
{
    GtkTreeIter iter;
    gchar *source = NULL;
    gchar *basename = NULL;
    gchar *destination = NULL;
    gboolean on_desktop = FALSE;
    GFile *source_file = NULL;
    GFile *destination_file = NULL;
    GError *error = NULL;
    GStatBuf st;

    if (!desktop_apps_get_selected(state, &iter, &source, &basename,
                                   &on_desktop))
        return;
    destination = desktop_apps_destination(state, basename);

    if (on_desktop || g_file_test(destination, G_FILE_TEST_EXISTS)) {
        desktop_apps_set_status(state,
            _("This application is already on the desktop"));
        desktop_apps_mark_state(state, &iter, TRUE);
        goto out;
    }

    if (g_mkdir_with_parents(state->desktop_dir, 0700) != 0) {
        desktop_apps_set_status(state,
            _("Could not create the Desktop directory"));
        goto out;
    }

    source_file = g_file_new_for_path(source);
    destination_file = g_file_new_for_path(destination);
    if (!g_file_copy(source_file, destination_file, G_FILE_COPY_NONE,
                     NULL, NULL, NULL, &error)) {
        gchar *message = g_strdup_printf(_("Unable to add the application: %s"),
                                         error ? error->message : "");
        desktop_apps_set_status(state, message);
        g_free(message);
        g_clear_error(&error);
        goto out;
    }

    /* Algunos escritorios exigen que los lanzadores copiados sean marcados
     * como ejecutables para considerarlos confiables. */
    if (g_stat(source, &st) == 0)
        g_chmod(destination, st.st_mode | S_IXUSR);
    else
        g_chmod(destination, 0755);

    desktop_apps_mark_state(state, &iter, TRUE);
    desktop_apps_set_status(state, _("Added to the desktop"));

out:
    g_clear_object(&source_file);
    g_clear_object(&destination_file);
    g_free(destination);
    g_free(source);
    g_free(basename);
}

static void desktop_apps_remove_selected(DesktopAppsDialog *state)
{
    GtkTreeIter iter;
    gchar *source = NULL;
    gchar *basename = NULL;
    gchar *destination = NULL;
    gchar *name = NULL;
    gboolean on_desktop = FALSE;
    GtkWidget *confirm;
    gint response;

    if (!desktop_apps_get_selected(state, &iter, &source, &basename,
                                   &on_desktop))
        return;

    if (!on_desktop) {
        desktop_apps_set_status(state,
            _("This application is not on the desktop"));
        goto out;
    }

    gtk_tree_model_get(GTK_TREE_MODEL(state->store), &iter,
                       APP_COL_NAME, &name, -1);
    confirm = gtk_message_dialog_new(GTK_WINDOW(state->dialog),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
        _("Remove \"%s\" from the desktop?"), name ? name : basename);
    gtk_window_set_position(GTK_WINDOW(confirm), GTK_WIN_POS_CENTER_ON_PARENT);
    gtk_dialog_add_buttons(GTK_DIALOG(confirm),
                           _("_Cancel"), GTK_RESPONSE_CANCEL,
                           _("_Remove"), GTK_RESPONSE_ACCEPT,
                           NULL);
    response = gtk_dialog_run(GTK_DIALOG(confirm));
    gtk_widget_destroy(confirm);
    if (response != GTK_RESPONSE_ACCEPT)
        goto out;

    destination = desktop_apps_destination(state, basename);
    if (g_remove(destination) != 0) {
        desktop_apps_set_status(state,
            _("Unable to remove the application from the desktop"));
        goto out;
    }

    desktop_apps_mark_state(state, &iter, FALSE);
    desktop_apps_set_status(state, _("Removed from the desktop"));

out:
    g_free(name);
    g_free(destination);
    g_free(source);
    g_free(basename);
}

static void desktop_apps_row_activated(GtkTreeView *tree,
                                       GtkTreePath *path,
                                       GtkTreeViewColumn *column,
                                       gpointer data)
{
    DesktopAppsDialog *state = data;
    GtkTreeSelection *selection;
    (void)column;

    selection = gtk_tree_view_get_selection(tree);
    gtk_tree_selection_select_path(selection, path);
    desktop_apps_add_selected(state);
}

static void desktop_apps_on_desktop_cell(GtkTreeViewColumn *column,
                                         GtkCellRenderer *renderer,
                                         GtkTreeModel *model,
                                         GtkTreeIter *iter,
                                         gpointer data)
{
    gboolean on_desktop = FALSE;
    (void)column;
    (void)data;

    gtk_tree_model_get(model, iter, APP_COL_ON_DESKTOP, &on_desktop, -1);
    g_object_set(renderer, "text", on_desktop ? _("Yes") : "", NULL);
}

gboolean desktop_apps_show_manager(GtkWindow *parent,
                                    const gchar *desktop_dir,
                                    gint *desktop_icon_size,
                                    gboolean *single_click)
{
    DesktopAppsDialog state = {0};
    GtkWidget *content;
    GtkWidget *box;
    GtkWidget *scrolled;
    GtkWidget *search_label;
    GtkWidget *settings_frame;
    GtkWidget *settings_box;
    GtkWidget *settings_label;
    GtkWidget *radio_box;
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    GtkTreeSelection *selection;

    gint original_icon_size;
    gboolean original_single_click;
    gboolean changed;

    g_return_val_if_fail(desktop_dir != NULL, FALSE);
    g_return_val_if_fail(desktop_icon_size != NULL, FALSE);
    g_return_val_if_fail(single_click != NULL, FALSE);

    original_icon_size = *desktop_icon_size;
    original_single_click = *single_click;

    state.desktop_dir = g_strdup(desktop_dir);
    state.store = gtk_list_store_new(APP_N_COLS,
                                     GDK_TYPE_PIXBUF,
                                     G_TYPE_STRING,
                                     G_TYPE_STRING,
                                     G_TYPE_STRING,
                                     G_TYPE_STRING,
                                     G_TYPE_BOOLEAN);

    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(state.store),
                                         APP_COL_NAME, GTK_SORT_ASCENDING);

    state.dialog = gtk_dialog_new_with_buttons(
        _("Desktop Programs"), parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        _("_Add"), GTK_RESPONSE_APPLY,
        _("_Remove"), GTK_RESPONSE_REJECT,
        _("_Close"), GTK_RESPONSE_CLOSE,
        NULL);
    gtk_window_set_default_size(GTK_WINDOW(state.dialog), 720, 480);
    gtk_window_set_position(GTK_WINDOW(state.dialog), parent ?
        GTK_WIN_POS_CENTER_ON_PARENT : GTK_WIN_POS_CENTER_ALWAYS);

    state.add_button = gtk_dialog_get_widget_for_response(GTK_DIALOG(state.dialog),
                                                           GTK_RESPONSE_APPLY);
    state.remove_button = gtk_dialog_get_widget_for_response(GTK_DIALOG(state.dialog),
                                                              GTK_RESPONSE_REJECT);
    gtk_widget_set_sensitive(state.add_button, FALSE);
    gtk_widget_set_sensitive(state.remove_button, FALSE);

    content = gtk_dialog_get_content_area(GTK_DIALOG(state.dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 10);

    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(content), box, FALSE, FALSE, 0);
    search_label = gtk_label_new(_("Search:"));
    gtk_box_pack_start(GTK_BOX(box), search_label, FALSE, FALSE, 0);
    state.search = gtk_search_entry_new();
    gtk_widget_set_hexpand(state.search, TRUE);
    gtk_box_pack_start(GTK_BOX(box), state.search, TRUE, TRUE, 0);

    /* Agregado por josejp2424 (2026): las preferencias básicas de los
     * lanzadores se pueden ajustar desde el mismo gestor de aplicaciones. */
    settings_frame = gtk_frame_new(_("Desktop icon behavior"));
    gtk_box_pack_start(GTK_BOX(content), settings_frame, FALSE, FALSE, 8);
    settings_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(settings_box), 8);
    gtk_container_add(GTK_CONTAINER(settings_frame), settings_box);

    settings_label = gtk_label_new(_("Icon size:"));
    gtk_box_pack_start(GTK_BOX(settings_box), settings_label, FALSE, FALSE, 0);
    {
        const RoxDesktopDropdownItem size_items[] = {
            { "24", "24" },
            { "32", "32" },
            { "48", "48" },
            { "64", "64" }
        };
        gchar *size_id = g_strdup_printf("%d", *desktop_icon_size);
        state.icon_size_combo = rox_desktop_dropdown_new(size_items,
            G_N_ELEMENTS(size_items), size_id);
        g_free(size_id);
    }
    gtk_box_pack_start(GTK_BOX(settings_box), state.icon_size_combo, FALSE, FALSE, 0);

    radio_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(settings_box), radio_box, FALSE, FALSE, 8);
    state.single_click_radio = gtk_radio_button_new_with_label(NULL,
        _("Open with one click"));
    gtk_box_pack_start(GTK_BOX(radio_box), state.single_click_radio,
                       FALSE, FALSE, 0);
    state.double_click_radio = gtk_radio_button_new_with_label_from_widget(
        GTK_RADIO_BUTTON(state.single_click_radio), _("Open with double click"));
    gtk_box_pack_start(GTK_BOX(radio_box), state.double_click_radio,
                       FALSE, FALSE, 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(
        *single_click ? state.single_click_radio : state.double_click_radio), TRUE);

    state.filter = GTK_TREE_MODEL_FILTER(
        gtk_tree_model_filter_new(GTK_TREE_MODEL(state.store), NULL));
    gtk_tree_model_filter_set_visible_func(state.filter,
                                           desktop_apps_filter_visible,
                                           &state, NULL);

    state.tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(state.filter));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(state.tree), TRUE);
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(state.tree), APP_COL_NAME);

    renderer = gtk_cell_renderer_pixbuf_new();
    column = gtk_tree_view_column_new_with_attributes("", renderer,
                                                       "pixbuf", APP_COL_ICON,
                                                       NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(state.tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes(_("Application"), renderer,
                                                       "text", APP_COL_NAME,
                                                       NULL);
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(state.tree), column);

    renderer = gtk_cell_renderer_text_new();
    g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
    column = gtk_tree_view_column_new_with_attributes(_("Description"), renderer,
                                                       "text", APP_COL_DESCRIPTION,
                                                       NULL);
    gtk_tree_view_column_set_expand(column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(state.tree), column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes(_("On desktop"), renderer,
                                                       NULL);
    gtk_tree_view_column_set_cell_data_func(column, renderer,
                                             desktop_apps_on_desktop_cell,
                                             NULL, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(state.tree), column);

    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_box_pack_start(GTK_BOX(content), scrolled, TRUE, TRUE, 8);
    gtk_container_add(GTK_CONTAINER(scrolled), state.tree);

    state.status = gtk_label_new(_("Select an application to add it to the desktop"));
    gtk_label_set_xalign(GTK_LABEL(state.status), 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(state.status), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(content), state.status, FALSE, FALSE, 0);

    desktop_apps_scan(&state);
    if (gtk_tree_model_iter_n_children(GTK_TREE_MODEL(state.store), NULL) == 0)
        desktop_apps_set_status(&state, _("No application launchers were found"));

    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(state.tree));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    g_signal_connect(selection, "changed",
                     G_CALLBACK(desktop_apps_selection_changed), &state);
    g_signal_connect(state.search, "changed",
                     G_CALLBACK(desktop_apps_search_changed), &state);
    g_signal_connect(state.tree, "row-activated",
                     G_CALLBACK(desktop_apps_row_activated), &state);
    gtk_widget_show_all(state.dialog);
    for (;;) {
        gint response = gtk_dialog_run(GTK_DIALOG(state.dialog));

        if (response == GTK_RESPONSE_APPLY)
            desktop_apps_add_selected(&state);
        else if (response == GTK_RESPONSE_REJECT)
            desktop_apps_remove_selected(&state);
        else
            break;
    }

    {
        const gchar *size_id = rox_desktop_dropdown_get_active_id(
            state.icon_size_combo);
        if (size_id)
            *desktop_icon_size = atoi(size_id);
        *single_click = gtk_toggle_button_get_active(
            GTK_TOGGLE_BUTTON(state.single_click_radio));
    }
    changed = original_icon_size != *desktop_icon_size ||
              original_single_click != *single_click;

    gtk_widget_destroy(state.dialog);
    g_object_unref(state.filter);
    g_object_unref(state.store);
    g_free(state.desktop_dir);
    return changed;
}

gboolean desktop_app_get_metadata(const gchar *path,
                                  gchar **display_name,
                                  GIcon **icon)
{
    GDesktopAppInfo *app;
    const gchar *name;
    GIcon *app_icon;

    g_return_val_if_fail(path != NULL, FALSE);

    if (display_name)
        *display_name = NULL;
    if (icon)
        *icon = NULL;

    app = g_desktop_app_info_new_from_filename(path);
    if (!app)
        return FALSE;

    name = g_app_info_get_display_name(G_APP_INFO(app));
    if (display_name) {
        if (name && *name)
            *display_name = g_strdup(name);
        else
            *display_name = g_path_get_basename(path);
    }
    app_icon = g_app_info_get_icon(G_APP_INFO(app));
    if (icon && app_icon)
        *icon = g_object_ref(app_icon);

    g_object_unref(app);
    return TRUE;
}

gboolean desktop_app_launch(const gchar *path, GError **error)
{
    GDesktopAppInfo *app;
    gboolean launched;

    g_return_val_if_fail(path != NULL, FALSE);

    app = g_desktop_app_info_new_from_filename(path);
    if (!app) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    "%s", _("The desktop launcher is invalid"));
        return FALSE;
    }

    launched = g_app_info_launch(G_APP_INFO(app), NULL, NULL, error);
    g_object_unref(app);
    return launched;
}
