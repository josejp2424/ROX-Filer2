/*
 * Rox-Filer2 interface style selection.
 * Copyright (C) 2026 josejp2424 and Rox-Filer2 contributors.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include "config.h"
#include <gtk/gtk.h>

#include "global.h"
#include "main.h"
#include "options.h"
#include "interface_style.h"
#include "debug_log.h"

static Option o_interface_style;
static RoxInterfaceStyle last_style = ROX_INTERFACE_CLASSIC;
static gint command_override = -1;

static RoxInterfaceStyle normalise_style(long value)
{
	return value == ROX_INTERFACE_MODERN
		? ROX_INTERFACE_MODERN : ROX_INTERFACE_CLASSIC;
}

static GtkWidget *preview_image(const gchar *filename, const gchar *fallback_icon)
{
	gchar *path;
	GdkPixbuf *source = NULL;
	GdkPixbuf *scaled = NULL;
	GError *error = NULL;
	GtkWidget *image;
	gint width, height;
	const gint max_width = 190;
	const gint max_height = 120;
	double factor;

	path = g_build_filename(app_dir, "images", filename, NULL);
	source = gdk_pixbuf_new_from_file(path, &error);
	g_free(path);

	if (!source) {
		ROX_LOG_WARNING("interface", "unable to load interface preview %s: %s",
		                filename, error ? error->message : "unknown error");
		g_clear_error(&error);
		return gtk_image_new_from_icon_name(fallback_icon, GTK_ICON_SIZE_DIALOG);
	}

	width = gdk_pixbuf_get_width(source);
	height = gdk_pixbuf_get_height(source);
	factor = MIN((double) max_width / MAX(width, 1),
	             (double) max_height / MAX(height, 1));
	if (factor > 1.0)
		factor = 1.0;
	width = MAX(1, (gint) (width * factor));
	height = MAX(1, (gint) (height * factor));
	scaled = gdk_pixbuf_scale_simple(source, width, height, GDK_INTERP_BILINEAR);
	g_object_unref(source);

	image = gtk_image_new_from_pixbuf(scaled);
	if (scaled)
		g_object_unref(scaled);
	return image;
}

static GtkWidget *style_card(GtkWidget *group_member,
                             const gchar *title,
                             const gchar *preview,
                             const gchar *fallback_icon)
{
	GtkWidget *radio;
	GtkWidget *box;
	GtkWidget *image;
	GtkWidget *title_label;
	PangoAttrList *attrs;

	radio = group_member
		? gtk_radio_button_new_from_widget(GTK_RADIO_BUTTON(group_member))
		: gtk_radio_button_new(NULL);
	gtk_widget_set_hexpand(radio, TRUE);
	gtk_widget_set_halign(radio, GTK_ALIGN_FILL);
	gtk_widget_set_valign(radio, GTK_ALIGN_START);

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 7);
	gtk_widget_set_margin_start(box, 6);
	gtk_widget_set_margin_end(box, 6);
	gtk_widget_set_margin_top(box, 6);
	gtk_widget_set_margin_bottom(box, 6);

	image = preview_image(preview, fallback_icon);
	gtk_widget_set_halign(image, GTK_ALIGN_CENTER);
	gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);

	title_label = gtk_label_new(_(title));
	gtk_label_set_xalign(GTK_LABEL(title_label), 0.5);
	attrs = pango_attr_list_new();
	pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
	gtk_label_set_attributes(GTK_LABEL(title_label), attrs);
	pango_attr_list_unref(attrs);
	gtk_box_pack_start(GTK_BOX(box), title_label, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(radio), box);
	return radio;
}

static void update_interface_chooser(Option *option)
{
	GtkWidget *classic;
	GtkWidget *modern;

	classic = g_object_get_data(G_OBJECT(option->widget), "rox-classic-radio");
	modern = g_object_get_data(G_OBJECT(option->widget), "rox-modern-radio");
	if (!classic || !modern)
		return;

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(
		option->int_value == ROX_INTERFACE_MODERN ? modern : classic), TRUE);
}

static guchar *read_interface_chooser(Option *option)
{
	GtkWidget *modern;

	modern = g_object_get_data(G_OBJECT(option->widget), "rox-modern-radio");
	return g_strdup((modern && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(modern)))
	                ? "1" : "0");
}

static GList *build_interface_chooser(Option *option, xmlNode *node, guchar *label)
{
	GtkWidget *grid;
	GtkWidget *classic;
	GtkWidget *modern;

	(void) node;
	(void) label;
	g_return_val_if_fail(option != NULL, NULL);

	grid = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
	gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
	gtk_widget_set_hexpand(grid, TRUE);

	/* 2.12.2-82: style_card() llama a _(title) sobre un parametro, y xgettext
	 * no puede extraer una cadena que no ve literalmente dentro de _().  Los
	 * marcadores N_() de abajo la ponen en messages.pot; la traduccion real
	 * sigue ocurriendo en style_card(). */
	classic = style_card(NULL, N_("Classic ROX"),
	                     "interface-classic-preview.png", "system-file-manager");
	modern = style_card(classic, N_("Modern"),
	                    "interface-modern-preview.png", "folder");
	gtk_grid_attach(GTK_GRID(grid), classic, 0, 0, 1, 1);
	gtk_grid_attach(GTK_GRID(grid), modern, 1, 0, 1, 1);

	option->widget = grid;
	option->update_widget = update_interface_chooser;
	option->read_widget = read_interface_chooser;
	g_object_set_data(G_OBJECT(grid), "rox-classic-radio", classic);
	g_object_set_data(G_OBJECT(grid), "rox-modern-radio", modern);

	g_signal_connect_swapped(classic, "toggled",
	                         G_CALLBACK(option_check_widget), option);
	g_signal_connect_swapped(modern, "toggled",
	                         G_CALLBACK(option_check_widget), option);

	return g_list_append(NULL, grid);
}

static void interface_style_changed(void)
{
	RoxInterfaceStyle style = normalise_style(o_interface_style.int_value);

	if (style == last_style)
		return;

	last_style = style;
	ROX_LOG_INFO("interface", "selected saved style=%s",
	             style == ROX_INTERFACE_MODERN ? "modern" : "classic");
}

void interface_style_init(void)
{
	option_add_int(&o_interface_style, "interface_style", ROX_INTERFACE_CLASSIC);
	option_register_widget("interface-style-chooser", build_interface_chooser);
	last_style = normalise_style(o_interface_style.int_value);
	option_add_notify(interface_style_changed);
	ROX_LOG_INFO("interface", "startup saved style=%s",
	             last_style == ROX_INTERFACE_MODERN ? "modern" : "classic");
}

void interface_style_set_command_override(RoxInterfaceStyle style)
{
	command_override = normalise_style(style);
	ROX_LOG_INFO("interface", "command-line override=%s",
	             command_override == ROX_INTERFACE_MODERN ? "modern" : "classic");
}

void interface_style_clear_command_override(void)
{
	command_override = -1;
}

RoxInterfaceStyle interface_style_current(void)
{
	if (command_override >= 0)
		return normalise_style(command_override);
	return normalise_style(o_interface_style.int_value);
}

gboolean interface_style_is_modern(void)
{
	return interface_style_current() == ROX_INTERFACE_MODERN;
}

const gchar *interface_style_name(void)
{
	return interface_style_is_modern() ? "modern" : "classic";
}
