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

/* abox.c - the dialog box widget used for filer operations.
 *
 * The actual code for specific operations is in action.c.
 */

/*
 * Modificado por josejp2424 (2026):
 * port a GTK3 y adaptaciones específicas de esta versión.
 */

#include "config.h"

#include <string.h>

#include "global.h"

#include "main.h"
#include "abox.h"
#include "gui_support.h"
#include "filer.h"
#include "display.h"
#include "support.h"
#include "diritem.h"
#include "pixmaps.h"

#define RESPONSE_QUIET 1

/* Parent class for chaining up finalize in a GTK3-safe way */
static GObjectClass *abox_parent_class = NULL;

/* Static prototypes */
static void abox_class_init(GObjectClass *gclass, gpointer data);
static void abox_init(GTypeInstance *object, gpointer gclass);
static gboolean abox_delete(GtkWidget *dialog, GdkEventAny *event);
static void response(GtkDialog *dialog, gint response_id);
static void abox_finalise(GObject *object);
static void shade(ABox *abox);
static void abox_set_log_visible(ABox *abox, gboolean visible);

GType abox_get_type(void)
{
	static GType type = 0;

	if (!type)
	{
		static const GTypeInfo info =
		{
			sizeof (ABoxClass),
			NULL,			/* base_init */
			NULL,			/* base_finalise */
			(GClassInitFunc) abox_class_init,
			NULL,			/* class_finalise */
			NULL,			/* class_data */
			sizeof(ABox),
			0,			/* n_preallocs */
			(GInstanceInitFunc) abox_init
		};

		type = g_type_register_static(GTK_TYPE_DIALOG,
						"ABox", &info, 0);
	}

	return type;
}

GtkWidget* abox_new(const gchar *title, gboolean quiet)
{
	GtkWidget *widget;
	ABox	  *abox;

	widget = GTK_WIDGET(gtk_widget_new(abox_get_type(), NULL));
	abox = (ABox *) widget;

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(abox->quiet), quiet);

	gtk_window_set_title(GTK_WINDOW(widget), title);

	return widget;
}

static void abox_class_init(GObjectClass *gclass, gpointer data)
{
	GtkWidgetClass *widget = (GtkWidgetClass *) gclass;
	GtkDialogClass *dialog = (GtkDialogClass *) gclass;
	ABoxClass *abox = (ABoxClass *) gclass;

	/* Cache the parent class so finalize can chain up through GObject. */
	abox_parent_class = g_type_class_peek_parent(gclass);

	widget->delete_event = abox_delete;
	dialog->response = response;
	abox->flag_toggled = NULL;
	abox->abort_operation = NULL;

	g_signal_new("flag_toggled", G_TYPE_FROM_CLASS(gclass),
		G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET(ABoxClass, flag_toggled),
		NULL, NULL, g_cclosure_marshal_VOID__INT,
		G_TYPE_NONE, 1, G_TYPE_INT);

	g_signal_new("abort_operation", G_TYPE_FROM_CLASS(gclass),
		G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET(ABoxClass, abort_operation),
		NULL, NULL, g_cclosure_marshal_VOID__VOID,
		G_TYPE_NONE, 0);

	gclass->finalize = abox_finalise;
}

