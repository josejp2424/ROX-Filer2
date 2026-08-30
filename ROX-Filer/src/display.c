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

/* display.c - code for arranging and displaying file items */

/*
 * Modificado por josejp2424 (2026):
 * port a GTK3 y adaptaciones específicas de esta versión.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <errno.h>
#include <sys/param.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>

#include "global.h"

#include "main.h"
#include "filer.h"
#include "display.h"
#include "support.h"
#include "gui_support.h"
#include "pixmaps.h"
#include "menu.h"
#include "dnd.h"
#include "run.h"
#include "mount.h"
#include "type.h"
#include "options.h"
#include "action.h"
#include "minibuffer.h"
#include "dir.h"
#include "diritem.h"
#include "fscache.h"
#include "view_iface.h"
#include "xtypes.h"

#define HUGE_WRAP (1.5 * o_large_width.int_value)

/* Options bits */
static Option o_display_caps_first;
static Option o_display_dirs_first;
Option o_display_size;
Option o_display_details;
Option o_display_sort_by;
static Option o_large_width;
Option o_small_width;
Option o_display_show_hidden;
Option o_display_show_thumbs;
Option o_display_show_headers;
Option o_display_show_full_type;
Option o_display_show_name;
Option o_display_show_type;
Option o_display_show_size;
Option o_display_show_permissions;
Option o_display_show_owner;
Option o_display_show_group;
Option o_display_show_mtime;
Option o_display_show_ctime;
Option o_display_show_atime;
Option o_display_inherit_options;
static Option o_filer_change_size_num;
Option o_vertical_order_small, o_vertical_order_large;
Option o_xattr_show;

/* Static prototypes */
static void display_details_set(FilerWindow *filer_window, DetailsType details);
static void display_style_set(FilerWindow *filer_window, DisplayStyle style);
static void options_changed(void);
static char *details(FilerWindow *filer_window, DirItem *item);
static void display_set_actual_size_real(FilerWindow *filer_window);

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void display_init()
{
	option_add_int(&o_display_caps_first, "display_caps_first", FALSE);
	/* Modificado por josejp2424: las carpetas se muestran primero de forma
	 * permanente; la opción histórica se conserva sólo para leer configuraciones. */
	option_add_int(&o_display_dirs_first, "display_dirs_first", TRUE);

	option_add_int(&o_display_inherit_options,
		       "display_inherit_options", FALSE);
	option_add_int(&o_display_sort_by, "display_sort_by", SORT_NAME);
	option_add_int(&o_display_show_hidden, "display_show_hidden", FALSE);
	option_add_int(&o_xattr_show, "xattr_show", TRUE);

	option_add_int(&o_display_size, "display_icon_size", AUTO_SIZE_ICONS);
	option_add_int(&o_display_details, "display_details", DETAILS_NONE);
	option_add_int(&o_filer_change_size_num, "filer_change_size_num", 30);

	option_add_int(&o_large_width, "display_large_width", 155);
	option_add_int(&o_small_width, "display_small_width", 250);

	option_add_int(&o_vertical_order_small, "vertical_order_small", FALSE);
	option_add_int(&o_vertical_order_large, "vertical_order_large", FALSE);
	option_add_int(&o_display_show_headers, "display_show_headers", TRUE);
	option_add_int(&o_display_show_full_type, "display_show_full_type", TRUE);

	option_add_int(&o_display_show_thumbs, "display_show_thumbs", FALSE);

	option_add_int(&o_display_show_name, "display_show_name", TRUE);
	option_add_int(&o_display_show_type, "display_show_type", TRUE);
	option_add_int(&o_display_show_size, "display_show_size", TRUE);
	option_add_int(&o_display_show_permissions, "display_show_permissions", TRUE);
	option_add_int(&o_display_show_owner, "display_show_owner", TRUE);
	option_add_int(&o_display_show_group, "display_show_group", TRUE);
	option_add_int(&o_display_show_mtime, "display_show_mtime", TRUE);
	option_add_int(&o_display_show_ctime, "display_show_ctime", FALSE);
	option_add_int(&o_display_show_atime, "display_show_atime", FALSE);

	option_add_notify(options_changed);
}

