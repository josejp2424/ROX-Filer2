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

/* view_collection.c - a subclass of Collection, used for displaying files */

/*
 * Modificado por josejp2424 (2026):
 * port a GTK3 y adaptaciones específicas de esta versión.
 */

#include "config.h"

#include <gtk/gtk.h>
#include <time.h>
#include <math.h>
#include <pango/pangocairo.h>
#include <string.h>

#include "global.h"

#include "collection.h"
#include "view_iface.h"
#include "view_collection.h"
#include "type.h"
#include "pixmaps.h"
#include "dir.h"
#include "diritem.h"
#include "gui_support.h"
#include "support.h"
#include "dnd.h"
#include "bind.h"
#include "options.h"

#include "toolbar.h"	/* for resizing */
#include "filer.h"
#include "display.h"
#include "menu.h"

#define MIN_ITEM_WIDTH 64

static gpointer parent_class = NULL;

struct _ViewCollectionClass {
	GtkViewportClass parent;
};

struct _ViewCollection {
	GtkViewport viewport;

	Collection *collection;
	FilerWindow *filer_window;	/* Used for styles, etc */

	int	cursor_base;		/* Cursor when minibuffer opened */
};

typedef struct _Template Template;

struct _Template {
	GdkRectangle	icon;
	GdkRectangle	leafname;
	GdkRectangle	details;
};


/* Static prototypes */
static void view_collection_dispose(GObject *object);
static void view_collection_finalize(GObject *object);
static void view_collection_class_init(gpointer gclass, gpointer data);
static void view_collection_init(GTypeInstance *object, gpointer gclass);

static void draw_item(GtkWidget *widget,
			cairo_t *cr,
			CollectionItem *item,
			GdkRectangle *area,
			gpointer user_data);
static void fill_template(GdkRectangle *area, CollectionItem *item,
			ViewCollection *view_collection, Template *template);
static void huge_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template);
static void large_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template);
static void small_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template);
static void huge_full_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template);
static void large_full_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template);
static void small_full_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template);
static gboolean test_point(Collection *collection,
				int point_x, int point_y,
				CollectionItem *item,
				int width, int height,
				gpointer user_data);
static void draw_string(GtkWidget *widget,
		cairo_t      *cr,
		PangoLayout  *layout,
		GdkRectangle *area,  /* Area available on screen */
		int          width,  /* Width of the full string */
		GtkStateFlags selection_state,
		const GdkRGBA *normal_colour,
		gboolean box);
static void view_collection_iface_init(gpointer giface, gpointer iface_data);
static gint coll_motion_notify(GtkWidget *widget,
			       GdkEventMotion *event,
			       ViewCollection *view_collection);
static gint coll_button_release(GtkWidget *widget,
			        GdkEventButton *event,
			        ViewCollection *view_collection);
static gint coll_button_press(GtkWidget *widget,
			      GdkEventButton *event,
			      ViewCollection *view_collection);
static void size_allocate(GtkWidget *w, GtkAllocation *a, gpointer data);
static void style_updated(GtkWidget *widget,
		      ViewCollection *view_collection);
static void display_free_colitem(Collection *collection,
				 CollectionItem *colitem);
static void lost_selection(Collection  *collection,
			   guint        time,
			   gpointer     user_data);
static void selection_changed(Collection *collection,
			      gint time,
			      gpointer user_data);
static void calc_size(FilerWindow *filer_window, CollectionItem *colitem,
		int *width, int *height);
static void make_iter(ViewCollection *view_collection, ViewIter *iter,
		      IterFlags flags);
static void make_item_iter(ViewCollection *vc, ViewIter *iter, int i);

static void view_collection_sort(ViewIface *view);
static void view_collection_style_changed(ViewIface *view, int flags);
static void view_collection_add_items(ViewIface *view, GPtrArray *items);
static void view_collection_update_items(ViewIface *view, GPtrArray *items);
static void view_collection_delete_if(ViewIface *view,
			  gboolean (*test)(gpointer item, gpointer data),
			  gpointer data);
static void view_collection_clear(ViewIface *view);
static void view_collection_select_all(ViewIface *view);
static void view_collection_clear_selection(ViewIface *view);
static int view_collection_count_items(ViewIface *view);
static int view_collection_count_selected(ViewIface *view);
static void view_collection_show_cursor(ViewIface *view);
static void view_collection_get_iter(ViewIface *view,
				     ViewIter *iter, IterFlags flags);
static void view_collection_get_iter_at_point(ViewIface *view, ViewIter *iter,
					      GdkWindow *src, int x, int y);
static void view_collection_cursor_to_iter(ViewIface *view, ViewIter *iter);
static void view_collection_set_selected(ViewIface *view,
					 ViewIter *iter,
					 gboolean selected);
static gboolean view_collection_get_selected(ViewIface *view, ViewIter *iter);
static void view_collection_select_only(ViewIface *view, ViewIter *iter);
static void view_collection_set_frozen(ViewIface *view, gboolean frozen);
static void view_collection_wink_item(ViewIface *view, ViewIter *iter);
static void view_collection_autosize(ViewIface *view);
static gboolean view_collection_cursor_visible(ViewIface *view);
static void view_collection_set_base(ViewIface *view, ViewIter *iter);
static void view_collection_start_lasso_box(ViewIface *view,
					     GdkEventButton *event);
static void view_collection_extend_tip(ViewIface *view, ViewIter *iter,
					GString *tip);
static gboolean view_collection_auto_scroll_callback(ViewIface *view);

static DirItem *iter_next(ViewIter *iter);
static DirItem *iter_prev(ViewIter *iter);
static DirItem *iter_peek(ViewIter *iter);


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

GtkWidget *view_collection_new(FilerWindow *filer_window)
{
	ViewCollection *view_collection;
	GtkAdjustment *adj;

	view_collection = g_object_new(view_collection_get_type(), NULL);
	view_collection->filer_window = filer_window;

	/* GtkViewport owns the authoritative adjustment after construction.
	 * Do not read Collection's struct field directly here: Collection changed
	 * parent class in WIP 15 and a direct layout-dependent access produced the
	 * invalid pointer reported by GTK. */
	adj = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view_collection));
	if (!GTK_IS_ADJUSTMENT(adj))
	{
		GtkAdjustment *fresh = GTK_ADJUSTMENT(gtk_adjustment_new(
			0.0, 0.0, 0.0, 1.0, 1.0, 0.0));
		gtk_scrollable_set_vadjustment(GTK_SCROLLABLE(view_collection),
					 fresh);
		g_object_unref(fresh);
		adj = gtk_scrollable_get_vadjustment(
			GTK_SCROLLABLE(view_collection));
	}

	g_return_val_if_fail(GTK_IS_ADJUSTMENT(adj),
			     GTK_WIDGET(view_collection));
	collection_set_vadjustment(view_collection->collection, adj);

	if (filer_window && GTK_IS_RANGE(filer_window->scrollbar))
		gtk_range_set_adjustment(GTK_RANGE(filer_window->scrollbar), adj);

	return GTK_WIDGET(view_collection);
}