static void abox_init(GTypeInstance *object, gpointer gclass)
{
	GtkWidget *frame, *text, *scrolled, *button;
	ABox *abox = ABOX(object);
	GtkDialog *dialog = GTK_DIALOG(object);
	GtkWidget *content = gtk_dialog_get_content_area(dialog);
	int i;

	/* Modificado por josejp2424 (2026): los diálogos de operaciones deben
	 * aparecer siempre centrados y no junto al puntero. */
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);

	abox->dir_label = gtk_label_new(_("<dir>"));
	gtk_widget_set_size_request(abox->dir_label, 8, -1);
	abox->results = NULL;
	abox->entry = NULL;
	abox->next_dir = NULL;
	abox->next_timer = 0;
	abox->question = FALSE;
	gtk_label_set_xalign(GTK_LABEL(abox->dir_label), 0.5);
	gtk_label_set_yalign(GTK_LABEL(abox->dir_label), 0.5);
	gtk_box_pack_start(GTK_BOX(content),
				abox->dir_label, FALSE, TRUE, 0);

	/* Agregado por josejp2424 (2026): contenedor oculto para las animaciones
	 * nativas de copia y borrado. Se muestra sólo en esas operaciones. */
	abox->operation_animation_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_halign(abox->operation_animation_box, GTK_ALIGN_CENTER);
	gtk_widget_set_no_show_all(abox->operation_animation_box, TRUE);
	gtk_box_pack_start(GTK_BOX(content), abox->operation_animation_box,
			FALSE, FALSE, 4);
	abox->operation_animation = NULL;

	abox->details = NULL;
	abox->compact_log = FALSE;
	abox->log_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(content),
				abox->log_hbox, TRUE, TRUE, 4);

	frame = gtk_frame_new(NULL);
	gtk_box_pack_start(GTK_BOX(abox->log_hbox), frame, TRUE, TRUE, 0);

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
				 GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	gtk_container_add(GTK_CONTAINER(frame), scrolled);

	text = gtk_text_view_new();
	gtk_container_add(GTK_CONTAINER(scrolled), text);

	gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(text), GTK_WRAP_WORD);
	abox->log = text;

	gtk_text_buffer_create_tag(
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(abox->log)),
			"error", "foreground", "red",
			NULL);
	gtk_text_buffer_create_tag(
			gtk_text_view_get_buffer(GTK_TEXT_VIEW(abox->log)),
			"question", "weight", PANGO_WEIGHT_BOLD,
			NULL);
	gtk_text_view_set_editable(GTK_TEXT_VIEW(text), FALSE);
	gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(text), FALSE);
	gtk_widget_set_size_request(text, 400, 100);


	dialog_add_icon_button(dialog, ROX_ICON_CANCEL, _("_Cancel"), GTK_RESPONSE_CANCEL);
	dialog_add_icon_button(dialog, ROX_ICON_NO, _("_No"), GTK_RESPONSE_NO);
	dialog_add_icon_button(dialog, ROX_ICON_YES, _("_Yes"), GTK_RESPONSE_YES);

	/* The comparison layout uses GtkGrid and a themed directional icon. */
	abox->cmp_area = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(abox->cmp_area), 2);
	gtk_grid_set_column_spacing(GTK_GRID(abox->cmp_area), 2);
	gtk_box_pack_start(GTK_BOX(content),
				abox->cmp_area, FALSE, FALSE, 2);

	for (i = 0; i < 2; i++)
	{
		abox->cmp_icon[i] = gtk_image_new();
		gtk_widget_set_margin_start(abox->cmp_icon[i], 4);
		gtk_widget_set_margin_end(abox->cmp_icon[i], 4);
		gtk_grid_attach(GTK_GRID(abox->cmp_area),
				abox->cmp_icon[i], 1, i, 1, 1);

		abox->cmp_name[i] = gtk_label_new("");
		gtk_label_set_line_wrap(GTK_LABEL(abox->cmp_name[i]), TRUE);
		gtk_label_set_xalign(GTK_LABEL(abox->cmp_name[i]), 0.0);
		gtk_label_set_yalign(GTK_LABEL(abox->cmp_name[i]), 0.5);
		gtk_widget_set_hexpand(abox->cmp_name[i], TRUE);
		gtk_widget_set_halign(abox->cmp_name[i], GTK_ALIGN_FILL);
		gtk_widget_set_margin_start(abox->cmp_name[i], 4);
		gtk_widget_set_margin_end(abox->cmp_name[i], 4);
		gtk_grid_attach(GTK_GRID(abox->cmp_area),
				abox->cmp_name[i], 2, i, 1, 1);

		abox->cmp_size[i] = gtk_label_new("");
		gtk_widget_set_margin_start(abox->cmp_size[i], 4);
		gtk_widget_set_margin_end(abox->cmp_size[i], 4);
		gtk_grid_attach(GTK_GRID(abox->cmp_area),
				abox->cmp_size[i], 3, i, 1, 1);

		abox->cmp_date[i] = gtk_label_new("");
		gtk_widget_set_margin_start(abox->cmp_date[i], 4);
		gtk_widget_set_margin_end(abox->cmp_date[i], 4);
		gtk_grid_attach(GTK_GRID(abox->cmp_area),
				abox->cmp_date[i], 4, i, 1, 1);
	}

	abox->cmp_arrow = gtk_image_new_from_icon_name(
				"go-down", GTK_ICON_SIZE_DIALOG);
	gtk_widget_set_size_request(abox->cmp_arrow, 32, 64);
	gtk_widget_set_valign(abox->cmp_arrow, GTK_ALIGN_CENTER);
	gtk_widget_set_vexpand(abox->cmp_arrow, TRUE);
	gtk_grid_attach(GTK_GRID(abox->cmp_area),
				abox->cmp_arrow, 0, 0, 1, 2);

	/* Agregado por josejp2424 (2026): permite que Reemplazar u Omitir
	 * se aplique a todos los conflictos restantes, como en los gestores
	 * de archivos modernos. Sólo se muestra durante un conflicto. */
	abox->apply_all = gtk_check_button_new_with_label(
			_("Apply this decision to all remaining conflicts"));
	gtk_widget_set_halign(abox->apply_all, GTK_ALIGN_START);
	gtk_widget_set_margin_start(abox->apply_all, 8);
	gtk_widget_set_margin_end(abox->apply_all, 8);
	gtk_box_pack_start(GTK_BOX(content), abox->apply_all, FALSE, FALSE, 2);

	abox->progress=NULL;

	abox->flag_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
	gtk_box_pack_end(GTK_BOX(content),
				abox->flag_box, FALSE, TRUE, 2);

	button = button_new_mixed(ROX_ICON_GO_LAST, _("_Quiet"));
	gtk_widget_set_can_default(button, TRUE);
	gtk_dialog_add_action_widget(dialog, button, RESPONSE_QUIET);
	gtk_dialog_set_default_response(dialog, RESPONSE_QUIET);

	gtk_widget_show_all(content);
	gtk_widget_hide(abox->cmp_area);
	gtk_widget_hide(abox->apply_all);

	abox->quiet = abox_add_flag(abox,
			_("Quiet"), _("Don't confirm every operation"),
			'Q', FALSE);

	shade(abox);
}