static const gchar *rox_emblem_icon_name(const gchar *icon_name)
{
	return rox_icon_name(icon_name);
}

void draw_emblem_on_icon(cairo_t *cr, const char *icon_name, int *x, int y)
{
	GError *error = NULL;
	GdkPixbuf *pixbuf;
	gint width = 16, height = 16;

	gtk_icon_size_lookup(mount_icon_size, &width, &height);
	pixbuf = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(),
		rox_emblem_icon_name(icon_name), MAX(width, height),
		GTK_ICON_LOOKUP_FORCE_SIZE, &error);
	if (!pixbuf)
	{
		if (error)
			g_error_free(error);
		pixbuf = g_object_ref(im_unknown->pixbuf);
	}

	cairo_save(cr);
	gdk_cairo_set_source_pixbuf(cr, pixbuf, *x, y);
	cairo_rectangle(cr, *x, y,
		gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf));
	cairo_fill(cr);
	cairo_restore(cr);

	*x += gdk_pixbuf_get_width(pixbuf) + 1;
	g_object_unref(pixbuf);
}

static void draw_icon_pixbuf(cairo_t *cr, const GdkRectangle *area,
		DirItem *item, GdkPixbuf *source,
		gint width, gint height, gint image_y,
		gboolean selected, GdkRGBA *color,
		gint symlink_y, gboolean centre_small)
{
	gint image_x;
	GdkPixbuf *pixbuf, *tmp;

	image_x = area->x + ((area->width - width) >> 1);
	if (centre_small)
		image_y = MAX(0, SMALL_HEIGHT - height);
	else
		image_y = MAX(0, area->height - height - 6);

	if (item->label)
	{
		tmp = create_spotlight_pixbuf(source, item->label);
		if (selected)
		{
			pixbuf = create_spotlight_pixbuf(tmp, color);
			g_object_unref(tmp);
		}
		else
			pixbuf = tmp;
	}
	else if (selected)
		pixbuf = create_spotlight_pixbuf(source, color);
	else
		pixbuf = g_object_ref(source);

	cairo_save(cr);
	cairo_rectangle(cr, area->x, area->y, area->width, area->height);
	cairo_clip(cr);
	gdk_cairo_set_source_pixbuf(cr, pixbuf, image_x, area->y + image_y);
	cairo_rectangle(cr, image_x, area->y + image_y, width, height);
	cairo_fill(cr);
	cairo_restore(cr);
	g_object_unref(pixbuf);

	if (item->flags & ITEM_FLAG_MOUNT_POINT)
	{
		const char *mp = item->flags & ITEM_FLAG_MOUNTED
			? ROX_ICON_MOUNTED : ROX_ICON_MOUNT;
		draw_emblem_on_icon(cr, mp, &image_x, area->y + 2);
	}
	if (item->flags & ITEM_FLAG_SYMLINK)
		draw_emblem_on_icon(cr, ROX_ICON_SYMLINK,
			&image_x, area->y + symlink_y);
	if ((item->flags & ITEM_FLAG_HAS_XATTR) && o_xattr_show.int_value)
		draw_emblem_on_icon(cr, ROX_ICON_XATTR,
			&image_x, area->y + symlink_y);
}

void draw_huge_icon(cairo_t *cr, const GdkRectangle *area,
		DirItem *item, MaskedPixmap *image, gboolean selected,
		GdkRGBA *color)
{
	if (!image)
		return;
	if (!image->huge_pixbuf)
		pixmap_make_huge(image);
	draw_icon_pixbuf(cr, area, item, image->huge_pixbuf,
		image->huge_width, image->huge_height, 0,
		selected, color, 2, FALSE);
}

