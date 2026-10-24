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
#include <cairo/cairo-xlib.h>
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
    /* 2.13.0-10: no combinar DESKTOP con _NET_WM_STATE_BELOW. PCManFM
     * demuestra que el tipo DESKTOP + un lower inicial es suficiente y evita
     * que JWM/XLibre reordene widgets pseudo-transparentes al hacer clic. */
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

/* 2.13.0-10: pixmap raíz nativo para pseudo-transparencia X11.
 *
 * Conky, sClock, wbar y muchos widgets X11 tradicionales no ven el contenido
 * dibujado dentro de una GtkWindow DESKTOP: consultan _XROOTMAP_ID o
 * _XROOTPMAP_ID y copian píxeles del fondo de la root window. Mantener aquí
 * el mismo wallpaper que ROX dibuja en desktop.c hace que esa transparencia
 * funcione sin feh/hsetroot/xwallpaper. */
static Display *root_wallpaper_display;
static Window root_wallpaper_window;
static Pixmap root_wallpaper_pixmap = None;
static gint root_wallpaper_width;
static gint root_wallpaper_height;

static gboolean desktop_x11_mode_is(const gchar *mode, const gchar *name)
{
    return g_strcmp0(mode ? mode : "fill", name) == 0;
}

static void desktop_x11_draw_scaled(cairo_t *cr, GdkPixbuf *pixbuf,
                                    const GdkRectangle *rect,
                                    gdouble scale_x, gdouble scale_y,
                                    gboolean center)
{
    gint src_w = gdk_pixbuf_get_width(pixbuf);
    gint src_h = gdk_pixbuf_get_height(pixbuf);
    gint width = MAX(1, (gint)(src_w * scale_x + 0.5));
    gint height = MAX(1, (gint)(src_h * scale_y + 0.5));
    gint x = center ? rect->x + (rect->width - width) / 2 : rect->x;
    gint y = center ? rect->y + (rect->height - height) / 2 : rect->y;
    GdkPixbuf *scaled;

    scaled = gdk_pixbuf_scale_simple(pixbuf, width, height,
                                     GDK_INTERP_BILINEAR);
    if (!scaled)
        return;
    gdk_cairo_set_source_pixbuf(cr, scaled, x, y);
    cairo_paint(cr);
    g_object_unref(scaled);
}

static void desktop_x11_draw_wallpaper_rect(cairo_t *cr, GdkPixbuf *pixbuf,
                                             const gchar *mode,
                                             const GdkRectangle *rect)
{
    gint src_w;
    gint src_h;

    if (!pixbuf || !rect || rect->width <= 0 || rect->height <= 0)
        return;
    src_w = gdk_pixbuf_get_width(pixbuf);
    src_h = gdk_pixbuf_get_height(pixbuf);
    if (src_w <= 0 || src_h <= 0)
        return;

    cairo_save(cr);
    cairo_rectangle(cr, rect->x, rect->y, rect->width, rect->height);
    cairo_clip(cr);

    if (desktop_x11_mode_is(mode, "fit")) {
        gdouble scale = MIN((gdouble)rect->width / src_w,
                            (gdouble)rect->height / src_h);
        desktop_x11_draw_scaled(cr, pixbuf, rect, scale, scale, TRUE);
    } else if (desktop_x11_mode_is(mode, "stretch")) {
        desktop_x11_draw_scaled(cr, pixbuf, rect,
            (gdouble)rect->width / src_w,
            (gdouble)rect->height / src_h, FALSE);
    } else if (desktop_x11_mode_is(mode, "center")) {
        gdk_cairo_set_source_pixbuf(cr, pixbuf,
            rect->x + (rect->width - src_w) / 2,
            rect->y + (rect->height - src_h) / 2);
        cairo_paint(cr);
    } else if (desktop_x11_mode_is(mode, "tile")) {
        gint x, y;
        for (y = rect->y; y < rect->y + rect->height; y += src_h)
            for (x = rect->x; x < rect->x + rect->width; x += src_w) {
                gdk_cairo_set_source_pixbuf(cr, pixbuf, x, y);
                cairo_paint(cr);
            }
    } else {
        gdouble scale = MAX((gdouble)rect->width / src_w,
                            (gdouble)rect->height / src_h);
        desktop_x11_draw_scaled(cr, pixbuf, rect, scale, scale, TRUE);
    }

    cairo_restore(cr);
}

