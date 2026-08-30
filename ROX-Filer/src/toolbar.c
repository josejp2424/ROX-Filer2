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

/* toolbar.c - for the button bars that go along the tops of windows */

/*
 * Modificado por josejp2424 (2026):
 * port a GTK3 y adaptaciones específicas de esta versión.
 */

#include "config.h"

#include <string.h>

#include "global.h"

#include "toolbar.h"
#include "options.h"
#include "support.h"
#include "main.h"
#include "menu.h"
#include "dnd.h"
#include "drives.h"
#include "filer.h"
#include "display.h"
#include "pixmaps.h"
#include "bind.h"
#include "type.h"
#include "dir.h"
#include "diritem.h"
#include "view_iface.h"
#include "bookmarks.h"
#include "gui_support.h"
#include "trash.h"
#include "filer_pair.h"
#include "search_integration.h"

typedef struct _Tool Tool;

typedef enum {DROP_NONE, DROP_TO_PARENT, DROP_TO_HOME, DROP_BOOKMARK} DropDest;

struct _Tool {
	const gchar	*label;
	const gchar	*name;
	const gchar	*tip;		/* Tooltip */
	void		(*clicked)(GtkWidget *w, FilerWindow *filer_window);
	DropDest	drop_action;
	gboolean	enabled;
	gboolean	menu;		/* Activate on button-press */
};

Option o_toolbar, o_toolbar_info, o_toolbar_disable;
Option o_toolbar_min_width;
static Option o_toolbar_order;

/* Rox-Filer2 2.12.2-11: Partitions stays fixed at the far left.  Every
 * regular toolbar tool has a persistent user-configurable order.  These
 * identifiers intentionally reuse the icon-name IDs already stored by
 * toolbar_disable, so existing configurations remain compatible. */
#define TOOLBAR_DEFAULT_ORDER \
	ROX_ICON_GO_BACK "," ROX_ICON_GO_FORWARD "," ROX_ICON_GO_UP "," \
	ROX_ICON_HOME "," ROX_ICON_BOOKMARKS "," ROX_ICON_REFRESH "," \
	ROX_ICON_ZOOM_IN "," ROX_ICON_ZOOM_FIT "," ROX_ICON_ZOOM_OUT "," \
	ROX_ICON_SHOW_DETAILS "," ROX_ICON_SHOW_HIDDEN "," ROX_ICON_SELECT "," \
	ROX_ICON_ADD "," ROX_ICON_FIND ",window-new"

static FilerWindow *filer_window_being_counted;
static gboolean updating_tool_positions = FALSE;

/* TRUE if the button presses (or released) should open a new window,
 * rather than reusing the existing one.
 */
#define NEW_WIN_BUTTON(button_event)	\
  (o_new_button_1.int_value		\
   	? ((GdkEventButton *) button_event)->button == 1	\
	: ((GdkEventButton *) button_event)->button != 1)

/* Static prototypes */
static void toolbar_back_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_forward_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_up_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_home_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_bookmarks_clicked(GtkWidget *widget,
				      FilerWindow *filer_window);
static void toolbar_refresh_clicked(GtkWidget *widget,
				    FilerWindow *filer_window);
static void toolbar_zoom_out_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_zoom_in_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_autosize_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_details_clicked(GtkWidget *widget,
				    FilerWindow *filer_window);
static void toolbar_hidden_clicked(GtkWidget *widget,
				   FilerWindow *filer_window);
static void toolbar_select_clicked(GtkWidget *widget,
				   FilerWindow *filer_window);
static void toolbar_new_clicked(GtkWidget *widget,
				   FilerWindow *filer_window);
static void toolbar_new_show_menu(GtkMenuToolButton *button,
				      FilerWindow *filer_window);
static void toolbar_search_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_pair_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_preferences_clicked(GtkWidget *widget, FilerWindow *filer_window);
static GtkWidget *add_button(GtkWidget *bar, Tool *tool,
				FilerWindow *filer_window);
static Tool *toolbar_find_tool(const gchar *name);
static GPtrArray *toolbar_ordered_tools(void);
static GtkWidget *create_toolbar(FilerWindow *filer_window);
static gboolean drag_motion(GtkWidget		*widget,
                            GdkDragContext	*context,
                            gint		x,
                            gint		y,
                            guint		time,
			    FilerWindow		*filer_window);
static void drag_leave(GtkWidget	*widget,
                       GdkDragContext	*context,
		       guint32		time,
		       FilerWindow	*filer_window);
static void handle_drops(FilerWindow *filer_window,
			 GtkWidget *button,
			 DropDest dest);
static void tool_visibility_toggled(GtkToggleButton *widget, gpointer data);
static void tool_position_changed(GtkSpinButton *spin, gpointer data);
static void update_tool_position_spins(GtkWidget *box);
static void option_notify(void);
static GList *build_tool_options(Option *option, xmlNode *node, guchar *label);
static void update_tools(Option *option);
static guchar *read_tools(Option *option);
static void update_tool_order(Option *option);
static guchar *read_tool_order(Option *option);
static void tally_items(gpointer key, gpointer value, gpointer data);

/* Modificado por josejp2424: se eliminó Ayuda de la barra principal y
 * de la lista de personalización; Acerca de permanece en el menú contextual. */