void draw_large_icon(cairo_t *cr, const GdkRectangle *area,
		DirItem *item, MaskedPixmap *image, gboolean selected,
		GdkRGBA *color)
{
	if (!image)
		return;
	draw_icon_pixbuf(cr, area, item, image->pixbuf,
		MIN(image->width, ICON_WIDTH), MIN(image->height, ICON_HEIGHT), 0,
		selected, color, 2, FALSE);
}

void draw_small_icon(cairo_t *cr, const GdkRectangle *area,
		DirItem *item, MaskedPixmap *image, gboolean selected,
		GdkRGBA *color)
{
	if (!image)
		return;
	if (!image->sm_pixbuf)
		pixmap_make_small(image);
	draw_icon_pixbuf(cr, area, item, image->sm_pixbuf,
		MIN(image->sm_width, SMALL_WIDTH),
		MIN(image->sm_height, SMALL_HEIGHT), 0,
		selected, color, 8, TRUE);
}

/* The sort functions aren't called from outside, but they are
 * passed as arguments to display_set_sort_fn().
 */

#define IS_A_DIR(item) (item->base_type == TYPE_DIRECTORY && \
			!(item->flags & ITEM_FLAG_APPDIR))

/* Modificado por josejp2424: las carpetas normales forman siempre el
 * primer grupo. La preferencia histórica ya no puede desactivar esta regla. */
#define SORT_DIRS	\
	do {	\
		gboolean id1 = IS_A_DIR(i1);	\
		gboolean id2 = IS_A_DIR(i2);	\
		if (id1 && !id2) return -1;				\
		if (id2 && !id1) return 1;				\
	} while (0)

int sort_by_name(const void *item1, const void *item2)
{
	const DirItem *i1 = (DirItem *) item1;
	const DirItem *i2 = (DirItem *) item2;
	CollateKey *n1 = i1->leafname_collate;
	CollateKey *n2 = i2->leafname_collate;
	int retval;

	SORT_DIRS;

	retval = collate_key_cmp(n1, n2, o_display_caps_first.int_value);

	return retval ? retval : strcmp(i1->leafname, i2->leafname);
}

int sort_by_type(const void *item1, const void *item2)
{
	const DirItem *i1 = (DirItem *) item1;
	const DirItem *i2 = (DirItem *) item2;
	MIME_type *m1, *m2;

	int	 diff = i1->base_type - i2->base_type;

	if (!diff)
		diff = (i1->flags & ITEM_FLAG_APPDIR)
		     - (i2->flags & ITEM_FLAG_APPDIR);
	if (diff)
		return diff > 0 ? 1 : -1;

	m1 = i1->mime_type;
	m2 = i2->mime_type;

	if (m1 && m2)
	{
		diff = strcmp(m1->media_type, m2->media_type);
		if (!diff)
			diff = strcmp(m1->subtype, m2->subtype);
	}
	else if (m1 || m2)
		diff = m1 ? 1 : -1;
	else
		diff = 0;

	if (diff)
		return diff > 0 ? 1 : -1;

	return sort_by_name(item1, item2);
}

int sort_by_owner(const void *item1, const void *item2)
{
	const DirItem *i1 = (DirItem *) item1;
	const DirItem *i2 = (DirItem *) item2;
	const gchar *name1;
	const gchar *name2;

	if(i1->uid==i2->uid)
		return sort_by_name(item1, item2);

	name1=user_name(i1->uid);
	name2=user_name(i2->uid);

	return strcmp(name1, name2);
}

int sort_by_group(const void *item1, const void *item2)
{
	const DirItem *i1 = (DirItem *) item1;
	const DirItem *i2 = (DirItem *) item2;
	const gchar *name1;
	const gchar *name2;

	if(i1->gid==i2->gid)
		return sort_by_name(item1, item2);

	name1=group_name(i1->gid);
	name2=group_name(i2->gid);

	return strcmp(name1, name2);
}