GType view_collection_get_type(void)
{
	static GType type = 0;

	if (!type)
	{
		static const GTypeInfo info =
		{
			sizeof (ViewCollectionClass),
			NULL,			/* base_init */
			NULL,			/* base_finalise */
			view_collection_class_init,
			NULL,			/* class_finalise */
			NULL,			/* class_data */
			sizeof(ViewCollection),
			0,			/* n_preallocs */
			view_collection_init
		};
		static const GInterfaceInfo iface_info =
		{
			view_collection_iface_init, NULL, NULL
		};

		type = g_type_register_static(gtk_viewport_get_type(),
						"ViewCollection", &info, 0);
		g_type_add_interface_static(type, VIEW_TYPE_IFACE, &iface_info);
	}

	return type;
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void view_collection_dispose(GObject *object)
{
	ViewCollection *view_collection = VIEW_COLLECTION(object);

	/* Release owned resources through the GObject dispose lifecycle. */
	view_collection->filer_window = NULL;
	G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void view_collection_finalize(GObject *object)
{
	G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void view_collection_grab_focus(GtkWidget *focus_widget)
{
	ViewCollection *view_collection = VIEW_COLLECTION(focus_widget);
	gtk_widget_grab_focus(GTK_WIDGET(view_collection->collection));
}

static void view_collection_class_init(gpointer gclass, gpointer data)
{
	GObjectClass *object = G_OBJECT_CLASS(gclass);
	GtkWidgetClass *widget = GTK_WIDGET_CLASS(gclass);

	parent_class = g_type_class_peek_parent(gclass);

	widget->grab_focus = view_collection_grab_focus;
	object->dispose = view_collection_dispose;
	object->finalize = view_collection_finalize;
}

static gboolean transparent_draw(GtkWidget *widget,
			cairo_t *cr,
			ViewCollection *view)
{
	GtkAllocation allocation;
	PangoLayout *layout;
	PangoFontDescription *font;

	if (!view->filer_window || !view->filer_window->directory ||
	    !view->filer_window->directory->error)
		return FALSE;

	gtk_widget_get_allocation(widget, &allocation);
	cairo_save(cr);
	cairo_set_source_rgba(cr, 0.9, 0.0, 0.0, 0.6);
	cairo_rectangle(cr, 0, 0, allocation.width, allocation.height);
	cairo_fill(cr);

	layout = gtk_widget_create_pango_layout(widget,
			view->filer_window->directory->error);
	font = pango_font_description_from_string("Sans Bold 15");
	pango_layout_set_font_description(layout, font);
	pango_layout_set_width(layout, MAX(1, allocation.width - 10) * PANGO_SCALE);
	pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
	cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 1.0);
	cairo_move_to(cr, 5, 5);
	pango_cairo_show_layout(cr, layout);
	pango_font_description_free(font);
	g_object_unref(layout);
	cairo_restore(cr);

	return FALSE;
}

static void view_collection_init(GTypeInstance *object, gpointer gclass)
{
	ViewCollection *view_collection = (ViewCollection *) object;
	GtkViewport *viewport = GTK_VIEWPORT(object);
	GtkWidget *collection;
	GtkAdjustment *adj;

	collection = collection_new();
	view_collection->collection = COLLECTION(collection);

	/* Draw directory errors after the collection content. */
	g_signal_connect_after(collection, "draw",
			G_CALLBACK(transparent_draw), object);

	/* Use one explicitly-created adjustment during instance initialisation.
	 * GtkViewport may replace it while finishing construction;
	 * view_collection_new() synchronises Collection with the final one. */
	adj = GTK_ADJUSTMENT(gtk_adjustment_new(
		0.0, 0.0, 0.0, 1.0, 1.0, 0.0));
	collection_set_vadjustment(view_collection->collection, adj);
	gtk_scrollable_set_vadjustment(GTK_SCROLLABLE(viewport), adj);
	g_object_unref(adj);
	gtk_scrollable_set_hadjustment(GTK_SCROLLABLE(viewport), NULL);

	/* Modificado por josejp2424 (2026): GtkViewport usa por defecto la
	 * requisición mínima del hijo para calcular el desplazamiento. Collection
	 * mantiene una altura mínima pequeña y una altura natural igual a todas
	 * sus filas; con GTK_SCROLL_MINIMUM el ajuste vertical quedaba limitado a
	 * la zona visible aunque el directorio tuviera miles de elementos. Usar la
	 * altura natural hace que el scrollbar abarque la colección completa. */
	gtk_scrollable_set_hscroll_policy(GTK_SCROLLABLE(viewport),
				 GTK_SCROLL_MINIMUM);
	gtk_scrollable_set_vscroll_policy(GTK_SCROLLABLE(viewport),
				 GTK_SCROLL_NATURAL);

	gtk_viewport_set_shadow_type(viewport, GTK_SHADOW_NONE);
	gtk_container_add(GTK_CONTAINER(object), collection);
	gtk_widget_show(collection);
	gtk_widget_set_size_request(GTK_WIDGET(view_collection), 4, 4);

	view_collection->collection->free_item = display_free_colitem;
	view_collection->collection->draw_item = draw_item;
	view_collection->collection->test_point = test_point;
	view_collection->collection->cb_user_data = view_collection;

	g_signal_connect(collection, "style-updated",
			G_CALLBACK(style_updated), view_collection);
	g_signal_connect(collection, "lose_selection",
			G_CALLBACK(lost_selection), view_collection);
	g_signal_connect(collection, "selection_changed",
			G_CALLBACK(selection_changed), view_collection);
	g_signal_connect(collection, "button-release-event",
			G_CALLBACK(coll_button_release), view_collection);
	g_signal_connect(collection, "button-press-event",
			G_CALLBACK(coll_button_press), view_collection);
	g_signal_connect(collection, "motion-notify-event",
			G_CALLBACK(coll_motion_notify), view_collection);
	g_signal_connect(viewport, "size-allocate",
			G_CALLBACK(size_allocate), view_collection);

	gtk_widget_add_events(collection,
			GDK_BUTTON1_MOTION_MASK | GDK_BUTTON2_MOTION_MASK |
			GDK_BUTTON3_MOTION_MASK | GDK_POINTER_MOTION_MASK |
			GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
			GDK_SCROLL_MASK);
}


static void draw_item(GtkWidget *widget,
			cairo_t *cr,
			CollectionItem *colitem,
			GdkRectangle *area,
			gpointer user_data)
{
	DirItem *item = (DirItem *) colitem->data;
	gboolean selected = colitem->selected;
	Template template;
	ViewData *view = (ViewData *) colitem->view_data;
	ViewCollection *view_collection = (ViewCollection *) user_data;
	FilerWindow *filer_window = view_collection->filer_window;
	GtkStyleContext *context = gtk_widget_get_style_context(widget);
	GtkStateFlags selection_state;
	GdkRGBA normal_colour = {0, 0, 0, 1};
	GdkRGBA selection_colour = {0.2, 0.4, 0.8, 1};
	GdkRGBA *type_colour;
	gboolean cut_visual;

	g_return_if_fail(view != NULL);
	g_return_if_fail(filer_window != NULL);

	selection_state = selected ? filer_window->selection_state
				   : GTK_STATE_FLAG_NORMAL;
	gtk_style_context_get_color(context, GTK_STATE_FLAG_NORMAL,
			&normal_colour);
	/* Modificado por josejp2424 (2026): no adivinar colores simbólicos.
	 * La vista de iconos se dibuja a mano, por lo que hay que colocar
	 * temporalmente el GtkStyleContext en el estado seleccionado y dejar que
	 * el tema GTK entregue el color real que usa para ese estado. */
	gtk_style_context_save(context);
	gtk_style_context_set_state(context, selection_state);
	rox_style_context_get_background(context, selection_state,
			&selection_colour);
	if (selection_colour.alpha <= 0.01 &&
	    !gtk_style_context_lookup_color(context, "theme_selected_bg_color",
			&selection_colour))
		selection_colour = normal_colour;
	gtk_style_context_restore(context);
	normal_colour.alpha = 1.0;
	selection_colour.alpha = 1.0;
	type_colour = type_get_colour(item, &normal_colour);

	fill_template(area, colitem, view_collection, &template);

	/* Rox-Filer2 2.12.2-26: keep Cut items visibly pending until they
	 * are pasted or the clipboard changes.  Fade only the file icon, not
	 * its label, so names remain easy to read. */
	cut_visual = menu_path_is_cut((const gchar *)make_path(
		filer_window->sym_path, item->leafname));
	if (cut_visual)
		cairo_push_group(cr);

	if (template.icon.width <= SMALL_WIDTH &&
	    template.icon.height <= SMALL_HEIGHT)
		draw_small_icon(cr, &template.icon, item, view->image,
				selected, &selection_colour);
	else if (template.icon.width <= ICON_WIDTH &&
		 template.icon.height <= ICON_HEIGHT)
		draw_large_icon(cr, &template.icon, item, view->image,
				selected, &selection_colour);
	else
		draw_huge_icon(cr, &template.icon, item, view->image,
				selected, &selection_colour);

	if (cut_visual)
	{
		cairo_pop_group_to_source(cr);
		cairo_paint_with_alpha(cr, 0.45);
	}

	draw_string(widget, cr, view->layout, &template.leafname,
			view->name_width, selection_state, type_colour, TRUE);
	if (view->details)
		draw_string(widget, cr, view->details, &template.details,
				template.details.width, selection_state,
				type_colour, TRUE);
}

/* A template contains the locations of the three rectangles (for the icon,
 * name and extra details).
 * Fill in the empty 'template' with the rectanges for this item.
 */
static void fill_template(GdkRectangle *area, CollectionItem *colitem,
			ViewCollection *view_collection, Template *template)
{
	DisplayStyle style = view_collection->filer_window->display_style;
	ViewData     *view = (ViewData *) colitem->view_data;

	if (view->details)
	{
		template->details.width = view->details_width;
		template->details.height = view->details_height;

		if (style == SMALL_ICONS)
			small_full_template(area, colitem,
						view_collection, template);
		else if (style == LARGE_ICONS)
			large_full_template(area, colitem,
						view_collection, template);
		else
			huge_full_template(area, colitem,
						view_collection, template);
	}
	else
	{
		if (style == HUGE_ICONS)
			huge_template(area, colitem,
					view_collection, template);
		else if (style == LARGE_ICONS)
			large_template(area, colitem,
					view_collection, template);
		else
			small_template(area, colitem,
					view_collection, template);
	}
}

static void huge_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template)
{
	ViewData *view = (ViewData *) colitem->view_data;
	MaskedPixmap *image = view->image;
	int col_width = view_collection->collection->item_width;
	int text_x, text_y;

	if (image)
	{
		if (!image->huge_pixbuf)
			pixmap_make_huge(image);
		template->icon.width = image->huge_width;
		template->icon.height = image->huge_height;
	}
	else
	{
		template->icon.width = HUGE_WIDTH * 3 / 2;
		template->icon.height = HUGE_HEIGHT;
	}

	template->leafname.width = view->name_width;
	template->leafname.height = view->name_height;

	text_x = area->x + ((col_width - template->leafname.width) >> 1);
	text_y = area->y + area->height - template->leafname.height;

	template->leafname.x = text_x;
	template->leafname.y = text_y;

	template->icon.x = area->x + ((col_width - template->icon.width) >> 1);
	template->icon.y = template->leafname.y - template->icon.height - 2;
}

static void large_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template)
{
	int col_width = view_collection->collection->item_width;
	int		iwidth, iheight;
	int		image_x;
	int		image_y;
	ViewData *view = (ViewData *) colitem->view_data;
	MaskedPixmap *image = view->image;

	int		text_x, text_y;

	if (image)
	{
		iwidth = MIN(image->width, ICON_WIDTH);
		iheight = MIN(image->height + 6, ICON_HEIGHT);
	}
	else
	{
		iwidth = ICON_WIDTH;
		iheight = ICON_HEIGHT;
	}
	image_x = area->x + ((col_width - iwidth) >> 1);

	template->leafname.width = view->name_width;
	template->leafname.height = view->name_height;

	text_x = area->x + ((col_width - template->leafname.width) >> 1);
	text_y = area->y + ICON_HEIGHT + 2;

	template->leafname.x = text_x;
	template->leafname.y = text_y;

	image_y = text_y - iheight;
	image_y = MAX(area->y, image_y);

	template->icon.x = image_x;
	template->icon.y = image_y;
	template->icon.width = iwidth;
	template->icon.height = MIN(ICON_HEIGHT, iheight);
}

