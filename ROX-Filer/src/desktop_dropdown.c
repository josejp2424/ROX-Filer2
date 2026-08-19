/*
 * Rox-Filer2: Wayland-safe dropdown helper for native desktop dialogs.
 */
#include "config.h"

#include <gtk/gtk.h>
#include <string.h>

#include "desktop_dropdown.h"

typedef struct {
    gchar *active_id;
    GtkWidget *label;
    GHashTable *labels;
} RoxDesktopDropdownState;

static void rox_desktop_dropdown_state_free(gpointer data)
{
    RoxDesktopDropdownState *state = data;

    if (!state)
        return;
    g_free(state->active_id);
    if (state->labels)
        g_hash_table_unref(state->labels);
    g_free(state);
}

static void rox_desktop_dropdown_choice_clicked(GtkButton *button,
                                                 gpointer data)
{
    GtkWidget *dropdown = data;
    RoxDesktopDropdownState *state;
    const gchar *id;
    const gchar *text;
    GtkPopover *popover;

    state = g_object_get_data(G_OBJECT(dropdown), "rox-desktop-dropdown-state");
    id = g_object_get_data(G_OBJECT(button), "rox-desktop-dropdown-id");
    text = gtk_button_get_label(button);
    if (!state || !id)
        return;

    g_free(state->active_id);
    state->active_id = g_strdup(id);
    gtk_label_set_text(GTK_LABEL(state->label), text ? text : "");

    popover = gtk_menu_button_get_popover(GTK_MENU_BUTTON(dropdown));
    if (popover)
        gtk_widget_hide(GTK_WIDGET(popover));
}

GtkWidget *rox_desktop_dropdown_new(const RoxDesktopDropdownItem *items,
                                    gsize n_items,
                                    const gchar *active_id)
{
    GtkWidget *menu_button;
    GtkWidget *content;
    GtkWidget *label;
    GtkWidget *arrow;
    GtkWidget *popover;
    GtkWidget *list;
    RoxDesktopDropdownState *state;
    gsize i;

    g_return_val_if_fail(items != NULL, NULL);
    g_return_val_if_fail(n_items > 0, NULL);

    menu_button = gtk_menu_button_new();
    gtk_widget_set_halign(menu_button, GTK_ALIGN_FILL);

    content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);
    arrow = gtk_image_new_from_icon_name("pan-down-symbolic", GTK_ICON_SIZE_MENU);
    gtk_box_pack_start(GTK_BOX(content), label, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content), arrow, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(menu_button), content);

    popover = gtk_popover_new(menu_button);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_set_border_width(GTK_CONTAINER(list), 4);
    gtk_container_add(GTK_CONTAINER(popover), list);

    state = g_new0(RoxDesktopDropdownState, 1);
    state->label = label;
    state->labels = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    g_object_set_data_full(G_OBJECT(menu_button), "rox-desktop-dropdown-state",
                           state, rox_desktop_dropdown_state_free);

    for (i = 0; i < n_items; i++) {
        GtkWidget *button;

        if (!items[i].id || !items[i].label)
            continue;

        g_hash_table_replace(state->labels,
                             g_strdup(items[i].id),
                             g_strdup(items[i].label));
        button = gtk_button_new_with_label(items[i].label);
        gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
        gtk_widget_set_halign(button, GTK_ALIGN_FILL);
        g_object_set_data_full(G_OBJECT(button), "rox-desktop-dropdown-id",
                               g_strdup(items[i].id), g_free);
        g_signal_connect(button, "clicked",
                         G_CALLBACK(rox_desktop_dropdown_choice_clicked),
                         menu_button);
        gtk_box_pack_start(GTK_BOX(list), button, FALSE, FALSE, 0);
    }

    /* GtkPopover is outside the normal dialog hierarchy.  Show its children
     * now and leave the popover itself hidden until GtkMenuButton opens it. */
    gtk_widget_show_all(list);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(menu_button), popover);

    if (!rox_desktop_dropdown_set_active_id(menu_button, active_id))
        rox_desktop_dropdown_set_active_id(menu_button, items[0].id);

    return menu_button;
}

const gchar *rox_desktop_dropdown_get_active_id(GtkWidget *dropdown)
{
    RoxDesktopDropdownState *state;

    g_return_val_if_fail(GTK_IS_MENU_BUTTON(dropdown), NULL);
    state = g_object_get_data(G_OBJECT(dropdown), "rox-desktop-dropdown-state");
    return state ? state->active_id : NULL;
}

gboolean rox_desktop_dropdown_set_active_id(GtkWidget *dropdown,
                                            const gchar *active_id)
{
    RoxDesktopDropdownState *state;
    const gchar *text;

    g_return_val_if_fail(GTK_IS_MENU_BUTTON(dropdown), FALSE);
    if (!active_id)
        return FALSE;

    state = g_object_get_data(G_OBJECT(dropdown), "rox-desktop-dropdown-state");
    if (!state)
        return FALSE;

    text = g_hash_table_lookup(state->labels, active_id);
    if (!text)
        return FALSE;

    g_free(state->active_id);
    state->active_id = g_strdup(active_id);
    gtk_label_set_text(GTK_LABEL(state->label), text);
    return TRUE;
}