int sort_by_datea(const void *item1, const void *item2)
{
	const DirItem *i1 = (DirItem *) item1;
	const DirItem *i2 = (DirItem *) item2;

	/* SORT_DIRS; -- too confusing! */

	return i1->atime < i2->atime ? -1 :
		i1->atime > i2->atime ? 1 :
		sort_by_name(item1, item2);
}

int sort_by_datec(const void *item1, const void *item2)
{
	const DirItem *i1 = (DirItem *) item1;
	const DirItem *i2 = (DirItem *) item2;

	/* SORT_DIRS; -- too confusing! */

	return i1->ctime < i2->ctime ? -1 :
		i1->ctime > i2->ctime ? 1 :
		sort_by_name(item1, item2);
}

int sort_by_datem(const void *item1, const void *item2)
{
	const DirItem *i1 = (DirItem *) item1;
	const DirItem *i2 = (DirItem *) item2;

	/* SORT_DIRS; -- too confusing! */

	return i1->mtime < i2->mtime ? -1 :
		i1->mtime > i2->mtime ? 1 :
		sort_by_name(item1, item2);
}

int sort_by_size(const void *item1, const void *item2)
{
	const DirItem *i1 = (DirItem *) item1;
	const DirItem *i2 = (DirItem *) item2;

	SORT_DIRS;

	return i1->size < i2->size ? -1 :
		i1->size > i2->size ? 1 :
		sort_by_name(item1, item2);
}

void display_set_sort_type(FilerWindow *filer_window, SortType sort_type,
			   GtkSortType order)
{
	/* Modificado por josejp2424 (2026): usar un orden único y estable.
	 * Las carpetas se muestran primero y todos los demás archivos después,
	 * ordenados por nombre. Se ignoran criterios históricos que podían dejar
	 * la colección incremental desincronizada y ocultar elementos de la vista. */
	(void) sort_type;
	(void) order;
	sort_type = SORT_NAME;
	order = GTK_SORT_ASCENDING;

	if (filer_window->sort_type == sort_type &&
	    filer_window->sort_order == order)
		return;

	filer_window->sort_type = sort_type;
	filer_window->sort_order = order;

	view_sort(filer_window->view);
}

/* Change the icon size and style.
 * force_resize should only be TRUE for new windows.
 */
void display_set_layout(FilerWindow  *filer_window,
			DisplayStyle style,
			DetailsType  details,
			gboolean     force_resize)
{
	gboolean style_changed = FALSE;

	g_return_if_fail(filer_window != NULL);

	if (filer_window->display_style_wanted != style
	    || filer_window->details_type != details)
	{
		style_changed = TRUE;
	}

	display_style_set(filer_window, style);
	display_details_set(filer_window, details);

	/* Recreate layouts because wrapping may have changed */
	view_style_changed(filer_window->view, VIEW_UPDATE_NAME);

	/* Modificado por josejp2424: impedir que el autoajuste inicial de GTK3
	 * reemplace la geometría estándar o la geometría guardada. */
	/* During initial construction, gtk_window_set_default_size() (or a
	 * saved per-directory geometry) must win.  The GTK3 port used to call
	 * view_autosize() here before mapping the window, producing a very wide
	 * one-row strip and replacing the requested square default size.
	 */
	if (!filer_window->initial_geometry_pending &&
	    (force_resize || o_filer_auto_resize.int_value == RESIZE_ALWAYS
	     || (o_filer_auto_resize.int_value == RESIZE_STYLE && style_changed)))
	{
		view_autosize(filer_window->view);
	}
}

/* Set the 'Show Thumbnails' flag for this window */
void display_set_thumbs(FilerWindow *filer_window, gboolean thumbs)
{
	if (filer_window->show_thumbs == thumbs)
		return;

	filer_window->show_thumbs = thumbs;

	view_style_changed(filer_window->view, VIEW_UPDATE_VIEWDATA);

	if (!thumbs)
		filer_cancel_thumbnails(filer_window);

	filer_set_title(filer_window);

	filer_create_thumbs(filer_window);
}

void display_update_hidden(FilerWindow *filer_window)
{
	filer_detach_rescan(filer_window);	/* (updates titlebar) */

	display_set_actual_size(filer_window, FALSE);
}

