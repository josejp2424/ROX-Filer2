/*
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2006, Thomas Leonard and others (see changelog for details).
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* gui_support.c - general (GUI) support routines */

/*
 * Modificado por josejp2424 (2026):
 * port a GTK3 y adaptaciones específicas de esta versión.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <gdk/gdkx.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>

#include "global.h"

#include "main.h"
#include "gui_support.h"
#include "debug_log.h"
#include "support.h"

/* EWMH _NET_WM_STATE client message actions.
 * These constants are defined by the EWMH spec, but are not provided by X11 headers.
 */
#ifndef _NET_WM_STATE_REMOVE
#define _NET_WM_STATE_REMOVE 0
#endif
#ifndef _NET_WM_STATE_ADD
#define _NET_WM_STATE_ADD    1
#endif
#ifndef _NET_WM_STATE_TOGGLE
#define _NET_WM_STATE_TOGGLE 2
#endif

#include "pixmaps.h"
#include "choices.h"
#include "options.h"
#include "run.h"

gint	screen_width, screen_height;

gint		n_monitors;
GdkRectangle	*monitor_geom = NULL;
gint		monitor_width, monitor_height;
MonitorAdjacent *monitor_adjacent;

static GdkAtom xa_cardinal;
GdkAtom xa__NET_WORKAREA = GDK_NONE;
GdkAtom xa__NET_WM_DESKTOP = GDK_NONE;
GdkAtom xa__NET_CURRENT_DESKTOP = GDK_NONE;
GdkAtom xa__NET_NUMBER_OF_DESKTOPS = GDK_NONE;

static GtkWidget *current_dialog = NULL;

static GtkWidget *tip_widget = NULL;
static time_t tip_time = 0; 	/* Time tip widget last closed */
static gint tip_timeout = 0;	/* When primed */

#define ROX_STANDARD_MIN_WIDTH  640
#define ROX_STANDARD_MIN_HEIGHT 400
#define ROX_WINDOW_EDGE_MARGIN   16

/* Agregado por josejp2424 (2026): centrar todas las ventanas normales dentro
 * del área útil real del monitor. gtk_window_set_position() no siempre respeta
 * _NET_WORKAREA cuando la ventana padre es el escritorio a pantalla completa;
 * por eso el movimiento final se realiza una vez que la ventana ya fue mapeada.
 * Paneles, escritorio, menús, tooltips y superficies DND quedan excluidos. */
static gboolean rox_window_is_position_exempt(GtkWindow *window)
{
	GdkWindowTypeHint type_hint;

	if (!window || gtk_window_get_window_type(window) != GTK_WINDOW_TOPLEVEL)
		return TRUE;
	if (g_object_get_data(G_OBJECT(window), "rox-paired-window"))
		return TRUE;

	type_hint = gtk_window_get_type_hint(window);
	return type_hint == GDK_WINDOW_TYPE_HINT_DESKTOP ||
		type_hint == GDK_WINDOW_TYPE_HINT_DOCK ||
		type_hint == GDK_WINDOW_TYPE_HINT_MENU ||
		type_hint == GDK_WINDOW_TYPE_HINT_DROPDOWN_MENU ||
		type_hint == GDK_WINDOW_TYPE_HINT_POPUP_MENU ||
		type_hint == GDK_WINDOW_TYPE_HINT_TOOLTIP ||
		type_hint == GDK_WINDOW_TYPE_HINT_DND ||
		type_hint == GDK_WINDOW_TYPE_HINT_COMBO;
}

static GdkMonitor *rox_window_target_monitor(GtkWindow *window)
{
	GdkDisplay *display;
	GtkWindow *parent;
	GdkWindow *gdk_window;
	GdkMonitor *monitor = NULL;

	display = gtk_widget_get_display(GTK_WIDGET(window));
	if (!display)
		return NULL;

	parent = gtk_window_get_transient_for(window);
	if (parent)
	{
		gdk_window = gtk_widget_get_window(GTK_WIDGET(parent));
		if (gdk_window)
			monitor = gdk_display_get_monitor_at_window(display, gdk_window);
	}

	if (!monitor)
	{
		gdk_window = gtk_widget_get_window(GTK_WIDGET(window));
		if (gdk_window)
			monitor = gdk_display_get_monitor_at_window(display, gdk_window);
	}

	if (!monitor)
		monitor = gdk_display_get_primary_monitor(display);
	if (!monitor && gdk_display_get_n_monitors(display) > 0)
		monitor = gdk_display_get_monitor(display, 0);

	return monitor;
}

static gboolean rox_window_center_idle(gpointer data)
{
	GtkWindow *window = GTK_WINDOW(data);
	GtkWidget *widget = GTK_WIDGET(window);
	GdkMonitor *monitor;
	GdkRectangle workarea;
	gint width = 1, height = 1;
	gint max_width, max_height;
	gint x, y;

	g_object_set_data(G_OBJECT(window), "rox-center-id", NULL);
	if (gtk_widget_in_destruction(widget) || !gtk_widget_get_mapped(widget) ||
	    rox_window_is_position_exempt(window))
		return G_SOURCE_REMOVE;

	monitor = rox_window_target_monitor(window);
	if (!monitor)
		return G_SOURCE_REMOVE;

	gdk_monitor_get_workarea(monitor, &workarea);
	gtk_window_get_size(window, &width, &height);

	max_width = MAX(1, workarea.width - ROX_WINDOW_EDGE_MARGIN * 2);
	max_height = MAX(1, workarea.height - ROX_WINDOW_EDGE_MARGIN * 2);
	if (width > max_width || height > max_height)
	{
		width = MIN(width, max_width);
		height = MIN(height, max_height);
		gtk_window_resize(window, width, height);
	}

	x = workarea.x + MAX(0, (workarea.width - width) / 2);
	y = workarea.y + MAX(0, (workarea.height - height) / 2);
	gtk_window_move(window, x, y);

	return G_SOURCE_REMOVE;
}

static void rox_schedule_window_center(GtkWindow *window)
{
	guint source_id;

	if (g_object_get_data(G_OBJECT(window), "rox-center-id"))
		return;

	source_id = g_idle_add_full(G_PRIORITY_HIGH_IDLE,
		rox_window_center_idle, g_object_ref(window), g_object_unref);
	g_object_set_data(G_OBJECT(window), "rox-center-id",
		GUINT_TO_POINTER(source_id));
}

/* Todas las ventanas normales se centran después del map. El mínimo 640x400
 * se mantiene sólo para ventanas principales; GtkDialog conserva su tamaño
 * natural para que confirmaciones pequeñas no ocupen media pantalla ni oculten
 * sus botones detrás del panel. */
static gboolean standard_window_map_hook(GSignalInvocationHint *hint,
		guint n_param_values, const GValue *param_values, gpointer data)
{
	GtkWidget *widget;
	GtkWindow *window;
	GdkWindowTypeHint type_hint;
	GdkGeometry geometry;
	gint width;
	gint height;
	gboolean size_exempt;

	(void) hint;
	(void) data;
	if (n_param_values < 1)
		return TRUE;

	widget = g_value_get_object(&param_values[0]);

	/* Modificado por josejp2424 (2026): GtkToolbar crea su menú de
	 * desbordamiento internamente, por lo que no pasa por rox_menu_new().
	 * Aplicar aquí las clases cuadradas a todos los GtkMenu y GtkPopover
	 * garantiza el mismo aspecto para menús propios, menús automáticos,
	 * submenús, combos y paneles emergentes. */
	if (GTK_IS_MENU(widget))
	{
		gtk_style_context_add_class(gtk_widget_get_style_context(widget),
			"rox-square-menu");
		return TRUE;
	}
	if (GTK_IS_POPOVER(widget))
	{
		gtk_style_context_add_class(gtk_widget_get_style_context(widget),
			"rox-square-popover");
		return TRUE;
	}
	if (!GTK_IS_WINDOW(widget))
		return TRUE;

	window = GTK_WINDOW(widget);
	if (GTK_IS_DIALOG(widget))
		gtk_style_context_add_class(gtk_widget_get_style_context(widget),
			"rox-square-dialog");
	if (rox_window_is_position_exempt(window))
		return TRUE;

	rox_schedule_window_center(window);

	type_hint = gtk_window_get_type_hint(window);
	size_exempt = GTK_IS_DIALOG(widget) ||
		type_hint == GDK_WINDOW_TYPE_HINT_DIALOG ||
		type_hint == GDK_WINDOW_TYPE_HINT_UTILITY ||
		g_object_get_data(G_OBJECT(window), "rox-standard-size-exempt") != NULL;
	if (size_exempt)
		return TRUE;

	memset(&geometry, 0, sizeof(geometry));
	geometry.min_width = ROX_STANDARD_MIN_WIDTH;
	geometry.min_height = ROX_STANDARD_MIN_HEIGHT;
	gtk_window_set_geometry_hints(window, NULL, &geometry, GDK_HINT_MIN_SIZE);

	gtk_window_get_size(window, &width, &height);
	if (width < ROX_STANDARD_MIN_WIDTH || height < ROX_STANDARD_MIN_HEIGHT)
		gtk_window_resize(window, MAX(width, ROX_STANDARD_MIN_WIDTH),
			MAX(height, ROX_STANDARD_MIN_HEIGHT));

	return TRUE;
}

/* Static prototypes */
static void run_error_info_dialog(GtkMessageType type, const char *message,
				  va_list args);
