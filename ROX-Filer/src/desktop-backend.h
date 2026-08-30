/*
 * Agregado por josejp2424 (2026): interfaz privada de los backends del
 * escritorio único. desktop.c conserva la lógica común y cada backend
 * encapsula la integración con el servidor gráfico.
 */
#ifndef ROX_DESKTOP_BACKEND_H
#define ROX_DESKTOP_BACKEND_H

#include <gtk/gtk.h>

typedef void (*DesktopBackendRefreshFunc)(gpointer user_data);

typedef struct _DesktopBackend {
    const gchar *name;
    gboolean (*supports_display)(GdkDisplay *display);
    gboolean (*prepare_display)(GdkDisplay *display, GError **error);
    void (*configure_window)(GtkWindow *window);
    void (*register_control)(GtkWidget *window,
                             DesktopBackendRefreshFunc refresh,
                             gpointer user_data);
    void (*unregister_control)(GtkWidget *window);
    void (*lower_window)(GtkWidget *window);
    gboolean (*send_refresh_request)(GdkDisplay *display);
} DesktopBackend;

/* Devuelve el backend apropiado para el display actual: X11, Wayland
 * Layer Shell o el respaldo GTK genérico. */
const DesktopBackend *desktop_backend_select(GdkDisplay *display);

/* Implementaciones proporcionadas por los backends gráficos. */
const DesktopBackend *desktop_x11_backend_get(void);
const DesktopBackend *desktop_wayland_backend_get(void);

#endif
