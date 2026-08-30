/*
 * Rox-Filer2: Wayland-safe dropdown helper for native desktop dialogs.
 *
 * GtkComboBox menu popups can be positioned as detached windows by some
 * Wayland compositors.  Desktop dialogs use GtkMenuButton + GtkPopover
 * instead, which keeps the popup anchored to the control on X11 and Wayland.
 */
#ifndef ROX_DESKTOP_DROPDOWN_H
#define ROX_DESKTOP_DROPDOWN_H

#include <gtk/gtk.h>

typedef struct {
    const gchar *id;
    const gchar *label;
} RoxDesktopDropdownItem;

GtkWidget *rox_desktop_dropdown_new(const RoxDesktopDropdownItem *items,
                                    gsize n_items,
                                    const gchar *active_id);
const gchar *rox_desktop_dropdown_get_active_id(GtkWidget *dropdown);
gboolean rox_desktop_dropdown_set_active_id(GtkWidget *dropdown,
                                            const gchar *active_id);

#endif