static void gui_get_monitor_adjacent(int monitor, MonitorAdjacent *adj);
static gint gui_monitor_at_point(gint x, gint y)
{
	GdkDisplay *display = gdk_display_get_default();
	GdkMonitor *target;
	gint i, count;

	if (!display)
		return 0;
	target = gdk_display_get_monitor_at_point(display, x, y);
	count = gdk_display_get_n_monitors(display);
	for (i = 0; i < count; i++)
		if (gdk_display_get_monitor(display, i) == target)
			return i;
	return 0;
}

typedef struct {
	gchar *name;
	gchar *class_name;
} RoxWmClass;

static void rox_wm_class_free(gpointer data, GClosure *closure)
{
	RoxWmClass *wm_class = data;
	(void) closure;
	g_free(wm_class->name);
	g_free(wm_class->class_name);
	g_free(wm_class);
}

static void rox_window_apply_wmclass(GtkWidget *widget, gpointer data)
{
	RoxWmClass *wm_class = data;
	GdkWindow *window = gtk_widget_get_window(widget);

	if (window && GDK_IS_X11_WINDOW(window))
	{
		XClassHint hint;
		hint.res_name = wm_class->name;
		hint.res_class = wm_class->class_name;
		XSetClassHint(gdk_x11_display_get_xdisplay(
			gdk_window_get_display(window)),
			gdk_x11_window_get_xid(window), &hint);
	}
}

void rox_window_set_wmclass(GtkWindow *window, const gchar *name,
		const gchar *class_name)
{
	RoxWmClass *wm_class;

	g_return_if_fail(GTK_IS_WINDOW(window));
	wm_class = g_new0(RoxWmClass, 1);
	wm_class->name = g_strdup(name ? name : PROJECT);
	wm_class->class_name = g_strdup(class_name ? class_name : PROJECT);

	if (gtk_widget_get_realized(GTK_WIDGET(window)))
	{
		rox_window_apply_wmclass(GTK_WIDGET(window), wm_class);
		rox_wm_class_free(wm_class, NULL);
	}
	else
	{
		g_signal_connect_data(window, "realize",
			G_CALLBACK(rox_window_apply_wmclass), wm_class,
			rox_wm_class_free, 0);
	}
}

void gui_store_screen_geometry(GdkScreen *screen)
{
	GdkDisplay *display;
	GdkRectangle bounds = {0, 0, 0, 0};
	gint mon;

	g_return_if_fail(GDK_IS_SCREEN(screen));
	display = gdk_screen_get_display(screen);
	n_monitors = gdk_display_get_n_monitors(display);
	if (n_monitors < 1)
		n_monitors = 1;

	g_clear_pointer(&monitor_adjacent, g_free);
	g_clear_pointer(&monitor_geom, g_free);
	monitor_geom = g_new0(GdkRectangle, n_monitors);
	monitor_adjacent = g_new0(MonitorAdjacent, n_monitors);
	monitor_width = monitor_height = G_MAXINT;

	for (mon = 0; mon < n_monitors; ++mon)
	{
		GdkMonitor *monitor = gdk_display_get_monitor(display, mon);
		GdkRectangle geometry = {0, 0, 1, 1};

		if (monitor)
			gdk_monitor_get_geometry(monitor, &geometry);
		monitor_geom[mon] = geometry;
		monitor_width = MIN(monitor_width, geometry.width);
		monitor_height = MIN(monitor_height, geometry.height);

		if (mon == 0)
			bounds = geometry;
		else
		{
			gint left = MIN(bounds.x, geometry.x);
			gint top = MIN(bounds.y, geometry.y);
			gint right = MAX(bounds.x + bounds.width,
				geometry.x + geometry.width);
			gint bottom = MAX(bounds.y + bounds.height,
				geometry.y + geometry.height);
			bounds = (GdkRectangle) {left, top, right - left, bottom - top};
		}
	}

	screen_width = MAX(1, bounds.width);
	screen_height = MAX(1, bounds.height);
	for (mon = 0; mon < n_monitors; ++mon)
		gui_get_monitor_adjacent(mon, &monitor_adjacent[mon]);
}

void gui_support_init()
{
	gpointer klass;

	xa_cardinal = gdk_atom_intern("CARDINAL", FALSE);
        xa__NET_WORKAREA = gdk_atom_intern("_NET_WORKAREA", FALSE);
        xa__NET_WM_DESKTOP = gdk_atom_intern("_NET_WM_DESKTOP", FALSE);
        xa__NET_CURRENT_DESKTOP = gdk_atom_intern("_NET_CURRENT_DESKTOP",
                                                  FALSE);
        xa__NET_NUMBER_OF_DESKTOPS = gdk_atom_intern("_NET_NUMBER_OF_DESKTOPS",
                                                     FALSE);

	gui_store_screen_geometry(gdk_screen_get_default());

	/* Modificado por josejp2424 (2026): menus and normal dialog windows use
	 * square corners.  Only geometry is overridden; the active GTK3 theme
	 * still supplies colours, fonts, spacing, borders and selection states.
	 * Using ordinary opaque GTK windows also prevents black corner artefacts
	 * on X11/XLibre sessions without compositing. */
	{
		static GtkCssProvider *square_provider = NULL;
		GdkScreen *screen = gdk_screen_get_default();

		if (!square_provider)
		{
			square_provider = gtk_css_provider_new();
			gtk_css_provider_load_from_data(square_provider,
				/* Todos los GtkMenu, incluso el overflow generado dentro de
				 * GtkToolbar, deben ser cuadrados. Se incluyen selectores por
				 * nodo y por clase para cubrir temas GTK3 antiguos y nuevos. */
				"menu, .menu, menu.rox-square-menu, .rox-square-menu, "
				"menu menuitem, .menu menuitem, .rox-square-menu menuitem { "
				"border-radius: 0; box-shadow: none; } "
				/* GtkPopover se usa en Particiones y algunos controles GTK. */
				"popover, popover.background, .popover, "
				"popover.rox-square-popover, .rox-square-popover { "
				"border-radius: 0; box-shadow: none; } "
				/* Los tooltips y ventanas popup auxiliares tampoco conservan
				 * esquinas redondeadas fuera del menú principal. */
				"tooltip, tooltip.background, window.popup { "
				"border-radius: 0; box-shadow: none; } "
				"window.dialog, window.message-dialog, window.rox-square-dialog, "
				"window.dialog decoration, window.message-dialog decoration, "
				"window.rox-square-dialog decoration { "
				"border-radius: 0; box-shadow: none; }",
				-1, NULL);
			if (screen)
				gtk_style_context_add_provider_for_screen(screen,
					GTK_STYLE_PROVIDER(square_provider),
					GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		}
	}

	/* Agregado por josejp2424: centrar globalmente todas las ventanas
	 * normales y aplicar 640x400 sólo a las ventanas principales. */
	{
		guint map_signal = g_signal_lookup("map", GTK_TYPE_WIDGET);
		if (map_signal)
			g_signal_add_emission_hook(map_signal, 0,
				standard_window_map_hook, NULL, NULL);
	}

	/* Work around the scrollbar placement bug */
	klass = g_type_class_ref(gtk_scrolled_window_get_type());
	((GtkScrolledWindowClass *) klass)->scrollbar_spacing = 0;
	/* (don't unref, ever) */
}

/* Open a modal dialog box showing a message.
 * The user can choose from a selection of buttons at the bottom.
 * Returns -1 if the window is destroyed, or the number of the button
 * if one is clicked (starting from zero).
 *
 * If a dialog is already open, returns -1 without waiting AND
 * brings the current dialog to the front.
 *
 * Each button has two arguments, a GTK_STOCK icon and some text. If the
 * text is NULL, the stock's text is used.
 */
int get_choice(const char *title,
	       const char *message,
	       int number_of_buttons, ...)
{
	GtkWidget	*dialog;
	GtkWidget	*button = NULL;
	int		i, retval;
	va_list	ap;

	if (current_dialog)
	{
		gtk_widget_hide(current_dialog);
		gtk_widget_show(current_dialog);
		return -1;
	}

	current_dialog = dialog = gtk_message_dialog_new(NULL,
					GTK_DIALOG_MODAL,
					GTK_MESSAGE_QUESTION,
					GTK_BUTTONS_NONE,
					"%s", message);
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
	/* Modificado por josejp2424 (2026): los cuadros de confirmación deben
	 * conservar el tamaño compacto tradicional de ROX y no heredar el mínimo
	 * global de 640x400 reservado para ventanas de trabajo. */
	g_object_set_data(G_OBJECT(dialog), "rox-standard-size-exempt",
		GINT_TO_POINTER(1));

	va_start(ap, number_of_buttons);

	for (i = 0; i < number_of_buttons; i++)
	{
		const char *stock = va_arg(ap, char *);
		const char *text = va_arg(ap, char *);

		if (text)
			button = button_new_mixed(stock, text);
		else
			button = button_new_icon(stock);

		gtk_widget_set_can_default(button, TRUE); /* Modificado por josejp2424: GTK3 */
		gtk_widget_show(button);

		gtk_dialog_add_action_widget(GTK_DIALOG(current_dialog),
						button, i);
	}

	gtk_window_set_title(GTK_WINDOW(dialog), title);
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);

	gtk_dialog_set_default_response(GTK_DIALOG(dialog), i - 1);

	va_end(ap);

	retval = gtk_dialog_run(GTK_DIALOG(dialog));
	if (retval == GTK_RESPONSE_NONE)
		retval = -1;
	gtk_widget_destroy(dialog);

	current_dialog = NULL;

	return retval;
}

void info_message(const char *message, ...)
{
        va_list args;

	va_start(args, message);

	run_error_info_dialog(GTK_MESSAGE_INFO, message, args);
}