static Tool all_tools[] = {
	/* Agregado por josejp2424 (2026): navegación de rutas por ventana con
	 * botones pequeños y tradicionales de ROX. */
	{N_("Back"), ROX_ICON_GO_BACK, N_("Go back to the previous directory"),
	 toolbar_back_clicked, DROP_NONE, TRUE,
	 FALSE},

	{N_("Forward"), ROX_ICON_GO_FORWARD, N_("Go forward to the next directory"),
	 toolbar_forward_clicked, DROP_NONE, TRUE,
	 FALSE},

	{N_("Up"), ROX_ICON_GO_UP, N_("Change to parent directory"),
	 toolbar_up_clicked, DROP_TO_PARENT, TRUE,
	 FALSE},

	{N_("Home"), ROX_ICON_HOME, N_("Change to home directory"),
	 toolbar_home_clicked, DROP_TO_HOME, TRUE,
	 FALSE},

	{N_("Bookmarks"), ROX_ICON_BOOKMARKS, N_("Bookmarks menu"),
	 toolbar_bookmarks_clicked, DROP_BOOKMARK, FALSE,
	 TRUE},

	{N_("Scan"), ROX_ICON_REFRESH, N_("Rescan directory contents"),
	 toolbar_refresh_clicked, DROP_NONE, TRUE,
	 FALSE},

	/* r69: controles de tamaño explícitos. Antes los dos botones se
	 * llamaban "Size" y reducir dependía de un clic secundario oculto. */
	{N_("Bigger Icons"), ROX_ICON_ZOOM_IN, N_("Bigger Icons"),
	 toolbar_zoom_in_clicked, DROP_NONE, TRUE,
	 FALSE},

	{N_("Automatic"), ROX_ICON_ZOOM_FIT, N_("Automatic size mode"),
	 toolbar_autosize_clicked, DROP_NONE, TRUE,
	 FALSE},

	{N_("Smaller Icons"), ROX_ICON_ZOOM_OUT, N_("Smaller Icons"),
	 toolbar_zoom_out_clicked, DROP_NONE, TRUE,
	 FALSE},

	{N_("Details"), ROX_ICON_SHOW_DETAILS, N_("Left: toggle List View\n"
				"Center: Return to normal Icon View\n"
				"Right: cycle extra details"),
	 toolbar_details_clicked, DROP_NONE, TRUE,
	 FALSE},

	/* Modificado por josejp2424 (2026): se retiró Ordenar de la barra.
	 * ROX usa ahora un orden estable: carpetas primero y archivos después. */

	{N_("Hidden"), ROX_ICON_SHOW_HIDDEN, N_("Left: Show/hide hidden files\n"
						 "Center: Reset to defaults\n"
						 "Right: Show/hide thumbnails"),
	 toolbar_hidden_clicked, DROP_NONE, TRUE,
	 FALSE},

	{N_("Select"), ROX_ICON_SELECT, N_("Select all/invert selection"),
	 toolbar_select_clicked, DROP_NONE, FALSE,
	 FALSE},

	{N_("New"), ROX_ICON_ADD, N_("Create a new directory, blank file, or file from a template"),
	 toolbar_new_clicked, DROP_NONE, FALSE,
	 FALSE},

	{N_("Search"), ROX_ICON_FIND, N_("Search in the current folder"),
	 toolbar_search_clicked, DROP_NONE, FALSE,
	 FALSE},

	{N_("Paired Windows"), "window-new", N_("Open two Rox-Filer2 windows side by side"),
	 toolbar_pair_clicked, DROP_NONE, FALSE,
	 FALSE},
};

/* Rox-Filer2 2.12.2-24: quick access requested by mistfire.  Settings stays
 * fixed at the far right of the toolbar, after Trash, so it is always easy to
 * find and does not disturb the user's custom order for normal tools. */
static Tool toolbar_preferences_tool = {
	N_("Options"), ROX_ICON_PREFERENCES, N_("Options..."),
	toolbar_preferences_clicked, DROP_NONE, TRUE, FALSE
};


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void toolbar_init(void)
{
	option_add_int(&o_toolbar, "toolbar_type", TOOLBAR_LARGE);
	option_add_int(&o_toolbar_info, "toolbar_show_info", 1);
	option_add_string(&o_toolbar_disable, "toolbar_disable",
					ROX_ICON_CLOSE);
	option_add_string(&o_toolbar_order, "toolbar_order",
					TOOLBAR_DEFAULT_ORDER);
	option_add_int(&o_toolbar_min_width, "toolbar_min_width", 0);
	option_add_notify(option_notify);

	option_register_widget("tool-options", build_tool_options);
}

void toolbar_update_info(FilerWindow *filer_window)
{
	gchar		*label;
	ViewIface	*view;
	int		n_selected;

	g_return_if_fail(filer_window != NULL);

	/* The information label now lives in the bottom status row rather than
	 * inside the toolbar, so it remains available even when the toolbar is
	 * hidden. The existing toolbar_show_info option still controls whether
	 * this status text is displayed. */
	if (!filer_window->toolbar_text)
		return;

	if (!o_toolbar_info.int_value)
	{
		gtk_label_set_text(GTK_LABEL(filer_window->toolbar_text), "");
		return;
	}

	if (filer_window->target_cb)
		return;

	view = filer_window->view;

	n_selected = view_count_selected(view);

	if (n_selected == 0)
	{
		gchar *s = NULL;
		int   n_items;

		if (filer_window->scanning)
		{
			gtk_label_set_text(
				GTK_LABEL(filer_window->toolbar_text), "");
			return;
		}

		if (!(filer_window->show_hidden ||
		      filer_window->temp_show_hidden) ||
		    filer_window->filter!=FILER_SHOW_ALL)
		{
			GHashTable *hash = filer_window->directory->known_items;
			int	   tally = 0;

			filer_window_being_counted=filer_window;
			g_hash_table_foreach(hash, tally_items, &tally);

			if (tally)
				s = g_strdup_printf(_(" (%u hidden)"), tally);
		}

		n_items = view_count_items(view);

		if (n_items)
			label = g_strdup_printf("%d %s%s",
					n_items,
					n_items != 1 ? _("items") : _("item"),
					s ? s : "");
		else /* (French plurals work differently for zero) */
			label = g_strdup_printf(_("No items%s"),
					s ? s : "");
		g_free(s);
	}
	else
	{
		double	size = 0;
		ViewIter iter;
		DirItem *item;

		view_get_iter(filer_window->view, &iter, VIEW_ITER_SELECTED);

		while ((item = iter.next(&iter)))
		{
			if (item->base_type != TYPE_DIRECTORY &&
			    item->base_type != TYPE_UNKNOWN)
				size += (double) item->size;
		}

		label = g_strdup_printf(_("%u selected (%s)"),
				n_selected, format_double_size(size));
	}

	gtk_label_set_text(GTK_LABEL(filer_window->toolbar_text), label);

	g_free(label);
}