/* Set the 'Show Hidden' flag for this window */
void display_set_hidden(FilerWindow *filer_window, gboolean hidden)
{
	if (filer_window->show_hidden == hidden)
		return;

	/*
	filer_window->show_hidden = hidden;
	*/
	filer_set_hidden(filer_window, hidden);

	display_update_hidden(filer_window);
}

/* Set the 'Filter Directories' flag for this window */
void display_set_filter_directories(FilerWindow *filer_window, gboolean filter_directories)
{
	if (filer_window->filter_directories == filter_directories)
		return;

	/*
	filer_window->show_hidden = hidden;
	*/
	filer_set_filter_directories(filer_window, filter_directories);

	display_update_hidden(filer_window);
}

void display_set_filter(FilerWindow *filer_window, FilterType type,
			const gchar *filter_string)
{
	if (filer_set_filter(filer_window, type, filter_string))
		display_update_hidden(filer_window);
}


/* Highlight (wink or cursor) this item in the filer window. If the item
 * isn't already there but we're scanning then highlight it if it
 * appears later.
 */
void display_set_autoselect(FilerWindow *filer_window, const gchar *leaf)
{
	gchar *new;

	g_return_if_fail(filer_window != NULL);
	g_return_if_fail(leaf != NULL);

	new = g_strdup(leaf);	/* leaf == old value sometimes */

	null_g_free(&filer_window->auto_select);

	if (view_autoselect(filer_window->view, new))
		g_free(new);
	else
		filer_window->auto_select = new;
}

/* Persist the user's preferred icon size for future filer windows.
 * New filer instances already read display_icon_size from the normal ROX
 * Options file; the historical size buttons only changed the current window.
 * Keep using the existing option system rather than adding another config. */
void display_set_default_size(DisplayStyle style)
{
	char value[16];

	if (style < LARGE_ICONS || style > AUTO_SIZE_ICONS)
		return;

	g_snprintf(value, sizeof(value), "%d", (int) style);
	option_set("display_icon_size", value);
}

/* Change the icon size (wraps) */
void display_change_size(FilerWindow *filer_window, gboolean bigger)
{
	DisplayStyle	new;

	g_return_if_fail(filer_window != NULL);

	switch (filer_window->display_style)
	{
		case LARGE_ICONS:
			new = bigger ? HUGE_ICONS : SMALL_ICONS;
			break;
		case HUGE_ICONS:
			/* If Automatic currently resolved to Huge, pressing Bigger
			 * still means "make Huge my preference". */
			if (bigger)
				new = HUGE_ICONS;
			else
				new = LARGE_ICONS;
			break;
		default:
			/* SMALL_ICONS, including Automatic resolved to Small. */
			if (!bigger)
				new = SMALL_ICONS;
			else
				new = LARGE_ICONS;
			break;
	}

	if (filer_window->display_style_wanted != new)
		display_set_layout(filer_window, new, filer_window->details_type,
				   FALSE);

	display_set_default_size(new);
}

ViewData *display_create_viewdata(FilerWindow *filer_window, DirItem *item)
{
	ViewData *view;

	view = g_new(ViewData, 1);

	view->layout = NULL;
	view->details = NULL;
	view->image = NULL;

	display_update_view(filer_window, item, view, TRUE);

	return view;
}

/* Set the display style to the desired style. If the desired style
 * is AUTO_SIZE_ICONS, choose an appropriate size. Also resizes filer
 * window, if requested.
 */
void display_set_actual_size(FilerWindow *filer_window, gboolean force_resize)
{
	display_set_layout(filer_window, filer_window->display_style_wanted,
			   filer_window->details_type, force_resize);
}