/* Agregado por josejp2424 (2026): ocultar completamente el registro
 * cuando no se solicitan detalles. Esto elimina la franja gris vacía que
 * quedaba debajo de las animaciones. La ventana vuelve a calcular su tamaño
 * y permanece centrada. */
static void abox_set_log_visible(ABox *abox, gboolean visible)
{
	if (!abox || !abox->log_hbox)
		return;

	if (visible)
		gtk_widget_show(abox->log_hbox);
	else
		gtk_widget_hide(abox->log_hbox);

	if (gtk_widget_get_realized(GTK_WIDGET(abox)))
		gtk_window_resize(GTK_WINDOW(abox), 1, 1);
}

static void flag_toggled(GtkToggleButton *toggle, ABox *abox)
{
	gint	code;

	code = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(toggle),
						 "abox-response"));

	if (code == 'Q')
		shade(abox);
	else if (code == 'D')
		abox_set_log_visible(abox,
			gtk_toggle_button_get_active(toggle));

	g_signal_emit_by_name(abox, "flag_toggled", code);

}

GtkWidget *abox_add_flag(ABox *abox, const gchar *label, const gchar *tip,
		   	 gint response, gboolean default_value)
{
	GtkWidget	*check;

	check = gtk_check_button_new_with_label(label);
	gtk_widget_set_tooltip_text(check, tip);
	g_object_set_data(G_OBJECT(check), "abox-response",
			  GINT_TO_POINTER(response));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check), default_value);
	g_signal_connect(check, "toggled", G_CALLBACK(flag_toggled), abox);
	gtk_box_pack_end(GTK_BOX(abox->flag_box), check, FALSE, TRUE, 0);
	gtk_widget_show(check);

	return check;
}

static void response(GtkDialog *dialog, gint response_id)
{
	ABox *abox = ABOX(dialog);

	if (response_id == GTK_RESPONSE_CANCEL)
		g_signal_emit_by_name(abox, "abort_operation");
	else if (response_id == RESPONSE_QUIET)
	{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(abox->quiet), TRUE);
		gtk_dialog_response(dialog, GTK_RESPONSE_YES);
	}
	else if (response_id == GTK_RESPONSE_YES ||
					response_id == GTK_RESPONSE_NO)
	{
		abox->question = FALSE;
		gtk_widget_hide(abox->apply_all);
		shade(abox);
	}
}