/* Create, destroy or recreate toolbar for this window so that it
 * matches the option setting.
 */
void toolbar_update_toolbar(FilerWindow *filer_window)
{
	g_return_if_fail(filer_window != NULL);

	if (filer_window->toolbar)
	{
		gtk_widget_destroy(filer_window->toolbar);
		filer_window->toolbar = NULL;
	}
	filer_window->toolbar_back = NULL;
	filer_window->toolbar_forward = NULL;

	if (o_toolbar.int_value != TOOLBAR_NONE)
	{
		filer_window->toolbar = create_toolbar(filer_window);
		gtk_box_pack_start(filer_window->toplevel_vbox,
				filer_window->toolbar, FALSE, TRUE, 0);
		gtk_box_reorder_child(filer_window->toplevel_vbox,
				filer_window->toolbar, 0);
		gtk_widget_show_all(filer_window->toolbar);
	}

	filer_target_mode(filer_window, NULL, NULL, NULL);
	toolbar_update_info(filer_window);
	toolbar_update_navigation(filer_window);
}

/* Agregado por josejp2424 (2026): mantener desactivados los botones cuando
 * no existe una ruta anterior o posterior en el historial de esta ventana. */
void toolbar_update_navigation(FilerWindow *filer_window)
{
	if (!filer_window)
		return;
	if (filer_window->toolbar_back)
		gtk_widget_set_sensitive(filer_window->toolbar_back,
			filer_history_can_back(filer_window));
	if (filer_window->toolbar_forward)
		gtk_widget_set_sensitive(filer_window->toolbar_forward,
			filer_history_can_forward(filer_window));
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

/* Wrapper for gtk_get_current_event() which creates a fake release event
 * if there is no current event. This is for ATK.
 */
static GdkEvent *get_current_event(int default_type)
{
	GdkEvent *event;

	event = gtk_get_current_event();

	if (event)
		return event;

	event = gdk_event_new(default_type);
	if (default_type == GDK_BUTTON_PRESS || default_type == GDK_BUTTON_RELEASE)
	{
		GdkEventButton *bev;
		bev = (GdkEventButton *) event;
		bev->button = 1;
	}
	return event;
}

static void toolbar_refresh_clicked(GtkWidget *widget,
				    FilerWindow *filer_window)
{
	GdkEvent	*event;

	event = get_current_event(GDK_BUTTON_RELEASE);
	if (event->type == GDK_BUTTON_RELEASE &&
			((GdkEventButton *) event)->button != 1)
	{
		filer_opendir(filer_window->sym_path, filer_window, NULL);
	}
	else
		filer_refresh(filer_window);
	gdk_event_free(event);
}

static void toolbar_home_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	GdkEvent	*event;

	event = get_current_event(GDK_BUTTON_RELEASE);
	if (event->type == GDK_BUTTON_RELEASE && NEW_WIN_BUTTON(event))
	{
		filer_opendir(home_dir, filer_window, NULL);
	}
	else
		filer_change_to(filer_window, home_dir, NULL);
	gdk_event_free(event);
}

static void toolbar_bookmarks_clicked(GtkWidget *widget,
				      FilerWindow *filer_window)
{
	(void) widget;
	g_return_if_fail(filer_window != NULL);

	/* GtkToolButton::clicked is emitted after the button release.  The old
	 * code asked for a BUTTON_PRESS event here, which made a normal left
	 * click unreliable.  Editing is already available from the bookmarks
	 * menu itself, so a normal click should always open that menu. */
	bookmarks_show_menu(filer_window);
}

static void toolbar_back_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	(void) widget;
	filer_history_back(filer_window);
}

static void toolbar_forward_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	(void) widget;
	filer_history_forward(filer_window);
}

static void toolbar_up_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	GdkEvent	*event;

	event = get_current_event(GDK_BUTTON_RELEASE);
	if (event->type == GDK_BUTTON_RELEASE && NEW_WIN_BUTTON(event))
	{
		filer_open_parent(filer_window);
	}
	else
		change_to_parent(filer_window);
	gdk_event_free(event);
}

static void toolbar_autosize_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	GdkEventButton	*bev;

	bev = (GdkEventButton *) get_current_event(GDK_BUTTON_RELEASE);
	if (bev->type == GDK_BUTTON_RELEASE)
	{
		display_set_layout(filer_window, AUTO_SIZE_ICONS, filer_window->details_type,
				TRUE);
		display_set_default_size(AUTO_SIZE_ICONS);
	}
	gdk_event_free((GdkEvent *) bev);
}

static void toolbar_zoom_out_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	(void) widget;
	display_change_size(filer_window, FALSE);
}

static void toolbar_zoom_in_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	(void) widget;
	display_change_size(filer_window, TRUE);
}

/* Modificado por josejp2424 (2026): se eliminó el callback de
 * ordenación de la barra. La vista usa carpetas primero y nombre ascendente. */