/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void options_changed(void)
{
	GList		*next;

	for (next = all_filer_windows; next; next = next->next)
	{
		FilerWindow *filer_window = (FilerWindow *) next->data;
		int flags = 0;

		if (o_display_caps_first.has_changed)
			view_sort(VIEW(filer_window->view));

		if (o_display_show_headers.has_changed)
			flags |= VIEW_UPDATE_HEADERS;

		if (o_large_width.has_changed || o_small_width.has_changed)
			flags |= VIEW_UPDATE_NAME; /* Recreate PangoLayout */

		view_style_changed(filer_window->view, flags);
	}
}

/* Return a new string giving details of this item, or NULL if details
 * are not being displayed. If details are not yet available, return
 * a string of the right length.
 */
static char *details(FilerWindow *filer_window, DirItem *item)
{
	mode_t	m = item->mode;
	guchar 	*buf = NULL;
	gboolean scanned = item->base_type != TYPE_UNKNOWN;

	if (filer_window->details_type == DETAILS_NONE)
		return NULL;

	if (scanned && item->lstat_errno)
		buf = g_strdup_printf(_("lstat(2) failed: %s"),
				g_strerror(item->lstat_errno));
	else if (filer_window->details_type == DETAILS_TYPE)
	{
		MIME_type	*type = item->mime_type;

		if (!scanned)
			return g_strdup("application/octet-stream");

		buf = g_strdup_printf("%s/%s",
				      type->media_type, type->subtype);
	}
	else if (filer_window->details_type == DETAILS_TIMES)
	{
		guchar	*ctime, *mtime, *atime;

		ctime = pretty_time(&item->ctime);
		mtime = pretty_time(&item->mtime);
		atime = pretty_time(&item->atime);

		buf = g_strdup_printf("a[%s] c[%s] m[%s]", atime, ctime, mtime);
		g_free(ctime);
		g_free(mtime);
		g_free(atime);
	}
	else if (filer_window->details_type == DETAILS_PERMISSIONS)
	{
		if (!scanned)
			return g_strdup("---,---,---/--"
#ifdef S_ISVTX
					"-"
#endif
					" 12345678 12345678");

		buf = g_strdup_printf("%s %-8.8s %-8.8s",
				pretty_permissions(m),
				user_name(item->uid),
				group_name(item->gid));
	}
	else
	{
		if (!scanned)
		{
			if (filer_window->display_style == SMALL_ICONS)
				return g_strdup("1234M");
			else
				return g_strdup("1234 bytes");
		}

		if (item->base_type != TYPE_DIRECTORY)
		{
			if (filer_window->display_style == SMALL_ICONS)
				buf = g_strdup(format_size_aligned(item->size));
			else
				buf = g_strdup(format_size(item->size));
		}
		else
			buf = g_strdup("-");
	}

	return buf;
}

/* Note: Call style_changed after this */
static void display_details_set(FilerWindow *filer_window, DetailsType details)
{
	filer_window->details_type = details;
}

/* Note: Call style_changed after this */
static void display_style_set(FilerWindow *filer_window, DisplayStyle style)
{
	filer_window->display_style_wanted = style;
	display_set_actual_size_real(filer_window);
}

/* Each displayed item has a ViewData structure with some cached information
 * to help quickly draw the item (eg, the PangoLayout). This function updates
 * this information.
 */