/* Display the question. Unshade the Yes, No and entry box (if any).
 * Will send a response signal when the user makes a choice.
 */
void abox_ask(ABox *abox, const gchar *question)
{
	g_return_if_fail(abox != NULL);
	g_return_if_fail(question != NULL);
	g_return_if_fail(IS_ABOX(abox));

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(abox->apply_all), FALSE);
	gtk_widget_hide(abox->apply_all);
	abox_log(abox, question, "question");

	abox->question = TRUE;
	shade(abox);
}

/* Agregado por josejp2424 (2026): pregunta de conflicto con la opción
 * de recordar Reemplazar u Omitir durante el resto de la operación. */
void abox_ask_conflict(ABox *abox, const gchar *question)
{
	g_return_if_fail(abox != NULL);
	g_return_if_fail(question != NULL);
	g_return_if_fail(IS_ABOX(abox));

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(abox->apply_all), FALSE);
	gtk_widget_show(abox->apply_all);
	abox_log(abox, question, "question");
	abox->question = TRUE;
	shade(abox);
}

gboolean abox_apply_to_all(ABox *abox)
{
	g_return_val_if_fail(abox != NULL, FALSE);
	g_return_val_if_fail(IS_ABOX(abox), FALSE);

	/* La señal GtkDialog::response puede ocultar el control antes o después
	 * del manejador conectado. El estado activo es la fuente fiable. */
	return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(abox->apply_all));
}

void abox_cancel_ask(ABox *abox)
{
	g_return_if_fail(abox != NULL);
	g_return_if_fail(IS_ABOX(abox));

	abox->question = FALSE;
	gtk_widget_hide(abox->apply_all);
	shade(abox);
}

void abox_log(ABox *abox, const gchar *message, const gchar *style)
{
	GtkTextIter end;
	GtkTextBuffer *text_buffer;
	GtkTextView *log = GTK_TEXT_VIEW(abox->log);
	gchar *u8 = NULL;

	if (!g_utf8_validate(message, -1, NULL))
		u8 = to_utf8(message);

	/* Las preguntas y errores deben ser visibles aunque el registro normal
	 * esté plegado durante una operación animada. */
	if (abox->compact_log && abox->details && style &&
	    (!strcmp(style, "error") || !strcmp(style, "question")))
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(abox->details), TRUE);

	text_buffer = gtk_text_view_get_buffer(log);

	gtk_text_buffer_get_end_iter(text_buffer, &end);
	gtk_text_buffer_insert_with_tags_by_name(text_buffer,
			&end, u8 ? u8 : message, -1, style, NULL);
	gtk_text_view_scroll_to_mark(
			log,
			gtk_text_buffer_get_mark(text_buffer, "insert"),
			0.0, FALSE, 0, 0);

	g_free(u8);
}

static void abox_finalise(GObject *object)
{
	GObjectClass *parent_class;
	ABox *abox = ABOX(object);

	if (abox->next_dir)
	{
		null_g_free(&abox->next_dir);
		g_source_remove(abox->next_timer);
	}

	/* Chain-up */
	parent_class = abox_parent_class;
	if (parent_class && parent_class->finalize)
		parent_class->finalize(object);
}

static gboolean show_next_dir(gpointer data)
{
	ABox	*abox = (ABox *) data;

	g_return_val_if_fail(IS_ABOX(abox), FALSE);

	gtk_label_set_text(GTK_LABEL(abox->dir_label), abox->next_dir);
	null_g_free(&abox->next_dir);

	return FALSE;
}

/* Display this message in the current-object area.
 * The display won't update too fast, even if you call this very often.
 */
void abox_set_current_object(ABox *abox, const gchar *message)
{
	g_return_if_fail(abox != NULL);
	g_return_if_fail(message != NULL);
	g_return_if_fail(IS_ABOX(abox));

	/* If a string is already set then replace it, but assume the
	 * timer is already running...
	 */

	if (abox->next_dir)
		g_free(abox->next_dir);
	else
	{
		gtk_label_set_text(GTK_LABEL(abox->dir_label), message);
		abox->next_timer = g_timeout_add(500, show_next_dir, abox);
	}

	abox->next_dir = g_strdup(message);
}

