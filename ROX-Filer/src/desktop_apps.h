/*
 * Agregado por josejp2424 (2026): gestor XDG de programas del escritorio.
 */
#ifndef ROX_DESKTOP_APPS_H
#define ROX_DESKTOP_APPS_H

#include <gtk/gtk.h>
#include <gio/gio.h>

/* Abre el selector de aplicaciones y copia o elimina lanzadores .desktop
 * dentro del directorio real XDG Desktop. */
gboolean desktop_apps_show_manager(GtkWindow *parent,
                                    const gchar *desktop_dir,
                                    gint *desktop_icon_size,
                                    gboolean *single_click);

/* Lee el nombre y el icono definidos por un lanzador XDG. */
gboolean desktop_app_get_metadata(const gchar *path,
                                  gchar **display_name,
                                  GIcon **icon);

/* Ejecuta un lanzador .desktop mediante GDesktopAppInfo. */
gboolean desktop_app_launch(const gchar *path, GError **error);

#endif