void display_update_view(FilerWindow *filer_window,
			 DirItem *item,
			 ViewData *view,
			 gboolean update_name_layout)
{
	DisplayStyle	style = filer_window->display_style;
	int	w, h;
	int	wrap_width = -1;
	char	*str;
	static PangoFontDescription *monospace = NULL;
	PangoAttrList *list = NULL;

	if (!monospace)
		monospace = pango_font_description_from_string("monospace");

	if (view->details)
	{
		g_object_unref(G_OBJECT(view->details));
		view->details = NULL;
	}

	str = details(filer_window, item);
	if (str)
	{
		PangoAttrList	*details_list;
		int	perm_offset = -1;

		view->details = gtk_widget_create_pango_layout(
					filer_window->window, str);
		g_free(str);

		pango_layout_set_font_description(view->details, monospace);
		pango_layout_get_size(view->details, &w, &h);
		view->details_width = w / PANGO_SCALE;
		view->details_height = h / PANGO_SCALE;

		if (filer_window->details_type == DETAILS_PERMISSIONS)
			perm_offset = 0;
		if (perm_offset > -1)
		{
			PangoAttribute	*attr;

			attr = pango_attr_underline_new(PANGO_UNDERLINE_SINGLE);

			perm_offset += 4 * applicable(item->uid, item->gid);
			attr->start_index = perm_offset;
			attr->end_index = perm_offset + 3;

			details_list = pango_attr_list_new();
			pango_attr_list_insert(details_list, attr);
			pango_layout_set_attributes(view->details,
							details_list);
		}
	}

	if (view->image)
	{
		g_object_unref(view->image);
		view->image = NULL;
	}

	if (filer_window->show_thumbs && item->base_type == TYPE_FILE /*&&
									strcmp(item->mime_type->media_type, "image") == 0*/)
	{
		const guchar    *path;

		path = make_path(filer_window->real_path, item->leafname);

		view->image = g_fscache_lookup_full(pixmap_cache, path,
				FSCACHE_LOOKUP_ONLY_NEW, NULL);
	}

	if (!view->image)
	{
		view->image = di_image(item);
		if (view->image)
			g_object_ref(view->image);
	}

	if (view->layout && update_name_layout)
	{
		g_object_unref(G_OBJECT(view->layout));
		view->layout = NULL;
	}

	if (view->layout)
	{
		/* Do nothing */
	}
	else if (g_utf8_validate(item->leafname, -1, NULL))
	{
		view->layout = gtk_widget_create_pango_layout(
				filer_window->window, item->leafname);
		pango_layout_set_auto_dir(view->layout, FALSE);
	}
	else
	{
		PangoAttribute	*attr;
		gchar *utf8;

		utf8 = to_utf8(item->leafname);
		view->layout = gtk_widget_create_pango_layout(
				filer_window->window, utf8);
		g_free(utf8);

		attr = pango_attr_foreground_new(0xffff, 0, 0);
		attr->start_index = 0;
		attr->end_index = -1;
		if (!list)
			list = pango_attr_list_new();
		pango_attr_list_insert(list, attr);
	}

	if (item->flags & ITEM_FLAG_RECENT)
	{
		PangoAttribute	*attr;

		attr = pango_attr_weight_new(PANGO_WEIGHT_BOLD);
		attr->start_index = 0;
		attr->end_index = -1;
		if (!list)
			list = pango_attr_list_new();
		pango_attr_list_insert(list, attr);
	}

	if (list)
	{
		pango_layout_set_attributes(view->layout, list);
		pango_attr_list_unref(list);
	}

	if (filer_window->details_type == DETAILS_NONE)
	{
		if (style == HUGE_ICONS)
			wrap_width = HUGE_WRAP * PANGO_SCALE;
		else if (style == LARGE_ICONS)
			wrap_width = o_large_width.int_value * PANGO_SCALE;
	}

#ifdef USE_PANGO_WRAP_WORD_CHAR
	pango_layout_set_wrap(view->layout, PANGO_WRAP_WORD_CHAR);
#endif
	if (wrap_width != -1)
		pango_layout_set_width(view->layout, wrap_width);

	pango_layout_get_size(view->layout, &w, &h);
	view->name_width = w / PANGO_SCALE;
	view->name_height = h / PANGO_SCALE;
}

/* Sets display_style from display_style_wanted.
 * See also display_set_actual_size().
 */
static void display_set_actual_size_real(FilerWindow *filer_window)
{
	DisplayStyle size = filer_window->display_style_wanted;
	int n;

	g_return_if_fail(filer_window != NULL);

	if (size == AUTO_SIZE_ICONS)
	{
		n = view_count_items(filer_window->view);

		if (n >= o_filer_change_size_num.int_value)
			size = SMALL_ICONS;
		else
			size = LARGE_ICONS;
	}

	filer_window->display_style = size;
}