/* Display a message in a window with "ROX-Filer" as title */
void report_error(const char *message, ...)
{
	va_list args;

	va_start(args, message);

	run_error_info_dialog(GTK_MESSAGE_ERROR, message, args);
}

void set_cardinal_property(GdkWindow *window, GdkAtom prop, gulong value)
{
	gdk_property_change(window, prop, xa_cardinal, 32,
				GDK_PROP_MODE_REPLACE, (const guchar *) &value, 1);
}

gboolean get_cardinal_property(GdkWindow *window, GdkAtom prop, gulong length,
                               gulong *data, gint *actual_length)
{
        GdkAtom actual_type;
        gint actual_format, act_length;
        guchar *d;
        gulong *p;
        int i;
        gboolean ok;

        /* Cardinals are format=32 so the length in bytes is 4 * number of
         * cardinals */
        ok=gdk_property_get(window, prop, xa_cardinal,
                                     0, length*4, FALSE,
                                     &actual_type, &actual_format,
                                     &act_length, &d);

        if(!ok)
                return FALSE;

        /* Check correct format */
        if(actual_format!=32)
        {
                g_free(d);
                return FALSE;
        }

        /* Actual data for cardinals returned as longs, which may be 64 bit */
        if(act_length/sizeof(gulong)>length)
        {
                g_free(d);
                return FALSE;
        }

        /* Copy data into return array */
        p=(gulong *) d;
        for(i=0; i<act_length/sizeof(gulong); i++)
                data[i]=p[i];
        g_free(d);
        *actual_length=act_length/sizeof(gulong);

        return ok;
}

int get_current_desktop(void)
{
        gint act_len;
        gulong current;
        GdkWindow *gdk_root = gdk_get_default_root_window();int desk=0;

        if(get_cardinal_property(gdk_root, xa__NET_CURRENT_DESKTOP, 1,
                                 &current, &act_len) && act_len==1)
                desk=(int) current;

        return desk;
}

int get_number_of_desktops(void)
{
        gint act_len;
        gulong num;
        GdkWindow *gdk_root = gdk_get_default_root_window();int desks=1;

        if(get_cardinal_property(gdk_root, xa__NET_NUMBER_OF_DESKTOPS, 1,
                                 &num, &act_len) && act_len==1)
                desks=(int) num;

        return desks;
}

/* Get the working area for the desktop, excluding things like the Gnome
 * panels. */
void get_work_area(int *x, int *y, int *width, int *height)
{
        gint act_len;
        gulong *work_area;
        GdkWindow *gdk_root = gdk_get_default_root_window();int x0, y0, w0, h0;
        int idesk, ndesk, nval;

        idesk=get_current_desktop();
        ndesk=get_number_of_desktops();
        nval=4*ndesk;
        work_area=g_new(gulong, nval);

        if(get_cardinal_property(gdk_root, xa__NET_WORKAREA, nval,
                                         work_area, &act_len) &&
           act_len==nval)
        {
                x0 = work_area[idesk*4+0];
                y0 = work_area[idesk*4+1];
                w0 = work_area[idesk*4+2];
                h0 = work_area[idesk*4+3];
        }
        else
        {
                x0 = y0 = 0;
                w0 = screen_width;
                h0 = screen_height;
        }

        g_free(work_area);

        if(x)
                *x = x0;
        if(y)
                *y = y0;
        if(width)
                *width = w0;
        if(height)
                *height = h0;
}

/* NB: Also used for pinned icons.
 * TODO: Set the level here too.
 */
void make_panel_window(GtkWidget *widget)
{
	static gboolean need_init = TRUE;
	static GdkAtom xa_state, xa_atom, xa_hints, xa_win_hints;
	GdkWindow *window = gtk_widget_get_window(widget);
	long wm_hints_values[] = {1, False, 0, 0, 0, 0, 0, 0};
	GdkAtom	wm_protocols[2];

	g_return_if_fail(window != NULL);

	if (o_override_redirect.int_value)
	{
		gdk_window_set_override_redirect(window, TRUE);
		return;
	}

	if (need_init)
	{
		xa_win_hints = gdk_atom_intern("_WIN_HINTS", FALSE);
		xa_state = gdk_atom_intern("_WIN_STATE", FALSE);
		xa_atom = gdk_atom_intern("ATOM", FALSE);
		xa_hints = gdk_atom_intern("WM_HINTS", FALSE);

		need_init = FALSE;
	}

	gdk_window_set_decorations(window, 0);
	gdk_window_set_functions(window, 0);
	gtk_window_set_resizable(GTK_WINDOW(widget), FALSE);

	/* Don't hide panel/desktop windows initially (WIN_STATE_HIDDEN).
	 * Needed for IceWM - Christopher Arndt <chris.arndt@web.de>
	 */
	set_cardinal_property(window, xa_state,
			WIN_STATE_STICKY |
			WIN_STATE_FIXED_POSITION | WIN_STATE_ARRANGE_IGNORE);

	set_cardinal_property(window, xa_win_hints,
			WIN_HINTS_SKIP_FOCUS | WIN_HINTS_SKIP_WINLIST |
			WIN_HINTS_SKIP_TASKBAR);

	/* Appear on all workspaces */
	set_cardinal_property(window, xa__NET_WM_DESKTOP, 0xffffffff);

	gdk_property_change(window, xa_hints, xa_hints, 32,
			GDK_PROP_MODE_REPLACE, (guchar *) wm_hints_values,
			sizeof(wm_hints_values) / sizeof(long));

	wm_protocols[0] = gdk_atom_intern("WM_DELETE_WINDOW", FALSE);
	wm_protocols[1] = gdk_atom_intern("_NET_WM_PING", FALSE);
	gdk_property_change(window,
			gdk_atom_intern("WM_PROTOCOLS", FALSE), xa_atom, 32,
			GDK_PROP_MODE_REPLACE, (guchar *) wm_protocols,
			sizeof(wm_protocols) / sizeof(GdkAtom));

	gdk_window_set_skip_taskbar_hint(window, TRUE);
	gdk_window_set_skip_pager_hint(window, TRUE);

	if (g_object_class_find_property(G_OBJECT_GET_CLASS(widget),
					"accept_focus"))
	{
		GValue vfalse = { 0, };
		g_value_init(&vfalse, G_TYPE_BOOLEAN);
		g_value_set_boolean(&vfalse, FALSE);
		g_object_set_property(G_OBJECT(widget),
					"accept_focus", &vfalse);
		g_value_unset(&vfalse);
	}
}

static gboolean error_idle_cb(gpointer data)
{
	char	**error = (char **) data;

	report_error("%s", *error);
	null_g_free(error);

	one_less_window();
	return FALSE;
}

/* Display an error with "ROX-Filer" as title next time we are idle.
 * If multiple errors are reported this way before the window is opened,
 * all are displayed in a single window.
 * If an error is reported while the error window is open, it is discarded.
 */
void delayed_error(const char *error, ...)
{
	static char *delayed_error_data = NULL;
	char *old, *new;
	va_list args;

	g_return_if_fail(error != NULL);

	old = delayed_error_data;

	va_start(args, error);
	new = g_strdup_vprintf(error, args);
	va_end(args);

	if (old)
	{
		delayed_error_data = g_strconcat(old,
				_("\n---\n"),
				new, NULL);
		g_free(old);
		g_free(new);
	}
	else
	{
		delayed_error_data = new;
		g_idle_add(error_idle_cb, &delayed_error_data);

		number_of_windows++;
	}
}

/* Load the file into memory. Return TRUE on success.
 * Block is zero terminated (but this is not included in the length).
 */
gboolean load_file(const char *pathname, char **data_out, long *length_out)
{
	gsize len;
	GError *error = NULL;

	if (!g_file_get_contents(pathname, data_out, &len, &error))
	{
		delayed_error("%s", error->message);
		g_error_free(error);
		return FALSE;
	}

	if (length_out)
		*length_out = len;
	return TRUE;
}

GtkWidget *new_help_button(HelpFunc show_help, gpointer data)
{
	GtkWidget	*b, *icon;

	b = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(b), GTK_RELIEF_NONE);
	icon = image_new_icon(ROX_ICON_HELP,
					GTK_ICON_SIZE_SMALL_TOOLBAR);
	gtk_container_add(GTK_CONTAINER(b), icon);
	g_signal_connect_swapped(b, "clicked", G_CALLBACK(show_help), data);

	gtk_widget_set_can_focus(b, FALSE); /* Modificado por josejp2424: GTK3 */

	return b;
}

/* Read file into memory. Call parse_line(guchar *line) for each line
 * in the file. Callback returns NULL on success, or an error message
 * if something went wrong. Only the first error is displayed to the user.
 */
void parse_file(const char *path, ParseFunc *parse_line)
{
	char		*data;
	long		length;
	gboolean	seen_error = FALSE;

	if (load_file(path, &data, &length))
	{
		char *eol;
		const char *error;
		char *line = data;
		int  line_number = 1;

		if (strncmp(data, "<?xml ", 6) == 0)
		{
			delayed_error(_("Attempt to read an XML file as "
					"a text file. File '%s' may be "
					"corrupted."), path);
			return;
		}

		while (line && *line)
		{
			eol = strchr(line, '\n');
			if (eol)
				*eol = '\0';

			error = parse_line(line);

			if (error && !seen_error)
			{
				delayed_error(
		_("Error in '%s' file at line %d: "
		"\n\"%s\"\n"
		"This may be due to upgrading from a previous version of "
		"Rox-Filer2. Open the Options window and try changing something "
		"and then changing it back (causing the file to be resaved).\n"
		"Further errors will be ignored."),
					path,
					line_number,
					error);
				seen_error = TRUE;
			}

			if (!eol)
				break;
			line = eol + 1;
			line_number++;
		}
		g_free(data);
	}
}