static void toolbar_details_clicked(GtkWidget *widget,
				    FilerWindow *filer_window)
{
	GdkEvent *event = get_current_event(GDK_BUTTON_RELEASE);

	if (event->type == GDK_BUTTON_RELEASE &&
		((GdkEventButton *)event)->button == 1)
	{
		if (filer_window->view_type == VIEW_TYPE_DETAILS)
			filer_set_view_type(filer_window, VIEW_TYPE_COLLECTION);
		else
			filer_set_view_type(filer_window, VIEW_TYPE_DETAILS);
	}
	else
	{
		DetailsType action;

		if (((GdkEventButton *)event)->button == 2)
			action = DETAILS_NONE;
		else if (filer_window->view_type != VIEW_TYPE_COLLECTION)
			action = filer_window->details_type;
		else
			switch (filer_window->details_type)
			{
				case DETAILS_NONE:
					action = DETAILS_SIZE;
					break;
				case DETAILS_SIZE:
					action = DETAILS_PERMISSIONS;
					break;
				case DETAILS_PERMISSIONS:
					action = DETAILS_TYPE;
					break;
				case DETAILS_TYPE:
					action = DETAILS_TIMES;
					break;
				case DETAILS_TIMES:
					action = DETAILS_NONE;
					break;
				default:
					action = DETAILS_NONE;
					break;
			}

		if (filer_window->view_type != VIEW_TYPE_COLLECTION)
			filer_set_view_type(filer_window, VIEW_TYPE_COLLECTION);

		if (action != filer_window->details_type)
			display_set_layout(filer_window,
					filer_window->display_style_wanted,
					action,
					FALSE);
	}

	gdk_event_free(event);
}

static void toolbar_hidden_clicked(GtkWidget *widget,
				   FilerWindow *filer_window)
{
	GdkEvent	*event;

	event = get_current_event(GDK_BUTTON_RELEASE);
	if(event->type == GDK_BUTTON_RELEASE) {
		if(((GdkEventButton*)event)->button == 1)
			display_set_hidden(filer_window, !filer_window->show_hidden);
		else if(((GdkEventButton*)event)->button == 2) {
			display_set_hidden(filer_window, o_display_show_hidden.int_value);
			display_set_thumbs(filer_window, o_display_show_thumbs.int_value);
		} else
			display_set_thumbs(filer_window, !filer_window->show_thumbs);
	}
}

/* Modificado por josejp2424: se eliminó el filtro Dirs/Files de la barra,
 * ya que podía ocultar archivos normales accidentalmente. */
static gboolean invert_cb(ViewIter *iter, gpointer data)
{
	return !view_get_selected((ViewIface *) data, iter);
}

static void toolbar_select_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	GdkEvent	*event;

	event = get_current_event(GDK_BUTTON_RELEASE);
	if (event->type == GDK_BUTTON_RELEASE)
	{
		if (((GdkEventButton *) event)->button == 1)
			view_select_all(filer_window->view);
		else
			view_select_if(filer_window->view, invert_cb,
				       filer_window->view);
	}
	filer_window->temp_item_selected = FALSE;
	gdk_event_free(event);
}

static gboolean toolbar_new_create_menu_proxy(GtkToolItem *tool_item,
                                                FilerWindow *filer_window)
{
	GtkWidget *proxy;
	GtkWidget *submenu;

	g_return_val_if_fail(GTK_IS_TOOL_ITEM(tool_item), FALSE);
	g_return_val_if_fail(filer_window != NULL, FALSE);

	/* Rox-Filer2 2.12.2-14:
	 * GtkToolbar creates a flat proxy item for GtkMenuToolButton when an item
	 * moves into the overflow menu.  Override that proxy so New remains the
	 * same submenu the user sees in the normal context menu:  + New >.
	 * menu_item_new_with_icon() also keeps the icon in the fixed GTK menu slot,
	 * so the plus is aligned with Search, Trash and the other overflow items. */
	proxy = menu_item_new_with_icon(_("New"), ROX_ICON_ADD);
	submenu = create_menu_new(filer_window);
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(proxy), submenu);
	gtk_widget_show(proxy);

	gtk_tool_item_set_proxy_menu_item(tool_item,
	                                  "rox-new-overflow", proxy);
	return TRUE;
}

static void toolbar_new_show_menu(GtkMenuToolButton *button,
                                  FilerWindow *filer_window)
{
	GtkWidget *menu;

	g_return_if_fail(GTK_IS_MENU_TOOL_BUTTON(button));
	g_return_if_fail(filer_window != NULL);

	/* Regenerar para que las plantillas de New estén siempre actualizadas. */
	menu = create_menu_new(filer_window);
	gtk_menu_tool_button_set_menu(button, menu);
}

static void toolbar_new_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	GtkWidget *menu;

	g_return_if_fail(widget != NULL);
	g_return_if_fail(filer_window != NULL);

	/* Rox-Filer2 2.12.2-13: New vuelve a ser un verdadero botón de menú
	 * GTK3. El mismo signo + abre su menú anclado debajo del botón y la
	 * flecha del GtkMenuToolButton ofrece exactamente el mismo submenú. */
	if (GTK_IS_MENU_TOOL_BUTTON(widget))
	{
		toolbar_new_show_menu(GTK_MENU_TOOL_BUTTON(widget), filer_window);
		menu = gtk_menu_tool_button_get_menu(GTK_MENU_TOOL_BUTTON(widget));
		if (menu)
			gtk_menu_popup_at_widget(GTK_MENU(menu), widget,
				GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
		return;
	}

	/* Fallback defensivo para toolbars creadas por configuraciones antiguas. */
	menu = create_menu_new(filer_window);
	g_signal_connect_swapped(menu, "selection-done",
		G_CALLBACK(gtk_widget_destroy), menu);
	gtk_menu_popup_at_widget(GTK_MENU(menu), widget,
		GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, NULL);
}

static void toolbar_search_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	(void)widget;
	search_integration_launch(filer_window);
}

static void toolbar_pair_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	(void)widget;
	filer_pair_open(filer_window, NULL, NULL);
}