static void small_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template)
{
	int	text_x = area->x + SMALL_WIDTH + 4;
	int	low_text_y;
	int	max_text_width = area->width - SMALL_WIDTH - 4;
	ViewData *view = (ViewData *) colitem->view_data;

	low_text_y = area->y + area->height / 2 - view->name_height / 2;

	template->leafname.x = text_x;
	template->leafname.y = low_text_y;
	template->leafname.width = MIN(max_text_width, view->name_width);
	template->leafname.height = view->name_height;

	template->icon.x = area->x;
	template->icon.y = area->y + 1;
	template->icon.width = SMALL_WIDTH;
	template->icon.height = SMALL_HEIGHT;
}

static void huge_full_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template)
{
	int	max_text_width = area->width - HUGE_WIDTH - 4;
	ViewData *view = (ViewData *) colitem->view_data;
	MaskedPixmap	*image = view->image;

	if (image)
	{
		if (!image->huge_pixbuf)
			pixmap_make_huge(image);
		template->icon.width = image->huge_width;
		template->icon.height = image->huge_height;
	}
	else
	{
		template->icon.width = HUGE_WIDTH * 3 / 2;
		template->icon.height = HUGE_HEIGHT;
	}

	template->icon.x = area->x + (HUGE_WIDTH - template->icon.width) / 2;
	template->icon.y = area->y + (area->height - template->icon.height) / 2;

	template->leafname.x = area->x + HUGE_WIDTH + 4;
	template->leafname.y = area->y + area->height / 2
			- (view->name_height + 2 + view->details_height) / 2;
	template->leafname.width = MIN(max_text_width, view->name_width);
	template->leafname.height = view->name_height;

	if (!image)
		return;		/* Not scanned yet */

	template->details.x = template->leafname.x;
	template->details.y = template->leafname.y + view->name_height + 2;
}

