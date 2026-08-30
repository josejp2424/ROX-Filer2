/* Native paired-window layout for ROX-Filer GTK3.
 * Copyright (C) 2026 josejp2424
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "config.h"

#include <gtk/gtk.h>
#include <string.h>

#include "global.h"
#include "main.h"
#include "support.h"
#include "filer_pair.h"
#include "options.h"
#include "gui_support.h"
#include "i18n.h"

static Option o_pair_enabled;
static Option o_pair_startup;
static Option o_pair_remember_dirs;
static Option o_pair_same_monitor;
static Option o_pair_second_mode;
static Option o_pair_custom_dir;
static Option o_pair_last_left;
static Option o_pair_last_right;
static Option o_pair_orientation;
static Option o_pair_split;
static Option o_pair_gap;

static GtkWidget *pair_left_window;
static GtkWidget *pair_right_window;
static guint pair_layout_source;
static guint pair_layout_round;

#define PAIR_MIN_WIDTH  240
#define PAIR_MIN_HEIGHT 180

static GList *build_pair_tools(Option *option, xmlNode *node, guchar *label);

static FilerWindow *pair_filer_from_window(GtkWidget *window)
{
    if (!window)
        return NULL;
    return g_object_get_data(G_OBJECT(window), "filer_window");
}

static const gchar *pair_current_path(GtkWidget *window)
{
    FilerWindow *filer = pair_filer_from_window(window);
    return filer ? filer->sym_path : NULL;
}

static void pair_remember_current_paths(void)
{
    const gchar *left_path;
    const gchar *right_path;

    if (!o_pair_remember_dirs.int_value)
        return;
    left_path = pair_current_path(pair_left_window);
    right_path = pair_current_path(pair_right_window);
    if (left_path && *left_path)
        option_set("pair_last_left", left_path);
    if (right_path && *right_path)
        option_set("pair_last_right", right_path);
}

static void pair_prepare_window(GtkWidget *window)
{
    GdkGeometry geometry;

    if (!window)
        return;
    g_object_set_data(G_OBJECT(window), "rox-paired-window",
                      GINT_TO_POINTER(1));
    g_object_set_data(G_OBJECT(window), "rox-standard-size-exempt",
                      GINT_TO_POINTER(1));
    /* Normal filer windows use a 640x400 minimum. Paired windows must be
     * allowed to share a small monitor without overlapping. */
    gtk_widget_set_size_request(window, -1, -1);
    memset(&geometry, 0, sizeof(geometry));
    geometry.min_width = PAIR_MIN_WIDTH;
    geometry.min_height = PAIR_MIN_HEIGHT;
    gtk_window_set_geometry_hints(GTK_WINDOW(window), NULL, &geometry,
                                  GDK_HINT_MIN_SIZE);
}

static void pair_window_gone(gpointer data, GObject *where_the_object_was)
{
    GtkWidget **slot = data;
    (void)where_the_object_was;
    *slot = NULL;
}

static void pair_set_window(GtkWidget **slot, GtkWidget *window)
{
    if (*slot)
        g_object_weak_unref(G_OBJECT(*slot), pair_window_gone, slot);
    *slot = window;
    if (window)
        g_object_weak_ref(G_OBJECT(window), pair_window_gone, slot);
}

static gboolean get_workarea(GtkWidget *reference, GdkRectangle *area)
{
    GdkDisplay *display;
    GdkWindow *gwindow;
    GdkMonitor *monitor = NULL;

    g_return_val_if_fail(area != NULL, FALSE);
    display = gdk_display_get_default();
    if (!display)
        return FALSE;

    gwindow = (o_pair_same_monitor.int_value && reference)
        ? gtk_widget_get_window(reference) : NULL;
    if (gwindow)
        monitor = gdk_display_get_monitor_at_window(display, gwindow);
    if (!monitor)
        monitor = gdk_display_get_primary_monitor(display);
    if (!monitor && gdk_display_get_n_monitors(display) > 0)
        monitor = gdk_display_get_monitor(display, 0);
    if (!monitor)
        return FALSE;

    gdk_monitor_get_workarea(monitor, area);
    return TRUE;
}