static void toolbar_preferences_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	(void)widget;
	(void)filer_window;
	/* Same entry point used by the normal Options menu.  The command-line
	 * equivalent is ROX-Filer --config-rox. */
	menu_show_options(NULL, 0, NULL);
}

static void toolbar_compact_tool_item(GtkToolItem *item)
{
    static GtkCssProvider *provider = NULL;
    GtkWidget *child;

    if (!item || !GTK_IS_BIN(item))
        return;
    child = gtk_bin_get_child(GTK_BIN(item));
    if (!child)
        return;

    if (!provider) {
        provider = gtk_css_provider_new();
        gtk_css_provider_load_from_data(provider,
            "* { padding-top: 1px; padding-bottom: 1px;"
            " padding-left: 4px; padding-right: 4px;"
            " margin-top: 0; margin-bottom: 0;"
            " min-height: 0; min-width: 0; }",
            -1, NULL);
    }
    gtk_style_context_add_provider(gtk_widget_get_style_context(child),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

static Tool *toolbar_find_tool(const gchar *name)
{
	guint i;

	if (!name || !*name)
		return NULL;

	for (i = 0; i < G_N_ELEMENTS(all_tools); i++)
		if (g_strcmp0(all_tools[i].name, name) == 0)
			return &all_tools[i];

	return NULL;
}

/* Return a complete, duplicate-free toolbar order.  Unknown IDs are ignored
 * and tools added by newer versions are appended in their built-in order. */
static GPtrArray *toolbar_ordered_tools(void)
{
	GPtrArray *ordered = g_ptr_array_new();
	GHashTable *seen = g_hash_table_new(g_direct_hash, g_direct_equal);
	gchar **parts = g_strsplit((const gchar *) o_toolbar_order.value, ",", -1);
	guint i;

	if (parts)
	{
		for (i = 0; parts[i]; i++)
		{
			Tool *tool;
			gchar *name = g_strstrip(parts[i]);

			tool = toolbar_find_tool(name);
			if (tool && !g_hash_table_contains(seen, tool))
			{
				g_ptr_array_add(ordered, tool);
				g_hash_table_add(seen, tool);
			}
		}
		g_strfreev(parts);
	}

	for (i = 0; i < G_N_ELEMENTS(all_tools); i++)
	{
		Tool *tool = &all_tools[i];
		if (!g_hash_table_contains(seen, tool))
			g_ptr_array_add(ordered, tool);
	}

	g_hash_table_destroy(seen);
	return ordered;
}

/* If filer_window is NULL, the toolbar is for the options window */
static GtkWidget *create_toolbar(FilerWindow *filer_window)
{
	GtkWidget *bar;
	GtkWidget *b;
	GPtrArray *ordered_tools;
	guint i;
	int width;

	bar = gtk_toolbar_new();
	/* Let GTK move items to its overflow menu when the window is narrow
	 * instead of making the toolbar dictate a very large natural width. */
	gtk_toolbar_set_show_arrow(GTK_TOOLBAR(bar), TRUE);
	if (filer_window)
	{
		filer_window->toolbar_back = NULL;
		filer_window->toolbar_forward = NULL;
	}

	if (o_toolbar.int_value == TOOLBAR_NORMAL || !filer_window)
		gtk_toolbar_set_style(GTK_TOOLBAR(bar), GTK_TOOLBAR_ICONS);
	else if (o_toolbar.int_value == TOOLBAR_HORIZONTAL)
		gtk_toolbar_set_style(GTK_TOOLBAR(bar), GTK_TOOLBAR_BOTH_HORIZ);
	else
		gtk_toolbar_set_style(GTK_TOOLBAR(bar), GTK_TOOLBAR_BOTH);

	width=0;

	/* Partitions is permanent and always occupies the first toolbar position.
	 * It is represented as a fixed row in Options, but it cannot be hidden or
	 * reordered.  All regular tools follow in the user's chosen order. */
	if (filer_window)
	{
		GtkToolItem *drive_item = drives_toolbar_button_new(filer_window);
		GtkToolItem *separator = gtk_separator_tool_item_new();
		GtkRequisition drive_req;

		gtk_toolbar_insert(GTK_TOOLBAR(bar), drive_item, -1);
		toolbar_compact_tool_item(drive_item);
		gtk_separator_tool_item_set_draw(
			GTK_SEPARATOR_TOOL_ITEM(separator), TRUE);
		gtk_toolbar_insert(GTK_TOOLBAR(bar), separator, -1);
		gtk_widget_get_preferred_size(GTK_WIDGET(drive_item), NULL,
			&drive_req);
		width += drive_req.width;
	}

	ordered_tools = toolbar_ordered_tools();
	for (i = 0; i < ordered_tools->len; i++)
	{
		Tool	*tool = g_ptr_array_index(ordered_tools, i);
		GtkRequisition req;

		if (filer_window && !tool->enabled)
			continue;
		if (filer_window && tool->clicked == toolbar_search_clicked &&
		    !search_integration_toolbar_enabled())
			continue;
		if (filer_window && tool->clicked == toolbar_pair_clicked &&
		    !filer_pair_is_enabled())
			continue;

		b = add_button(bar, tool, filer_window);

		/* Agregado por josejp2424 (2026): conservar referencias sólo a los
		 * dos botones de historial para actualizar su sensibilidad. */
		if (filer_window && tool->clicked == toolbar_back_clicked)
			filer_window->toolbar_back = b;
		else if (filer_window && tool->clicked == toolbar_forward_clicked)
			filer_window->toolbar_forward = b;

		gtk_widget_get_preferred_size(b, NULL, &req);
		width += req.width;

		if (filer_window && tool->drop_action != DROP_NONE)
			handle_drops(filer_window, b, tool->drop_action);
	}
	g_ptr_array_free(ordered_tools, TRUE);

	if (filer_window)
	{
		GtkToolItem *trash_item = rox_trash_toolbar_button_new(filer_window);
		GtkRequisition trash_req;
		gtk_toolbar_insert(GTK_TOOLBAR(bar), trash_item, -1);
		toolbar_compact_tool_item(trash_item);
		gtk_widget_get_preferred_size(GTK_WIDGET(trash_item), NULL, &trash_req);
		width += trash_req.width;

		/* Settings is intentionally the final toolbar item. */
		b = add_button(bar, &toolbar_preferences_tool, filer_window);
		gtk_widget_get_preferred_size(b, NULL, &trash_req);
		width += trash_req.width;
	}

	if (filer_window)
	{
		if(o_toolbar_min_width.int_value)
		{
			/* Make the toolbar wide enough for all configured icons. */
			gtk_widget_set_size_request(bar, width+32, -1);
		} else {
			gtk_widget_set_size_request(bar, 100, -1);
		}
	}

	return bar;
}

/* This is used to simulate a click when button 3 is used (GtkButton
 * normally ignores this).
 */
static gint toolbar_other_button = 0;
static gint toolbar_button_pressed(GtkWidget *button,
				GdkEventButton *event,
				FilerWindow *filer_window)
{
	gint	b = event->button;
	Tool	*tool;

	tool = g_object_get_data(G_OBJECT(button), "rox-tool");
	g_return_val_if_fail(tool != NULL, TRUE);

	if (tool->menu && b == 1)
	{
		tool->clicked((GtkWidget *) button, filer_window);
		return TRUE;
	}

	if ((b == 2 || b == 3) && toolbar_other_button == 0)
	{
		toolbar_other_button = event->button;
		return TRUE;
	}

	return FALSE;
}

static gint toolbar_button_released(GtkWidget *button,
				GdkEventButton *event,
				FilerWindow *filer_window)
{
	if (event->button == toolbar_other_button)
	{
		toolbar_other_button = 0;
		g_signal_emit_by_name(button, "clicked");
		return TRUE;
	}

	return FALSE;
}

/* If filer_window is NULL, the toolbar is for the options window */
static GtkWidget *add_button(GtkWidget *bar, Tool *tool,
				FilerWindow *filer_window)
{
	GtkToolItem *item;
	GtkWidget *button;
	GtkWidget *icon_widget;

	/* Rox-Filer2 2.12.2-13: New es un GtkMenuToolButton real. El signo +
	 * conserva su acción y el menú queda unido al propio botón, sin GUI
	 * separada. Los demás botones siguen siendo GtkToolButton normales. */
	icon_widget = image_new_icon(tool->name,
					      GTK_ICON_SIZE_LARGE_TOOLBAR);
	if (filer_window && tool->clicked == toolbar_new_clicked)
	{
		item = gtk_menu_tool_button_new(icon_widget, _(tool->label));
		gtk_menu_tool_button_set_menu(GTK_MENU_TOOL_BUTTON(item),
			create_menu_new(filer_window));
		g_signal_connect(item, "show-menu",
			G_CALLBACK(toolbar_new_show_menu), filer_window);
		g_signal_connect(item, "create-menu-proxy",
			G_CALLBACK(toolbar_new_create_menu_proxy), filer_window);
	}
	else if (filer_window)
	{
		item = gtk_tool_button_new(icon_widget, _(tool->label));
	}
	else
	{
		item = gtk_toggle_tool_button_new();
		gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(item), icon_widget);
		gtk_tool_button_set_label(GTK_TOOL_BUTTON(item), _(tool->label));
	}

	/* GTK3 themes (especially under Wayland) may give homogeneous toolbar
	 * items a lot of extra horizontal space. Keep each item at its natural
	 * icon/label width; the overflow arrow handles narrow windows. */
	gtk_tool_item_set_homogeneous(item, FALSE);
	gtk_tool_item_set_tooltip_text(item, _(tool->tip));
	gtk_toolbar_insert(GTK_TOOLBAR(bar), item, -1);
	toolbar_compact_tool_item(item);

	button = GTK_WIDGET(item);
	gtk_widget_set_can_focus(button, FALSE);
	g_object_set_data(G_OBJECT(button), "rox-tool", tool);

	if (filer_window)
	{
		g_signal_connect(item, "clicked",
			G_CALLBACK(tool->clicked), filer_window);
		g_signal_connect(button, "button-press-event",
			G_CALLBACK(toolbar_button_pressed), filer_window);
		g_signal_connect(button, "button-release-event",
			G_CALLBACK(toolbar_button_released), filer_window);
	}
	else
	{
		/* The options page no longer uses these preview toggle items; its
		 * custom row widget owns visibility and order controls. */
	}

	return filer_window ? button : GTK_WIDGET(item);
}

/* Called during the drag when the mouse is in a widget registered
 * as a drop target. Returns TRUE if we can accept the drop.
 */
static gboolean drag_motion(GtkWidget		*widget,
                            GdkDragContext	*context,
                            gint		x,
                            gint		y,
                            guint		time,
			    FilerWindow		*filer_window)
{
	GdkDragAction	action = gdk_drag_context_get_suggested_action(context);
	DropDest	dest;
	gpointer	type = (gpointer) drop_dest_dir;

	dest = (DropDest) g_object_get_data(G_OBJECT(widget), "toolbar_dest");

	if ((gdk_drag_context_get_actions(context) & GDK_ACTION_ASK) && o_dnd_left_menu.int_value &&
		dest != DROP_BOOKMARK)
	{
		guint state;
		rox_gdk_window_get_pointer(NULL, NULL, NULL, &state);
		if (state & GDK_BUTTON1_MASK)
			action = GDK_ACTION_ASK;
	}

	if (dest == DROP_TO_HOME)
		g_dataset_set_data(context, "drop_dest_path",
				   (gchar *) home_dir);
	else if (dest == DROP_BOOKMARK)
		type = (gpointer) drop_dest_bookmark;
	else
		g_dataset_set_data_full(context, "drop_dest_path",
				g_path_get_dirname(filer_window->sym_path),
				g_free);

	g_dataset_set_data(context, "drop_dest_type", type);
	gdk_drag_status(context, action, time);

	dnd_spring_load(context, filer_window);
	gtk_button_set_relief(GTK_BUTTON(widget), GTK_RELIEF_NORMAL);

	return TRUE;
}

static void drag_leave(GtkWidget	*widget,
                       GdkDragContext	*context,
		       guint32		time,
		       FilerWindow	*filer_window)
{
	gtk_button_set_relief(GTK_BUTTON(widget), GTK_RELIEF_NONE);
	dnd_spring_abort();
}

static void handle_drops(FilerWindow *filer_window,
			 GtkWidget *button,
			 DropDest dest)
{
	make_drop_target(button, 0);
	g_signal_connect(button, "drag_motion",
			G_CALLBACK(drag_motion), filer_window);
	g_signal_connect(button, "drag_leave",
			G_CALLBACK(drag_leave), filer_window);
	g_object_set_data(G_OBJECT(button), "toolbar_dest", (gpointer) dest);
}

static void tally_items(gpointer key, gpointer value, gpointer data)
{
	DirItem *item = (DirItem *) value;
	int     *tally = (int *) data;

	if (!filer_match_filter(filer_window_being_counted, item))
		(*tally)++;
}

static void option_notify(void)
{
	guint		i;
	gboolean	changed = FALSE;
	guchar		*list = o_toolbar_disable.value;

	for (i = 0; i < G_N_ELEMENTS(all_tools); i++)
	{
		Tool	*tool = &all_tools[i];
		gboolean old = tool->enabled;

		tool->enabled = !in_list(tool->name, list);

		if (old != tool->enabled)
			changed = TRUE;
	}

	if (changed || o_toolbar.has_changed || o_toolbar_info.has_changed ||
	    o_toolbar_order.has_changed)
	{
		GList	*next;

		for (next = all_filer_windows; next; next = next->next)
		{
			FilerWindow *filer_window = (FilerWindow *) next->data;

			toolbar_update_toolbar(filer_window);
		}
	}
}

/* One configurable row in Options -> Toolbar. Visibility keeps the old
 * checkbox semantics. Position is a number so a button can be moved from one
 * end of the toolbar to the other with a single edit instead of many clicks. */
static GtkWidget *tool_option_row_new(Tool *tool)
{
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *check = gtk_check_button_new();
	GtkWidget *image = image_new_icon(tool->name, GTK_ICON_SIZE_BUTTON);
	GtkWidget *label = gtk_label_new(_(tool->label));
	GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget *position_label = gtk_label_new(_("Position:"));
	GtkAdjustment *adjustment = gtk_adjustment_new(2, 2,
		G_N_ELEMENTS(all_tools) + 1, 1, 1, 0);
	GtkWidget *spin = gtk_spin_button_new(adjustment, 1, 0);

	gtk_widget_set_halign(label, GTK_ALIGN_START);
	gtk_widget_set_hexpand(spacer, TRUE);
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
	gtk_spin_button_set_wrap(GTK_SPIN_BUTTON(spin), FALSE);
	gtk_entry_set_width_chars(GTK_ENTRY(spin), 2);
	gtk_widget_set_tooltip_text(spin,
		_("Choose this button's position in the toolbar"));

	gtk_box_pack_start(GTK_BOX(row), check, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row), image, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row), label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row), spacer, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(row), position_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row), spin, FALSE, FALSE, 0);

	g_object_set_data(G_OBJECT(row), "rox-toolbar-tool", tool);
	g_object_set_data(G_OBJECT(row), "rox-toolbar-check", check);
	g_object_set_data(G_OBJECT(row), "rox-toolbar-position", spin);

	g_signal_connect(check, "toggled", G_CALLBACK(tool_visibility_toggled), row);
	g_signal_connect(spin, "value-changed",
		G_CALLBACK(tool_position_changed), row);
	return row;
}