static void large_full_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template)
{
	int	max_text_width = area->width - ICON_WIDTH - 4;
	ViewData *view = (ViewData *) colitem->view_data;
	MaskedPixmap *image = view->image;

	if (image)
	{
		template->icon.width = image->width;
		template->icon.height = image->height;
	}
	else
	{
		template->icon.width = ICON_WIDTH;
		template->icon.height = ICON_HEIGHT;
	}

	template->icon.x = area->x + (ICON_WIDTH - template->icon.width) / 2;
	template->icon.y = area->y + (area->height - template->icon.height) / 2;


	template->leafname.x = area->x + ICON_WIDTH + 4;
	template->leafname.y = area->y + area->height / 2
			- (view->name_height + 2 + view->details_height) / 2;
	template->leafname.width = MIN(max_text_width, view->name_width);
	template->leafname.height = view->name_height;

	if (!image)
		return;		/* Not scanned yet */

	template->details.x = template->leafname.x;
	template->details.y = template->leafname.y + view->name_height + 2;
}

static void small_full_template(GdkRectangle *area, CollectionItem *colitem,
			   ViewCollection *view_collection, Template *template)
{
	int	col_width = view_collection->collection->item_width;
	ViewData *view = (ViewData *) colitem->view_data;

	small_template(area, colitem, view_collection, template);

	if (!view->image)
		return;		/* Not scanned yet */

	template->details.x = area->x + col_width - template->details.width;
	template->details.y = area->y + area->height / 2 - \
				view->details_height / 2;
}

#define INSIDE(px, py, area, adj)	\
	(px >= area.x && py >= area.y && \
	 px <= area.x + area.width + adj && py <= area.y + area.height)

static gboolean test_point(Collection *collection,
				int point_x, int point_y,
				CollectionItem *colitem,
				int width, int height,
				gpointer user_data)
{
	Template	template;
	GdkRectangle	area;
	ViewData	*view = (ViewData *) colitem->view_data;
	ViewCollection	*view_collection = (ViewCollection *) user_data;

	area.x = 0;
	area.y = 0;
	area.width = width;
	area.height = height;

	fill_template(&area, colitem, view_collection, &template);

	int adj = view_collection->filer_window->display_style == SMALL_ICONS ? 4 : 0;

	return INSIDE(point_x, point_y, template.leafname, 0) ||
	       INSIDE(point_x, point_y, template.icon, adj) ||
	       (view->details && INSIDE(point_x, point_y, template.details, 0));
}

/* 'box' renders a background box if the string is also selected */
static void draw_string(GtkWidget *widget,
		cairo_t *cr,
		PangoLayout *layout,
		GdkRectangle *area,
		int width,
		GtkStateFlags selection_state,
		const GdkRGBA *normal_colour,
		gboolean box)
{
	GtkStyleContext *context = gtk_widget_get_style_context(widget);
	GdkRGBA text_colour = {0, 0, 0, 1};
	gboolean truncated = width > area->width;

	if (selection_state != GTK_STATE_FLAG_NORMAL)
	{
		gint render_width = MAX(1, MIN(width, area->width));

		/* Modificado por josejp2424 (2026): GtkDrawingArea no pinta por sí
		 * solo el estado SELECTED. Renderizar el fondo y el marco con las
		 * funciones del tema GTK evita texto gris sobre blanco en temas claros
		 * y mantiene exactamente los colores definidos por el tema activo. */
		gtk_style_context_save(context);
		gtk_style_context_set_state(context, selection_state);
		gtk_style_context_get_color(context, selection_state, &text_colour);
		if (box)
		{
			cairo_save(cr);
			gtk_render_background(context, cr, area->x, area->y,
					render_width, area->height);
			gtk_render_frame(context, cr, area->x, area->y,
					render_width, area->height);
			cairo_restore(cr);
		}
		gtk_style_context_restore(context);
	}
	else if (normal_colour)
		text_colour = *normal_colour;

	cairo_save(cr);
	if (truncated)
	{
		cairo_rectangle(cr, area->x, area->y, area->width, area->height);
		cairo_clip(cr);
	}
	cairo_set_source_rgba(cr, text_colour.red, text_colour.green,
			text_colour.blue, text_colour.alpha);
	cairo_move_to(cr, area->x, area->y);
	pango_cairo_show_layout(cr, layout);
	cairo_restore(cr);

	if (truncated)
	{
		cairo_save(cr);
		cairo_set_source_rgb(cr, 1.0, 0.0, 0.0);
		cairo_rectangle(cr, area->x + area->width - 1, area->y,
				1, area->height);
		cairo_fill(cr);
		cairo_restore(cr);
	}
}