static void lost_preview(GtkWidget *window, ABox *abox)
{
	abox->preview = NULL;
}

static void select_row_callback(GtkTreeView *treeview,
				GtkTreePath *path,
				GtkTreeViewColumn *col,
				ABox	    *abox)
{
	GtkTreeModel	*model;
	GtkTreeIter	iter;
	char		*leaf, *dir;

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(abox->results));
	gtk_tree_model_get_iter(model, &iter, path);
	gtk_tree_model_get(model, &iter, 0, &leaf, 1, &dir, -1);

	if (abox->preview)
	{
		if (strcmp(abox->preview->real_path, dir) == 0)
			display_set_autoselect(abox->preview, leaf);
		else
			filer_change_to(abox->preview, dir, leaf);
		goto out;
	}

	abox->preview = filer_opendir(dir, NULL, NULL);
	if (abox->preview)
	{
		display_set_autoselect(abox->preview, leaf);
		g_signal_connect_object(abox->preview->window, "destroy",
				G_CALLBACK(lost_preview), abox, 0);
	}

out:
	g_free(dir);
	g_free(leaf);
}

/* Add a list-of-results area. You must use this before adding files
 * with abox_add_filename().
 */
void abox_add_results(ABox *abox)
{
	GtkTreeViewColumn	*column;
	GtkWidget	*scroller, *frame;
	GtkListStore	*model;
	GtkCellRenderer	*cell_renderer;

	g_return_if_fail(abox != NULL);
	g_return_if_fail(IS_ABOX(abox));
	g_return_if_fail(abox->results == NULL);

	scroller = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
			GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);

	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(abox))),
				frame, TRUE, TRUE, 4);

	gtk_container_add(GTK_CONTAINER(frame), scroller);

	model = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_STRING);
	abox->results = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
	g_object_unref(G_OBJECT(model));

	cell_renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(
				_("Name"), cell_renderer, "text", 0, NULL);
	gtk_tree_view_column_set_resizable(column, TRUE);
	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_GROW_ONLY);
	gtk_tree_view_append_column(GTK_TREE_VIEW(abox->results), column);
	gtk_tree_view_insert_column_with_attributes(
			GTK_TREE_VIEW(abox->results),
			1, (gchar *) _("Directory"), cell_renderer,
			"text", 1, NULL);

	gtk_container_add(GTK_CONTAINER(scroller), abox->results);

	gtk_widget_set_size_request(abox->results, 100, 100);
	gtk_box_set_child_packing(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(abox))),
			  abox->log_hbox, FALSE, TRUE, 4, GTK_PACK_START);

	g_signal_connect(abox->results, "row-activated",
			G_CALLBACK(select_row_callback), abox);

	gtk_widget_show_all(frame);
}

void abox_add_filename(ABox *abox, const gchar *path)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	gchar	*dir;
	gchar	*base = g_path_get_basename(path);

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(abox->results));

	gtk_list_store_append(GTK_LIST_STORE(model), &iter);

	dir = g_path_get_dirname(path);
	gtk_list_store_set(GTK_LIST_STORE(model), &iter,
			   0, base,
			   1, dir, -1);
	g_free(dir);
	g_free(base);
}

/* Clear search results area */
void abox_clear_results(ABox *abox)
{
	GtkTreeModel *model;

	g_return_if_fail(abox != NULL);
	g_return_if_fail(IS_ABOX(abox));

	model = gtk_tree_view_get_model(GTK_TREE_VIEW(abox->results));

	gtk_list_store_clear(GTK_LIST_STORE(model));
}

void abox_add_combo(ABox *abox, const gchar *tlabel, GList *presets,
		    const gchar *text, GtkWidget *help_button)
{
	GtkWidget *hbox, *label, *combo;

	g_return_if_fail(abox != NULL);
	g_return_if_fail(IS_ABOX(abox));
	g_return_if_fail(abox->entry == NULL);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	if (tlabel)
	{
		label = gtk_label_new(tlabel);
		gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 4);
	}

	/* Use the GTK3 entry-enabled text combo for preset values. */
	combo = gtk_combo_box_text_new_with_entry();
	for (GList *l = presets; l; l = l->next)
	{
		if (l->data)
			gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo),
				(const gchar *) l->data);
	}
	abox->entry = gtk_bin_get_child(GTK_BIN(combo));
	gtk_entry_set_activates_default(GTK_ENTRY(abox->entry), TRUE);
	gtk_entry_set_text(GTK_ENTRY(abox->entry), text);
	gtk_box_pack_start(GTK_BOX(hbox), combo, TRUE, TRUE, 4);

	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(abox))),
				hbox, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), help_button, FALSE, TRUE, 4);

	gtk_widget_show_all(hbox);

	shade(abox);
}

