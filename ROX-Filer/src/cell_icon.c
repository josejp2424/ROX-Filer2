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

/* cell_icon.c - a GtkCellRenderer used for the icons in details mode
 *
 * Based on gtkcellrendererpixbuf.c.
 */

/*
 * Modificado por josejp2424 (2026):
 * port a GTK3 y adaptaciones específicas de esta versión.
 */

#include "config.h"

#include <gtk/gtk.h>
#include <string.h>

#include "global.h"

#include "view_details.h"
#include "cell_icon.h"
#include "filer.h"
#include "display.h"
#include "diritem.h"
#include "pixmaps.h"
#include "type.h"
#include "support.h"
#include "gui_support.h"
#include "fscache.h"
#include "menu.h"

typedef struct _CellIcon CellIcon;
typedef struct _CellIconClass CellIconClass;

struct _CellIcon {
	GtkCellRenderer parent;

	ViewDetails *view_details;
	ViewItem *item;
	GdkRGBA background;
};

struct _CellIconClass {
	GtkCellRendererClass parent_class;
};


/* Static prototypes */
static void cell_icon_set_property(GObject *object, guint param_id,
				       const GValue *value, GParamSpec *pspec);
static void cell_icon_init       (CellIcon *cell);
static void cell_icon_class_init (CellIconClass *class);
/* Modificado por josejp2424: GTK3 - ajustar prototipos */
static void cell_icon_get_size   (GtkCellRenderer	*cell,
			      GtkWidget		*widget,
			      const GdkRectangle	*rectangle,
			      gint		*x_offset,
			      gint		*y_offset,
			      gint		*width,
			      gint		*height);

/* GTK3: firma nueva con cairo_t */
static void cell_icon_render     (GtkCellRenderer		*cell,
			      cairo_t		*cr,
			      GtkWidget		*widget,
			      const GdkRectangle	*background_area,
			      const GdkRectangle	*cell_area,
			      GtkCellRendererState	flags);

/* Modificado por josejp2424: GtkType -> GType */
static GType cell_icon_get_type(void);

enum {
	PROP_ZERO,
	PROP_ITEM,
	PROP_BACKGROUND_RGBA,
};

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

GtkCellRenderer *cell_icon_new(ViewDetails *view_details)
{
	GtkCellRenderer *cell;

	cell = GTK_CELL_RENDERER(g_object_new(cell_icon_get_type(), NULL));
	((CellIcon *) cell)->view_details = view_details;

	return cell;
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/


/* Modificado por josejp2424: GtkType -> GType para GTK3 */
static GType cell_icon_get_type(void)
{
	static GType cell_icon_type = 0;

	if (!cell_icon_type)
	{
		static const GTypeInfo cell_icon_info =
		{
			sizeof (CellIconClass),
			NULL,		/* base_init */
			NULL,		/* base_finalize */
			(GClassInitFunc) cell_icon_class_init,
			NULL,		/* class_finalize */
			NULL,		/* class_data */
			sizeof (CellIcon),
			0,              /* n_preallocs */
			(GInstanceInitFunc) cell_icon_init,
		};

		cell_icon_type = g_type_register_static(GTK_TYPE_CELL_RENDERER,
							"CellIcon",
							&cell_icon_info, 0);
	}

	return cell_icon_type;
}

static void cell_icon_init(CellIcon *icon)
{
	icon->view_details = NULL;
}

static void cell_icon_class_init(CellIconClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS(class);
	GtkCellRendererClass *cell_class = GTK_CELL_RENDERER_CLASS(class);

	object_class->set_property = cell_icon_set_property;

	cell_class->get_size = cell_icon_get_size;
	cell_class->render = cell_icon_render;

	g_object_class_install_property(object_class,
			PROP_ITEM,
			g_param_spec_pointer("item",
				"DirItem",
				"The item to render.",
				G_PARAM_WRITABLE));

	g_object_class_install_property(object_class,
			PROP_BACKGROUND_RGBA,
			g_param_spec_boxed("background_rgba",
				"Background color",
				"Background color as a GdkRGBA",
				GDK_TYPE_RGBA,
				G_PARAM_WRITABLE));
}

static void cell_icon_set_property(GObject *object, guint param_id,
				       const GValue *value, GParamSpec *pspec)
{
	CellIcon *icon = (CellIcon *) object;

	switch (param_id)
	{
		case PROP_ITEM:
			icon->item = (ViewItem *) g_value_get_pointer(value);
			break;
		case PROP_BACKGROUND_RGBA:
		{
			GdkRGBA *bg = g_value_get_boxed(value);
			if (bg)
			{
				icon->background.red = bg->red;
				icon->background.green = bg->green;
				icon->background.blue = bg->blue;
			}
			break;
		}
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object,
							  param_id, pspec);
			break;
	}
}

/* Return the size to display this icon at. If huge, ensures that the image
 * exists in that size, if present at all.
 */