/* Create the handers for the View interface */
static void view_collection_iface_init(gpointer giface, gpointer iface_data)
{
	ViewIfaceClass *iface = giface;

	g_assert(G_TYPE_FROM_INTERFACE(iface) == VIEW_TYPE_IFACE);

	/* override stuff */
	iface->sort = view_collection_sort;
	iface->style_changed = view_collection_style_changed;
	iface->add_items = view_collection_add_items;
	iface->update_items = view_collection_update_items;
	iface->delete_if = view_collection_delete_if;
	iface->clear = view_collection_clear;
	iface->select_all = view_collection_select_all;
	iface->clear_selection = view_collection_clear_selection;
	iface->count_items = view_collection_count_items;
	iface->count_selected = view_collection_count_selected;
	iface->show_cursor = view_collection_show_cursor;
	iface->get_iter = view_collection_get_iter;
	iface->get_iter_at_point = view_collection_get_iter_at_point;
	iface->cursor_to_iter = view_collection_cursor_to_iter;
	iface->set_selected = view_collection_set_selected;
	iface->get_selected = view_collection_get_selected;
	iface->set_frozen = view_collection_set_frozen;
	iface->select_only = view_collection_select_only;
	iface->wink_item = view_collection_wink_item;
	iface->autosize = view_collection_autosize;
	iface->cursor_visible = view_collection_cursor_visible;
	iface->set_base = view_collection_set_base;
	iface->start_lasso_box = view_collection_start_lasso_box;
	iface->extend_tip = view_collection_extend_tip;
	iface->auto_scroll_callback = view_collection_auto_scroll_callback;
}

static void view_collection_extend_tip(ViewIface *view, ViewIter *iter,
					GString *tip)
{
	ViewCollection *view_collection = (ViewCollection *) view;
	Collection *collection = view_collection->collection;
	FilerWindow *filer_window = view_collection->filer_window;
	Template template;
	int i = iter->i;
	CollectionItem *colitem = &collection->items[i];
	ViewData *view_data = (ViewData *) colitem->view_data;
	GdkRectangle area;
	GtkAllocation allocation;
	int row, col;

	collection_item_to_rowcol(collection, i, &row, &col);
	g_return_if_fail(iter->view == (ViewIface *) view_collection);
	g_return_if_fail(i >= 0 && i < collection->number_of_items);

	if (filer_window->display_style == LARGE_ICONS ||
	    filer_window->display_style == HUGE_ICONS)
		return;

	area.x = col * collection->item_width;
	area.y = row * collection->item_height;
	area.height = collection->item_height;
	gtk_widget_get_allocation(GTK_WIDGET(collection), &allocation);
	if (col == collection->columns - 1)
		area.width = MAX(1, allocation.width - area.x);
	else
		area.width = collection->item_width;

	fill_template(&area, colitem, view_collection, &template);
	if (template.leafname.width < view_data->name_width)
	{
		DirItem *item = (DirItem *) collection->items[i].data;
		g_string_append(tip, item->leafname);
		g_string_append_c(tip, '\n');
	}
}

static gint coll_motion_notify(GtkWidget *widget,
			       GdkEventMotion *event,
			       ViewCollection *view_collection)
{
	return filer_motion_notify(view_collection->filer_window, event);
}

/* Viewport is to be resized, so calculate increments */
static void size_allocate(GtkWidget *widget, GtkAllocation *allocation,
			  gpointer data)
{
	Collection *collection = ((ViewCollection *) data)->collection;
	GtkAdjustment *adj = collection_get_vadjustment(collection);

	if (!GTK_IS_ADJUSTMENT(adj))
		return;

	gtk_adjustment_set_step_increment(adj,
			MAX(1, collection->item_height));
	gtk_adjustment_set_page_increment(adj,
			MAX(1.0, gtk_adjustment_get_page_size(adj)));
}

static gint coll_button_release(GtkWidget *widget,
			        GdkEventButton *event,
				ViewCollection *view_collection)
{
	if (dnd_motion_release(event))
	{
		if (motion_buttons_pressed == 0 &&
		    view_collection->collection->lasso_box)
		{
			filer_set_autoscroll(view_collection->filer_window, FALSE);
			collection_end_lasso(view_collection->collection,
				event->button == 1 ? COLLECTION_SELECTION_SET
						   : COLLECTION_SELECTION_INVERT);
		}
		return FALSE;
	}

	filer_perform_action(view_collection->filer_window, event);
	return FALSE;
}

static gint coll_button_press(GtkWidget *widget,
			      GdkEventButton *event,
			      ViewCollection *view_collection)
{
	collection_set_cursor_item(view_collection->collection, -1, TRUE);

	if (dnd_motion_press(widget, event))
		filer_perform_action(view_collection->filer_window, event);

	return FALSE;
}

/* Nothing is selected anymore - give up primary */
static void lost_selection(Collection  *collection,
			   guint        time,
			   gpointer     user_data)
{
	ViewCollection *view_collection = VIEW_COLLECTION(user_data);

	filer_lost_selection(view_collection->filer_window, time);
}

static void selection_changed(Collection *collection,
			      gint time,
			      gpointer user_data)
{
	ViewCollection *view_collection = VIEW_COLLECTION(user_data);

	filer_selection_changed(view_collection->filer_window, time);
}

static void display_free_colitem(Collection *collection,
				 CollectionItem *colitem)
{
	ViewData	*view = (ViewData *) colitem->view_data;

	if (!view)
		return;

	if (view->layout)
	{
		g_object_unref(G_OBJECT(view->layout));
		view->layout = NULL;
	}
	if (view->details)
		g_object_unref(G_OBJECT(view->details));

	if (view->image)
		g_object_unref(view->image);

	g_free(view);
}

static void add_item(ViewCollection *view_collection, DirItem *item)
{
	Collection *collection = view_collection->collection;
	FilerWindow	*filer_window = view_collection->filer_window;
	int		old_w = collection->item_width;
	int		old_h = collection->item_height;
	int		w, h, i;

	i = collection_insert(collection, item,
				display_create_viewdata(filer_window, item));

	calc_size(filer_window, &collection->items[i], &w, &h);

	if (w > old_w || h > old_h)
		collection_set_item_size(collection,
					 MAX(old_w, w),
					 MAX(old_h, h));
}

static void style_updated(GtkWidget *widget,
		      ViewCollection *view_collection)
{
	view_collection_style_changed(VIEW(view_collection),
			VIEW_UPDATE_VIEWDATA | VIEW_UPDATE_NAME);
}