/* Returns the position of the pointer.
 * TRUE if any modifier keys or mouse buttons are pressed.
 */
gboolean get_pointer_xy(int *x, int *y)
{
	unsigned int mask;

	rox_gdk_window_get_pointer(NULL, x, y, &mask);

	return mask != 0;
}

int get_monitor_under_pointer(void)
{
	int x, y;

	get_pointer_xy(&x, &y);
	return gui_monitor_at_point(x, y);
}

#define DECOR_BORDER 32

/* Centre the window at these coords */
void centre_window(GdkWindow *window, int x, int y)
{
	int	w, h;
	int m;

	g_return_if_fail(window != NULL);

	m = gui_monitor_at_point(x, y);

	gdk_window_get_geometry(window, NULL, NULL, &w, &h);

	x -= w / 2;
	y -= h / 2;

	gdk_window_move(window,
		CLAMP(x, DECOR_BORDER + monitor_geom[m].x,
			monitor_geom[m].x + monitor_geom[m].width
			- w - DECOR_BORDER),
		CLAMP(y, DECOR_BORDER + monitor_geom[m].y,
			monitor_geom[m].y + monitor_geom[m].height
			- h - DECOR_BORDER));
}

static void run_error_info_dialog(GtkMessageType type, const char *message,
				  va_list args)
{
	GtkWidget *dialog;
	gchar *s;

	g_return_if_fail(message != NULL);

	s = g_strdup_vprintf(message, args);
	va_end(args);

	dialog = gtk_message_dialog_new(NULL,
					GTK_DIALOG_MODAL,
					type,
					GTK_BUTTONS_OK,
					"%s", s);
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_OK);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);

	g_free(s);
}

static GtkWidget *current_wink_widget = NULL;
static gint	wink_timeout = -1;	/* Called when it's time to stop */
static gulong	wink_destroy;		/* Called if the widget dies first */

static gboolean end_wink(gpointer data)
{
	gtk_drag_unhighlight(current_wink_widget);

	g_signal_handler_disconnect(current_wink_widget, wink_destroy);

	current_wink_widget = NULL;

	return FALSE;
}

static void cancel_wink(void)
{
	g_source_remove(wink_timeout);
	end_wink(NULL);
}

static void wink_widget_died(gpointer data)
{
	current_wink_widget = NULL;
	g_source_remove(wink_timeout);
}

/* Draw a black box around this widget, briefly.
 * Note: uses the drag highlighting code for now.
 */
void wink_widget(GtkWidget *widget)
{
	g_return_if_fail(widget != NULL);

	if (current_wink_widget)
		cancel_wink();

	current_wink_widget = widget;
	gtk_drag_highlight(current_wink_widget);

	wink_timeout = g_timeout_add(300, (GSourceFunc) end_wink, NULL);

	wink_destroy = g_signal_connect_swapped(widget, "destroy",
				G_CALLBACK(wink_widget_died), NULL);
}

static gboolean idle_destroy_cb(GtkWidget *widget)
{
	g_object_unref(widget);
	gtk_widget_destroy(widget);
	return FALSE;
}

/* Destroy the widget in an idle callback */
void destroy_on_idle(GtkWidget *widget)
{
	g_object_ref(widget);
	g_idle_add((GSourceFunc) idle_destroy_cb, widget);
}

/* Append a launch diagnostic line when ROX_DEBUG_LOG points to a file.
 * This is intentionally disabled by default and is useful for reproducing
 * MIME/terminal failures in the real desktop session. */
void rox_debug_log(const gchar *category, const gchar *format, ...)
{
	const gchar *path = g_getenv("ROX_DEBUG_LOG");
	GDateTime *now;
	gchar *stamp;
	gchar *message;
	va_list args;
	FILE *stream;

	if (!format)
		return;

	va_start(args, format);
	message = g_strdup_vprintf(format, args);
	va_end(args);
	if (!message)
		return;

	/* New r74 logger: disabled by default, rotated and shared by X11,
	 * Wayland, MIME and terminal diagnostics. */
	if (rox_debug_log_is_enabled())
	{
		rox_debug_log_message(ROX_DEBUG_LEVEL_DEBUG,
			category && *category ? category : "ROX", "%s", message);
		g_free(message);
		return;
	}

	/* Compatibility with the r72 trace helper. This path is used only when
	 * ROX_DEBUG_LOG is explicitly exported by the user. */
	if (!path || !*path)
	{
		g_free(message);
		return;
	}
	stream = fopen(path, "a");
	if (!stream)
	{
		g_free(message);
		return;
	}

	now = g_date_time_new_now_local();
	stamp = now ? g_date_time_format(now, "%Y-%m-%d %H:%M:%S.%f") : NULL;
	fprintf(stream, "[%s] pid=%ld %s: %s\n",
		stamp ? stamp : "unknown-time", (long)getpid(),
		category && *category ? category : "ROX", message);
	fclose(stream);
	g_free(stamp);
	if (now)
		g_date_time_unref(now);
	g_free(message);
}

/* Spawn a child process (as spawn_full), and report errors.
 * Returns the child's PID on succes, or 0 on failure.
 */
gint rox_spawn(const gchar *dir, const gchar **argv)
{
	GError	*error = NULL;
	gint pid = 0;

	if (!g_spawn_async_with_pipes(dir, (gchar **) argv, NULL,
			G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDOUT_TO_DEV_NULL |
			G_SPAWN_SEARCH_PATH,
			NULL, NULL,		/* Child setup fn */
			&pid,			/* Child PID */
			NULL, NULL, NULL,	/* Standard pipes */
			&error))
	{
		delayed_error("%s", error ? error->message : "(null)");
		g_error_free(error);

		return 0;
	}

	return pid;
}

/* Return the best icon name provided by the active GTK3 icon theme.
 * Historical GTK and ROX names remain accepted for compatibility. */
/* Agregado por josejp2424: resolución de iconos normales y -symbolic
 * desde el GtkIconTheme activo. */
const char *rox_icon_name(const char *icon_name)
{
	static const struct { const char *legacy; const char *modern; } aliases[] = {
		{"gtk-open", ROX_ICON_OPEN}, {"gtk-save", ROX_ICON_SAVE},
		{"gtk-cancel", ROX_ICON_CANCEL}, {"gtk-delete", ROX_ICON_DELETE},
		{"gtk-preferences", ROX_ICON_PREFERENCES},
		{"gtk-refresh", ROX_ICON_REFRESH}, {"gtk-copy", ROX_ICON_COPY},
		{"gtk-cut", ROX_ICON_CUT}, {"gtk-paste", ROX_ICON_PASTE},
		{"gtk-close", ROX_ICON_CLOSE}, {"gtk-home", ROX_ICON_HOME},
		{"gtk-help", ROX_ICON_HELP}, {"gtk-zoom-in", ROX_ICON_ZOOM_IN},
		{"gtk-zoom-out", ROX_ICON_ZOOM_OUT},
		{"gtk-execute", ROX_ICON_EXECUTE}, {"gtk-find", ROX_ICON_FIND},
		{"gtk-properties", ROX_ICON_PROPERTIES},
		{"gtk-clear", ROX_ICON_CLEAR}, {"gtk-add", ROX_ICON_ADD},
		{"gtk-remove", ROX_ICON_REMOVE}, {"gtk-go-up", ROX_ICON_GO_UP},
		{"gtk-go-down", ROX_ICON_GO_DOWN}, {"gtk-goto-last", ROX_ICON_GO_LAST},
		{"gtk-jump-to", ROX_ICON_JUMP_TO}, {"gtk-undo", ROX_ICON_UNDO},
		{"gtk-new", ROX_ICON_NEW},
		{"gtk-dialog-warning", ROX_ICON_DIALOG_WARNING},
		{"gtk-dialog-question", ROX_ICON_DIALOG_QUESTION},
		{"gtk-dialog-info", ROX_ICON_DIALOG_INFO},
		{"gtk-dnd-multiple", ROX_ICON_DND_MULTIPLE},
		/* Names used by the historical files in ROX-Filer/images. */
		{"rox-show-details", ROX_ICON_SHOW_DETAILS},
		{"rox-show-hidden", ROX_ICON_SHOW_HIDDEN},
		{"rox-select", ROX_ICON_SELECT},
		{"rox-mount", ROX_ICON_MOUNT},
		{"rox-mounted", ROX_ICON_MOUNTED},
		{"rox-xattr", ROX_ICON_XATTR},
		{"rox-symlink", ROX_ICON_SYMLINK},
		{"symlink", ROX_ICON_SYMLINK},
	};
	static const struct {
		const char *canonical;
		const char *alternatives[7];
	} themed_names[] = {
		{ROX_ICON_SHOW_DETAILS, {"view-list", "view-list-symbolic", "view-list-details", NULL}},
		/* Modificado por josejp2424 (2026): priorizar el ojo cab_view de Puppy
		 * y conservar alternativas de otros temas GTK3. */
		{ROX_ICON_SHOW_HIDDEN, {"cab_view", "view-hidden-files",
			"view-hidden-files-symbolic", "view-reveal",
			"view-reveal-symbolic", NULL, NULL}},
		{ROX_ICON_SELECT, {"edit-select-all", "edit-select-all-symbolic", NULL, NULL}},
		{ROX_ICON_MOUNT, {"drive-harddisk", "drive-harddisk-symbolic", NULL, NULL}},
		{ROX_ICON_MOUNTED, {"media-eject", "media-eject-symbolic", NULL, NULL}},
		{ROX_ICON_XATTR, {"document-properties", "document-properties-symbolic", "emblem-system", NULL}},
		{ROX_ICON_SYMLINK, {"emblem-symbolic-link", "emblem-symbolic-link-symbolic", NULL, NULL}},
		{"application-x-executable", {"application-x-executable", "application-x-executable-symbolic", NULL, NULL}},
		{ROX_ICON_DIRECTORY, {"folder", "folder-symbolic", NULL, NULL}},
	};
	GtkIconTheme *theme;
	const char *resolved;
	guint i, j;

	if (!icon_name || !*icon_name)
		return "image-missing";

	resolved = icon_name;
	for (i = 0; i < G_N_ELEMENTS(aliases); i++)
		if (g_str_equal(icon_name, aliases[i].legacy))
		{
			resolved = aliases[i].modern;
			break;
		}

	theme = gtk_icon_theme_get_default();
	if (!theme)
		return resolved;

	for (i = 0; i < G_N_ELEMENTS(themed_names); i++)
	{
		if (!g_str_equal(resolved, themed_names[i].canonical))
			continue;
		for (j = 0; themed_names[i].alternatives[j]; j++)
			if (gtk_icon_theme_has_icon(theme,
					themed_names[i].alternatives[j]))
				return themed_names[i].alternatives[j];
		return resolved;
	}

	if (gtk_icon_theme_has_icon(theme, resolved))
		return resolved;
	if (!g_str_has_suffix(resolved, "-symbolic"))
	{
		gchar *symbolic = g_strconcat(resolved, "-symbolic", NULL);
		if (gtk_icon_theme_has_icon(theme, symbolic))
		{
			const gchar *interned = g_intern_string(symbolic);
			g_free(symbolic);
			return interned;
		}
		g_free(symbolic);
	}

	return resolved;
}