static gboolean pair_layout_cb(gpointer data)
{
    GdkRectangle area;
    gint gap, split, first_size, second_size;
    gboolean vertical;
    (void)data;

    if (!pair_left_window || !pair_right_window) {
        pair_layout_source = 0;
        pair_layout_round = 0;
        return G_SOURCE_REMOVE;
    }
    if (!gtk_widget_get_realized(pair_left_window) ||
        !gtk_widget_get_realized(pair_right_window))
        return G_SOURCE_CONTINUE;
    if (!get_workarea(pair_left_window, &area)) {
        pair_layout_source = 0;
        return G_SOURCE_REMOVE;
    }

    gap = CLAMP((gint)o_pair_gap.int_value, 0, 64);
    split = CLAMP((gint)o_pair_split.int_value, 20, 80);
    vertical = o_pair_orientation.int_value != 0;

    if (vertical) {
        first_size = (area.height - gap) * split / 100;
        second_size = area.height - gap - first_size;
        gtk_window_move(GTK_WINDOW(pair_left_window), area.x, area.y);
        gtk_window_resize(GTK_WINDOW(pair_left_window), area.width,
                          MAX(first_size, 1));
        gtk_window_move(GTK_WINDOW(pair_right_window), area.x,
                        area.y + first_size + gap);
        gtk_window_resize(GTK_WINDOW(pair_right_window), area.width,
                          MAX(second_size, 1));
    } else {
        first_size = (area.width - gap) * split / 100;
        second_size = area.width - gap - first_size;
        gtk_window_move(GTK_WINDOW(pair_left_window), area.x, area.y);
        gtk_window_resize(GTK_WINDOW(pair_left_window), MAX(first_size, 1),
                          area.height);
        gtk_window_move(GTK_WINDOW(pair_right_window),
                        area.x + first_size + gap, area.y);
        gtk_window_resize(GTK_WINDOW(pair_right_window), MAX(second_size, 1),
                          area.height);
    }

    pair_layout_round++;
    if (pair_layout_round < 4)
        return G_SOURCE_CONTINUE;
    pair_layout_source = 0;
    pair_layout_round = 0;
    return G_SOURCE_REMOVE;
}

static void pair_schedule_layout(void)
{
    if (pair_layout_source)
        g_source_remove(pair_layout_source);
    pair_layout_round = 0;
    pair_layout_source = g_timeout_add(180, pair_layout_cb, NULL);
}

void filer_pair_init(void)
{
    option_add_int(&o_pair_enabled, "pair_enabled", FALSE);
    option_add_int(&o_pair_startup, "pair_startup", FALSE);
    option_add_int(&o_pair_remember_dirs, "pair_remember_dirs", TRUE);
    option_add_int(&o_pair_same_monitor, "pair_same_monitor", TRUE);
    option_add_int(&o_pair_second_mode, "pair_second_mode", 0);
    option_add_string(&o_pair_custom_dir, "pair_custom_dir", home_dir);
    option_add_string(&o_pair_last_left, "pair_last_left", home_dir);
    option_add_string(&o_pair_last_right, "pair_last_right", home_dir);
    option_add_int(&o_pair_orientation, "pair_orientation", 0);
    option_add_int(&o_pair_split, "pair_split", 50);
    option_add_int(&o_pair_gap, "pair_gap", 6);
    option_register_widget("pair-tools", build_pair_tools);
}

gboolean filer_pair_is_enabled(void)
{
    return o_pair_enabled.int_value != 0;
}

static gchar *choose_right_path(const gchar *left_path,
                                const gchar *explicit_path)
{
    if (explicit_path && *explicit_path)
        return pathdup(explicit_path);

    switch (o_pair_second_mode.int_value) {
        case 1:
            return pathdup(left_path);
        case 2:
        {
            const gchar *current = pair_current_path(pair_right_window);
            if (current && *current)
                return pathdup(current);
            if (o_pair_last_right.value && *o_pair_last_right.value)
                return pathdup((const gchar *)o_pair_last_right.value);
            return g_strdup(home_dir);
        }
        case 3:
            if (o_pair_custom_dir.value && *o_pair_custom_dir.value)
                return pathdup((const gchar *)o_pair_custom_dir.value);
            return g_strdup(home_dir);
        case 0:
        default:
            return g_strdup(home_dir);
    }
}