/* Return the size needed for this item */
static void calc_size(FilerWindow *filer_window, CollectionItem *colitem,
		int *width, int *height)
{
	int		pix_width, pix_height;
	int		w;
	DisplayStyle	style = filer_window->display_style;
	ViewData     *view = (ViewData *) colitem->view_data;

	if (filer_window->details_type == DETAILS_NONE)
	{
                if (style == HUGE_ICONS)
		{
			if (view->image)
			{
				if (!view->image->huge_pixbuf)
					pixmap_make_huge(view->image);
				pix_width = view->image->huge_width;
				pix_height = view->image->huge_height;
			}
			else
			{
				pix_width = HUGE_WIDTH * 3 / 2;
				pix_height = HUGE_HEIGHT * 3 / 2;
			}
			*width = MAX(pix_width, view->name_width) + 4;
			*height = MAX(view->name_height + pix_height + 4,
					HUGE_HEIGHT * 3 / 4);
		}
		else if (style == SMALL_ICONS)
		{
			w = MIN(view->name_width, o_small_width.int_value);
			*width = SMALL_WIDTH + 12 + w;
			*height = MAX(view->name_height, SMALL_HEIGHT) + 4;
		}
		else
		{
			if (view->image)
				pix_width = view->image->width;
			else
				pix_width = ICON_WIDTH;
			*width = MAX(pix_width, view->name_width) + 4;
			*height = view->name_height + ICON_HEIGHT + 2;
		}
	}
	else
	{
		w = view->details_width;
		if (style == HUGE_ICONS)
		{
			*width = HUGE_WIDTH + 12 + MAX(w, view->name_width);
			*height = HUGE_HEIGHT - 4;
		}
		else if (style == SMALL_ICONS)
		{
			int	text_height;

			*width = SMALL_WIDTH + view->name_width + 12 + w;
			text_height = MAX(view->name_height,
					  view->details_height);
			*height = MAX(text_height, SMALL_HEIGHT) + 4;
		}
		else
		{
                        *width = ICON_WIDTH + 12 + MAX(w, view->name_width);
			*height = ICON_HEIGHT;
		}
	}
}

static void update_item(ViewCollection *view_collection, int i)
{
	Collection *collection = view_collection->collection;
	int	old_w = collection->item_width;
	int	old_h = collection->item_height;
	int	w, h;
	CollectionItem *colitem;
	FilerWindow *filer_window = view_collection->filer_window;

	g_return_if_fail(i >= 0 && i < collection->number_of_items);
	colitem = &collection->items[i];

	display_update_view(filer_window,
			(DirItem *) colitem->data,
			(ViewData *) colitem->view_data,
			FALSE);

	calc_size(filer_window, colitem, &w, &h);
	if (w > old_w || h > old_h)
		collection_set_item_size(collection,
					 MAX(old_w, w),
					 MAX(old_h, h));

	collection_draw_item(collection, i, TRUE);
}

/* Implementations of the View interface. See view_iface.c for comments. */

static void view_collection_style_changed(ViewIface *view, int flags)
{
	ViewCollection *view_collection = VIEW_COLLECTION(view);
	FilerWindow	*filer_window = view_collection->filer_window;
	int		i;
	Collection	*col = view_collection->collection;
	int		width = MIN_ITEM_WIDTH;
	int		height = SMALL_HEIGHT;
	int		n = col->number_of_items;

	if (n == 0 && filer_window->display_style != SMALL_ICONS)
		height = ICON_HEIGHT;

	view_collection->collection->vertical_order = FALSE;
	if (filer_window->display_style == SMALL_ICONS &&
	    o_vertical_order_small.int_value)
	  view_collection->collection->vertical_order = TRUE;
	if (filer_window->display_style != SMALL_ICONS &&
	    o_vertical_order_large.int_value)
	  view_collection->collection->vertical_order = TRUE;

	/* Recalculate all the ViewData structs for this window
	 * (needed if the text or image has changed in any way) and
	 * get the size of each item.
	 */
	for (i = 0; i < n; i++)
	{
		CollectionItem *ci = &col->items[i];
		int	w, h;

		if (flags & (VIEW_UPDATE_VIEWDATA | VIEW_UPDATE_NAME))
			display_update_view(filer_window,
					(DirItem *) ci->data,
					(ViewData *) ci->view_data,
					(flags & VIEW_UPDATE_NAME) != 0);

		calc_size(filer_window, ci, &w, &h);
		if (w > width)
			width = w;
		if (h > height)
			height = h;
	}

	collection_set_item_size(col, width, height);

	gtk_widget_queue_draw(GTK_WIDGET(view_collection));
}

/* Modificado por josejp2424 (2026): orden seguro y determinista.
 * La función sort_by_name ya mantiene las carpetas normales primero; usarla
 * directamente evita comparadores con contexto global durante actualizaciones. */
static void view_collection_sort(ViewIface *view)
{
	ViewCollection *view_collection = VIEW_COLLECTION(view);

	collection_qsort(view_collection->collection, sort_by_name,
			GTK_SORT_ASCENDING);
	gtk_widget_queue_resize(GTK_WIDGET(view_collection->collection));
}

static void view_collection_add_items(ViewIface *view, GPtrArray *items)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;
	FilerWindow	*filer_window = view_collection->filer_window;
	int old_num, i;

	old_num = collection->number_of_items;
	for (i = 0; i < items->len; i++)
	{
		DirItem *item = (DirItem *) items->pdata[i];
		if (!filer_match_filter(filer_window, item))
			continue;

		add_item(view_collection, item);
	}

	if (old_num != collection->number_of_items)
		view_collection_sort(view);
}

static void view_collection_update_items(ViewIface *view, GPtrArray *items)
{
	ViewCollection *view_collection = VIEW_COLLECTION(view);
	Collection     *collection = view_collection->collection;
	FilerWindow    *filer_window = view_collection->filer_window;
	int      i;

	g_return_if_fail(items->len > 0);

	/* Modificado por josejp2424 (2026): localizar los elementos por identidad
	 * de puntero antes de ordenar. Una búsqueda binaria deja de ser válida
	 * cuando el restat cambia TYPE_UNKNOWN a carpeta o archivo y fue la causa
	 * de vistas parciales en las que sólo permanecían las carpetas. */
	for (i = 0; i < items->len; i++)
	{
		DirItem *item = (DirItem *) items->pdata[i];
		const gchar *leafname = item->leafname;
		int j;

		if (!filer_match_filter(filer_window, item))
			continue;

		for (j = 0; j < collection->number_of_items; j++)
		{
			if (collection->items[j].data == item)
				break;
		}

		if (j >= collection->number_of_items)
			g_warning("Failed to find '%s'\n", leafname);
		else
			update_item(view_collection, j);
	}

	view_collection_sort(view);
}

