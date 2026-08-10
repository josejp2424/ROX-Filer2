/*
 * Modificado por josejp2424 (2026):
 * port a GTK3 y adaptaciones específicas de esta versión.
 */

#pragma once

/* Shared GTK3 helpers used by the ROX-Filer port.
 *
 * This file contains only ROX-owned helpers and public GTK3/GDK3 accessors.
 * It must not reintroduce private GTK structures or legacy GDK drawing APIs.
 * Modificado por josejp2424
 */

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <cairo.h>

/* GTK3-only helpers; no removed toolkit types or private-structure shims. */


#ifndef ROX_USING_GTK3
#define ROX_USING_GTK3 1
#endif

/* ROX panel compatibility remains X11-specific.  GTK3 no
 * longer exposes the old global gdk_display variable, so obtain Xlib's
 * Display through the public GDK-X11 backend API. */
static inline Display *rox_x11_display(void)
{
    GdkDisplay *display = gdk_display_get_default();
    return display && GDK_IS_X11_DISPLAY(display)
        ? gdk_x11_display_get_xdisplay(display) : NULL;
}

/* GTK3 display helpers replacing the old process-global GDK calls. */
static inline void rox_display_flush(void)
{
    GdkDisplay *display = gdk_display_get_default();
    if (display)
        gdk_display_flush(display);
}

static inline void rox_x11_error_trap_push(void)
{
    GdkDisplay *display = gdk_display_get_default();
    if (display && GDK_IS_X11_DISPLAY(display))
        gdk_x11_display_error_trap_push(display);
}

static inline gint rox_x11_error_trap_pop(void)
{
    GdkDisplay *display = gdk_display_get_default();
    return (display && GDK_IS_X11_DISPLAY(display))
        ? gdk_x11_display_error_trap_pop(display)
        : Success;
}

/* GLib/GIO watcher used for ROX child-process pipes.
 * Modificado por josejp2424 */
/* Agregado por josejp2424: adaptador GIO para reemplazar los watchers GTK2
 * y conservar los mensajes pendientes cuando llegan G_IO_IN y G_IO_HUP juntos. */
typedef GIOCondition RoxInputCondition;

typedef void (*RoxInputFunction)(gpointer data, gint source, RoxInputCondition condition);

#ifndef ROX_INPUT_READ
#define ROX_INPUT_READ  ((RoxInputCondition)G_IO_IN)
#endif
#ifndef ROX_INPUT_WRITE
#define ROX_INPUT_WRITE ((RoxInputCondition)G_IO_OUT)
#endif
#ifndef ROX_INPUT_EXCEPTION
#define ROX_INPUT_EXCEPTION ((RoxInputCondition)G_IO_PRI)
#endif

typedef struct _RoxInputWatch {
    GIOChannel *ch;
    gint fd;
    RoxInputFunction func;
    gpointer data;
    GDestroyNotify destroy;
} RoxInputWatch;

static inline gboolean rox_input_trampoline(GIOChannel *source, GIOCondition cond, gpointer user_data)
{
    RoxInputWatch *w = (RoxInputWatch *) user_data;

    (void) source;

    if (w && w->func)
        w->func(w->data, w->fd, (RoxInputCondition) cond);

    /* A pipe can report G_IO_IN and G_IO_HUP together when the child has
     * already closed its write end but framed messages are still buffered.
     * ROX callbacks consume one framed message per dispatch.  Removing the
     * watch immediately on HUP would therefore discard the remaining frames,
     * including the final operation-complete message, leaving action dialogs
     * open forever even though copy/delete had finished.
     *
     * Keep the watch while readable data remains.  Once the buffered messages
     * have been drained, GLib dispatches HUP/ERR/NVAL without G_IO_IN; the
     * callback then handles EOF and the watch can be removed safely. */
    if (cond & G_IO_IN)
        return TRUE;

    if (cond & (G_IO_HUP | G_IO_ERR | G_IO_NVAL))
        return FALSE;

    return TRUE;
}

static inline void rox_input_watch_free(gpointer user_data)
{
    RoxInputWatch *w = (RoxInputWatch *)user_data;
    if (!w) return;
    if (w->destroy)
        w->destroy(w->data);
    if (w->ch)
        g_io_channel_unref(w->ch);
    g_free(w);
}

