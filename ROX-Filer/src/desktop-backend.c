/*
 * Agregado por josejp2424 (2026): selección del backend del escritorio.
 */
#include "config.h"

#include <gtk/gtk.h>

#include "desktop-backend.h"
#include "debug_log.h"

/* Respaldo transitorio para displays que todavía no tienen un backend ROX
 * propio. Conserva el comportamiento GTK3 anterior, pero no implementa el
 * control de instancia. Se conserva únicamente para displays no reconocidos. */
static gboolean desktop_generic_supports_display(GdkDisplay *display)
{
    (void)display;
    return TRUE;
}

static gboolean desktop_generic_prepare_display(GdkDisplay *display, GError **error)
{
    (void)display;
    (void)error;
    return TRUE;
}

static void desktop_generic_configure_window(GtkWindow *window)
{
    g_return_if_fail(GTK_IS_WINDOW(window));

    gtk_window_set_skip_taskbar_hint(window, TRUE);
    gtk_window_set_skip_pager_hint(window, TRUE);
    gtk_window_set_type_hint(window, GDK_WINDOW_TYPE_HINT_DESKTOP);
    gtk_window_set_keep_below(window, TRUE);
    gtk_window_stick(window);
}

static void desktop_generic_register_control(GtkWidget *window,
                                              DesktopBackendRefreshFunc refresh,
                                              gpointer user_data)
{
    (void)window;
    (void)refresh;
    (void)user_data;
}

static void desktop_generic_unregister_control(GtkWidget *window)
{
    (void)window;
}

static void desktop_generic_lower_window(GtkWidget *window)
{
    GdkWindow *gwindow;

    if (!window || !gtk_widget_get_realized(window))
        return;
    gwindow = gtk_widget_get_window(window);
    if (gwindow)
        gdk_window_lower(gwindow);
}

static gboolean desktop_generic_send_refresh_request(GdkDisplay *display)
{
    (void)display;
    return FALSE;
}

static const DesktopBackend *desktop_generic_backend_get(void)
{
    static const DesktopBackend backend = {
        "gtk-generic",
        desktop_generic_supports_display,
        desktop_generic_prepare_display,
        desktop_generic_configure_window,
        desktop_generic_register_control,
        desktop_generic_unregister_control,
        desktop_generic_lower_window,
        desktop_generic_send_refresh_request
    };

    return &backend;
}

const DesktopBackend *desktop_backend_select(GdkDisplay *display)
{
    const DesktopBackend *backend;

    backend = desktop_x11_backend_get();
    if (backend && backend->supports_display &&
        backend->supports_display(display)) {
        ROX_LOG_INFO("desktop-backend", "selected backend=%s display=%s",
                     backend->name, display ? G_OBJECT_TYPE_NAME(display) : "none");
        return backend;
    }

    backend = desktop_wayland_backend_get();
    if (backend && backend->supports_display &&
        backend->supports_display(display)) {
        ROX_LOG_INFO("desktop-backend", "selected backend=%s display=%s",
                     backend->name, display ? G_OBJECT_TYPE_NAME(display) : "none");
        return backend;
    }

    backend = desktop_generic_backend_get();
    ROX_LOG_WARNING("desktop-backend", "using fallback backend=%s display=%s",
                    backend->name, display ? G_OBJECT_TYPE_NAME(display) : "none");
    return backend;
}