static void view_collection_delete_if(ViewIface *view,
			  gboolean (*test)(gpointer item, gpointer data),
			  gpointer data)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	collection_delete_if(collection, test, data);
}

static void view_collection_clear(ViewIface *view)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	collection_clear(collection);
}

static void view_collection_select_all(ViewIface *view)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	collection_select_all(collection);
}

static void view_collection_clear_selection(ViewIface *view)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	collection_clear_selection(collection);
}

static int view_collection_count_items(ViewIface *view)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	return collection->number_of_items;
}

static int view_collection_count_selected(ViewIface *view)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	return collection->number_selected;
}

static void view_collection_show_cursor(ViewIface *view)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	collection_move_cursor(collection, 0, 0, 0);
}

/* The first time the next() method is used, this is called */
static DirItem *iter_init(ViewIter *iter)
{
	ViewCollection *view_collection = (ViewCollection *) iter->view;
	Collection *collection = view_collection->collection;
	int i = -1;
	int n = collection->number_of_items;
	int flags = iter->flags;

	iter->peek = iter_peek;

	if (iter->n_remaining == 0)
		return NULL;

	if (flags & VIEW_ITER_FROM_CURSOR)
	{
		i = collection->cursor_item;
		if (i == -1)
			return NULL;	/* No cursor */
	}
	else if (flags & VIEW_ITER_FROM_BASE)
		i = view_collection->cursor_base;

	if (i < 0 || i >= n)
	{
		/* Either a normal iteration, or an iteration from an
		 * invalid starting point.
		 */
		if (flags & VIEW_ITER_BACKWARDS)
			i = n - 1;
		else
			i = 0;
	}

	if (i < 0 || i >= n)
		return NULL;	/* No items at all! */

	iter->next = flags & VIEW_ITER_BACKWARDS ? iter_prev : iter_next;
	iter->n_remaining--;
	iter->i = i;

	if (flags & VIEW_ITER_SELECTED && !collection->items[i].selected)
		return iter->next(iter);
	return iter->peek(iter);
}
/* Advance iter to point to the next item and return the new item
 * (this saves you calling peek after next each time).
 */
static DirItem *iter_next(ViewIter *iter)
{
	Collection *collection = ((ViewCollection *) iter->view)->collection;
	int n = collection->number_of_items;
	int i = iter->i;

	g_return_val_if_fail(iter->n_remaining >= 0, NULL);

	/* i is the last item returned (always valid) */

	g_return_val_if_fail(i >= 0 && i < n, NULL);

	while (iter->n_remaining)
	{
		i++;
		iter->n_remaining--;

		if (i == n)
			i = 0;

		g_return_val_if_fail(i >= 0 && i < n, NULL);

		if (iter->flags & VIEW_ITER_SELECTED &&
		    !collection->items[i].selected)
			continue;

		iter->i = i;
		return collection->items[i].data;
	}

	iter->i = -1;
	return NULL;
}

/* Like iter_next, but in the other direction */
static DirItem *iter_prev(ViewIter *iter)
{
	Collection *collection = ((ViewCollection *) iter->view)->collection;
	int n = collection->number_of_items;
	int i = iter->i;

	g_return_val_if_fail(iter->n_remaining >= 0, NULL);

	/* i is the last item returned (always valid) */

	g_return_val_if_fail(i >= 0 && i < n, NULL);

	while (iter->n_remaining)
	{
		i--;
		iter->n_remaining--;

		if (i == -1)
			i = collection->number_of_items - 1;

		g_return_val_if_fail(i >= 0 && i < n, NULL);

		if (iter->flags & VIEW_ITER_SELECTED &&
		    !collection->items[i].selected)
			continue;

		iter->i = i;
		return collection->items[i].data;
	}

	iter->i = -1;
	return NULL;
}

static DirItem *iter_peek(ViewIter *iter)
{
	Collection *collection = ((ViewCollection *) iter->view)->collection;
	int i = iter->i;

	if (i == -1)
		return NULL;

	g_return_val_if_fail(i >= 0 && i < collection->number_of_items, NULL);

	return collection->items[i].data;
}

static void make_iter(ViewCollection *view_collection, ViewIter *iter,
		      IterFlags flags)
{
	Collection *collection = view_collection->collection;

	iter->view = (ViewIface *) view_collection;
	iter->next = iter_init;
	iter->peek = NULL;
	iter->i = -1;

	iter->flags = flags;

	if (flags & VIEW_ITER_ONE_ONLY)
	{
		iter->n_remaining = 1;
		iter->next(iter);
	}
	else
		iter->n_remaining = collection->number_of_items;
}

/* Set the iterator to return 'i' on the next peek() */
static void make_item_iter(ViewCollection *view_collection,
			   ViewIter *iter, int i)
{
	Collection *collection = view_collection->collection;

	g_return_if_fail(i >= -1 && i < collection->number_of_items);

	make_iter(view_collection, iter, 0);

	iter->i = i;
	iter->next = iter_next;
	iter->peek = iter_peek;
	iter->n_remaining = 0;
}

static void view_collection_get_iter(ViewIface *view,
				     ViewIter *iter, IterFlags flags)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);

	make_iter(view_collection, iter, flags);
}

static void view_collection_get_iter_at_point(ViewIface *view, ViewIter *iter,
					      GdkWindow *src, int x, int y)
{
	ViewCollection *view_collection = VIEW_COLLECTION(view);
	Collection *collection = view_collection->collection;
	int i;

	/* Collection now owns the input window.  Only viewport-originated
	 * events need conversion from visible coordinates to document
	 * coordinates. */
	if (src != gtk_widget_get_window(GTK_WIDGET(collection)))
	{
		GdkWindow *view_window = gtk_viewport_get_view_window(
			GTK_VIEWPORT(view_collection));
		GtkAdjustment *adj = collection_get_vadjustment(collection);
		if ((src == gtk_widget_get_window(GTK_WIDGET(view_collection)) ||
		     src == view_window) && GTK_IS_ADJUSTMENT(adj))
			y += (int) gtk_adjustment_get_value(adj);
	}

	i = collection_get_item(collection, x, y);
	make_item_iter(view_collection, iter, i);
}

