/* ROX File Search integration.
 * Copyright (C) 2026 josejp2424
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "config.h"

#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include "global.h"
#include "search_integration.h"
#include "options.h"
#include "view_iface.h"
#include "diritem.h"
#include "gui_support.h"
#include "support.h"
#include "i18n.h"

static Option o_search_context_menu;
static Option o_search_toolbar;
static Option o_search_recursive;
static Option o_search_hidden;
static Option o_search_follow_links;
static Option o_search_one_filesystem;
static Option o_search_content_limit_mb;

static GList *build_search_tools(Option *option, xmlNode *node, guchar *label);

void search_integration_init(void)
{
    option_add_int(&o_search_context_menu, "search_context_menu", TRUE);
    option_add_int(&o_search_toolbar, "search_toolbar", TRUE);
    option_add_int(&o_search_recursive, "search_recursive", TRUE);
    option_add_int(&o_search_hidden, "search_hidden", FALSE);
    option_add_int(&o_search_follow_links, "search_follow_links", FALSE);
    option_add_int(&o_search_one_filesystem, "search_one_filesystem", TRUE);
    option_add_int(&o_search_content_limit_mb, "search_content_limit_mb", 20);
    option_register_widget("search-tools", build_search_tools);
}

gboolean search_integration_enabled(void)
{
    return o_search_context_menu.int_value != 0;
}

gboolean search_integration_toolbar_enabled(void)
{
    return o_search_toolbar.int_value != 0;
}

gboolean search_integration_available(FilerWindow *filer_window)
{
    ViewIter iter;
    DirItem *item;
    gint count;

    if (!filer_window || !o_search_context_menu.int_value)
        return FALSE;
    count = view_count_selected(filer_window->view);
    if (count == 0)
        return TRUE;

    view_get_iter(filer_window->view, &iter, VIEW_ITER_SELECTED);
    while ((item = iter.next(&iter))) {
        const gchar *path;
        if (item->base_type == TYPE_DIRECTORY)
            continue;
        path = (const gchar *)make_path(filer_window->sym_path, item->leafname);
        if (!g_file_test(path, G_FILE_TEST_IS_DIR))
            return FALSE;
    }
    return TRUE;
}

void search_integration_launch(FilerWindow *filer_window)
{
    GPtrArray *argv;
    GList *paths = NULL, *node;
    gchar *program;
    GError *error = NULL;
    guint roots_added = 0;
    gchar *content_limit_arg;

    g_return_if_fail(filer_window != NULL);

    program = g_find_program_in_path("rox-find");
    if (!program) {
        report_error("%s", _("ROX File Search is not installed or is not in PATH."));
        return;
    }

    argv = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(argv, program);
    if (o_search_hidden.int_value)
        g_ptr_array_add(argv, g_strdup("--hidden"));
    if (!o_search_recursive.int_value)
        g_ptr_array_add(argv, g_strdup("--no-recursive"));
    if (o_search_follow_links.int_value)
        g_ptr_array_add(argv, g_strdup("--follow-links"));
    if (!o_search_one_filesystem.int_value)
        g_ptr_array_add(argv, g_strdup("--cross-filesystems"));
    content_limit_arg = g_strdup_printf("--max-content-mb=%d",
        CLAMP(o_search_content_limit_mb.int_value, 1, 1024));
    g_ptr_array_add(argv, content_limit_arg);

    if (view_count_selected(filer_window->view) > 0)
        paths = filer_selected_items(filer_window);
    else
        paths = g_list_append(NULL, g_strdup(filer_window->sym_path));

    for (node = paths; node; node = node->next) {
        const gchar *path = node->data;
        if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
            g_ptr_array_add(argv, g_strdup(path));
            roots_added++;
        }
    }
    g_list_free_full(paths, g_free);
    g_ptr_array_add(argv, NULL);

    if (roots_added == 0) {
        g_ptr_array_free(argv, TRUE);
        report_error("%s", _("Select one or more folders to search."));
        return;
    }

    if (!g_spawn_async(NULL, (gchar **)argv->pdata, NULL,
                       G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error)) {
        report_error(_("Unable to start ROX File Search: %s"),
                     error ? error->message : _("Unknown error"));
        g_clear_error(&error);
    }
    g_ptr_array_free(argv, TRUE);
}

static void search_open_button(GtkButton *button, gpointer data)
{
    FilerWindow *source = window_with_focus;
    (void)button; (void)data;
    if (!source && all_filer_windows)
        source = all_filer_windows->data;
    if (source)
        search_integration_launch(source);
}

static GList *build_search_tools(Option *option, xmlNode *node, guchar *label)
{
    GtkWidget *button;
    (void)node; (void)label;
    g_return_val_if_fail(option == NULL, NULL);
    button = gtk_button_new_with_label(_("Open ROX File Search"));
    gtk_button_set_image(GTK_BUTTON(button),
        gtk_image_new_from_icon_name("rox-find", GTK_ICON_SIZE_BUTTON));
    gtk_widget_set_halign(button, GTK_ALIGN_START);
    g_signal_connect(button, "clicked", G_CALLBACK(search_open_button), NULL);
    return g_list_append(NULL, button);
}