static Pixmap desktop_x11_root_pixmap(Display *display, Window root,
                                      gint screen_num, gint width, gint height)
{
    Pixmap new_pixmap;

    if (root_wallpaper_display == display &&
        root_wallpaper_window == root &&
        root_wallpaper_pixmap != None &&
        root_wallpaper_width == width &&
        root_wallpaper_height == height)
        return root_wallpaper_pixmap;

    new_pixmap = XCreatePixmap(display, root,
                               (unsigned int)width,
                               (unsigned int)height,
                               (unsigned int)DefaultDepth(display,
                                                          screen_num));
    if (new_pixmap == None)
        return None;

    /* Los cambios de geometría son raros. No liberar aquí el pixmap anterior:
     * un cliente pseudo-transparente puede haber cacheado su XID hasta recibir
     * el siguiente evento de fondo. El servidor lo recupera al cerrar ROX. */
    root_wallpaper_pixmap = new_pixmap;
    root_wallpaper_display = display;
    root_wallpaper_window = root;
    root_wallpaper_width = width;
    root_wallpaper_height = height;
    return root_wallpaper_pixmap;
}

static void desktop_x11_sync_wallpaper(GdkDisplay *gdisplay,
                                       GdkPixbuf *pixbuf,
                                       const gchar *mode)
{
    Display *display;
    Window root;
    Pixmap pixmap;
    cairo_surface_t *surface;
    cairo_t *cr;
    Atom rootmap_atom;
    Atom rootpmap_atom;
    gint screen_num;
    gint width;
    gint height;
    gint count;
    gint i;

    if (!desktop_x11_supports_display(gdisplay))
        return;

    display = gdk_x11_display_get_xdisplay(gdisplay);
    screen_num = DefaultScreen(display);
    root = RootWindow(display, screen_num);
    width = DisplayWidth(display, screen_num);
    height = DisplayHeight(display, screen_num);
    if (width <= 0 || height <= 0)
        return;

    pixmap = desktop_x11_root_pixmap(display, root, screen_num, width, height);
    if (pixmap == None) {
        ROX_LOG_WARNING("x11", "unable to create root wallpaper pixmap %dx%d",
                        width, height);
        return;
    }

    surface = cairo_xlib_surface_create(display, pixmap,
                                        DefaultVisual(display, screen_num),
                                        width, height);
    if (!surface || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        if (surface)
            cairo_surface_destroy(surface);
        ROX_LOG_WARNING("x11", "unable to create cairo root wallpaper surface");
        return;
    }

    cr = cairo_create(surface);
    /* Igual que desktop_draw(): fondo neutro detrás de FIT/CENTER/alpha. */
    cairo_set_source_rgb(cr, 0.12, 0.14, 0.18);
    cairo_paint(cr);

    count = gdk_display_get_n_monitors(gdisplay);
    if (count <= 0) {
        GdkRectangle rect = {0, 0, width, height};
        desktop_x11_draw_wallpaper_rect(cr, pixbuf, mode, &rect);
    } else {
        for (i = 0; i < count; i++) {
            GdkMonitor *monitor = gdk_display_get_monitor(gdisplay, i);
            GdkRectangle rect;
            if (!monitor)
                continue;
            gdk_monitor_get_geometry(monitor, &rect);
            desktop_x11_draw_wallpaper_rect(cr, pixbuf, mode, &rect);
        }
    }

    cairo_destroy(cr);
    cairo_surface_flush(surface);
    cairo_surface_destroy(surface);

    rootmap_atom = XInternAtom(display, "_XROOTMAP_ID", False);
    rootpmap_atom = XInternAtom(display, "_XROOTPMAP_ID", False);

    XGrabServer(display);
    XChangeProperty(display, root, rootmap_atom, XA_PIXMAP, 32,
                    PropModeReplace, (const unsigned char *)&pixmap, 1);
    XChangeProperty(display, root, rootpmap_atom, XA_PIXMAP, 32,
                    PropModeReplace, (const unsigned char *)&pixmap, 1);
    XSetWindowBackgroundPixmap(display, root, pixmap);
    XClearWindow(display, root);
    XUngrabServer(display);
    XFlush(display);

    ROX_LOG_INFO("x11",
                 "root wallpaper published pixmap=0x%lx size=%dx%d mode=%s monitors=%d",
                 (unsigned long)pixmap, width, height,
                 mode ? mode : "fill", count);
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
        desktop_x11_sync_wallpaper,
        desktop_x11_send_refresh_request
    };

    return &backend;
}