void abox_add_entry(ABox *abox, const gchar *text, GtkWidget *help_button)
{
	GtkWidget *hbox, *label;

	g_return_if_fail(abox != NULL);
	g_return_if_fail(IS_ABOX(abox));
	g_return_if_fail(abox->entry == NULL);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	label = gtk_label_new(_("Expression:"));
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 4);
	abox->entry = gtk_entry_new();
	gtk_widget_set_name(abox->entry, "fixed-style");
	gtk_entry_set_text(GTK_ENTRY(abox->entry), text);
	gtk_box_pack_start(GTK_BOX(hbox), abox->entry, TRUE, TRUE, 4);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(abox))),
				hbox, FALSE, TRUE, 4);
	gtk_box_pack_start(GTK_BOX(hbox), help_button,
				FALSE, TRUE, 4);

	gtk_entry_set_activates_default(GTK_ENTRY(abox->entry), TRUE);

	gtk_widget_show_all(hbox);

	shade(abox);
}

static void shade(ABox *abox)
{
	GtkDialog *dialog = (GtkDialog *) abox;
	gboolean quiet, on = abox->question;

	quiet = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(abox->quiet));

	gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_YES, on);
	gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_NO, on);

	if (on && !quiet)
		gtk_dialog_set_response_sensitive(dialog, RESPONSE_QUIET, TRUE);
	else
		gtk_dialog_set_response_sensitive(dialog, RESPONSE_QUIET, FALSE);

	/* Unsetting the focus means that set_default will put it in the
	 * right place...
	 */
	gtk_window_set_focus(GTK_WINDOW(abox), NULL);
	/* Keep the default response synchronized with the enabled action. */
	if (quiet)
		gtk_dialog_set_default_response(dialog, GTK_RESPONSE_YES);
	else
		gtk_dialog_set_default_response(dialog, RESPONSE_QUIET);

	if (abox->entry)
	{
		gtk_widget_set_sensitive(abox->entry, on);
		if (on)
			gtk_widget_grab_focus(abox->entry);
	}
}

static gboolean abox_delete(GtkWidget *dialog, GdkEventAny *event)
{
	g_signal_emit_by_name(dialog, "abort_operation");
	return TRUE;
}

void abox_show_compare(ABox *abox, gboolean show)
{
	if (show)
		gtk_widget_show(abox->cmp_area);
	else
		gtk_widget_hide(abox->cmp_area);
}

void abox_set_file(ABox *abox, int i, const gchar *path)
{
	DirItem *item;
	gchar *base;

	g_return_if_fail(i >= 0 && i < 2);
	g_return_if_fail(IS_ABOX(abox));

	if (!path || !path[0])
	{
		gtk_widget_hide(abox->cmp_icon[i]);
		gtk_widget_hide(abox->cmp_name[i]);
		gtk_widget_hide(abox->cmp_size[i]);
		gtk_widget_hide(abox->cmp_date[i]);
		gtk_widget_hide(abox->cmp_arrow);
		return;
	}

	base = g_path_get_basename(path);
	item = diritem_new(base);
	g_free(base);
	diritem_restat(path, item, NULL);

	gtk_image_set_from_pixbuf(GTK_IMAGE(abox->cmp_icon[i]),
				  di_image(item)->pixbuf);
	gtk_widget_show(abox->cmp_icon[i]);

	gtk_label_set_text(GTK_LABEL(abox->cmp_name[i]), item->leafname);
	gtk_widget_show(abox->cmp_name[i]);
	gtk_widget_show(abox->cmp_arrow);

	if (item->lstat_errno)
	{
		gtk_label_set_text(GTK_LABEL(abox->cmp_size[i]), "Error");
		gtk_label_set_text(GTK_LABEL(abox->cmp_date[i]),
				g_strerror(item->lstat_errno));
	}
	else
	{
		gchar *str;

		gtk_label_set_text(GTK_LABEL(abox->cmp_size[i]),
				format_size_aligned(item->size));

		str = pretty_time(&item->mtime);
		gtk_label_set_text(GTK_LABEL(abox->cmp_date[i]), str);
		g_free(str);
	}

	gtk_widget_show(abox->cmp_size[i]);
	gtk_widget_show(abox->cmp_date[i]);

	diritem_free(item);
}