/* Modificado por josejp2424 (2026): cab_view y las alternativas del tema
 * se intentan primero. El recurso interno se conserva únicamente como último
 * respaldo para temas que no proporcionan ningún icono de visibilidad. */
const char *rox_icon_fallback_name(const char *icon_name)
{
	if (!icon_name)
		return NULL;
	if (g_str_equal(icon_name, ROX_ICON_SHOW_HIDDEN) ||
	    g_str_equal(icon_name, "rox-show-hidden"))
		return "rox-show-hidden";
	if (g_str_equal(icon_name, "rox-find"))
		return "rox-find";
	return NULL;
}

static const char *rox_icon_default_label(const char *icon_name)
{
	if (!icon_name) return "";
	if (g_str_equal(icon_name, ROX_ICON_CANCEL)) return _("_Cancel");
	if (g_str_equal(icon_name, ROX_ICON_CLOSE)) return _("_Close");
	if (g_str_equal(icon_name, ROX_ICON_DELETE)) return _("_Delete");
	if (g_str_equal(icon_name, ROX_ICON_SAVE)) return _("_Save");
	if (g_str_equal(icon_name, ROX_ICON_OPEN)) return _("_Open");
	if (g_str_equal(icon_name, ROX_ICON_HELP)) return _("_Help");
	if (g_str_equal(icon_name, ROX_ICON_YES)) return _("_Yes");
	if (g_str_equal(icon_name, ROX_ICON_NO)) return _("_No");
	if (g_str_equal(icon_name, ROX_ICON_OK)) return _("_OK");
	if (g_str_equal(icon_name, ROX_ICON_APPLY)) return _("_Apply");
	return "";
}

GtkWidget *image_new_icon(const char *icon_name, GtkIconSize size)
{
	const char *resolved = rox_icon_name(icon_name);
	GtkIconTheme *theme = gtk_icon_theme_get_default();
	GtkWidget *image;

	if (theme && gtk_icon_theme_has_icon(theme, resolved))
		return gtk_image_new_from_icon_name(resolved, size);

	/* Keep only explicit compatibility fallbacks in AppDir/images. */
	{
		gint width = 16, height = 16;
		const gchar *extensions[] = {"png", "svg", "xpm", NULL};
		const gchar *fallback = rox_icon_fallback_name(icon_name);
		guint i;

		if (!fallback)
			return gtk_image_new_from_icon_name("image-missing", size);

		gtk_icon_size_lookup(size, &width, &height);
		for (i = 0; extensions[i]; i++)
		{
			gchar *basename = g_strdup_printf("%s.%s", fallback, extensions[i]);
			gchar *path = g_build_filename(app_dir, "images", basename, NULL);
			GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(
				path, width, height, TRUE, NULL);
			g_free(path);
			g_free(basename);
			if (pixbuf)
			{
				image = gtk_image_new_from_pixbuf(pixbuf);
				g_object_unref(pixbuf);
				return image;
			}
		}
	}

	return gtk_image_new_from_icon_name("image-missing", size);
}

GtkWidget *button_new_icon(const char *icon_name)
{
	return button_new_image_text(image_new_icon(icon_name, GTK_ICON_SIZE_BUTTON),
			rox_icon_default_label(icon_name));
}

GtkWidget *button_new_image_text(GtkWidget *image, const char *message)
{
	GtkWidget *button, *box, *label;

	button = gtk_button_new();
	label = gtk_label_new_with_mnemonic(message ? message : "");
	gtk_label_set_mnemonic_widget(GTK_LABEL(label), button);

	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
	gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(button), box);
	gtk_widget_show_all(box);

	return button;
}

GtkWidget *button_new_mixed(const char *icon_name, const char *message)
{
	return button_new_image_text(image_new_icon(icon_name, GTK_ICON_SIZE_BUTTON),
			message);
}

GtkWidget *dialog_add_icon_button(GtkDialog *dialog, const char *icon_name,
                                  const char *label, gint response_id)
{
	GtkWidget *button;

	g_return_val_if_fail(GTK_IS_DIALOG(dialog), NULL);
	button = button_new_mixed(icon_name, label);
	gtk_widget_set_can_default(button, TRUE);
	gtk_widget_show(button);
	gtk_dialog_add_action_widget(dialog, button, response_id);
	return button;
}

static void menu_item_set_content(GtkWidget *item, const char *label, GtkWidget *image)
{
	GList *children, *iter;
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *text = gtk_label_new_with_mnemonic(label ? label : "");

	gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(text, GTK_ALIGN_CENTER);

	children = gtk_container_get_children(GTK_CONTAINER(item));
	for (iter = children; iter; iter = iter->next)
		gtk_container_remove(GTK_CONTAINER(item), GTK_WIDGET(iter->data));
	g_list_free(children);

	gtk_label_set_mnemonic_widget(GTK_LABEL(text), item);
	gtk_label_set_xalign(GTK_LABEL(text), 0.0);
	if (image)
		gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), text, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(item), box);
	g_object_set_data(G_OBJECT(item), "rox-menu-label", text);
	gtk_widget_show_all(box);
}

/* Create a normal GTK3 menu.  Menus are intentionally square and use the
 * complete rectangular popup surface, avoiding transparent/rounded native
 * corners that appear black on X11/XLibre without a compositor.  Colours,
 * typography, spacing and selection remain controlled by the active theme. */
GtkWidget *rox_menu_new(void)
{
	GtkWidget *menu = gtk_menu_new();

	gtk_style_context_add_class(gtk_widget_get_style_context(menu),
		"rox-square-menu");
	return menu;
}

GtkWidget *menu_item_new_label(const char *label)
{
	GtkWidget *item = gtk_menu_item_new();
	menu_item_set_content(item, label, NULL);
	return item;
}

GtkWidget *check_menu_item_new_label(const char *label)
{
	GtkWidget *item = gtk_check_menu_item_new();
	menu_item_set_content(item, label, NULL);
	return item;
}

GtkWidget *menu_item_get_label_widget(GtkWidget *item)
{
	GtkWidget *label;

	g_return_val_if_fail(GTK_IS_MENU_ITEM(item), NULL);
	label = g_object_get_data(G_OBJECT(item), "rox-menu-label");
	return GTK_IS_LABEL(label) ? label : NULL;
}

static GtkWidget *menu_item_new_with_image(const char *label, GtkWidget *image)
{
	GtkWidget *item = gtk_menu_item_new();
	menu_item_set_content(item, label, image);
	return item;
}

GtkWidget *menu_item_new_with_icon(const char *label, const char *icon_name)
{
	GtkWidget *image = image_new_icon(icon_name, GTK_ICON_SIZE_MENU);
	gint width = 16, height = 16;

	/* Rox-Filer2 2.12.2-12: keep every menu icon in the same fixed
	 * GTK menu slot.  Some icon themes return a larger natural size for
	 * document-new/list-add; on submenu rows that pushed New to the right
	 * compared with Search, Trash, etc.  Pixel-size only affects themed
	 * GtkImage icons; AppDir pixbuf fallbacks are already scaled. */
	gtk_icon_size_lookup(GTK_ICON_SIZE_MENU, &width, &height);
	if (GTK_IS_IMAGE(image))
	{
		gtk_image_set_pixel_size(GTK_IMAGE(image), MAX(width, height));
		gtk_widget_set_valign(image, GTK_ALIGN_CENTER);
	}

	return menu_item_new_with_image(label, image);
}

GtkWidget *menu_item_new_with_pixbuf(const char *label, GdkPixbuf *pixbuf)
{
	GtkWidget *image = pixbuf ? gtk_image_new_from_pixbuf(pixbuf)
			: gtk_image_new_from_icon_name("image-missing", GTK_ICON_SIZE_MENU);
	return menu_item_new_with_image(label, image);
}