static GtkWidget *tool_option_fixed_partitions_row_new(void)
{
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	GtkWidget *check = gtk_check_button_new();
	GtkWidget *image = image_new_icon(ROX_ICON_MOUNT, GTK_ICON_SIZE_BUTTON);
	GtkWidget *label = gtk_label_new(_("Partitions (fixed)"));
	GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	GtkWidget *position_label = gtk_label_new(_("Position:"));
	GtkWidget *position = gtk_label_new("1");

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), TRUE);
	gtk_widget_set_sensitive(check, FALSE);
	gtk_widget_set_halign(label, GTK_ALIGN_START);
	gtk_widget_set_hexpand(spacer, TRUE);
	gtk_box_pack_start(GTK_BOX(row), check, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row), image, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row), label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row), spacer, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(row), position_label, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(row), position, FALSE, FALSE, 7);
	return row;
}

static void tool_visibility_toggled(GtkToggleButton *widget, gpointer data)
{
	(void) widget;
	(void) data;
	option_check_widget(&o_toolbar_disable);
}

static void update_tool_position_spins(GtkWidget *box)
{
	GList *kids, *next;
	gint position = 2; /* Partitions is always position 1. */

	if (!box || !GTK_IS_BOX(box))
		return;

	updating_tool_positions = TRUE;
	kids = gtk_container_get_children(GTK_CONTAINER(box));
	for (next = kids; next; next = next->next)
	{
		GtkWidget *row = GTK_WIDGET(next->data);
		GtkWidget *spin;

		if (!g_object_get_data(G_OBJECT(row), "rox-toolbar-tool"))
			continue;
		spin = g_object_get_data(G_OBJECT(row), "rox-toolbar-position");
		if (spin)
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), position++);
	}
	g_list_free(kids);
	updating_tool_positions = FALSE;
}