static inline guint rox_input_add_full(gint source, RoxInputCondition condition,
                                      RoxInputFunction function, gpointer data,
                                      GDestroyNotify destroy)
{
    RoxInputWatch *w = g_new0(RoxInputWatch, 1);
    w->fd = source;
    w->func = function;
    w->data = data;
    w->destroy = destroy;
    w->ch = g_io_channel_unix_new(source);
    /* ROX closes fds itself; don't close on unref. */
    g_io_channel_set_close_on_unref(w->ch, FALSE);
    /* HUP/ERR/NVAL are required for reliable child completion on GTK3. */
    condition = (RoxInputCondition)(condition | G_IO_HUP | G_IO_ERR | G_IO_NVAL);
    return g_io_add_watch_full(w->ch, G_PRIORITY_DEFAULT, (GIOCondition)condition,
                               rox_input_trampoline, w, rox_input_watch_free);
}

static inline guint rox_input_add(gint source, RoxInputCondition condition,
                                 RoxInputFunction function, gpointer data)
{
    return rox_input_add_full(source, condition, function, data, NULL);
}

static inline void rox_input_remove(guint tag)
{
    g_source_remove(tag);
}

/* Active widgets draw directly with Cairo; there is no drawing emulation. */

/* Public GTK3 pointer query replacing deprecated gdk_window_get_pointer(). */
static inline GdkWindow *rox_gdk_window_get_pointer(GdkWindow *window,
        gint *x, gint *y, GdkModifierType *mask)
{
    GdkDisplay *display;
    GdkSeat *seat;
    GdkDevice *pointer;

    if (!window)
        window = gdk_get_default_root_window();
    if (!window)
        return NULL;

    display = gdk_window_get_display(window);
    seat = gdk_display_get_default_seat(display);
    pointer = seat ? gdk_seat_get_pointer(seat) : NULL;
    if (!pointer)
        return NULL;

    return gdk_window_get_device_position(window, pointer, x, y, mask);
}

/* Symmetric padding implemented with GTK3 widget margins. */
static inline void rox_widget_set_padding(GtkWidget *widget, gint horizontal, gint vertical)
{
    gtk_widget_set_margin_start(widget, horizontal);
    gtk_widget_set_margin_end(widget, horizontal);
    gtk_widget_set_margin_top(widget, vertical);
    gtk_widget_set_margin_bottom(widget, vertical);
}

/* Public GTK3 accessors used while the custom ROX widgets are migrated. */
static inline GdkWindow *rox_widget_get_window(GtkWidget *widget)
{
    return widget ? gtk_widget_get_window(widget) : NULL;
}

static inline GtkAllocation rox_widget_get_allocation(GtkWidget *widget)
{
    GtkAllocation allocation = {0, 0, 0, 0};
    if (widget)
        gtk_widget_get_allocation(widget, &allocation);
    return allocation;
}

static inline gint rox_widget_get_x(GtkWidget *widget)
{
    return rox_widget_get_allocation(widget).x;
}

static inline gint rox_widget_get_y(GtkWidget *widget)
{
    return rox_widget_get_allocation(widget).y;
}

static inline gint rox_widget_get_width(GtkWidget *widget)
{
    return rox_widget_get_allocation(widget).width;
}

static inline gint rox_widget_get_height(GtkWidget *widget)
{
    return rox_widget_get_allocation(widget).height;
}

static inline GtkRequisition rox_widget_get_natural_size(GtkWidget *widget)
{
    GtkRequisition natural = {0, 0};
    if (widget)
        gtk_widget_get_preferred_size(widget, NULL, &natural);
    return natural;
}

/* ROX-owned declarative menu table API implemented in rox_itemfactory.c. */
#ifndef ROX_ITEM_FACTORY_DEFINED
#define ROX_ITEM_FACTORY_DEFINED 1

typedef void (*RoxItemFactoryCallback)(gpointer callback_data,
                                      guint callback_action,
                                      GtkWidget *widget);

typedef struct _RoxItemFactoryEntry {
    gchar *path;                    /* owned by caller for translated tables */
    const gchar *accelerator;       /* borrowed */
    RoxItemFactoryCallback callback;/* borrowed */
    guint callback_action;
    const gchar *item_type;         /* borrowed */
    gpointer extra_data;            /* borrowed */
} RoxItemFactoryEntry;

typedef struct _RoxItemFactory RoxItemFactory;

RoxItemFactory *rox_item_factory_new(GType container_type,
                                     const gchar *path,
                                     GtkAccelGroup *accel_group);

void rox_item_factory_create_items(RoxItemFactory *ifactory,
                                  guint n_entries,
                                  RoxItemFactoryEntry *entries,
                                  gpointer callback_data);

GtkWidget *rox_item_factory_get_widget(RoxItemFactory *ifactory,
                                       const gchar *path);

void rox_item_factory_free(RoxItemFactory *ifactory);

#endif /* ROX_ITEM_FACTORY_DEFINED */
