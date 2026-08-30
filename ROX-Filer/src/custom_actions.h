/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Modern user-defined actions for ROX-Filer. */

#ifndef ROX_CUSTOM_ACTIONS_H
#define ROX_CUSTOM_ACTIONS_H

#include <gtk/gtk.h>

void custom_actions_init(void);
GtkWidget *custom_actions_create_menu(GList *paths, GtkWindow *parent);

/* Return matching action items without putting them in a submenu. The caller
 * owns the list container and each widget until it is inserted or destroyed. */
GList *custom_actions_create_items(GList *paths, GtkWindow *parent);

/* Open a pre-filled action editor for the current selection. */
void custom_actions_add_for_paths(GList *paths, GtkWindow *parent);

void custom_actions_show_manager(GtkWindow *parent);

#endif