static void tool_position_changed(GtkSpinButton *spin, gpointer data)
{
	GtkWidget *row = GTK_WIDGET(data);
	GtkWidget *box;
	GList *kids;
	gint current_index;
	gint target_index;
	gint count;

	if (updating_tool_positions)
		return;

	box = gtk_widget_get_parent(row);
	if (!box || !GTK_IS_BOX(box))
		return;

	kids = gtk_container_get_children(GTK_CONTAINER(box));
	current_index = g_list_index(kids, row);
	count = g_list_length(kids);
	target_index = gtk_spin_button_get_value_as_int(spin) - 1;

	/* Child 0 is Partitions. It can never be replaced or moved. GtkBox
	 * automatically shifts the intervening rows, so every position remains
	 * unique and all displayed numbers are renumbered immediately. */
	if (current_index > 0)
	{
		if (target_index < 1)
			target_index = 1;
		if (target_index >= count)
			target_index = count - 1;

		if (target_index != current_index)
			gtk_box_reorder_child(GTK_BOX(box), row, target_index);
	}
	g_list_free(kids);

	update_tool_position_spins(box);
	option_check_widget(&o_toolbar_order);
}

static void update_tools(Option *option)
{
	GList *kids, *next;

	kids = gtk_container_get_children(GTK_CONTAINER(option->widget));
	for (next = kids; next; next = next->next)
	{
		GtkWidget *row = GTK_WIDGET(next->data);
		Tool *tool = g_object_get_data(G_OBJECT(row), "rox-toolbar-tool");
		GtkWidget *check;

		if (!tool)
			continue;
		check = g_object_get_data(G_OBJECT(row), "rox-toolbar-check");
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check),
			!in_list(tool->name, option->value));
	}
	g_list_free(kids);
}