/* Highlight entry in red if 'error' is TRUE. */
void entry_set_error(GtkWidget *entry, gboolean error)
{
	static GtkCssProvider *provider = NULL;
	GtkStyleContext *context;

	if (!provider)
	{
		provider = gtk_css_provider_new();
		gtk_css_provider_load_from_data(provider,
			".rox-entry-error { color: #c00000; background-color: #ffffff; }",
			-1, NULL);
		gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
			GTK_STYLE_PROVIDER(provider),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	}

	/* Entry validation is represented with a GTK3 CSS class. */
	context = gtk_widget_get_style_context(entry);
	if (error)
		gtk_style_context_add_class(context, "rox-entry-error");
	else
		gtk_style_context_remove_class(context, "rox-entry-error");
}

/* Change stacking position of higher to be just above lower.
 * If lower is NULL, put higher at the bottom of the stack.
 */
void window_put_just_above(GdkWindow *higher, GdkWindow *lower)
{
	if (o_override_redirect.int_value && lower)
	{
		XWindowChanges restack;
		GdkDisplay *display;

		display = gdk_window_get_display(higher);

		rox_x11_error_trap_push();

		restack.sibling = gdk_x11_window_get_xid(lower);
		restack.stack_mode = Above;
		XConfigureWindow(gdk_x11_display_get_xdisplay(display),
				 gdk_x11_window_get_xid(higher),
				 CWSibling | CWStackMode, &restack);

		rox_display_flush();
		if (rox_x11_error_trap_pop())
			g_warning("window_put_just_above()\n");
	}
	else
		gdk_window_lower(higher);	/* To bottom of stack */
}


/* GtkFixed es público en GTK3; no se accede a su lista interna. */
void fixed_move_fast(GtkFixed *fixed, GtkWidget *widget, int x, int y)
{
	g_return_if_fail(GTK_IS_FIXED(fixed));
	g_return_if_fail(GTK_IS_WIDGET(widget));
	gtk_fixed_move(fixed, widget, x, y);
}


/* When the tips window closed, record the time. If we try to open another
 * tip soon, it will appear more quickly.
 */
static void tooltip_destroyed(gpointer data)
{
	time(&tip_time);
}

/* Display a tooltip-like widget near the pointer with 'text'. If 'text' is
 * NULL, close any current tooltip.
 */
void tooltip_show(guchar *text)
{
	GtkWidget *label;
	int	x, y, py;
	int	w, h;
	int m;

	if (tip_timeout)
	{
		g_source_remove(tip_timeout);
		tip_timeout = 0;
	}

	if (tip_widget)
	{
		gtk_widget_destroy(tip_widget);
		tip_widget = NULL;
	}

	if (!text)
		return;

	/* Show the tip. Modificado por josejp2424 (2026): el tooltip anterior
	 * usaba app-paintable y dibujaba un borde negro manual; con algunos temas
	 * eso producía una barra negra sin texto que parecía un submenú roto.
	 * Dejar que GTK pinte por completo fondo, borde, texto y esquinas. */
	tip_widget = gtk_window_new(GTK_WINDOW_POPUP);
	gtk_window_set_type_hint(GTK_WINDOW(tip_widget), GDK_WINDOW_TYPE_HINT_TOOLTIP);
	gtk_window_set_resizable(GTK_WINDOW(tip_widget), FALSE);
	gtk_widget_set_name(tip_widget, "gtk-tooltip");
	gtk_style_context_add_class(gtk_widget_get_style_context(tip_widget),
				    "tooltip");

	label = gtk_label_new((const gchar *) text);
	gtk_style_context_add_class(gtk_widget_get_style_context(label), "tooltip");
	gtk_widget_set_margin_start(label, 6);
	gtk_widget_set_margin_end(label, 6);
	gtk_widget_set_margin_top(label, 3);
	gtk_widget_set_margin_bottom(label, 3);
	gtk_container_add(GTK_CONTAINER(tip_widget), label);
	gtk_widget_show(label);
	gtk_widget_realize(tip_widget);

	w = gtk_widget_get_allocated_width(tip_widget);
	h = gtk_widget_get_allocated_height(tip_widget);
	{
		GdkDisplay *display = gdk_display_get_default();
		GdkSeat *seat = display ? gdk_display_get_default_seat(display) : NULL;
		GdkDevice *pointer = seat ? gdk_seat_get_pointer(seat) : NULL;

		if (pointer)
			gdk_device_get_position(pointer, NULL, &x, &py);
		else
			x = py = 0;
	}

	m = gui_monitor_at_point(x, py);

	x -= w / 2;
	y = py + 12; /* I don't know the pointer height so I use a constant */

	/* Now check for screen boundaries */
	x = CLAMP(x, monitor_geom[m].x,
			monitor_geom[m].x + monitor_geom[m].width - w);
	y = CLAMP(y, monitor_geom[m].y,
			monitor_geom[m].y + monitor_geom[m].height - h);

	/* And again test if pointer is over the tooltip window */
	if (py >= y && py <= y + h)
		y = py - h - 2;
	gtk_window_move(GTK_WINDOW(tip_widget), x, y);
	gtk_widget_show(tip_widget);

	g_signal_connect_swapped(tip_widget, "destroy",
			G_CALLBACK(tooltip_destroyed), NULL);
	time(&tip_time);
}

/* Call callback(user_data) after a while, unless cancelled.
 * Object is refd now and unref when cancelled / after callback called.
 */
void tooltip_prime(GSourceFunc callback, GObject *object)
{
	time_t  now;
	int	delay;

	g_return_if_fail(tip_timeout == 0);

	time(&now);
	delay = now - tip_time > 2 ? 1000 : 200;

	g_object_ref(object);
	tip_timeout = g_timeout_add_full(G_PRIORITY_DEFAULT_IDLE,
					 delay,
					 (GSourceFunc) callback,
					 object,
					 g_object_unref);
}

/* Apply an optional font with a widget-local GTK3 CSS provider. */
void widget_modify_font(GtkWidget *widget, PangoFontDescription *font_desc)
{
	GtkCssProvider *old_provider;
	GtkStyleContext *context;

	g_return_if_fail(GTK_IS_WIDGET(widget));
	context = gtk_widget_get_style_context(widget);
	old_provider = g_object_get_data(G_OBJECT(widget), "rox-font-provider");
	if (old_provider)
	{
		gtk_style_context_remove_provider(context,
			GTK_STYLE_PROVIDER(old_provider));
		g_object_set_data(G_OBJECT(widget), "rox-font-provider", NULL);
	}

	if (font_desc)
	{
		GtkCssProvider *provider = gtk_css_provider_new();
		const gchar *family = pango_font_description_get_family(font_desc);
		gint size = pango_font_description_get_size(font_desc);
		gchar *escaped = g_strescape(family ? family : "Sans", NULL);
		gchar *css = g_strdup_printf(
			"* { font-family: \"%s\"; font-size: %.2fpt; "
			"font-weight: %d; font-style: %s; }",
			escaped,
			size > 0 ? size / (gdouble) PANGO_SCALE : 10.0,
			(gint) pango_font_description_get_weight(font_desc),
			pango_font_description_get_style(font_desc) == PANGO_STYLE_NORMAL
				? "normal" : "italic");

		gtk_css_provider_load_from_data(provider, css, -1, NULL);
		gtk_style_context_add_provider(context,
			GTK_STYLE_PROVIDER(provider),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
		g_object_set_data_full(G_OBJECT(widget), "rox-font-provider",
			provider, g_object_unref);
		g_free(css);
		g_free(escaped);
	}
}

void rox_style_context_get_background(GtkStyleContext *context,
		GtkStateFlags state, GdkRGBA *colour)
{
	GdkRGBA *css_colour = NULL;

	g_return_if_fail(GTK_IS_STYLE_CONTEXT(context));
	g_return_if_fail(colour != NULL);
	*colour = (GdkRGBA) {0.0, 0.0, 0.0, 0.0};
	gtk_style_context_get(context, state,
		"background-color", &css_colour, NULL);
	if (css_colour)
	{
		*colour = *css_colour;
		gdk_rgba_free(css_colour);
	}
}

/* Confirm the action with the user. If action is NULL, the text from stock
 * is used.
 */
gboolean confirm(const gchar *message, const gchar *stock, const gchar *action)
{
	return get_choice(PROJECT, message, 2,
			  ROX_ICON_CANCEL, NULL,
			  stock, action) == 1;
}

struct _Radios {
	GList *widgets;

	void (*changed)(Radios *, gpointer data);
	gpointer changed_data;
};

/* Create a new set of radio buttons.
 * Use radios_add to add options, then radios_pack to put them into something.
 * The radios object will self-destruct with the first widget it contains.
 * changed(data) is called (if not NULL) when pack is called, and on any
 * change after that.
 */
Radios *radios_new(void (*changed)(Radios *, gpointer data), gpointer data)
{
	Radios *radios;

	radios = g_new(Radios, 1);

	radios->widgets = NULL;
	radios->changed = changed;
	radios->changed_data = data;

	return radios;
}

static void radios_free(GtkWidget *radio, Radios *radios)
{
	g_return_if_fail(radios != NULL);

	g_list_free(radios->widgets);
	g_free(radios);
}

void radios_add(Radios *radios, const gchar *tip, gint value,
		const gchar *label, ...)
{
	GtkWidget *radio;
	GSList *group = NULL;
	gchar *s;
	va_list args;

	g_return_if_fail(radios != NULL);
	g_return_if_fail(label != NULL);

	va_start(args, label);
	s = g_strdup_vprintf(label, args);
	va_end(args);

	if (radios->widgets)
	{
		GtkRadioButton *first = GTK_RADIO_BUTTON(radios->widgets->data);
		group = gtk_radio_button_get_group(first);
	}

	radio = gtk_radio_button_new(group);
	{
		GtkWidget *radio_label = gtk_label_new(s);
		gtk_label_set_line_wrap(GTK_LABEL(radio_label), TRUE);
		gtk_label_set_xalign(GTK_LABEL(radio_label), 0.0);
		gtk_container_add(GTK_CONTAINER(radio), radio_label);
	}
	gtk_widget_show(radio);
	if (tip)
		gtk_widget_set_tooltip_text(radio, tip);
	if (!group)
		g_signal_connect(G_OBJECT(radio), "destroy",
				G_CALLBACK(radios_free), radios);

	radios->widgets = g_list_prepend(radios->widgets, radio);
	g_object_set_data(G_OBJECT(radio), "rox-radios-value",
			  GINT_TO_POINTER(value));
}

static void radio_toggled(GtkToggleButton *button, Radios *radios)
{
	g_return_if_fail(radios != NULL);

	if (button && !gtk_toggle_button_get_active(button))
		return;	/* Stop double-notifies */

	if (radios->changed)
		radios->changed(radios, radios->changed_data);
}

void radios_pack(Radios *radios, GtkBox *box)
{
	GList *next;

	g_return_if_fail(radios != NULL);

	for (next = g_list_last(radios->widgets); next; next = next->prev)
	{
		GtkWidget *button = GTK_WIDGET(next->data);

		gtk_box_pack_start(box, button, FALSE, TRUE, 0);
		g_signal_connect(button, "toggled",
				G_CALLBACK(radio_toggled), radios);
	}
	radio_toggled(NULL, radios);
}

void radios_set_value(Radios *radios, gint value)
{
	GList *next;

	g_return_if_fail(radios != NULL);

	for (next = radios->widgets; next; next = next->next)
	{
		GtkToggleButton *radio = GTK_TOGGLE_BUTTON(next->data);
		int radio_value;

		radio_value = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(radio),
						"rox-radios-value"));

		if (radio_value == value)
		{
			gtk_toggle_button_set_active(radio, TRUE);
			return;
		}
	}

	g_warning("Value %d not in radio group!", value);
}