void    abox_set_percentage(ABox *abox, int per)
{
	if(!abox->progress) {
		GtkDialog *dialog = GTK_DIALOG(abox);
		GtkWidget *content = gtk_dialog_get_content_area(dialog);

		abox->progress=gtk_progress_bar_new ();
		gtk_box_pack_start(GTK_BOX(content),
				abox->progress, FALSE, FALSE, 2);
		gtk_widget_show(abox->progress);
	}
	if(per<0 || per>100) {
		gtk_widget_hide(abox->progress);
		return;
	}
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(abox->progress),
				      per/100.);
}

/* Agregado por josejp2424 (2026): carga una animación GIF desde
 * ROX-Filer/images y la muestra centrada en la ventana de operación.
 * GtkImage mantiene viva la GdkPixbufAnimation hasta que el diálogo se
 * destruye, por lo que no se necesita ningún temporizador externo. */
void abox_set_operation_animation(ABox *abox, const gchar *filename)
{
	GList *children;
	GList *iter;
	GdkPixbufAnimation *animation;
	GError *error = NULL;
	gchar *path;

	g_return_if_fail(abox != NULL);
	g_return_if_fail(IS_ABOX(abox));

	children = gtk_container_get_children(
		GTK_CONTAINER(abox->operation_animation_box));
	for (iter = children; iter; iter = iter->next)
		gtk_widget_destroy(GTK_WIDGET(iter->data));
	g_list_free(children);
	abox->operation_animation = NULL;

	if (!filename || !*filename)
	{
		gtk_widget_hide(abox->operation_animation_box);
		return;
	}

	/* Modificado por josejp2424 (2026): las operaciones con animación usan
	 * un diseño compacto. El registro se pliega por defecto y se puede abrir
	 * con Detalles. No se reutiliza la opción Brief, porque esa opción sólo
	 * controla cuánto se registra y no la visibilidad del panel. */
	abox->compact_log = TRUE;
	gtk_widget_set_no_show_all(abox->log_hbox, TRUE);
	if (!abox->details)
		abox->details = abox_add_flag(abox,
			_("Details"), _("Show Log"), 'D', FALSE);
	abox_set_log_visible(abox, FALSE);

	path = g_build_filename(app_dir, "images", filename, NULL);
	animation = gdk_pixbuf_animation_new_from_file(path, &error);
	g_free(path);

	if (!animation)
	{
		if (error)
		{
			g_warning("Unable to load ROX operation animation '%s': %s",
				filename, error->message);
			g_error_free(error);
		}
		gtk_widget_hide(abox->operation_animation_box);
		return;
	}

	abox->operation_animation = gtk_image_new_from_animation(animation);
	g_object_unref(animation);
	gtk_widget_set_halign(abox->operation_animation, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(abox->operation_animation, GTK_ALIGN_CENTER);
	gtk_box_pack_start(GTK_BOX(abox->operation_animation_box),
		abox->operation_animation, FALSE, FALSE, 0);
	gtk_widget_show(abox->operation_animation);
	gtk_widget_show(abox->operation_animation_box);
}

/* Agregado por josejp2424 (2026): detener y liberar la animación apenas
 * termina la operación. Dejar un GIF activo en un diálogo de resultados
 * mantiene repintados periódicos y puede producir lag en equipos modestos. */
void abox_stop_operation_animation(ABox *abox)
{
	GList *children;

	g_return_if_fail(abox != NULL);
	g_return_if_fail(IS_ABOX(abox));

	children = gtk_container_get_children(
		GTK_CONTAINER(abox->operation_animation_box));
	g_list_free_full(children, (GDestroyNotify) gtk_widget_destroy);
	abox->operation_animation = NULL;
	gtk_widget_hide(abox->operation_animation_box);
}