static DisplayStyle get_style(GtkCellRenderer *cell)
{
	CellIcon *icon = (CellIcon *) cell;
	ViewItem *view_item = icon->item;
	DisplayStyle size;
	DirItem *item = view_item->item;

	if (!view_item->image)
	{
		FilerWindow *filer_window = icon->view_details->filer_window;

		if (filer_window->show_thumbs && item->base_type == TYPE_FILE)
		{
			const char *path;

			path = (const char *) make_path(filer_window->real_path,
					 (const char *) item->leafname);

			view_item->image = g_fscache_lookup_full(pixmap_cache,
					path, FSCACHE_LOOKUP_ONLY_NEW, NULL);
		}
		if (!view_item->image)
		{
			view_item->image = di_image(item);
			if (view_item->image)
				g_object_ref(view_item->image);
		}
	}

	size = icon->view_details->filer_window->display_style_wanted;

	if (size == AUTO_SIZE_ICONS)
	{
		if (!view_item->image || view_item->image == di_image(item))
			size = SMALL_ICONS;
		else
			size = HUGE_ICONS;
	}
	if (size == HUGE_ICONS && view_item->image &&
	    !view_item->image->huge_pixbuf)
		pixmap_make_huge(view_item->image);

	return size;
}

static void cell_icon_get_size(GtkCellRenderer *cell,
				   GtkWidget       *widget,
				   const GdkRectangle    *cell_area, /* Modificado por josejp2424 */
				   gint            *x_offset,
				   gint            *y_offset,
				   gint            *width,
				   gint            *height)
{
	MaskedPixmap *image;
	DisplayStyle size;
	int w, h;

	size = get_style(cell);
	image = ((CellIcon *) cell)->item->image;

	if (x_offset)
		*x_offset = 0;
	if (y_offset)
		*y_offset = 0;

	switch (size)
	{
		case SMALL_ICONS:
			w = SMALL_WIDTH;
			h = SMALL_HEIGHT;
			break;
		case LARGE_ICONS:
			if (image)
			{
				w = image->width;
				h = image->height;
			}
			else
			{
				w = ICON_WIDTH;
				h = ICON_HEIGHT;
			}
			break;
		case HUGE_ICONS:
			if (image)
			{
				w = image->huge_width;
				h = image->huge_height;
			}
			else
			{
				w = HUGE_WIDTH;
				h = HUGE_HEIGHT;
			}
			break;
		default:
			w = 2;
			h = 2;
			break;
	}

	if (width)
		*width = w;
	if (height)
		*height = h;
}

/* GTK3 renderer: draw directly into the cairo_t supplied by GtkTreeView.
 * This preserves GTK clipping and scrolling and removes the old GdkWindow
 * drawing bridge.  Modificado por josejp2424 */
static void cell_icon_render(GtkCellRenderer        *cell,
			     cairo_t                *cr,
			     GtkWidget              *widget,
			     const GdkRectangle     *background_area,
			     const GdkRectangle     *cell_area,
			     GtkCellRendererState    flags)
{
	CellIcon *icon = (CellIcon *) cell;
	ViewItem *view_item = icon->item;
	DirItem *item;
	DisplayStyle size;
	gboolean selected = (flags & GTK_CELL_RENDERER_SELECTED) != 0;
	GdkRGBA color = {0.25, 0.45, 0.75, 1.0};
	GtkStyleContext *context;
	gboolean cut_visual;

	if (!cr || !widget || !cell_area || !view_item)
		return;

	item = view_item->item;
	size = get_style(cell);
	if (!view_item->image)
		return;

	context = gtk_widget_get_style_context(widget);
	rox_style_context_get_background(context,
		selected ? GTK_STATE_FLAG_SELECTED : GTK_STATE_FLAG_NORMAL,
		&color);
	color.alpha = 1.0;

	cut_visual = menu_path_is_cut((const gchar *)make_path(
		icon->view_details->filer_window->sym_path, item->leafname));

	cairo_save(cr);
	cairo_rectangle(cr, cell_area->x, cell_area->y,
		cell_area->width, cell_area->height);
	cairo_clip(cr);
	if (cut_visual)
		cairo_push_group(cr);

	switch (size)
	{
		case SMALL_ICONS:
		{
			GdkRectangle area = *cell_area;
			area.width = MIN(area.width, SMALL_WIDTH);
			area.x = cell_area->x + cell_area->width - area.width;
			draw_small_icon(cr, &area, item, view_item->image,
				selected, &color);
			break;
		}
		case LARGE_ICONS:
			draw_large_icon(cr, cell_area, item,
				view_item->image, selected, &color);
			break;
		case HUGE_ICONS:
			if (!view_item->image->huge_pixbuf)
				pixmap_make_huge(view_item->image);
			draw_huge_icon(cr, cell_area, item,
				view_item->image, selected, &color);
			break;
		default:
			g_warning("Unknown size %d\n", size);
			break;
	}

	if (cut_visual)
	{
		cairo_pop_group_to_source(cr);
		cairo_paint_with_alpha(cr, 0.45);
	}
	cairo_restore(cr);
}