gint radios_get_value(Radios *radios)
{
	GList *next;

	g_return_val_if_fail(radios != NULL, -1);

	for (next = radios->widgets; next; next = next->next)
	{
		GtkToggleButton *radio = GTK_TOGGLE_BUTTON(next->data);

		if (gtk_toggle_button_get_active(radio))
			return GPOINTER_TO_INT(g_object_get_data(
					G_OBJECT(radio), "rox-radios-value"));
	}

	g_warning("Nothing in the radio group is selected!");

	return -1;
}

/* Convert a list of URIs as a string into a GList of EscapedPath URIs.
 * No unescaping is done.
 * Lines beginning with # are skipped.
 * The text block passed in is zero terminated (after the final CRLF)
 */
GList *uri_list_to_glist(const char *uri_list)
{
	GList   *list = NULL;

	while (*uri_list)
	{
		char	*linebreak;
		int	length;

		linebreak = strchr(uri_list, 13);

		if (!linebreak || linebreak[1] != 10)
		{
			g_warning("uri_list_to_glist: %s",
					_("Incorrect or missing line "
					  "break in text/uri-list data"));
			/* If this is the first, append it anyway (Firefox
			 * 3.5) */
			if (!list && uri_list[0] != '#')
				list = g_list_append(list, g_strdup(uri_list));
			return list;
		}

		length = linebreak - uri_list;

		if (length && uri_list[0] != '#')
			list = g_list_append(list, g_strndup(uri_list, length));

		uri_list = linebreak + 2;
	}

	return list;
}

/* GTK3: evitamos el widget custom (expose/size_request). Usamos GtkImage. */
GtkWidget *simple_image_new(GdkPixbuf *pixbuf)
{
	g_return_val_if_fail(pixbuf != NULL, NULL);
	return gtk_image_new_from_pixbuf(pixbuf);
}

/* Whether a line l1 long starting from n1 overlaps a line l2 from n2 */
inline static gboolean gui_ranges_overlap(int n1, int l1, int n2, int l2)
{
	return (n1 > n2 && n1 < n2 + l2) ||
		(n1 + l1 > n2 && n1 + l1 < n2 + l2) ||
		(n1 <= n2 && n1 + l1 >= n2 + l2);
}

static void gui_get_monitor_adjacent(int monitor, MonitorAdjacent *adj)
{
	int m;

	adj->left = adj->right = adj->top = adj->bottom = FALSE;

	for (m = 0; m < n_monitors; ++m)
	{
		if (m == monitor)
			continue;
		if (gui_ranges_overlap(monitor_geom[m].y,
				monitor_geom[m].height,
				monitor_geom[monitor].y,
				monitor_geom[monitor].height))
		{
			if (monitor_geom[m].x < monitor_geom[monitor].x)
			{
				adj->left = TRUE;
			}
			else if (monitor_geom[m].x > monitor_geom[monitor].x)
			{
				adj->right = TRUE;
			}
		}
		if (gui_ranges_overlap(monitor_geom[m].x,
				monitor_geom[m].width,
				monitor_geom[monitor].x,
				monitor_geom[monitor].width))
		{
			if (monitor_geom[m].y < monitor_geom[monitor].y)
			{
				adj->top = TRUE;
			}
			else if (monitor_geom[m].y > monitor_geom[monitor].y)
			{
				adj->bottom = TRUE;
			}
		}
	}
}

static void rox_wmspec_change_state(gboolean add, GdkWindow *window,
				GdkAtom state1, GdkAtom state2)
{
	Display *xdisplay;
	GdkDisplay *display;
	GdkScreen *screen;
	GdkWindow *root;
	XEvent xev;
	long action;

	g_return_if_fail(window != NULL);

	display = gdk_window_get_display(window);
	screen = gdk_window_get_screen(window);
	root = gdk_screen_get_root_window(screen);
	xdisplay = gdk_x11_display_get_xdisplay(display);

	action = add ? _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE;

	memset(&xev, 0, sizeof(xev));
	xev.xclient.type = ClientMessage;
	xev.xclient.window = gdk_x11_window_get_xid(window);
	xev.xclient.message_type =
		gdk_x11_get_xatom_by_name_for_display(display, "_NET_WM_STATE");
	xev.xclient.format = 32;
	xev.xclient.data.l[0] = action;
	xev.xclient.data.l[1] = gdk_x11_atom_to_xatom_for_display(display, state1);
	xev.xclient.data.l[2] = (state2 != GDK_NONE)
		? gdk_x11_atom_to_xatom_for_display(display, state2)
		: 0;
	xev.xclient.data.l[3] = 1; /* source indication: normal application */

	XSendEvent(xdisplay, gdk_x11_window_get_xid(root), False,
		   SubstructureRedirectMask | SubstructureNotifyMask, &xev);
}


/* Tell the window manager whether to keep this window below others. */
void keep_below(GdkWindow *window, gboolean setting)
{
	g_return_if_fail(GDK_IS_WINDOW(window));


	if (gdk_window_is_visible(window))
	{
		if (setting)
		{
			rox_wmspec_change_state(FALSE, window,
				gdk_atom_intern("_NET_WM_STATE_ABOVE", FALSE),
				GDK_NONE);
		}
		rox_wmspec_change_state(setting, window,
				gdk_atom_intern("_NET_WM_STATE_BELOW", FALSE),
				GDK_NONE);
	}
#if 0
	else
	{
#if GTK_CHECK_VERSION(2,4,0)
	  gdk_synthesize_window_state(window,
				setting ? GDK_WINDOW_STATE_ABOVE :
					GDK_WINDOW_STATE_BELOW,
				setting ? GDK_WINDOW_STATE_BELOW : 0);
#endif
	}
#endif
}

static void
size_prepared_cb (GdkPixbufLoader *loader,
		  int              width,
		  int              height,
		  gpointer         data)
{
	struct {
		gint width;
		gint height;
		gboolean preserve_aspect_ratio;
	} *info = data;

	g_return_if_fail (width > 0 && height > 0);

	if(info->preserve_aspect_ratio) {
		if ((double)height * (double)info->width >
		    (double)width * (double)info->height) {
			width = 0.5 + (double)width * (double)info->height / (double)height;
			height = info->height;
		} else {
			height = 0.5 + (double)height * (double)info->width / (double)width;
			width = info->width;
		}
	} else {
		width = info->width;
		height = info->height;
	}

	gdk_pixbuf_loader_set_size (loader, width, height);
}

/**
 * rox_pixbuf_new_from_file_at_scale:
 * @filename: Name of file to load.
 * @width: The width the image should have
 * @height: The height the image should have
 * @preserve_aspect_ratio: %TRUE to preserve the image's aspect ratio
 * @error: Return location for an error
 *
 * Creates a new pixbuf by loading an image from a file.  The file format is
 * detected automatically. If %NULL is returned, then @error will be set.
 * Possible errors are in the #GDK_PIXBUF_ERROR and #G_FILE_ERROR domains.
 * The image will be scaled to fit in the requested size, optionally preserving
 * the image's aspect ratio.
 *
 * Return value: A newly-created pixbuf with a reference count of 1, or %NULL
 * if any of several error conditions occurred:  the file could not be opened,
 * there was no loader for the file's format, there was not enough memory to
 * allocate the image buffer, or the image file contained invalid data.
 *
 * Kept as a ROX-owned scaled pixbuf loader for predictable error handling.
 **/
