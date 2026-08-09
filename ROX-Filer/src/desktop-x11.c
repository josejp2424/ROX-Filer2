/*
 * Agregado por josejp2424 (2026): backend X11 del escritorio único.
 *
 * Mantiene fuera de desktop.c el registro de instancia, los mensajes de
 * actualización y las sugerencias EWMH propias del escritorio X11.
 */
#include "config.h"

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <string.h>

#include "global.h"
#include "desktop-backend.h"
#include "debug_log.h"

#define ROX_DESKTOP_WINDOW_ATOM "_ROX_DESKTOP_WINDOW"
#define ROX_DESKTOP_REFRESH_ATOM "_ROX_DESKTOP_REFRESH"

static Atom desktop_window_atom;
static Atom desktop_refresh_atom;
static gboolean desktop_filter_installed;
static DesktopBackendRefreshFunc desktop_refresh_callback;
static gpointer desktop_refresh_data;

static gboolean desktop_x11_supports_display(GdkDisplay *display)
{
    return display != NULL && GDK_IS_X11_DISPLAY(display);
}

static GdkFilterReturn desktop_x11_filter(GdkXEvent *xevent,
                                           GdkEvent *event,
                                           gpointer data)
{
    XEvent *xe = (XEvent *)xevent;
    (void)event;
    (void)data;

    if (!xe || xe->type != ClientMessage)
        return GDK_FILTER_CONTINUE;

    if (desktop_refresh_atom != None &&
        xe->xclient.message_type == desktop_refresh_atom) {
        if (desktop_refresh_callback)
            desktop_refresh_callback(desktop_refresh_data);
        return GDK_FILTER_REMOVE;
    }

    return GDK_FILTER_CONTINUE;
}

static gboolean desktop_x11_prepare_display(GdkDisplay *display, GError **error)
{
    gboolean supported = desktop_x11_supports_display(display);
    (void)error;
    ROX_LOG_INFO("x11", "prepare display supported=%d name=%s", supported,
                 display ? gdk_display_get_name(display) : "none");
    return supported;
}

static void desktop_x11_configure_window(GtkWindow *window)
{
    g_return_if_fail(GTK_IS_WINDOW(window));

    ROX_LOG_DEBUG("x11", "configuring desktop window");
    gtk_window_set_skip_taskbar_hint(window, TRUE);
    gtk_window_set_skip_pager_hint(window, TRUE);
    gtk_window_set_type_hint(window, GDK_WINDOW_TYPE_HINT_DESKTOP);
    gtk_window_set_keep_below(window, TRUE);
    gtk_window_stick(window);
}

static void desktop_x11_register_control(GtkWidget *window,
                                         DesktopBackendRefreshFunc refresh,
                                         gpointer user_data)
{
    GdkDisplay *gdisplay;
    Display *display;
    GdkWindow *gwindow;
    Window xid;

    if (!window || !gtk_widget_get_realized(window))
        return;

    gdisplay = gtk_widget_get_display(window);
    if (!desktop_x11_supports_display(gdisplay))
        return;

    display = gdk_x11_display_get_xdisplay(gdisplay);
    gwindow = gtk_widget_get_window(window);
    if (!gwindow)
        return;

    desktop_refresh_callback = refresh;
    desktop_refresh_data = user_data;
    xid = gdk_x11_window_get_xid(gwindow);
    desktop_window_atom = XInternAtom(display, ROX_DESKTOP_WINDOW_ATOM, False);
    desktop_refresh_atom = XInternAtom(display, ROX_DESKTOP_REFRESH_ATOM, False);
    ROX_LOG_INFO("x11", "registering desktop xid=0x%lx root=0x%lx",
                 (unsigned long)xid,
                 (unsigned long)DefaultRootWindow(display));
    XChangeProperty(display, DefaultRootWindow(display), desktop_window_atom,
                    XA_WINDOW, 32, PropModeReplace,
                    (const unsigned char *)&xid, 1);

    if (!desktop_filter_installed) {
        gdk_window_add_filter(gwindow, desktop_x11_filter, NULL);
        desktop_filter_installed = TRUE;
    }
    XFlush(display);
}