static void view_collection_cursor_to_iter(ViewIface *view, ViewIter *iter)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;
	FilerWindow	*filer_window = view_collection->filer_window;

	g_return_if_fail(iter == NULL ||
			 iter->view == (ViewIface *) view_collection);

	collection_set_cursor_item(collection, iter ? iter->i : -1,
			filer_window->auto_scroll == -1);
}

static void view_collection_set_selected(ViewIface *view,
					 ViewIter *iter,
					 gboolean selected)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	g_return_if_fail(iter != NULL &&
			 iter->view == (ViewIface *) view_collection);
	g_return_if_fail(iter->i >= 0 && iter->i < collection->number_of_items);

	if (selected)
		collection_select_item(collection, iter->i);
	else
		collection_unselect_item(collection, iter->i);
}

static gboolean view_collection_get_selected(ViewIface *view, ViewIter *iter)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	g_return_val_if_fail(iter != NULL &&
			iter->view == (ViewIface *) view_collection, FALSE);
	g_return_val_if_fail(iter->i >= 0 &&
				iter->i < collection->number_of_items, FALSE);

	return collection->items[iter->i].selected;
}

static void view_collection_select_only(ViewIface *view, ViewIter *iter)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	g_return_if_fail(iter != NULL &&
			 iter->view == (ViewIface *) view_collection);
	g_return_if_fail(iter->i >= 0 && iter->i < collection->number_of_items);

	collection_clear_except(collection, iter->i);
}

static void view_collection_set_frozen(ViewIface *view, gboolean frozen)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	if (frozen)
		collection->block_selection_changed++;
	else
		collection_unblock_selection_changed(collection,
				gtk_get_current_event_time(), TRUE);
}

static void view_collection_wink_item(ViewIface *view, ViewIter *iter)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	if (!iter)
	{
		collection_wink_item(collection, -1);
		return;
	}

	g_return_if_fail(iter != NULL &&
			 iter->view == (ViewIface *) view_collection);
	g_return_if_fail(iter->i >= 0 && iter->i < collection->number_of_items);

	collection_wink_item(collection, iter->i);
}

static void view_collection_autosize(ViewIface *view)
{
	ViewCollection *view_collection = VIEW_COLLECTION(view);
	FilerWindow *filer_window = view_collection->filer_window;
	Collection *collection = view_collection->collection;
	int n;
	int w = collection->item_width;
	int h = collection->item_height;
	int x;
	int rows, cols;
	int max_x, max_rows;
	const float r = 2.5;
	int t = 0;
	int space = 0;
	GtkAllocation allocation;

	if (o_toolbar.int_value != TOOLBAR_NONE && filer_window->toolbar)
	{
		gtk_widget_get_allocation(filer_window->toolbar, &allocation);
		t = allocation.height;
	}
	if (filer_window->message)
	{
		gtk_widget_get_allocation(filer_window->message, &allocation);
		t += allocation.height;
	}
	if (gtk_widget_get_visible(filer_window->minibuffer_area))
	{
		gint minimum = 0, natural = 0;
		gtk_widget_get_preferred_height(filer_window->minibuffer_area,
				&minimum, &natural);
		space = MAX(minimum, natural) + 2;
		t += space;
	}

	n = collection->number_of_items;
	if (n == 0)
		h = ICON_HEIGHT * 1.5;
	n = MAX(n, 2);
	max_x = (o_filer_size_limit.int_value * monitor_width) / 100;
	max_rows = MAX(1, (o_filer_size_limit.int_value * monitor_height) /
			MAX(1, h * 100));
	x = (r * t + sqrt(r * r * t * t + 4 * h * r * (n * w - 1))) / 2
			+ w - 1;
	x = MIN(x, max_x);
	cols = MAX(x / MAX(w, 1), 1);
	rows = MIN((n + cols - 1) / cols, max_rows);
	if (space == 0)
		space = filer_window->display_style == SMALL_ICONS ? h : 2;

	filer_window_set_size(filer_window,
			w * MAX(cols, 1), h * MAX(rows, 1) + space);
}

static gboolean view_collection_cursor_visible(ViewIface *view)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);
	Collection	*collection = view_collection->collection;

	return collection->cursor_item != -1;
}

static void view_collection_set_base(ViewIface *view, ViewIter *iter)
{
	ViewCollection	*view_collection = VIEW_COLLECTION(view);

	view_collection->cursor_base = iter->i;
}

static void view_collection_start_lasso_box(ViewIface *view,
					    GdkEventButton *event)
{
	ViewCollection	*view_collection = (ViewCollection *) view;
	Collection	*collection = view_collection->collection;

	filer_set_autoscroll(view_collection->filer_window, TRUE);
	collection_lasso_box(collection, event->x, event->y);
}


/* Change the adjustment by this amount. Bounded. */
static void diff_vpos(Collection *collection, int diff)
{
	GtkAdjustment *adj = collection_get_vadjustment(collection);
	double old, upper, page, value;

	if (!GTK_IS_ADJUSTMENT(adj))
		return;

	old = gtk_adjustment_get_value(adj);
	upper = gtk_adjustment_get_upper(adj);
	page = gtk_adjustment_get_page_size(adj);
	value = CLAMP(old + diff, 0.0, MAX(0.0, upper - page));

	gtk_adjustment_set_value(adj, value);
	if (gtk_adjustment_get_value(adj) != old)
		dnd_spring_abort();
}

static gboolean view_collection_auto_scroll_callback(ViewIface *view)
{
	ViewCollection *view_collection = (ViewCollection *) view;
	Collection *collection = view_collection->collection;
	GtkWidget *widget = GTK_WIDGET(collection);
	GdkWindow *window = gtk_widget_get_window(widget);
	GtkAllocation allocation;
	gint x, y, width, height;
	GdkModifierType mask;
	GtkAdjustment *adj = collection_get_vadjustment(collection);
	int diff = 0;

	if (!window || !GTK_IS_ADJUSTMENT(adj))
		return FALSE;
	rox_gdk_window_get_pointer(window, &x, &y, &mask);
	gtk_widget_get_allocation(widget, &allocation);
	width = allocation.width;
	height = MAX(1, (gint) gtk_adjustment_get_page_size(adj));

	if ((x < 0 || x > width || y < 0 || y > height) &&
	    !collection->lasso_box)
		return FALSE;
	if (y < AUTOSCROLL_STEP)
		diff = y - AUTOSCROLL_STEP;
	else if (y > height - AUTOSCROLL_STEP)
		diff = AUTOSCROLL_STEP + y - height;
	if (diff)
		diff_vpos(collection, diff);
	return TRUE;
}
