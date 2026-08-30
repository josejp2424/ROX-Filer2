/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Modern XDG/GIO application associations for ROX-Filer. */

#ifndef ROX_XDG_APPS_H
#define ROX_XDG_APPS_H

#include <gtk/gtk.h>
#include <gio/gio.h>

void xdg_apps_init(void);
gboolean xdg_apps_launch_app_info(GAppInfo *app, GList *paths,
                                   GtkWindow *parent);

/* Update the user MIME association without rewriting unrelated entries in
 * ~/.config/mimeapps.list. If set_default is TRUE the application is also
 * placed first in [Default Applications]. In both modes it is placed first
 * in [Added Associations] and removed only from the matching MIME entry in
 * [Removed Associations]. */
gboolean xdg_apps_set_mime_association(GAppInfo *app,
                                        const gchar *mime_type,
                                        gboolean set_default,
                                        GError **error);

/* Hidden diagnostic entry point used by the bundled test script. */
gboolean xdg_apps_diagnose_launch(const gchar *desktop_file,
                                   const gchar *path);

/* Build an Open With submenu for the supplied local paths. The caller owns
 * the returned widget. Callback data keeps private copies of every path. */
GtkWidget *xdg_apps_create_open_with_menu(GList *paths, GtkWindow *parent);

/* Put the applications and matching file actions directly at the start of the
 * selected-item menu, preserving the classic ROX MIME-tools workflow. */
int xdg_apps_add_mime_tools(GtkWidget *menu, GList *paths, GtkWindow *parent);
void xdg_apps_remove_mime_tools(void);

/* Open the modern chooser and optionally remember the selected application. */
void xdg_apps_choose_for_paths(GList *paths, GtkWindow *parent,
                               gboolean default_remember);

/* Create a user .desktop application for the selected files. */
void xdg_apps_add_custom_for_paths(GList *paths, GtkWindow *parent);

/* Show the manager for applications created by ROX-Filer. */
void xdg_apps_show_manager(GtkWindow *parent);

/* Shared icon helpers used by Custom Actions. The returned strings must be
 * freed by the caller. */
gchar *xdg_apps_choose_icon(GtkWindow *parent, const gchar *initial);
gchar *xdg_apps_install_user_icon(const gchar *source_or_name,
                                   const gchar *desktop_id,
                                   GError **error);

#endif