static void desktop_x11_unregister_control(GtkWidget *window)
{
    GdkDisplay *gdisplay;
    Display *display;
    GdkWindow *gwindow;

    if (!window || !gtk_widget_get_realized(window))
        goto clear_state;

    gdisplay = gtk_widget_get_display(window);
    if (!desktop_x11_supports_display(gdisplay))
        goto clear_state;

    display = gdk_x11_display_get_xdisplay(gdisplay);
    gwindow = gtk_widget_get_window(window);
    if (!gwindow)
        goto clear_state;

    if (desktop_filter_installed) {
        gdk_window_remove_filter(gwindow, desktop_x11_filter, NULL);
        desktop_filter_installed = FALSE;
    }

    if (desktop_window_atom != None) {
        Atom actual_type;
        gint actual_format;
        gulong nitems, bytes_after;
        unsigned char *property = NULL;
        Window registered = None;
        Window own_xid = gdk_x11_window_get_xid(gwindow);

        if (XGetWindowProperty(display, DefaultRootWindow(display),
                desktop_window_atom, 0, 1, False, XA_WINDOW,
                &actual_type, &actual_format, &nitems, &bytes_after,
                &property) == Success && property &&
            actual_type == XA_WINDOW && actual_format == 32 && nitems >= 1)
            registered = *(Window *)property;
        if (property)
            XFree(property);

        /* No borrar la propiedad si una instancia nueva ya la reemplazó. */
        if (registered == own_xid)
            XDeleteProperty(display, DefaultRootWindow(display),
                            desktop_window_atom);
    }
    XFlush(display);
    ROX_LOG_INFO("x11", "desktop control unregistered");

clear_state:
    desktop_filter_installed = FALSE;
    desktop_refresh_callback = NULL;
    desktop_refresh_data = NULL;
    desktop_window_atom = None;
    desktop_refresh_atom = None;
}

static void desktop_x11_lower_window(GtkWidget *window)
{
    GdkWindow *gwindow;

    if (!window || !gtk_widget_get_realized(window))
        return;
    gwindow = gtk_widget_get_window(window);
    if (gwindow) {
        ROX_LOG_TRACE("x11", "lowering desktop window");
        gdk_window_lower(gwindow);
    }
}

static gboolean desktop_x11_send_refresh_request(GdkDisplay *gdisplay)
{
    Display *display;
    Atom window_atom, refresh_atom, actual_type;
    gint actual_format;
    gulong nitems, bytes_after;
    unsigned char *data = NULL;
    Window destination = None;
    XEvent event;
    gint status;

    if (!desktop_x11_supports_display(gdisplay))
        return FALSE;

    display = gdk_x11_display_get_xdisplay(gdisplay);
    window_atom = XInternAtom(display, ROX_DESKTOP_WINDOW_ATOM, False);
    refresh_atom = XInternAtom(display, ROX_DESKTOP_REFRESH_ATOM, False);
    status = XGetWindowProperty(display, DefaultRootWindow(display),
        window_atom, 0, 1, False, XA_WINDOW, &actual_type, &actual_format,
        &nitems, &bytes_after, &data);
    if (status != Success || !data || actual_type != XA_WINDOW ||
        actual_format != 32 || nitems < 1) {
        ROX_LOG_DEBUG("x11", "no existing desktop registration found");
        if (data)
            XFree(data);
        return FALSE;
    }

    destination = *(Window *)data;
    XFree(data);
    memset(&event, 0, sizeof(event));
    event.xclient.type = ClientMessage;
    event.xclient.display = display;
    event.xclient.window = destination;
    event.xclient.message_type = refresh_atom;
    event.xclient.format = 32;
    event.xclient.data.l[0] = CurrentTime;

    /* Una propiedad huérfana no debe terminar el cliente de actualización. */
    rox_x11_error_trap_push();
    status = XSendEvent(display, destination, False, NoEventMask, &event);
    XSync(display, False);
    if (rox_x11_error_trap_pop() != 0)
        status = 0;
    ROX_LOG_INFO("x11", "refresh request destination=0x%lx result=%d",
                 (unsigned long)destination, status != 0);
    return status != 0;
}

const DesktopBackend *desktop_x11_backend_get(void)
{
    static const DesktopBackend backend = {
        "x11",
        desktop_x11_supports_display,
        desktop_x11_prepare_display,
        desktop_x11_configure_window,
        desktop_x11_register_control,
        desktop_x11_unregister_control,
        desktop_x11_lower_window,
        desktop_x11_send_refresh_request
    };

    return &backend;
}
