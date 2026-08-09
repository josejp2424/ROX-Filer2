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
 */

/* wrapped.c - a small wrapping label used by panel icons.
 *
 * GTK3 port: this widget now uses the GTK3 draw and preferred-size virtual
 * functions.  It no longer reads GtkWidget internals or uses GtkStyle,
 * expose-event or size-request.
 */

/*
 * Modificado por josejp2424 (2026):
 * port a GTK3 y adaptaciones específicas de esta versión.
 */

#include "config.h"

#include <gtk/gtk.h>

#include "global.h"
#include "wrapped.h"

G_DEFINE_TYPE(WrappedLabel, wrapped_label, GTK_TYPE_WIDGET)

static gint wrapped_label_layout_width(WrappedLabel *wl, gint allocation_width)
{
	gint width = wl->width;

	if (allocation_width > 0 && (width < 0 || allocation_width < width))
		width = allocation_width;

	return width;
}

static void wrapped_label_set_layout_width(WrappedLabel *wl, gint width)
{
	if (!wl->layout)
		return;

	pango_layout_set_width(wl->layout,
		width < 0 ? -1 : MAX(1, width) * PANGO_SCALE);
}

static void wrapped_label_update_metrics(WrappedLabel *wl)
{
	PangoRectangle logical_rect;

	if (!wl->layout)
	{
		wl->x_off = 0;
		wl->y_off = 0;
		wl->text_width = 0;
		return;
	}

	pango_layout_get_pixel_extents(wl->layout, NULL, &logical_rect);
	wl->x_off = logical_rect.x;
	wl->y_off = logical_rect.y;
	wl->text_width = logical_rect.width;
}

static GtkSizeRequestMode wrapped_label_get_request_mode(GtkWidget *widget)
{
	(void) widget;
	return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH;
}

static void wrapped_label_get_preferred_width(GtkWidget *widget,
		gint *minimum_width, gint *natural_width)
{
	WrappedLabel *wl = WRAPPED_LABEL(widget);
	PangoRectangle logical_rect;
	gint width = 1;

	if (wl->layout)
	{
		wrapped_label_set_layout_width(wl, wl->width);
		pango_layout_get_pixel_extents(wl->layout, NULL, &logical_rect);
		width = MAX(1, logical_rect.width);
		wrapped_label_update_metrics(wl);
	}

	if (minimum_width)
		*minimum_width = width;
	if (natural_width)
		*natural_width = width;
}

static void wrapped_label_get_preferred_height_for_width(GtkWidget *widget,
		gint width, gint *minimum_height, gint *natural_height)
{
	WrappedLabel *wl = WRAPPED_LABEL(widget);
	PangoRectangle logical_rect;
	gint height = 1;

	if (wl->layout)
	{
		wrapped_label_set_layout_width(wl,
			wrapped_label_layout_width(wl, width));
		pango_layout_get_pixel_extents(wl->layout, NULL, &logical_rect);
		height = MAX(1, logical_rect.height);
		wrapped_label_update_metrics(wl);
	}

	if (minimum_height)
		*minimum_height = height;
	if (natural_height)
		*natural_height = height;
}

static void wrapped_label_get_preferred_height(GtkWidget *widget,
		gint *minimum_height, gint *natural_height)
{
	WrappedLabel *wl = WRAPPED_LABEL(widget);

	wrapped_label_get_preferred_height_for_width(widget, wl->width,
		minimum_height, natural_height);
}

static gboolean wrapped_label_draw(GtkWidget *widget, cairo_t *cr)
{
	WrappedLabel *wl = WRAPPED_LABEL(widget);
	GtkAllocation allocation;
	GtkStyleContext *context;
	gint layout_width;
	gint x;
	gint y;

	if (!wl->layout)
		return FALSE;

	gtk_widget_get_allocation(widget, &allocation);
	layout_width = wrapped_label_layout_width(wl, allocation.width);
	wrapped_label_set_layout_width(wl, layout_width);
	wrapped_label_update_metrics(wl);

	x = (allocation.width - wl->text_width) / 2 - wl->x_off;
	y = -wl->y_off;

	context = gtk_widget_get_style_context(widget);
	gtk_render_layout(context, cr, x, y, wl->layout);

	return FALSE;
}

static void wrapped_label_style_updated(GtkWidget *widget)
{
	WrappedLabel *wl = WRAPPED_LABEL(widget);
	GtkWidgetClass *parent = GTK_WIDGET_CLASS(wrapped_label_parent_class);

	if (parent->style_updated)
		parent->style_updated(widget);

	if (wl->layout)
		pango_layout_context_changed(wl->layout);

	gtk_widget_queue_resize(widget);
	gtk_widget_queue_draw(widget);
}

static void wrapped_label_finalize(GObject *object)
{
	WrappedLabel *wl = WRAPPED_LABEL(object);

	g_clear_object(&wl->layout);

	G_OBJECT_CLASS(wrapped_label_parent_class)->finalize(object);
}

static void wrapped_label_class_init(WrappedLabelClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS(klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

	object_class->finalize = wrapped_label_finalize;

	widget_class->get_request_mode = wrapped_label_get_request_mode;
	widget_class->get_preferred_width = wrapped_label_get_preferred_width;
	widget_class->get_preferred_height = wrapped_label_get_preferred_height;
	widget_class->get_preferred_height_for_width =
		wrapped_label_get_preferred_height_for_width;
	widget_class->draw = wrapped_label_draw;
	widget_class->style_updated = wrapped_label_style_updated;
}

static void wrapped_label_init(WrappedLabel *wl)
{
	GtkWidget *widget = GTK_WIDGET(wl);

	gtk_widget_set_has_window(widget, FALSE);
	gtk_style_context_add_class(gtk_widget_get_style_context(widget),
		GTK_STYLE_CLASS_LABEL);

	wl->layout = NULL;
	wl->width = -1;
	wl->text_width = -1;
	wl->x_off = 0;
	wl->y_off = 0;
}

/****************************************************************
 *                      EXTERNAL INTERFACE                      *
 ****************************************************************/

GtkWidget *wrapped_label_new(const char *text, gint width)
{
	WrappedLabel *wl;

	wl = g_object_new(TYPE_WRAPPED_LABEL, NULL);
	wl->width = width;

	wrapped_label_set_text(wl, text);

	return GTK_WIDGET(wl);
}

void wrapped_label_set_text(WrappedLabel *wl, const char *text)
{
	GtkWidget *widget;

	g_return_if_fail(wl != NULL);

	widget = GTK_WIDGET(wl);

	if (!wl->layout)
	{
		wl->layout = gtk_widget_create_pango_layout(widget,
			text ? text : "");
		pango_layout_set_wrap(wl->layout, PANGO_WRAP_WORD_CHAR);
		pango_layout_set_alignment(wl->layout, PANGO_ALIGN_CENTER);
	}
	else
		pango_layout_set_text(wl->layout, text ? text : "", -1);

	wrapped_label_set_layout_width(wl, wl->width);
	wrapped_label_update_metrics(wl);

	gtk_widget_queue_resize(widget);
	gtk_widget_queue_draw(widget);
}