void filer_pair_open(FilerWindow *source, const gchar *left_path,
                     const gchar *right_path)
{
    FilerWindow *left = source;
    FilerWindow *right;
    gchar *left_resolved;
    gchar *right_resolved;
    gint unique_value;

    if (!o_pair_enabled.int_value) {
        info_message(_("Paired windows are disabled in Options."));
        return;
    }

    pair_remember_current_paths();

    if (left_path && *left_path)
        left_resolved = pathdup(left_path);
    else if (source && source->sym_path)
        left_resolved = g_strdup(source->sym_path);
    else if (o_pair_remember_dirs.int_value &&
             o_pair_last_left.value && *o_pair_last_left.value)
        left_resolved = pathdup((const gchar *)o_pair_last_left.value);
    else
        left_resolved = g_strdup(home_dir);

    right_resolved = choose_right_path(left_resolved, right_path);

    unique_value = o_unique_filer_windows.int_value;
    o_unique_filer_windows.int_value = FALSE;
    if (left && left_path && *left_path &&
        (!left->sym_path || strcmp(left->sym_path, left_resolved) != 0))
        left = NULL;
    if (!left)
        left = filer_opendir(left_resolved, NULL, NULL);
    right = left ? filer_opendir(right_resolved, left, NULL) : NULL;
    o_unique_filer_windows.int_value = unique_value;

    if (!left || !right || left == right) {
        report_error("%s", _("Unable to create the paired windows."));
        g_free(left_resolved);
        g_free(right_resolved);
        return;
    }

    pair_prepare_window(left->window);
    pair_prepare_window(right->window);
    pair_set_window(&pair_left_window, left->window);
    pair_set_window(&pair_right_window, right->window);

    if (o_pair_remember_dirs.int_value) {
        option_set("pair_last_left", left_resolved);
        option_set("pair_last_right", right_resolved);
    }

    gtk_window_present(GTK_WINDOW(left->window));
    gtk_window_present(GTK_WINDOW(right->window));
    pair_schedule_layout();

    g_free(left_resolved);
    g_free(right_resolved);
}

void filer_pair_realign(void)
{
    if (!pair_left_window || !pair_right_window) {
        info_message(_("No paired windows are currently open."));
        return;
    }
    pair_schedule_layout();
}

void filer_pair_startup_if_enabled(void)
{
    FilerWindow *source;

    if (!o_pair_enabled.int_value || !o_pair_startup.int_value)
        return;
    if (!all_filer_windows)
        return;
    source = all_filer_windows->data;
    if (!source)
        return;
    filer_pair_open(source, NULL, NULL);
}

static void pair_open_button(GtkButton *button, gpointer data)
{
    FilerWindow *source = window_with_focus;
    (void)button; (void)data;
    if (!source && all_filer_windows)
        source = all_filer_windows->data;
    filer_pair_open(source, NULL, NULL);
}

static void pair_realign_button(GtkButton *button, gpointer data)
{
    (void)button; (void)data;
    filer_pair_realign();
}

static GList *build_pair_tools(Option *option, xmlNode *node, guchar *label)
{
    GtkWidget *box, *open, *realign;
    (void)node; (void)label;
    g_return_val_if_fail(option == NULL, NULL);
    box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    open = gtk_button_new_with_label(_("Open Paired Windows"));
    gtk_button_set_image(GTK_BUTTON(open),
        gtk_image_new_from_icon_name("window-new", GTK_ICON_SIZE_BUTTON));
    realign = gtk_button_new_with_label(_("Realign Windows"));
    gtk_button_set_image(GTK_BUTTON(realign),
        gtk_image_new_from_icon_name("view-restore", GTK_ICON_SIZE_BUTTON));
    g_signal_connect(open, "clicked", G_CALLBACK(pair_open_button), NULL);
    g_signal_connect(realign, "clicked", G_CALLBACK(pair_realign_button), NULL);
    gtk_box_pack_start(GTK_BOX(box), open, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), realign, FALSE, FALSE, 0);
    return g_list_append(NULL, box);
}