static guchar *read_tools(Option *option)
{
	GList *kids, *next;
	GString *list = g_string_new(NULL);
	guchar *retval;

	kids = gtk_container_get_children(GTK_CONTAINER(option->widget));
	for (next = kids; next; next = next->next)
	{
		GtkWidget *row = GTK_WIDGET(next->data);
		Tool *tool = g_object_get_data(G_OBJECT(row), "rox-toolbar-tool");
		GtkWidget *check;

		if (!tool)
			continue;
		check = g_object_get_data(G_OBJECT(row), "rox-toolbar-check");
		if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check)))
		{
			if (list->len)
				g_string_append(list, ", ");
			g_string_append(list, tool->name);
		}
	}
	g_list_free(kids);
	retval = (guchar *) g_string_free(list, FALSE);
	return retval;
}

static GtkWidget *find_tool_option_row(GtkWidget *box, Tool *tool)
{
	GList *kids, *next;
	GtkWidget *found = NULL;

	kids = gtk_container_get_children(GTK_CONTAINER(box));
	for (next = kids; next; next = next->next)
	{
		GtkWidget *row = GTK_WIDGET(next->data);
		if (g_object_get_data(G_OBJECT(row), "rox-toolbar-tool") == tool)
		{
			found = row;
			break;
		}
	}
	g_list_free(kids);
	return found;
}

static void update_tool_order(Option *option)
{
	GtkWidget *box = option->widget;
	GPtrArray *ordered;
	guint i;
	gint position = 1; /* 0 is fixed Partitions. */

	if (!box)
		return;

	ordered = toolbar_ordered_tools();
	for (i = 0; i < ordered->len; i++)
	{
		Tool *tool = g_ptr_array_index(ordered, i);
		GtkWidget *row = find_tool_option_row(box, tool);
		if (row)
			gtk_box_reorder_child(GTK_BOX(box), row, position++);
	}
	g_ptr_array_free(ordered, TRUE);
	update_tool_position_spins(box);
}

static guchar *read_tool_order(Option *option)
{
	GList *kids, *next;
	GString *list = g_string_new(NULL);
	guchar *retval;

	kids = gtk_container_get_children(GTK_CONTAINER(option->widget));
	for (next = kids; next; next = next->next)
	{
		GtkWidget *row = GTK_WIDGET(next->data);
		Tool *tool = g_object_get_data(G_OBJECT(row), "rox-toolbar-tool");
		if (!tool)
			continue;
		if (list->len)
			g_string_append_c(list, ',');
		g_string_append(list, tool->name);
	}
	g_list_free(kids);
	retval = (guchar *) g_string_free(list, FALSE);
	return retval;
}

static GList *build_tool_options(Option *option, xmlNode *node, guchar *label)
{
	GtkWidget *box;
	GPtrArray *ordered;
	guint i;

	(void) node;
	(void) label;
	g_return_val_if_fail(option != NULL, NULL);

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
	gtk_box_pack_start(GTK_BOX(box), tool_option_fixed_partitions_row_new(),
		FALSE, FALSE, 0);

	ordered = toolbar_ordered_tools();
	for (i = 0; i < ordered->len; i++)
	{
		Tool *tool = g_ptr_array_index(ordered, i);
		gtk_box_pack_start(GTK_BOX(box), tool_option_row_new(tool),
			FALSE, FALSE, 0);
	}
	g_ptr_array_free(ordered, TRUE);

	/* Visibility and order share one customiser.  Each option keeps its own
	 * reader so either checkbox or move changes are applied immediately. */
	option->update_widget = update_tools;
	option->read_widget = read_tools;
	option->widget = box;
	o_toolbar_order.update_widget = update_tool_order;
	o_toolbar_order.read_widget = read_tool_order;
	o_toolbar_order.widget = box;

	update_tool_position_spins(box);
	return g_list_append(NULL, box);
}