GdkPixbuf *
rox_pixbuf_new_from_file_at_scale (const char *filename,
				   int         width,
				   int         height,
				   gboolean    preserve_aspect_ratio,
				   GError    **error)
{

	GdkPixbufLoader *loader;
	GdkPixbuf       *pixbuf;

	guchar buffer [4096];
	int length;
	FILE *f;
	struct {
		gint width;
		gint height;
		gboolean preserve_aspect_ratio;
	} info;

	g_return_val_if_fail (filename != NULL, NULL);
        g_return_val_if_fail (width > 0 && height > 0, NULL);

	f = fopen (filename, "rb");
	if (!f) {
                gchar *utf8_filename = g_filename_to_utf8 (filename, -1,
                                                           NULL, NULL, NULL);
                g_set_error (error,
                             G_FILE_ERROR,
                             g_file_error_from_errno (errno),
                             _("Failed to open file '%s': %s"),
                             utf8_filename ? utf8_filename : "???",
                             g_strerror (errno));
                g_free (utf8_filename);
		return NULL;
        }

	loader = gdk_pixbuf_loader_new ();

	info.width = width;
	info.height = height;
        info.preserve_aspect_ratio = preserve_aspect_ratio;

	g_signal_connect (loader, "size-prepared", G_CALLBACK (size_prepared_cb), &info);

	while (!feof (f) && !ferror (f)) {
		length = fread (buffer, 1, sizeof (buffer), f);
		if (length > 0)
			if (!gdk_pixbuf_loader_write (loader, buffer, length, error)) {
				gdk_pixbuf_loader_close (loader, NULL);
				fclose (f);
				g_object_unref (loader);
				return NULL;
			}
	}

	fclose (f);

	if (!gdk_pixbuf_loader_close (loader, error)) {
		g_object_unref (loader);
		return NULL;
	}

	pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);

	if (!pixbuf) {
                gchar *utf8_filename = g_filename_to_utf8 (filename, -1,
                                                           NULL, NULL, NULL);

		g_object_unref (loader);

                g_set_error (error,
                             GDK_PIXBUF_ERROR,
                             GDK_PIXBUF_ERROR_FAILED,
                             _("Failed to load image '%s': reason not known, probably a corrupt image file"),
                             utf8_filename ? utf8_filename : "???");
                g_free (utf8_filename);
		return NULL;
	}

	g_object_ref (pixbuf);

	g_object_unref (loader);

	return pixbuf;
}

/* Make the name bolder and larger.
 * scale_factor can be PANGO_SCALE_X_LARGE, etc.
 */
void make_heading(GtkWidget *label, double scale_factor)
{
	PangoAttribute *attr;
	PangoAttrList *list;

	list = pango_attr_list_new();

	attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
	attr->start_index = 0;
	attr->end_index = -1;
	pango_attr_list_insert(list, attr);

	attr = pango_attr_scale_new(scale_factor);
	attr->start_index = 0;
	attr->end_index = -1;
	pango_attr_list_insert(list, attr);

	gtk_label_set_attributes(GTK_LABEL(label), list);
	pango_attr_list_unref(list);
}

/* Launch a program using 0launch.
 * If button-3 is used, open the GUI with -g.
 */
void launch_uri(GObject *button, const char *uri)
{
	const char *argv[] = {"0launch", NULL, NULL, NULL};
	const char *uri_0launch = "/uri/0install/zero-install.sourceforge.net"
				  "/bin/0launch";

	if (!available_in_path(argv[0]))
	{
		if (access(uri_0launch, X_OK) == 0)
			argv[0] = uri_0launch;
		else
		{
			const char *appname=g_object_get_data(button,
							      "appname");

			if (appname)
			{
				gchar *path=find_app(appname);
				if(path)
				{
					run_by_path(path);
					g_free(path);
					return;
				}
			}

			delayed_error(_("This program (%s) cannot be run, "
				"as the 0launch command is not available. "
				"It can be downloaded from here:\n\n"
				"http://0install.net/injector.html"),
				uri);
			return;
		}
	}

	if (current_event_button() == 3)
	{
		argv[1] = "-g";
		argv[2] = uri;
	}
	else
		argv[1] = uri;

	rox_spawn(NULL, argv);
}

static gint button3_button_pressed(GtkButton *button,
				GdkEventButton *event,
				gpointer data)
{
	(void)data;
	if (event->button != 3)
		return FALSE;

	g_object_set_data(G_OBJECT(button), "rox-alt-button",
		GUINT_TO_POINTER(event->button));
	return TRUE;
}

static gint button3_button_released(GtkButton *button,
				GdkEventButton *event,
				gpointer data)
{
	guint pressed;

	(void)data;
	pressed = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button),
		"rox-alt-button"));
	if (pressed != event->button || event->button != 3)
		return FALSE;

	g_object_set_data(G_OBJECT(button), "rox-alt-button", NULL);
	g_signal_emit_by_name(button, "clicked");
	return TRUE;
}

void allow_right_click(GtkWidget *button)
{
	g_signal_connect(button, "button_press_event",
		G_CALLBACK(button3_button_pressed), NULL);
	g_signal_connect(button, "button_release_event",
		G_CALLBACK(button3_button_released), NULL);
}

/* Return mouse button used in the current event, or -1 if none (no event,
 * or not a click).
 */
gint current_event_button(void)
{
	GdkEventButton *bev;
	gint button = -1;

	bev = (GdkEventButton *) gtk_get_current_event();

	if (bev &&
	    (bev->type == GDK_BUTTON_PRESS || bev->type == GDK_BUTTON_RELEASE))
		button = bev->button;

	if (bev)
		gdk_event_free((GdkEvent *) bev);

	return button;
}

/* Create a new pixbuf by colourizing 'src' to 'color'. If the function fails,
 * 'src' will be returned (with an increased reference count, so it is safe to
 * g_object_unref() the return value whether the function fails or not).
 */
GdkPixbuf *create_spotlight_pixbuf(GdkPixbuf *src, const GdkRGBA *color)
{
	guchar opacity = 127;//192;
	guchar alpha = 255 - opacity;
	GdkPixbuf *dst;
	GdkColorspace colorspace;
	int width, height, src_rowstride, dst_rowstride, x, y;
	int n_channels, bps;
	int r, g, b;
	guchar *spixels, *dpixels, *src_pixels, *dst_pixels;
	gboolean has_alpha;

	has_alpha = gdk_pixbuf_get_has_alpha(src);
	colorspace = gdk_pixbuf_get_colorspace(src);
	n_channels = gdk_pixbuf_get_n_channels(src);
	bps = gdk_pixbuf_get_bits_per_sample(src);

	if ((colorspace != GDK_COLORSPACE_RGB) ||
	    (!has_alpha && n_channels != 3) ||
	    (has_alpha && n_channels != 4) ||
	    (bps != 8))
		goto error;

	width = gdk_pixbuf_get_width(src);
	height = gdk_pixbuf_get_height(src);

	dst = gdk_pixbuf_new(colorspace, has_alpha, bps, width, height);
	if (dst == NULL)
		goto error;

	src_pixels = gdk_pixbuf_get_pixels(src);
	dst_pixels = gdk_pixbuf_get_pixels(dst);
	src_rowstride = gdk_pixbuf_get_rowstride(src);
	dst_rowstride = gdk_pixbuf_get_rowstride(dst);

	if (color == NULL)
		goto error;
	r = opacity * (gint) CLAMP(color->red * 255.0 + 0.5, 0.0, 255.0);
	g = opacity * (gint) CLAMP(color->green * 255.0 + 0.5, 0.0, 255.0);
	b = opacity * (gint) CLAMP(color->blue * 255.0 + 0.5, 0.0, 255.0);

	for (y = 0; y < height; y++)
	{
		spixels = src_pixels + y * src_rowstride;
		dpixels = dst_pixels + y * dst_rowstride;
		for (x = 0; x < width; x++)
		{
			*dpixels++ = (*spixels++ * alpha + r) >> 8;
			*dpixels++ = (*spixels++ * alpha + g) >> 8;
			*dpixels++ = (*spixels++ * alpha + b) >> 8;
			if (has_alpha)
				*dpixels++ = *spixels++;
		}

	}
	return dst;

error:
	g_object_ref(src);
	return src;
}

/* Load the Templates.ui file and build a component. */
GtkBuilder *get_gtk_builder(gchar **ids)
{
	GError	*error = NULL;
	char *path;
	GtkBuilder *builder = NULL;

	builder = gtk_builder_new();
	gtk_builder_set_translation_domain(builder, "ROX-Filer");

	path = g_build_filename(app_dir, "Templates.ui", NULL);
	if (!gtk_builder_add_objects_from_file(builder, path, ids, &error))
	{
		g_warning("Failed to load builder file %s: %s",
				path, error->message);
		g_error_free(error);
	}

	g_free(path);

	return builder;
}

void menu_item_set_icon(GtkWidget *item, const char *icon_name)
{
	GtkWidget *label_widget;
	const gchar *current;
	gchar *label;

	g_return_if_fail(GTK_IS_MENU_ITEM(item));
	label_widget = menu_item_get_label_widget(item);
	current = label_widget ? gtk_label_get_text(GTK_LABEL(label_widget))
		: gtk_menu_item_get_label(GTK_MENU_ITEM(item));
	label = g_strdup(current ? current : "");
	menu_item_set_content(item, label,
			image_new_icon(icon_name, GTK_ICON_SIZE_MENU));
	g_free(label);
}
