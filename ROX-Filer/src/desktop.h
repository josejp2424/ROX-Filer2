/*
 * Agregado por josejp2424 (2026): interfaz pública del modo ROX Desktop.
 * La integración con el servidor gráfico queda detrás de
 * desktop-backend.h; los demás módulos sólo deben incluir este archivo.
 */
#ifndef ROX_DESKTOP_H
#define ROX_DESKTOP_H

#include <glib.h>
#include <gdk/gdk.h>

void desktop_init(void);
void desktop_start(void);
gboolean desktop_is_running(void);
GdkWindow *desktop_get_gdk_window(void);
void desktop_refresh_now(void);
gboolean desktop_send_refresh_request(void);

/* Herramientas independientes que pueden abrirse desde la línea de comandos. */
void desktop_open_wallpaper_manager(void);
void desktop_open_apps_manager(void);

/* Recalcula el área útil después de cambios externos de JWM/wbar. */
void desktop_refresh_after_environment_change(void);

/* Agregado por josejp2424 (2026): establece y guarda el wallpaper desde
 * acciones internas de ROX-Filer, sin scripts externos. */
gboolean desktop_set_wallpaper(const gchar *path, gboolean save,
                               GError **error);

#endif
