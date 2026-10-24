/*
 * Rox-Filer2 Modern interface shell.
 *
 * This module only adds the alternative window chrome. File browsing,
 * history, MIME, mounts, Trash, Desktop and file operations remain in the
 * existing Rox-Filer2 backend and are shared with Classic ROX.
 */

#include "config.h"

#include <gtk/gtk.h>
#include <errno.h>
#include <string.h>

#include "global.h"
#include "view_iface.h"
#include "diritem.h"
#include "gui_support.h"
#include "modern_ui.h"
#include "filer.h"
#include "main.h"
#include "menu.h"
#include "bookmarks.h"
#include "search_integration.h"
#include "options.h"
#include "desktop.h"
#include "trash.h"
#include "drives.h"
#include "drives_monitor.h"
#include "mount.h"
#include "smb.h"
#include "samba_share.h"
#include "display.h"
#include "dnd.h"
#include "toolbar.h"
#include "support.h"
#include "type.h"
#include "view_details.h"
#include "action.h"



typedef struct
{
	FilerWindow *host;
	FilerWindow *right;
	FilerWindow *active;
	GtkWidget *paned;
	GtkWidget *left_outer;
	GtkWidget *left_content;
	GtkWidget *right_outer;
	GtkWidget *right_content;
	gboolean enabled;
	gint last_position;
} ModernSplitState;

static ModernSplitState *modern_split_state(FilerWindow *filer_window)
{
	if (!filer_window || !filer_window->window)
		return NULL;
	if (filer_window->modern_embedded)
	{
		FilerWindow *host = g_object_get_data(G_OBJECT(filer_window->window),
			"rox-modern-split-host");
		if (!host || !host->window)
			return NULL;
		return g_object_get_data(G_OBJECT(host->window), "rox-modern-split-state");
	}
	return g_object_get_data(G_OBJECT(filer_window->window), "rox-modern-split-state");
}

static void modern_split_state_free(gpointer data)
{
	ModernSplitState *state = data;
	g_free(state);
}

static GtkWidget *icon_button(const gchar *icon, const gchar *tip);
static void modern_sidebar_set_visible(FilerWindow *filer_window, gboolean visible);
static void modern_ui_select_current_place(FilerWindow *filer_window);
static void modern_tabs_sync_current(FilerWindow *filer_window);
static void modern_breadcrumb_rebuild(FilerWindow *filer_window);
static void modern_location_show_editor(FilerWindow *filer_window);
static void modern_location_show_breadcrumbs(FilerWindow *filer_window);
static void modern_location_show_default(FilerWindow *filer_window);
static GtkWidget *modern_location_widget_new(FilerWindow *filer_window, gboolean compact);
static GtkWidget *modern_filter_bar_new(FilerWindow *filer_window);
static void modern_filter_toggle(FilerWindow *filer_window);
static FilerWindow *modern_preview_owner(FilerWindow *filer_window);
static void modern_preview_set_visible(FilerWindow *filer_window, gboolean visible);
static void modern_split_toggle(FilerWindow *filer_window);
static FilerWindow *modern_nav_target(FilerWindow *host);
static void modern_transfer_to_other_pane(FilerWindow *host, gboolean move);
static void modern_bookmarks_refresh(FilerWindow *filer_window);

static void modern_install_style(GtkWidget *widget)
{
	GdkScreen *screen;
	GtkCssProvider *provider;
	const gchar *css =
		".rox-modern-sidebar, .rox-modern-sidebar viewport, "
		".rox-modern-sidebar list { background-color: @theme_bg_color; }\n"
		".rox-modern-sidebar row { background-color: transparent; border: 0; box-shadow: none; }\n"
		".rox-modern-sidebar row:selected { background-color: @theme_selected_bg_color; color: @theme_selected_fg_color; }\n"
		".rox-modern-sidebar row image { color: @theme_fg_color; }\n"
		".rox-modern-sidebar row:selected image, .rox-modern-sidebar row:selected label { color: @theme_selected_fg_color; }\n"
		".rox-modern-sidebar-heading { font-weight: bold; }\n"
		".rox-modern-tabbar { background-color: @theme_bg_color; }\n"
		".rox-modern-tab { border-radius: 0; }\n"
		".rox-modern-tab-close { padding: 2px; min-width: 24px; min-height: 24px; }\n"
		".rox-modern-split-pane { border: 1px solid transparent; }\n"
		".rox-modern-split-pane-active { border-color: @theme_selected_bg_color; }\n";

	if (!widget)
		return;
	screen = gtk_widget_get_screen(widget);
	if (!screen || g_object_get_data(G_OBJECT(screen), "rox-modern-css-installed"))
		return;
	provider = gtk_css_provider_new();
	gtk_css_provider_load_from_data(provider, css, -1, NULL);
	gtk_style_context_add_provider_for_screen(screen, GTK_STYLE_PROVIDER(provider),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(provider);
	g_object_set_data(G_OBJECT(screen), "rox-modern-css-installed", GINT_TO_POINTER(1));
}

static GtkWidget *modern_sidebar_icon(const gchar *icon_name, gint size)
{
	GtkIconTheme *theme;
	GtkWidget *image;
	gchar *symbolic;
	const gchar *use_name;

	if (!icon_name || !*icon_name)
		icon_name = "folder";
	theme = gtk_icon_theme_get_default();
	if (g_str_has_suffix(icon_name, "-symbolic"))
		symbolic = g_strdup(icon_name);
	else
		symbolic = g_strconcat(icon_name, "-symbolic", NULL);
	use_name = theme && gtk_icon_theme_has_icon(theme, symbolic) ? symbolic : icon_name;
	image = gtk_image_new_from_icon_name(use_name, GTK_ICON_SIZE_MENU);
	gtk_image_set_pixel_size(GTK_IMAGE(image), size);
	g_free(symbolic);
	return image;
}

/* -------------------------------------------------------------------------
 * Modern session persistence (2.12.2-42)
 *
 * Only Modern UI state is stored here.  The Classic ROX configuration and all
 * shared file/mount/MIME/Trash/Desktop backends remain untouched.
 */

#define MODERN_SESSION_FILE "modern-session.ini"
#define MODERN_SESSION_GROUP "ModernWindow"
#define MODERN_DEFAULT_WIDTH 800
#define MODERN_DEFAULT_HEIGHT 640
static gboolean modern_session_restored_once = FALSE;
static GList *modern_windows = NULL;
static FilerWindow *modern_session_owner = NULL;

static gchar *modern_session_path(void)
{
	gchar *dir = g_build_filename(g_get_user_config_dir(),
		"rox.sourceforge.net", "ROX-Filer", NULL);
	gchar *path;
	g_mkdir_with_parents(dir, 0700);
	path = g_build_filename(dir, MODERN_SESSION_FILE, NULL);
	g_free(dir);
	return path;
}

static GKeyFile *modern_session_load(void)
{
	GKeyFile *kf = g_key_file_new();
	gchar *path = modern_session_path();
	GError *error = NULL;
	if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &error))
		g_clear_error(&error);
	g_free(path);
	return kf;
}

static void modern_session_write(GKeyFile *kf)
{
	gchar *path;
	gchar *data;
	gsize len = 0;
	GError *error = NULL;
	if (!kf)
		return;
	data = g_key_file_to_data(kf, &len, NULL);
	path = modern_session_path();
	if (!g_file_set_contents(path, data, (gssize) len, &error))
		g_clear_error(&error);
	g_free(path);
	g_free(data);
}

/* -------------------------------------------------------------------------
 * Modern tabs and tab DnD (2.12.2-42)
 *
 * The Modern tab layer keeps one shared Rox-Filer2 file-operation backend, while
 * each tab stores and restores its own navigation/view state.  Directory, MIME,
 * mount, Trash and file-operation code remain shared with Classic ROX.
 */

typedef struct _ModernTabsState ModernTabsState;

typedef struct
{
	ModernTabsState *state;
	GtkWidget *box;
	GtkWidget *button;
	GtkWidget *icon;
	GtkWidget *label;
	GtkWidget *close;
	gchar *path;

	/* 2.12.2-40: per-tab UI/navigation state.  The underlying directory,
	 * MIME, mount and operation engine is still the shared Rox-Filer2
	 * FilerWindow backend; only the state that belongs to a tab is saved and
	 * restored when the user changes tabs. */
	GList *history_back;
	GList *history_forward;
	GPtrArray *selected_names;
	ViewType view_type;
	DisplayStyle display_style_wanted;
	DetailsType details_type;
	SortType sort_type;
	GtkSortType sort_order;
	gboolean show_hidden;
	gdouble scroll_value;
	guint restore_source;
} ModernTab;

struct _ModernTabsState
{
	FilerWindow *filer_window;
	GtkWidget *bar;
	GtkWidget *tabs_box;
	GtkWidget *new_button;
	GPtrArray *tabs;
	ModernTab *active;
	ModernTab *drag_hover_tab;
	guint drag_hover_source;
	guint session_restore_source;
	gboolean updating_tab_buttons;
};

static void modern_tab_cancel_hover(ModernTabsState *state);

static GList *modern_history_copy(GList *src)
{
	GList *out = NULL;
	for (; src; src = src->next)
		out = g_list_append(out, g_strdup((const gchar *) src->data));
	return out;
}

static void modern_history_replace(GList **dst, GList *src)
{
	if (!dst)
		return;
	g_list_free_full(*dst, g_free);
	*dst = modern_history_copy(src);
}

static void modern_tab_clear_selection(ModernTab *tab)
{
	if (!tab)
		return;
	if (tab->selected_names)
		g_ptr_array_set_size(tab->selected_names, 0);
}

static void modern_tab_capture_state(ModernTab *tab)
{
	FilerWindow *fw;
	GtkAdjustment *adj;
	ViewIter iter;
	DirItem *item;

	if (!tab || !tab->state || !(fw = tab->state->filer_window))
		return;

	g_free(tab->path);
	tab->path = g_strdup(fw->sym_path ? fw->sym_path : home_dir);
	modern_history_replace(&tab->history_back, fw->history_back);
	modern_history_replace(&tab->history_forward, fw->history_forward);
	tab->view_type = fw->view_type;
	tab->display_style_wanted = fw->display_style_wanted;
	tab->details_type = fw->details_type;
	tab->sort_type = fw->sort_type;
	tab->sort_order = fw->sort_order;
	tab->show_hidden = fw->show_hidden;

	adj = fw->scrollbar ? gtk_range_get_adjustment(GTK_RANGE(fw->scrollbar)) : NULL;
	tab->scroll_value = adj ? gtk_adjustment_get_value(adj) : 0.0;

	modern_tab_clear_selection(tab);
	if (fw->view) {
		view_get_iter(fw->view, &iter, VIEW_ITER_SELECTED);
		while ((item = iter.next(&iter)))
			g_ptr_array_add(tab->selected_names, g_strdup(item->leafname));
	}
}

static gboolean modern_tab_restore_view_idle(gpointer data)
{
	ModernTab *tab = data;
	ModernTabsState *state;
	FilerWindow *fw;
	GtkAdjustment *adj;
	GHashTable *wanted;
	ViewIter iter;
	DirItem *item;
	guint i;

	if (!tab)
		return G_SOURCE_REMOVE;
	tab->restore_source = 0;
	if (!(state = tab->state) || state->active != tab ||
	    !(fw = state->filer_window) || !fw->window || !GTK_IS_WIDGET(fw->window))
		return G_SOURCE_REMOVE;

	if (fw->view && tab->selected_names && tab->selected_names->len) {
		wanted = g_hash_table_new(g_str_hash, g_str_equal);
		for (i = 0; i < tab->selected_names->len; i++)
			g_hash_table_add(wanted, g_ptr_array_index(tab->selected_names, i));
		view_clear_selection(fw->view);
		view_get_iter(fw->view, &iter, 0);
		while ((item = iter.next(&iter)))
			if (g_hash_table_contains(wanted, item->leafname))
				view_set_selected(fw->view, &iter, TRUE);
		g_hash_table_destroy(wanted);
	}

	adj = fw->scrollbar ? gtk_range_get_adjustment(GTK_RANGE(fw->scrollbar)) : NULL;
	if (adj) {
		gdouble maxv = MAX(0.0, gtk_adjustment_get_upper(adj) -
			gtk_adjustment_get_page_size(adj));
		gtk_adjustment_set_value(adj, CLAMP(tab->scroll_value, 0.0, maxv));
	}
	return G_SOURCE_REMOVE;
}

static void modern_tab_restore_state(ModernTab *tab)
{
	ModernTabsState *state;
	FilerWindow *fw;

	if (!tab || !(state = tab->state) || !(fw = state->filer_window))
		return;

	modern_history_replace(&fw->history_back, tab->history_back);
	modern_history_replace(&fw->history_forward, tab->history_forward);

	/* Switching tabs must not create an artificial Back entry in the target
	 * tab.  The tab already owns its own history. */
	if (tab->path && g_strcmp0(tab->path, fw->sym_path) != 0) {
		fw->history_navigation = TRUE;
		filer_change_to(fw, tab->path, NULL);
		fw->history_navigation = FALSE;
	}

	if (fw->view_type != tab->view_type)
		filer_set_view_type(fw, tab->view_type);
	if (tab->display_style_wanted != UNKNOWN_STYLE)
		display_set_layout(fw, tab->display_style_wanted, tab->details_type, FALSE);
	display_set_sort_type(fw, tab->sort_type, tab->sort_order);
	if (fw->show_hidden != tab->show_hidden)
		display_set_hidden(fw, tab->show_hidden);

	modern_ui_update_navigation(fw);
	modern_ui_update_path(fw);
	if (tab->restore_source)
		g_source_remove(tab->restore_source);
	tab->restore_source = g_idle_add(modern_tab_restore_view_idle, tab);
}

static void modern_tab_free(gpointer data)
{
	ModernTab *tab = data;
	if (!tab)
		return;
	if (tab->restore_source) {
		g_source_remove(tab->restore_source);
		tab->restore_source = 0;
	}
	if (tab->box && GTK_IS_WIDGET(tab->box))
		gtk_widget_destroy(tab->box);
	g_free(tab->path);
	g_list_free_full(tab->history_back, g_free);
	g_list_free_full(tab->history_forward, g_free);
	if (tab->selected_names)
		g_ptr_array_free(tab->selected_names, TRUE);
	g_free(tab);
}

static void modern_tabs_state_free(gpointer data)
{
	ModernTabsState *state = data;
	if (!state)
		return;
	modern_tab_cancel_hover(state);
	if (state->session_restore_source) {
		g_source_remove(state->session_restore_source);
		state->session_restore_source = 0;
	}
	if (state->tabs)
		g_ptr_array_free(state->tabs, TRUE);
	g_free(state);
}

static ModernTabsState *modern_tabs_state(FilerWindow *filer_window)
{
	if (!filer_window || !filer_window->window)
		return NULL;
	return g_object_get_data(G_OBJECT(filer_window->window), "rox-modern-tabs-state");
}

FilerWindow *modern_ui_primary_window(void)
{
	if (modern_session_owner && modern_session_owner->window &&
	    GTK_IS_WIDGET(modern_session_owner->window))
		return modern_session_owner;
	if (modern_windows)
		return (FilerWindow *) modern_windows->data;
	return NULL;
}

static gchar *modern_tab_title_for_path(const gchar *path)
{
	gchar *title;

	if (!path || !*path || g_strcmp0(path, "/") == 0)
		return g_strdup(_("File System"));
	if (home_dir && g_strcmp0(path, home_dir) == 0)
		return g_strdup(_("Home"));

	title = g_filename_display_basename(path);
	if (!title || !*title) {
		g_free(title);
		return g_strdup(path);
	}
	return title;
}

static const gchar *modern_tab_icon_for_path(const gchar *path)
{
	const gchar *xdg;
	gchar *desktop_path;

	if (!path || !*path || g_strcmp0(path, "/") == 0)
		return "drive-harddisk-symbolic";
	if (home_dir && g_strcmp0(path, home_dir) == 0)
		return "user-home-symbolic";

	desktop_path = desktop_dup_directory();
	if (desktop_path && g_strcmp0(path, desktop_path) == 0) {
		g_free(desktop_path);
		return "user-desktop-symbolic";
	}
	g_free(desktop_path);

	xdg = g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
	if (xdg && g_strcmp0(path, xdg) == 0) return "folder-download-symbolic";
	xdg = g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS);
	if (xdg && g_strcmp0(path, xdg) == 0) return "folder-documents-symbolic";
	xdg = g_get_user_special_dir(G_USER_DIRECTORY_MUSIC);
	if (xdg && g_strcmp0(path, xdg) == 0) return "folder-music-symbolic";
	xdg = g_get_user_special_dir(G_USER_DIRECTORY_PICTURES);
	if (xdg && g_strcmp0(path, xdg) == 0) return "folder-pictures-symbolic";
	xdg = g_get_user_special_dir(G_USER_DIRECTORY_VIDEOS);
	if (xdg && g_strcmp0(path, xdg) == 0) return "folder-videos-symbolic";
	xdg = g_get_user_special_dir(G_USER_DIRECTORY_TEMPLATES);
	if (xdg && g_strcmp0(path, xdg) == 0) return "folder-templates-symbolic";

	if (g_str_has_prefix(path, "/mnt/") || g_str_has_prefix(path, "/media/") ||
	    g_str_has_prefix(path, "/run/media/"))
		return "drive-harddisk-symbolic";
	return "folder-symbolic";
}

static void modern_tab_refresh_label(ModernTab *tab)
{
	gchar *title;
	if (!tab || !tab->label)
		return;
	title = modern_tab_title_for_path(tab->path);
	gtk_label_set_text(GTK_LABEL(tab->label), title);
	if (tab->icon && GTK_IS_IMAGE(tab->icon))
		gtk_image_set_from_icon_name(GTK_IMAGE(tab->icon),
			modern_tab_icon_for_path(tab->path), GTK_ICON_SIZE_MENU);
	gtk_widget_set_tooltip_text(tab->box ? tab->box : tab->button,
		tab->path ? tab->path : title);
	g_free(title);
}

static void modern_tabs_refresh_active(ModernTabsState *state)
{
	guint i;
	if (!state || !state->tabs)
		return;
	state->updating_tab_buttons = TRUE;
	for (i = 0; i < state->tabs->len; i++) {
		ModernTab *tab = g_ptr_array_index(state->tabs, i);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tab->button), tab == state->active);
	}
	state->updating_tab_buttons = FALSE;
}

static gint modern_tab_index(ModernTabsState *state, ModernTab *tab)
{
	guint i;
	if (!state || !state->tabs || !tab)
		return -1;
	for (i = 0; i < state->tabs->len; i++)
		if (g_ptr_array_index(state->tabs, i) == tab)
			return (gint) i;
	return -1;
}

static void modern_tab_activate(ModernTab *tab)
{
	ModernTabsState *state;
	if (!tab || !(state = tab->state) || !state->filer_window)
		return;
	if (state->active == tab) {
		modern_tabs_refresh_active(state);
		return;
	}
	if (state->active)
		modern_tab_capture_state(state->active);
	state->active = tab;
	modern_tabs_refresh_active(state);
	modern_tab_restore_state(tab);
	if (state->filer_window->view)
		gtk_widget_grab_focus(GTK_WIDGET(state->filer_window->view));
}

static void modern_tab_toggled(GtkToggleButton *button, gpointer data)
{
	ModernTab *tab = data;
	ModernTabsState *state;

	if (!tab || !(state = tab->state) || state->updating_tab_buttons)
		return;
	/* Only activation selects a tab.  Clicking the current tab may toggle it
	 * off momentarily; immediately restore the one-active-tab invariant. */
	if (!gtk_toggle_button_get_active(button)) {
		if (state->active == tab)
			modern_tabs_refresh_active(state);
		return;
	}
	modern_tab_activate(tab);
}

static void modern_tab_cancel_hover(ModernTabsState *state)
{
	if (!state)
		return;
	if (state->drag_hover_source)
	{
		g_source_remove(state->drag_hover_source);
		state->drag_hover_source = 0;
	}
	state->drag_hover_tab = NULL;
}

static gboolean modern_tab_hover_activate(gpointer data)
{
	ModernTabsState *state = data;
	ModernTab *tab;
	if (!state)
		return G_SOURCE_REMOVE;
	tab = state->drag_hover_tab;
	state->drag_hover_source = 0;
	state->drag_hover_tab = NULL;
	if (tab && state->active != tab)
		modern_tab_activate(tab);
	return G_SOURCE_REMOVE;
}

static gboolean modern_tab_drag_motion(GtkWidget *widget, GdkDragContext *context,
		gint x, gint y, guint time, gpointer data)
{
	ModernTab *tab = data;
	ModernTabsState *state;
	GdkDragAction action;
	(void) widget; (void) x; (void) y;
	if (!tab || !(state = tab->state) || !tab->path)
		return FALSE;
	g_dataset_set_data(context, "drop_dest_type", (gpointer) drop_dest_dir);
	g_dataset_set_data_full(context, "drop_dest_path", g_strdup(tab->path), g_free);
	action = gdk_drag_context_get_suggested_action(context);
	if (action == 0)
		action = GDK_ACTION_COPY;
	gdk_drag_status(context, action, time);
	if (state->active != tab && state->drag_hover_tab != tab)
	{
		modern_tab_cancel_hover(state);
		state->drag_hover_tab = tab;
		state->drag_hover_source = g_timeout_add(650, modern_tab_hover_activate, state);
	}
	return TRUE;
}

static void modern_tab_drag_leave(GtkWidget *widget, GdkDragContext *context,
		guint time, gpointer data)
{
	ModernTab *tab = data;
	(void) widget; (void) context; (void) time;
	if (tab && tab->state && tab->state->drag_hover_tab == tab)
		modern_tab_cancel_hover(tab->state);
}

static ModernTab *modern_tabs_add(ModernTabsState *state, const gchar *path,
		gboolean activate)
{
	ModernTab *tab;
	GtkWidget *box;
	GtkWidget *button;
	GtkWidget *label;
	GtkWidget *close;
	GtkWidget *image;
	GtkWidget *button_box;

	if (!state || !state->tabs_box)
		return NULL;
	if (activate && state->active)
		modern_tab_capture_state(state->active);
	tab = g_new0(ModernTab, 1);
	tab->state = state;
	tab->path = g_strdup((path && *path) ? path : home_dir);
	tab->selected_names = g_ptr_array_new_with_free_func(g_free);
	if (state->filer_window) {
		FilerWindow *fw = state->filer_window;
		tab->view_type = fw->view_type;
		tab->display_style_wanted = fw->display_style_wanted;
		tab->details_type = fw->details_type;
		tab->sort_type = fw->sort_type;
		tab->sort_order = fw->sort_order;
		tab->show_hidden = fw->show_hidden;
	}
	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(box), "linked");
	gtk_style_context_add_class(gtk_widget_get_style_context(box), "rox-modern-tab");
	gtk_widget_set_size_request(box, -1, 42);

	button = gtk_toggle_button_new();
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NORMAL);
	gtk_widget_set_can_focus(button, FALSE);
	gtk_widget_set_hexpand(button, FALSE);
	gtk_widget_set_size_request(button, -1, 42);

	button_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_container_set_border_width(GTK_CONTAINER(button_box), 5);
	image = gtk_image_new_from_icon_name("folder", GTK_ICON_SIZE_MENU);
	gtk_image_set_pixel_size(GTK_IMAGE(image), 18);
	gtk_box_pack_start(GTK_BOX(button_box), image, FALSE, FALSE, 0);
	label = gtk_label_new(NULL);
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
	gtk_label_set_max_width_chars(GTK_LABEL(label), 14);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_box_pack_start(GTK_BOX(button_box), label, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(button), button_box);

	close = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(close), GTK_RELIEF_NONE);
	gtk_widget_set_can_focus(close, FALSE);
	gtk_widget_set_size_request(close, 28, 28);
	gtk_style_context_add_class(gtk_widget_get_style_context(close), "rox-modern-tab-close");
	gtk_widget_set_tooltip_text(close, _("Close Tab"));
	{
		GtkWidget *close_image = gtk_image_new_from_icon_name(
			"window-close-symbolic", GTK_ICON_SIZE_MENU);
		gtk_button_set_image(GTK_BUTTON(close), close_image);
	}
	gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), close, FALSE, FALSE, 0);
	tab->box = box;
	tab->button = button;
	tab->icon = image;
	tab->label = label;
	tab->close = close;
	modern_tab_refresh_label(tab);
	g_ptr_array_add(state->tabs, tab);
	gtk_box_pack_start(GTK_BOX(state->tabs_box), box, FALSE, FALSE, 0);
	if (state->new_button && gtk_widget_get_parent(state->new_button) == state->tabs_box)
		gtk_box_reorder_child(GTK_BOX(state->tabs_box), state->new_button, -1);
	g_signal_connect(button, "toggled", G_CALLBACK(modern_tab_toggled), tab);
	make_drop_target(button, 0);
	g_signal_connect(button, "drag-motion", G_CALLBACK(modern_tab_drag_motion), tab);
	g_signal_connect(button, "drag-leave", G_CALLBACK(modern_tab_drag_leave), tab);
	gtk_widget_show_all(box);
	if (activate) {
		state->active = tab;
		modern_tabs_refresh_active(state);
	}
	return tab;
}

static void modern_tab_close(GtkButton *button, gpointer data)
{
	ModernTab *tab = data;
	ModernTabsState *state;
	gint index;
	gboolean was_active;
	ModernTab *next = NULL;
	(void) button;

	if (!tab || !(state = tab->state) || !state->tabs)
		return;
	if (state->tabs->len <= 1) {
		gtk_widget_destroy(state->filer_window->window);
		return;
	}
	index = modern_tab_index(state, tab);
	if (index < 0)
		return;
	was_active = state->active == tab;
	if (was_active)
		modern_tab_capture_state(tab);
	if (was_active) {
		if ((guint) index + 1 < state->tabs->len)
			next = g_ptr_array_index(state->tabs, index + 1);
		else if (index > 0)
			next = g_ptr_array_index(state->tabs, index - 1);
	}
	g_ptr_array_remove_index(state->tabs, (guint) index);
	if (was_active && next) {
		state->active = next;
		modern_tabs_refresh_active(state);
		modern_tab_restore_state(next);
	} else {
		modern_tabs_refresh_active(state);
	}
}

static ModernTab *modern_tabs_add_connected(ModernTabsState *state,
		const gchar *path, gboolean activate)
{
	ModernTab *tab = modern_tabs_add(state, path, activate);
	if (tab)
		g_signal_connect(tab->close, "clicked", G_CALLBACK(modern_tab_close), tab);
	return tab;
}

void modern_ui_open_path_in_new_tab(FilerWindow *filer_window, const gchar *path)
{
	ModernTabsState *state = modern_tabs_state(filer_window);
	ModernTab *tab;

	if (!state || !filer_window || !path || !*path)
		return;

	/* 2.12.2-88: paths opened by an external action (notably Image
	 * Mounter) must become the real directory of the Modern FilerWindow,
	 * not merely a visual tab carrying that name.  The old implementation
	 * created/activated the tab and then restored generic saved-tab state.
	 * On some Puppy/ROX event sequences this could leave the toggle/tab on
	 * the mounted image while the actual view and location entry still
	 * pointed at the directory containing the .sfs/.iso.
	 *
	 * Capture the old tab explicitly, create the destination tab inactive,
	 * make it the active owner, and then navigate the real FilerWindow to
	 * the requested directory.  filer_change_to() updates sym_path, the
	 * view, title/location entry and modern_tabs_sync_current(), so the tab
	 * and the file view cannot legitimately describe different paths. */
	if (state->session_restore_source) {
		g_source_remove(state->session_restore_source);
		state->session_restore_source = 0;
	}

	if (state->active)
		modern_tab_capture_state(state->active);

	tab = modern_tabs_add_connected(state, path, FALSE);
	if (!tab)
		return;

	state->active = tab;
	modern_tabs_refresh_active(state);

	/* A fresh tab starts with its own history.  Do not manufacture a Back
	 * entry from the directory that happened to be displayed by the previous
	 * tab. */
	modern_history_replace(&filer_window->history_back, tab->history_back);
	modern_history_replace(&filer_window->history_forward, tab->history_forward);

	if (g_strcmp0(filer_window->sym_path, path) != 0) {
		filer_window->history_navigation = TRUE;
		filer_change_to(filer_window, path, NULL);
		filer_window->history_navigation = FALSE;
	}

	/* Keep all Modern chrome authoritative even when the requested path was
	 * already current (for example Ctrl+T on the current directory). */
	modern_tabs_sync_current(filer_window);
	modern_ui_update_navigation(filer_window);
	if (filer_window->modern_path_entry && filer_window->sym_path &&
	    g_strcmp0(gtk_entry_get_text(GTK_ENTRY(filer_window->modern_path_entry)),
	              filer_window->sym_path) != 0)
		gtk_entry_set_text(GTK_ENTRY(filer_window->modern_path_entry),
		                   filer_window->sym_path);
	modern_ui_select_current_place(filer_window);
	/* A newly-created tab is immediately usable even if the + button or
	 * another Modern chrome control owned keyboard focus. */
	if (filer_window->view)
		gtk_widget_grab_focus(GTK_WIDGET(filer_window->view));
}

gboolean modern_ui_open_path_as_tab_if_configured(FilerWindow *source, const gchar *path)
{
	if (!source || !source->modern_mode || !path || !*path ||
	    o_modern_new_instance_mode.int_value != 1)
		return FALSE;

	modern_ui_open_path_in_new_tab(source, path);
	if (source->window)
		gtk_window_present(GTK_WINDOW(source->window));
	return TRUE;
}

static void modern_new_tab(GtkWidget *widget, FilerWindow *filer_window)
{
	(void) widget;
	modern_ui_open_path_in_new_tab(filer_window,
		filer_window->sym_path ? filer_window->sym_path : home_dir);
}

static void modern_close_current_tab(GtkWidget *widget, FilerWindow *filer_window)
{
	ModernTabsState *state = modern_tabs_state(filer_window);
	(void) widget;
	if (state && state->active)
		modern_tab_close(NULL, state->active);
}

static void modern_tab_copy_saved_state(ModernTab *dst, ModernTab *src)
{
	guint i;
	if (!dst || !src)
		return;
	modern_history_replace(&dst->history_back, src->history_back);
	modern_history_replace(&dst->history_forward, src->history_forward);
	g_ptr_array_set_size(dst->selected_names, 0);
	for (i = 0; src->selected_names && i < src->selected_names->len; i++)
		g_ptr_array_add(dst->selected_names,
			g_strdup(g_ptr_array_index(src->selected_names, i)));
	dst->view_type = src->view_type;
	dst->display_style_wanted = src->display_style_wanted;
	dst->details_type = src->details_type;
	dst->sort_type = src->sort_type;
	dst->sort_order = src->sort_order;
	dst->show_hidden = src->show_hidden;
	dst->scroll_value = src->scroll_value;
}

static void modern_duplicate_tab(GtkWidget *widget, FilerWindow *filer_window)
{
	ModernTabsState *state = modern_tabs_state(filer_window);
	ModernTab *src;
	ModernTab *dst;
	(void) widget;
	if (!state || !(src = state->active))
		return;
	modern_tab_capture_state(src);
	dst = modern_tabs_add_connected(state, src->path, TRUE);
	if (!dst)
		return;
	modern_tab_copy_saved_state(dst, src);
	modern_tab_restore_state(dst);
}

static void modern_close_other_tabs(GtkWidget *widget, FilerWindow *filer_window)
{
	ModernTabsState *state = modern_tabs_state(filer_window);
	ModernTab *keep;
	gint i;
	(void) widget;
	if (!state || !(keep = state->active))
		return;
	for (i = (gint) state->tabs->len - 1; i >= 0; i--) {
		ModernTab *tab = g_ptr_array_index(state->tabs, (guint) i);
		if (tab != keep)
			g_ptr_array_remove_index(state->tabs, (guint) i);
	}
	modern_tabs_refresh_active(state);
}

static void modern_close_tabs_right(GtkWidget *widget, FilerWindow *filer_window)
{
	ModernTabsState *state = modern_tabs_state(filer_window);
	gint index;
	gint i;
	(void) widget;
	if (!state || !state->active)
		return;
	index = modern_tab_index(state, state->active);
	for (i = (gint) state->tabs->len - 1; i > index; i--)
		g_ptr_array_remove_index(state->tabs, (guint) i);
	modern_tabs_refresh_active(state);
}

static void modern_cycle_tab(FilerWindow *filer_window, gint direction)
{
	ModernTabsState *state = modern_tabs_state(filer_window);
	gint index;
	gint next;
	ModernTab *tab;
	if (!state || state->tabs->len < 2 || !state->active)
		return;
	index = modern_tab_index(state, state->active);
	if (index < 0)
		return;
	next = (index + direction + (gint) state->tabs->len) % (gint) state->tabs->len;
	tab = g_ptr_array_index(state->tabs, (guint) next);
	modern_tab_activate(tab);
}

static gboolean modern_tabs_key_press(GtkWidget *widget, GdkEventKey *event,
		FilerWindow *filer_window)
{
	guint state;
	GtkWidget *focus;
	FilerWindow *target;
	(void) widget;
	if (!event || !filer_window)
		return FALSE;

	state = event->state & gtk_accelerator_get_default_mod_mask();
	focus = filer_window->window ? gtk_window_get_focus(GTK_WINDOW(filer_window->window)) : NULL;
	target = modern_ui_key_target(filer_window, focus);

	/* Dolphin-style workspace shortcuts live only in Modern. */
	if (event->keyval == GDK_KEY_F3 && state == 0) {
		modern_split_toggle(filer_window);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_F11 && state == 0) {
		FilerWindow *owner = modern_preview_owner(target);
		if (owner && owner->modern_preview_box)
			modern_preview_set_visible(target, !gtk_widget_get_visible(owner->modern_preview_box));
		return TRUE;
	}
	if (event->keyval == GDK_KEY_F5 && state == GDK_SHIFT_MASK) {
		modern_transfer_to_other_pane(filer_window, FALSE);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_F6 && state == GDK_SHIFT_MASK) {
		modern_transfer_to_other_pane(filer_window, TRUE);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_F5 && state == 0) {
		filer_refresh(target);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_F6 && state == 0) {
		modern_location_show_editor(target);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_F9 && state == 0) {
		modern_sidebar_set_visible(filer_window,
			!filer_window->modern_sidebar || !gtk_widget_get_visible(filer_window->modern_sidebar));
		return TRUE;
	}
	if ((state & GDK_MOD1_MASK) && !(state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK))) {
		if (event->keyval == GDK_KEY_Left) {
			filer_history_back(target);
			return TRUE;
		}
		if (event->keyval == GDK_KEY_Right) {
			filer_history_forward(target);
			return TRUE;
		}
		if (event->keyval == GDK_KEY_Up) {
			change_to_parent(target);
			return TRUE;
		}
	}
	if (!(state & GDK_CONTROL_MASK))
		return FALSE;
	if (event->keyval == GDK_KEY_l || event->keyval == GDK_KEY_L) {
		modern_location_show_editor(target);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_i || event->keyval == GDK_KEY_I) {
		modern_filter_toggle(target);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_f || event->keyval == GDK_KEY_F) {
		search_integration_launch(target);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_1) {
		if (target->view_type != VIEW_TYPE_COLLECTION)
			filer_set_view_type(target, VIEW_TYPE_COLLECTION);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_2) {
		target->modern_full_details = TRUE;
		if (target->view_type != VIEW_TYPE_DETAILS)
			filer_set_view_type(target, VIEW_TYPE_DETAILS);
		else
			view_details_apply_modern_columns(target);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_t || event->keyval == GDK_KEY_T) {
		modern_new_tab(NULL, filer_window);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_w || event->keyval == GDK_KEY_W) {
		modern_close_current_tab(NULL, filer_window);
		return TRUE;
	}
	if (event->keyval == GDK_KEY_Tab) {
		modern_cycle_tab(filer_window,
			(state & GDK_SHIFT_MASK) ? -1 : 1);
		return TRUE;
	}
	return FALSE;
}

static gboolean modern_tab_restore_state_idle_cb(gpointer data)
{
	ModernTab *tab = data;
	if (tab && tab->state)
		tab->state->session_restore_source = 0;
	if (tab && tab->state && tab->state->active == tab)
		modern_tab_restore_state(tab);
	return G_SOURCE_REMOVE;
}

static void modern_tabs_restore_session(ModernTabsState *state)
{
	GKeyFile *kf;
	gchar **paths = NULL;
	gsize count = 0;
	gint active = 0;
	gint tab_layout_version = 0;
	gint migrated_active = 0;
	gboolean migrated_active_set = FALSE;
	const gchar *active_path = NULL;
	GHashTable *seen = NULL;
	guint i;

	if (!state)
		return;
	if (modern_session_restored_once) {
		modern_tabs_add_connected(state, state->filer_window->sym_path, TRUE);
		return;
	}
	modern_session_restored_once = TRUE;
	kf = modern_session_load();
	paths = g_key_file_get_string_list(kf, MODERN_SESSION_GROUP, "Tabs", &count, NULL);
	active = g_key_file_get_integer(kf, MODERN_SESSION_GROUP, "ActiveTab", NULL);
	tab_layout_version = g_key_file_get_integer(kf, MODERN_SESSION_GROUP, "TabLayoutVersion", NULL);
	if (paths && count && active >= 0 && active < (gint) count)
		active_path = paths[active];
	if (tab_layout_version < 80)
		seen = g_hash_table_new(g_str_hash, g_str_equal);
	if (paths && count) {
		for (i = 0; i < count; i++) {
			if (!paths[i] || !*paths[i] || !g_file_test(paths[i], G_FILE_TEST_IS_DIR))
				continue;
			if (seen && g_hash_table_contains(seen, paths[i]))
				continue;
			if (seen)
				g_hash_table_add(seen, paths[i]);
			modern_tabs_add_connected(state, paths[i], FALSE);
			if (!migrated_active_set && active_path && g_strcmp0(paths[i], active_path) == 0) {
				migrated_active = (gint) state->tabs->len - 1;
				migrated_active_set = TRUE;
			}
		}
	}
	if (seen)
		g_hash_table_destroy(seen);
	if (tab_layout_version < 80 && migrated_active_set)
		active = migrated_active;
	if (!state->tabs->len)
		modern_tabs_add_connected(state, state->filer_window->sym_path, FALSE);
	if (active < 0 || active >= (gint) state->tabs->len)
		active = 0;
	state->active = g_ptr_array_index(state->tabs, (guint) active);
	modern_tabs_refresh_active(state);
	if (state->active) {
		if (state->session_restore_source)
			g_source_remove(state->session_restore_source);
		state->session_restore_source = g_idle_add(modern_tab_restore_state_idle_cb, state->active);
	}
	g_strfreev(paths);
	g_key_file_unref(kf);
}

static GtkWidget *modern_tabs_build(FilerWindow *filer_window)
{
	ModernTabsState *state;
	GtkWidget *bar;
	GtkWidget *scroller;
	GtkWidget *tabs_box;
	GtkWidget *new_button;

	state = g_new0(ModernTabsState, 1);
	state->filer_window = filer_window;
	state->tabs = g_ptr_array_new_with_free_func(modern_tab_free);
	bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(bar), "rox-modern-tabbar");
	scroller = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroller), GTK_SHADOW_NONE);
	gtk_scrolled_window_set_overlay_scrolling(GTK_SCROLLED_WINDOW(scroller), TRUE);
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroller), 42);
	gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroller), 42);
	gtk_widget_set_hexpand(scroller, TRUE);
	tabs_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 1);
	gtk_widget_set_halign(tabs_box, GTK_ALIGN_START);
	gtk_widget_set_valign(tabs_box, GTK_ALIGN_CENTER);
	gtk_container_add(GTK_CONTAINER(scroller), tabs_box);
	gtk_box_pack_start(GTK_BOX(bar), scroller, TRUE, TRUE, 0);
	new_button = icon_button("list-add-symbolic", _("New Tab"));
	gtk_widget_set_size_request(new_button, 44, 42);
	state->bar = bar;
	state->tabs_box = tabs_box;
	state->new_button = new_button;
	gtk_box_pack_start(GTK_BOX(tabs_box), new_button, FALSE, FALSE, 0);
	g_object_set_data_full(G_OBJECT(filer_window->window), "rox-modern-tabs-state",
		state, modern_tabs_state_free);
	g_signal_connect(new_button, "clicked", G_CALLBACK(modern_new_tab), filer_window);
	modern_tabs_restore_session(state);
	if (state->active)
		modern_tab_capture_state(state->active);
	g_signal_connect(filer_window->window, "key-press-event",
		G_CALLBACK(modern_tabs_key_press), filer_window);
	gtk_widget_show_all(bar);
	return bar;
}

static void modern_tabs_sync_current(FilerWindow *filer_window)
{
	ModernTabsState *state = modern_tabs_state(filer_window);
	ModernTab *tab;
	if (!state || !(tab = state->active) || !filer_window->sym_path)
		return;
	if (g_strcmp0(tab->path, filer_window->sym_path) != 0) {
		g_free(tab->path);
		tab->path = g_strdup(filer_window->sym_path);
	}
	/* Navigation belongs to the active tab. It updates that tab in place;
	 * changing directories never creates or destroys a tab. */
	modern_tab_refresh_label(tab);
	/* Keep the active tab's history/view state current after ordinary ROX
	 * navigation. Selection and scroll are captured immediately before a tab
	 * switch, when their values are authoritative. */
	modern_history_replace(&tab->history_back, filer_window->history_back);
	modern_history_replace(&tab->history_forward, filer_window->history_forward);
	tab->view_type = filer_window->view_type;
	tab->display_style_wanted = filer_window->display_style_wanted;
	tab->details_type = filer_window->details_type;
	tab->sort_type = filer_window->sort_type;
	tab->sort_order = filer_window->sort_order;
	tab->show_hidden = filer_window->show_hidden;
}

static GtkWidget *icon_button(const gchar *icon, const gchar *tip)
{
	GtkWidget *button;
	GtkWidget *image;

	button = gtk_button_new();
	image = gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_SMALL_TOOLBAR);
	gtk_button_set_image(GTK_BUTTON(button), image);
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_widget_set_can_focus(button, FALSE);
	if (tip)
		gtk_widget_set_tooltip_text(button, tip);
	return button;
}

static GtkWidget *modern_nav_button(const gchar *icon, const gchar *label)
{
	GtkWidget *button;
	GtkWidget *box;
	GtkWidget *image;
	GtkWidget *text;

	button = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_widget_set_can_focus(button, FALSE);
	gtk_widget_set_tooltip_text(button, label);
	gtk_widget_set_size_request(button, 72, 48);

	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	gtk_container_set_border_width(GTK_CONTAINER(box), 5);
	image = gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_LARGE_TOOLBAR);
	text = gtk_label_new(label);
	gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), text, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(button), box);
	return button;
}

static void go_back(GtkWidget *widget, FilerWindow *filer_window)
{
	(void) widget;
	filer_history_back(modern_nav_target(filer_window));
}

static void go_forward(GtkWidget *widget, FilerWindow *filer_window)
{
	(void) widget;
	filer_history_forward(modern_nav_target(filer_window));
}

static void go_up(GtkWidget *widget, FilerWindow *filer_window)
{
	(void) widget;
	change_to_parent(modern_nav_target(filer_window));
}

static void go_home(GtkWidget *widget, FilerWindow *filer_window)
{
	(void) widget;
	filer_change_to(modern_nav_target(filer_window), home_dir, NULL);
}

static void reload_dir(GtkWidget *widget, FilerWindow *filer_window)
{
	(void) widget;
	filer_refresh(modern_nav_target(filer_window));
}

static void search_dir(GtkWidget *widget, FilerWindow *filer_window)
{
	(void) widget;
	search_integration_launch(modern_nav_target(filer_window));
}

static void modern_close_window(GtkWidget *widget, FilerWindow *filer_window)
{
	(void) widget;
	if (filer_window && filer_window->window &&
	    !filer_window_delete(filer_window->window, NULL, filer_window))
		gtk_widget_destroy(filer_window->window);
}

static void path_activated(GtkEntry *entry, FilerWindow *filer_window)
{
	const gchar *text;
	gchar *expanded = NULL;

	text = gtk_entry_get_text(entry);
	if (!text || !*text)
		return;

	if (text[0] == '~' && (text[1] == '\0' || text[1] == '/'))
		expanded = g_build_filename(home_dir, text + (text[1] == '/' ? 2 : 1), NULL);

	filer_change_to(filer_window, expanded ? expanded : text, NULL);
	g_free(expanded);
	modern_location_show_default(filer_window);
	if (filer_window->view)
		gtk_widget_grab_focus(GTK_WIDGET(filer_window->view));
}

static void modern_breadcrumb_clicked(GtkButton *button, FilerWindow *filer_window)
{
	const gchar *path = g_object_get_data(G_OBJECT(button), "rox-breadcrumb-path");
	if (path && *path)
		filer_change_to(filer_window, path, NULL);
}

static void modern_breadcrumb_add_button(FilerWindow *filer_window,
		const gchar *label, const gchar *path, gboolean first)
{
	GtkWidget *button;

	if (!filer_window || !filer_window->modern_breadcrumb_box || !path)
		return;
	if (!first)
	{
		GtkWidget *sep = gtk_label_new("›");
		gtk_widget_set_margin_start(sep, 2);
		gtk_widget_set_margin_end(sep, 2);
		gtk_box_pack_start(GTK_BOX(filer_window->modern_breadcrumb_box), sep, FALSE, FALSE, 0);
	}
	button = gtk_button_new_with_label(label);
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_widget_set_can_focus(button, FALSE);
	gtk_widget_set_tooltip_text(button, path);
	g_object_set_data_full(G_OBJECT(button), "rox-breadcrumb-path", g_strdup(path), g_free);
	g_signal_connect(button, "clicked", G_CALLBACK(modern_breadcrumb_clicked), filer_window);
	gtk_box_pack_start(GTK_BOX(filer_window->modern_breadcrumb_box), button, FALSE, FALSE, 0);
}

static void modern_breadcrumb_rebuild(FilerWindow *filer_window)
{
	GList *children, *iter;
	gchar **parts;
	gchar *accum;
	gboolean first = TRUE;
	guint i;

	if (!filer_window || !filer_window->modern_breadcrumb_box || !filer_window->sym_path)
		return;

	children = gtk_container_get_children(GTK_CONTAINER(filer_window->modern_breadcrumb_box));
	for (iter = children; iter; iter = iter->next)
		gtk_widget_destroy(GTK_WIDGET(iter->data));
	g_list_free(children);

	if (!g_path_is_absolute(filer_window->sym_path))
	{
		modern_breadcrumb_add_button(filer_window, filer_window->sym_path,
			filer_window->sym_path, TRUE);
		gtk_widget_show_all(filer_window->modern_breadcrumb_box);
		return;
	}

	modern_breadcrumb_add_button(filer_window, "/", "/", TRUE);
	first = FALSE;
	accum = g_strdup("/");
	parts = g_strsplit(filer_window->sym_path, "/", -1);
	for (i = 0; parts && parts[i]; i++)
	{
		gchar *next;
		const gchar *label;
		if (!parts[i][0])
			continue;
		next = g_build_filename(accum, parts[i], NULL);
		label = (home_dir && g_strcmp0(next, home_dir) == 0) ? _("Home") : parts[i];
		modern_breadcrumb_add_button(filer_window, label, next, first);
		first = FALSE;
		g_free(accum);
		accum = next;
	}
	g_strfreev(parts);
	g_free(accum);
	gtk_widget_show_all(filer_window->modern_breadcrumb_box);
}

static void modern_location_show_editor(FilerWindow *filer_window)
{
	if (!filer_window || !filer_window->modern_path_stack ||
	    !filer_window->modern_path_entry)
		return;
	if (filer_window->sym_path)
		gtk_entry_set_text(GTK_ENTRY(filer_window->modern_path_entry), filer_window->sym_path);
	gtk_stack_set_visible_child_name(GTK_STACK(filer_window->modern_path_stack), "entry");
	gtk_widget_grab_focus(filer_window->modern_path_entry);
	gtk_editable_select_region(GTK_EDITABLE(filer_window->modern_path_entry), 0, -1);
}

static void modern_location_show_breadcrumbs(FilerWindow *filer_window)
{
	if (!filer_window || !filer_window->modern_path_stack)
		return;
	modern_breadcrumb_rebuild(filer_window);
	gtk_stack_set_visible_child_name(GTK_STACK(filer_window->modern_path_stack), "crumbs");
}

static void modern_location_show_default(FilerWindow *filer_window)
{
	if (!filer_window || !filer_window->modern_path_stack)
		return;
	if (filer_window->modern_breadcrumbs_enabled)
		modern_location_show_breadcrumbs(filer_window);
	else
		gtk_stack_set_visible_child_name(GTK_STACK(filer_window->modern_path_stack), "entry");
}

static gboolean modern_path_entry_key(GtkWidget *widget, GdkEventKey *event,
		FilerWindow *filer_window)
{
	(void) widget;
	if (event && event->keyval == GDK_KEY_Escape)
	{
		modern_location_show_default(filer_window);
		if (filer_window->view)
			gtk_widget_grab_focus(GTK_WIDGET(filer_window->view));
		return TRUE;
	}
	return FALSE;
}

static GtkWidget *modern_location_widget_new(FilerWindow *filer_window, gboolean compact)
{
	GtkWidget *outer;
	GtkWidget *stack;
	GtkWidget *crumb_scroller;
	GtkWidget *crumb_box;
	GtkWidget *entry;

	(void) compact;
	outer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	stack = gtk_stack_new();
	gtk_stack_set_transition_type(GTK_STACK(stack), GTK_STACK_TRANSITION_TYPE_NONE);
	gtk_widget_set_hexpand(stack, TRUE);

	/* Keep the original Rox-Filer2 Modern location entry as the default.
	 * Breadcrumbs remain available as an optional presentation mode instead
	 * of permanently changing the toolbar's visual language. */
	entry = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(entry), _("Path"));
	gtk_entry_set_icon_from_icon_name(GTK_ENTRY(entry), GTK_ENTRY_ICON_PRIMARY, "folder");
	g_signal_connect(entry, "activate", G_CALLBACK(path_activated), filer_window);
	g_signal_connect(entry, "key-press-event", G_CALLBACK(modern_path_entry_key), filer_window);
	gtk_stack_add_named(GTK_STACK(stack), entry, "entry");

	crumb_scroller = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(crumb_scroller),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(crumb_scroller), GTK_SHADOW_NONE);
	crumb_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_container_add(GTK_CONTAINER(crumb_scroller), crumb_box);
	gtk_stack_add_named(GTK_STACK(stack), crumb_scroller, "crumbs");

	gtk_box_pack_start(GTK_BOX(outer), stack, TRUE, TRUE, 0);
	filer_window->modern_path_stack = stack;
	filer_window->modern_breadcrumb_box = crumb_box;
	filer_window->modern_path_entry = entry;
	if (filer_window->sym_path)
		gtk_entry_set_text(GTK_ENTRY(entry), filer_window->sym_path);
	modern_breadcrumb_rebuild(filer_window);
	modern_location_show_default(filer_window);
	return outer;
}



/* -------------------------------------------------------------------------
 * Modern quick filter, preview pane and true in-window split view (2.14).
 */

static void modern_filter_apply(GtkEditable *editable, FilerWindow *filer_window)
{
	const gchar *text;
	(void) editable;
	if (!filer_window || !filer_window->modern_filter_entry)
		return;
	text = gtk_entry_get_text(GTK_ENTRY(filer_window->modern_filter_entry));
	if (g_strcmp0(text, filer_window->modern_quick_filter) == 0)
		return;
	g_free(filer_window->modern_quick_filter);
	filer_window->modern_quick_filter = (text && *text) ? g_strdup(text) : NULL;
	g_free(filer_window->modern_quick_filter_path);
	filer_window->modern_quick_filter_path = filer_window->modern_quick_filter && filer_window->sym_path
		? g_strdup(filer_window->sym_path) : NULL;
	filer_detach_rescan(filer_window);
}

static void modern_filter_set_lock_icon(FilerWindow *filer_window)
{
	GtkWidget *image;
	if (!filer_window || !filer_window->modern_filter_lock)
		return;
	image = gtk_image_new_from_icon_name(
		filer_window->modern_filter_locked ? "changes-prevent-symbolic" : "changes-allow-symbolic",
		GTK_ICON_SIZE_MENU);
	gtk_button_set_image(GTK_BUTTON(filer_window->modern_filter_lock), image);
}

static void modern_filter_lock_toggled(GtkToggleButton *button, FilerWindow *filer_window)
{
	if (!filer_window)
		return;
	filer_window->modern_filter_locked = gtk_toggle_button_get_active(button);
	modern_filter_set_lock_icon(filer_window);
	/* Match Dolphin's useful behaviour: unlocking drops a filter that was
	 * intentionally carried between folders.  A filter in the current folder
	 * remains until the user edits/closes it. */
	if (!filer_window->modern_filter_locked && filer_window->modern_quick_filter_path &&
	    filer_window->sym_path &&
	    g_strcmp0(filer_window->modern_quick_filter_path, filer_window->sym_path) != 0 &&
	    filer_window->modern_filter_entry)
		gtk_entry_set_text(GTK_ENTRY(filer_window->modern_filter_entry), "");
}

static void modern_filter_clear_for_navigation(FilerWindow *filer_window)
{
	if (!filer_window || filer_window->modern_filter_locked ||
	    !filer_window->modern_quick_filter || !filer_window->modern_quick_filter_path ||
	    !filer_window->sym_path ||
	    g_strcmp0(filer_window->modern_quick_filter_path, filer_window->sym_path) == 0)
		return;

	g_clear_pointer(&filer_window->modern_quick_filter, g_free);
	g_clear_pointer(&filer_window->modern_quick_filter_path, g_free);
	if (filer_window->modern_filter_entry)
	{
		g_signal_handlers_block_by_func(filer_window->modern_filter_entry,
			G_CALLBACK(modern_filter_apply), filer_window);
		gtk_entry_set_text(GTK_ENTRY(filer_window->modern_filter_entry), "");
		g_signal_handlers_unblock_by_func(filer_window->modern_filter_entry,
			G_CALLBACK(modern_filter_apply), filer_window);
	}
	if (filer_window->modern_filter_bar)
		gtk_widget_hide(filer_window->modern_filter_bar);
}

static gboolean modern_filter_key(GtkWidget *widget, GdkEventKey *event,
		FilerWindow *filer_window)
{
	if (!event || !filer_window)
		return FALSE;
	if (event->keyval == GDK_KEY_Escape)
	{
		if (gtk_entry_get_text_length(GTK_ENTRY(widget)) > 0)
			gtk_entry_set_text(GTK_ENTRY(widget), "");
		else if (filer_window->modern_filter_bar)
			gtk_widget_hide(filer_window->modern_filter_bar);
		if (filer_window->view)
			gtk_widget_grab_focus(GTK_WIDGET(filer_window->view));
		return TRUE;
	}
	if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter)
	{
		if (filer_window->view)
			gtk_widget_grab_focus(GTK_WIDGET(filer_window->view));
		return TRUE;
	}
	return FALSE;
}

static void modern_filter_close(GtkButton *button, FilerWindow *filer_window)
{
	(void) button;
	if (!filer_window)
		return;
	if (filer_window->modern_filter_entry)
		gtk_entry_set_text(GTK_ENTRY(filer_window->modern_filter_entry), "");
	filer_window->modern_filter_locked = FALSE;
	if (filer_window->modern_filter_lock)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(filer_window->modern_filter_lock), FALSE);
	if (filer_window->modern_filter_bar)
		gtk_widget_hide(filer_window->modern_filter_bar);
	if (filer_window->view)
		gtk_widget_grab_focus(GTK_WIDGET(filer_window->view));
}

static GtkWidget *modern_filter_bar_new(FilerWindow *filer_window)
{
	GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
	GtkWidget *lock = gtk_toggle_button_new();
	GtkWidget *entry = gtk_entry_new();
	GtkWidget *close = icon_button("window-close-symbolic", _("Close"));

	gtk_container_set_border_width(GTK_CONTAINER(bar), 4);
	gtk_button_set_relief(GTK_BUTTON(lock), GTK_RELIEF_NONE);
	gtk_widget_set_can_focus(lock, FALSE);
	gtk_widget_set_tooltip_text(lock, _("Keep Filter When Changing Folders"));
	gtk_box_pack_start(GTK_BOX(bar), lock, FALSE, FALSE, 0);
	gtk_entry_set_placeholder_text(GTK_ENTRY(entry), _("Quick Filter"));
	gtk_widget_set_tooltip_text(entry, _("Filter the current folder as you type"));
	gtk_box_pack_start(GTK_BOX(bar), entry, TRUE, TRUE, 0);
	gtk_widget_set_size_request(close, 30, 30);
	gtk_box_pack_end(GTK_BOX(bar), close, FALSE, FALSE, 0);
	filer_window->modern_filter_bar = bar;
	filer_window->modern_filter_entry = entry;
	filer_window->modern_filter_lock = lock;
	filer_window->modern_filter_locked = FALSE;
	modern_filter_set_lock_icon(filer_window);
	g_signal_connect(lock, "toggled", G_CALLBACK(modern_filter_lock_toggled), filer_window);
	g_signal_connect(entry, "changed", G_CALLBACK(modern_filter_apply), filer_window);
	g_signal_connect(entry, "key-press-event", G_CALLBACK(modern_filter_key), filer_window);
	g_signal_connect(close, "clicked", G_CALLBACK(modern_filter_close), filer_window);
	gtk_widget_set_no_show_all(bar, TRUE);
	gtk_widget_hide(bar);
	return bar;
}


static void modern_filter_toggle(FilerWindow *filer_window)
{
	if (!filer_window || !filer_window->modern_filter_bar || !filer_window->modern_filter_entry)
		return;
	if (gtk_widget_get_visible(filer_window->modern_filter_bar))
	{
		if (gtk_entry_get_text_length(GTK_ENTRY(filer_window->modern_filter_entry)) == 0)
			gtk_widget_hide(filer_window->modern_filter_bar);
		else
			gtk_widget_grab_focus(filer_window->modern_filter_entry);
	}
	else
	{
		gtk_widget_show_all(filer_window->modern_filter_bar);
		gtk_widget_show(filer_window->modern_filter_bar);
		gtk_widget_grab_focus(filer_window->modern_filter_entry);
		gtk_editable_select_region(GTK_EDITABLE(filer_window->modern_filter_entry), 0, -1);
	}
}

static GtkWidget *modern_preview_box_new(FilerWindow *filer_window)
{
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	GtkWidget *heading = gtk_label_new(NULL);
	GtkWidget *image = gtk_image_new_from_icon_name("text-x-generic", GTK_ICON_SIZE_DIALOG);
	GtkWidget *name = gtk_label_new(_("No selection"));
	GtkWidget *meta = gtk_label_new("");
	PangoAttrList *attrs = pango_attr_list_new();

	pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
	gtk_label_set_attributes(GTK_LABEL(heading), attrs);
	pango_attr_list_unref(attrs);
	gtk_label_set_text(GTK_LABEL(heading), _("Preview"));
	gtk_label_set_xalign(GTK_LABEL(heading), 0.0);
	gtk_label_set_xalign(GTK_LABEL(name), 0.0);
	gtk_label_set_line_wrap(GTK_LABEL(name), TRUE);
	gtk_label_set_xalign(GTK_LABEL(meta), 0.0);
	gtk_label_set_yalign(GTK_LABEL(meta), 0.0);
	gtk_label_set_line_wrap(GTK_LABEL(meta), TRUE);
	gtk_widget_set_size_request(box, 250, -1);
	gtk_container_set_border_width(GTK_CONTAINER(box), 10);
	gtk_box_pack_start(GTK_BOX(box), heading, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 4);
	gtk_box_pack_start(GTK_BOX(box), name, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(box), meta, FALSE, FALSE, 0);
	gtk_widget_set_no_show_all(box, TRUE);
	gtk_widget_hide(box);
	filer_window->modern_preview_box = box;
	filer_window->modern_preview_image = image;
	filer_window->modern_preview_name = name;
	filer_window->modern_preview_meta = meta;
	return box;
}

static FilerWindow *modern_preview_owner(FilerWindow *filer_window)
{
	FilerWindow *host;
	if (!filer_window)
		return NULL;
	if (!filer_window->modern_embedded)
		return filer_window;
	host = g_object_get_data(G_OBJECT(filer_window->window), "rox-modern-split-host");
	return host ? host : filer_window;
}

void modern_ui_selection_changed(FilerWindow *filer_window)
{
	FilerWindow *owner;
	ViewIter iter;
	DirItem *item;
	gint selected;
	gchar *path = NULL;
	gchar *mtime = NULL;
	gchar *meta = NULL;

	if (!filer_window || !filer_window->modern_mode || !filer_window->view)
		return;
	owner = modern_preview_owner(filer_window);
	if (!owner || !owner->modern_preview_box ||
	    !gtk_widget_get_visible(owner->modern_preview_box))
		return;

	selected = view_count_selected(filer_window->view);
	if (selected <= 0)
	{
		gtk_image_set_from_icon_name(GTK_IMAGE(owner->modern_preview_image),
			"text-x-generic", GTK_ICON_SIZE_DIALOG);
		gtk_label_set_text(GTK_LABEL(owner->modern_preview_name), _("No selection"));
		gtk_label_set_text(GTK_LABEL(owner->modern_preview_meta), "");
		return;
	}
	if (selected > 1)
	{
		gchar *label = g_strdup_printf(_("%d items selected"), selected);
		gtk_image_set_from_icon_name(GTK_IMAGE(owner->modern_preview_image),
			"edit-select-all", GTK_ICON_SIZE_DIALOG);
		gtk_label_set_text(GTK_LABEL(owner->modern_preview_name), label);
		gtk_label_set_text(GTK_LABEL(owner->modern_preview_meta), "");
		g_free(label);
		return;
	}

	view_get_iter(filer_window->view, &iter, VIEW_ITER_SELECTED);
	item = iter.next(&iter);
	if (!item)
		return;
	path = g_build_filename(filer_window->sym_path, item->leafname, NULL);
	gtk_label_set_text(GTK_LABEL(owner->modern_preview_name), item->leafname);

	if (item->mime_type && g_strcmp0(item->mime_type->media_type, "image") == 0 &&
	    item->base_type != TYPE_DIRECTORY)
	{
		GError *error = NULL;
		GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file_at_scale(path, 230, 180, TRUE, &error);
		if (pixbuf)
		{
			gtk_image_set_from_pixbuf(GTK_IMAGE(owner->modern_preview_image), pixbuf);
			g_object_unref(pixbuf);
		}
		else
		{
			gtk_image_set_from_icon_name(GTK_IMAGE(owner->modern_preview_image),
				"image-x-generic", GTK_ICON_SIZE_DIALOG);
			g_clear_error(&error);
		}
	}
	else
	{
		const gchar *icon = item->base_type == TYPE_DIRECTORY ? "folder" :
			(item->mime_type && g_strcmp0(item->mime_type->media_type, "text") == 0 ?
			 "text-x-generic" : "application-x-generic");
		gtk_image_set_from_icon_name(GTK_IMAGE(owner->modern_preview_image), icon, GTK_ICON_SIZE_DIALOG);
	}

	mtime = pretty_time(&item->mtime);
	meta = g_strdup_printf("%s: %s/%s\n%s: %s\n%s: %s\n%s: %s\n%s: %s\n%s: %s",
		_("Type"), item->mime_type ? item->mime_type->media_type : "inode",
		item->mime_type ? item->mime_type->subtype : "unknown",
		_("Size"), item->base_type == TYPE_DIRECTORY ? "—" : format_size(item->size),
		_("Permissions"), pretty_permissions(item->mode),
		_("Owner"), user_name(item->uid),
		_("Group"), group_name(item->gid),
		_("Last Modified"), mtime ? mtime : "—");
	gtk_label_set_text(GTK_LABEL(owner->modern_preview_meta), meta);
	g_free(meta);
	g_free(mtime);
	g_free(path);
}

static void modern_preview_set_visible(FilerWindow *filer_window, gboolean visible)
{
	FilerWindow *owner = modern_preview_owner(filer_window);
	if (!owner || !owner->modern_preview_box)
		return;
	if (visible)
	{
		gtk_widget_show_all(owner->modern_preview_box);
		gtk_widget_show(owner->modern_preview_box);
		modern_ui_selection_changed(filer_window);
	}
	else
		gtk_widget_hide(owner->modern_preview_box);
	if (owner->modern_preview_menu_item && GTK_IS_CHECK_MENU_ITEM(owner->modern_preview_menu_item) &&
	    gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(owner->modern_preview_menu_item)) != visible)
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(owner->modern_preview_menu_item), visible);
}

static void modern_preview_toggled(GtkCheckMenuItem *item, FilerWindow *filer_window)
{
	FilerWindow *target = modern_nav_target(filer_window);
	modern_preview_set_visible(target, gtk_check_menu_item_get_active(item));
}

static void modern_split_update_active_style(ModernSplitState *state)
{
	GtkStyleContext *context;
	if (!state)
		return;
	if (state->left_outer)
	{
		context = gtk_widget_get_style_context(state->left_outer);
		gtk_style_context_add_class(context, "rox-modern-split-pane");
		if (state->enabled && state->active == state->host)
			gtk_style_context_add_class(context, "rox-modern-split-pane-active");
		else
			gtk_style_context_remove_class(context, "rox-modern-split-pane-active");
	}
	if (state->right_outer)
	{
		context = gtk_widget_get_style_context(state->right_outer);
		gtk_style_context_add_class(context, "rox-modern-split-pane");
		if (state->enabled && state->active == state->right)
			gtk_style_context_add_class(context, "rox-modern-split-pane-active");
		else
			gtk_style_context_remove_class(context, "rox-modern-split-pane-active");
	}
}

static void modern_split_focus_changed(GtkWindow *window, GtkWidget *focus, gpointer data)
{
	ModernSplitState *state = data;
	(void) window;
	if (!state || !focus)
		return;
	if (state->right_outer &&
	    (focus == state->right_outer || gtk_widget_is_ancestor(focus, state->right_outer)))
		state->active = state->right;
	else if (state->left_outer &&
	         (focus == state->left_outer || gtk_widget_is_ancestor(focus, state->left_outer)))
		state->active = state->host;
	else
		return;
	modern_split_update_active_style(state);
	modern_ui_update_navigation(state->active);
	modern_ui_select_current_place(state->host);
	modern_ui_selection_changed(state->active);
}

FilerWindow *modern_ui_key_target(FilerWindow *host, GtkWidget *focus)
{
	ModernSplitState *state;
	if (!host)
		return host;
	if (host->modern_embedded)
		return host;
	state = modern_split_state(host);
	if (!state || !state->enabled || !state->right)
		return host;
	if (focus && state->right_outer &&
	    (focus == state->right_outer || gtk_widget_is_ancestor(focus, state->right_outer)))
		return state->right;
	if (focus && state->left_outer &&
	    (focus == state->left_outer || gtk_widget_is_ancestor(focus, state->left_outer)))
		return host;
	return state->active ? state->active : host;
}

static FilerWindow *modern_nav_target(FilerWindow *host)
{
	GtkWidget *focus = NULL;
	if (!host || !host->window)
		return host;
	focus = gtk_window_get_focus(GTK_WINDOW(host->window));
	return modern_ui_key_target(host, focus);
}

static FilerWindow *modern_split_other_pane(FilerWindow *host, FilerWindow *active)
{
	ModernSplitState *state = modern_split_state(host);
	if (!state || !state->enabled || !state->right)
		return NULL;
	return active == state->right ? state->host : state->right;
}

static void modern_transfer_to_other_pane(FilerWindow *host, gboolean move)
{
	FilerWindow *active;
	FilerWindow *other;
	GList *paths;

	if (!host)
		return;
	active = modern_nav_target(host);
	other = modern_split_other_pane(host, active);
	if (!active || !other || !other->sym_path ||
	    g_strcmp0(active->sym_path, other->sym_path) == 0)
		return;
	paths = filer_selected_items(active);
	if (!paths)
		return;
	if (move)
		action_move(paths, other->sym_path, NULL, -1);
	else
		action_copy(paths, other->sym_path, NULL, -1);
	destroy_glist(&paths);
}

static void modern_copy_other_pane(GtkWidget *widget, FilerWindow *filer_window)
{
	(void) widget;
	modern_transfer_to_other_pane(filer_window, FALSE);
}

static void modern_move_other_pane(GtkWidget *widget, FilerWindow *filer_window)
{
	(void) widget;
	modern_transfer_to_other_pane(filer_window, TRUE);
}

static void modern_split_create_right(ModernSplitState *state)
{
	FilerWindow *right;
	GtkWidget *right_outer;
	GtkWidget *right_header;
	GtkWidget *right_hbox;
	GtkWidget *filter_bar;
	gchar *path = NULL;
	GKeyFile *kf;

	if (!state || state->right)
		return;
	kf = modern_session_load();
	path = g_key_file_get_string(kf, MODERN_SESSION_GROUP, "SplitRightPath", NULL);
	g_key_file_unref(kf);
	if (!path || !*path || !g_file_test(path, G_FILE_TEST_IS_DIR))
	{
		g_free(path);
		path = g_strdup(state->host->sym_path ? state->host->sym_path : home_dir);
	}

	right = filer_opendir_embedded(path, state->host);
	g_free(path);
	if (!right)
		return;
	right->modern_breadcrumbs_enabled = state->host->modern_breadcrumbs_enabled;
	g_object_set_data(G_OBJECT(right->window), "rox-modern-split-host", state->host);
	gtk_widget_hide(right->window);

	right_outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(right_outer), "rox-modern-split-pane");
	right_header = modern_location_widget_new(right, TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(right_header), 4);
	gtk_box_pack_start(GTK_BOX(right_outer), right_header, FALSE, FALSE, 0);
	filter_bar = modern_filter_bar_new(right);

	right_hbox = GTK_WIDGET(right->view_hbox);
	g_object_ref(right_hbox);
	if (gtk_widget_get_parent(right_hbox))
		gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(right_hbox)), right_hbox);
	gtk_box_pack_start(GTK_BOX(right_outer), right_hbox, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(right_outer), filter_bar, FALSE, FALSE, 0);
	g_object_unref(right_hbox);

	state->right = right;
	state->right_outer = right_outer;
	state->right_content = right_hbox;
	gtk_paned_pack2(GTK_PANED(state->paned), right_outer, TRUE, FALSE);
	gtk_widget_hide(right_outer);
}

static void modern_split_toggle(FilerWindow *filer_window)
{
	ModernSplitState *state = modern_split_state(filer_window);
	gint width;
	if (!state)
		return;
	if (!state->right)
		modern_split_create_right(state);
	if (!state->right || !state->right_outer)
		return;

	state->enabled = !state->enabled;
	if (state->enabled)
	{
		gtk_widget_show_all(state->right_outer);
		if (state->right->modern_filter_bar &&
		    !state->right->modern_quick_filter)
			gtk_widget_hide(state->right->modern_filter_bar);
		width = gtk_widget_get_allocated_width(state->paned);
		gtk_paned_set_position(GTK_PANED(state->paned),
			state->last_position > 0 ? state->last_position : MAX(width / 2, 250));
		state->active = state->right;
		modern_split_update_active_style(state);
		if (state->right->view)
			gtk_widget_grab_focus(GTK_WIDGET(state->right->view));
	}
	else
	{
		state->last_position = gtk_paned_get_position(GTK_PANED(state->paned));
		gtk_widget_hide(state->right_outer);
		state->active = state->host;
		modern_split_update_active_style(state);
		if (state->host->view)
			gtk_widget_grab_focus(GTK_WIDGET(state->host->view));
	}
}

static void modern_split_menu(GtkMenuItem *item, FilerWindow *filer_window)
{
	(void) item;
	modern_split_toggle(filer_window);
}

static void modern_filter_button(GtkWidget *widget, FilerWindow *filer_window)
{
	FilerWindow *target;
	(void) widget;
	target = modern_nav_target(filer_window);
	modern_filter_toggle(target);
}

static void modern_icons_view(GtkWidget *widget, FilerWindow *filer_window)
{
	FilerWindow *target = modern_nav_target(filer_window);
	(void) widget;
	if (target && target->view_type != VIEW_TYPE_COLLECTION)
		filer_set_view_type(target, VIEW_TYPE_COLLECTION);
}

static void modern_details_view(GtkWidget *widget, FilerWindow *filer_window)
{
	FilerWindow *target = modern_nav_target(filer_window);
	(void) widget;
	if (!target)
		return;
	target->modern_full_details = TRUE;
	if (target->view_type != VIEW_TYPE_DETAILS)
		filer_set_view_type(target, VIEW_TYPE_DETAILS);
	else
		view_details_apply_modern_columns(target);
}

static gboolean modern_split_restore_idle(gpointer data)
{
	FilerWindow *filer_window = data;
	ModernSplitState *state;
	if (!filer_window || !filer_exists(filer_window))
		return G_SOURCE_REMOVE;
	state = modern_split_state(filer_window);
	if (state && !state->enabled)
		modern_split_toggle(filer_window);
	return G_SOURCE_REMOVE;
}

/* 2.14.0-4: Modern reparents the ROX view while building the F3/preview
 * workspace.  Reparenting a focused GtkWidget drops keyboard focus; GTK then
 * tends to choose the editable location entry as the next focus target.  That
 * broke ROX's long-standing type-ahead selection: typing "peke" edited the
 * path bar instead of selecting a matching file.
 *
 * Restore focus only after the workspace (and an optional restored F3 pane)
 * has settled.  Explicit text modes still own focus when the user invokes
 * Ctrl+L/F6 or Ctrl+I. */
static gboolean modern_restore_file_view_focus_idle(gpointer data)
{
	FilerWindow *filer_window = data;
	ModernSplitState *state;
	FilerWindow *target;

	if (!filer_window || !filer_exists(filer_window) || !filer_window->modern_mode)
		return G_SOURCE_REMOVE;

	state = modern_split_state(filer_window);
	target = (state && state->enabled && state->active) ? state->active : filer_window;
	if (target && target->view)
		gtk_widget_grab_focus(GTK_WIDGET(target->view));

	return G_SOURCE_REMOVE;
}

void modern_ui_finalize_content(FilerWindow *filer_window)
{
	ModernSplitState *state;
	GtkWidget *view;
	GtkWidget *scrollbar;
	GtkWidget *paned;
	GtkWidget *left_outer;
	GtkWidget *left_content;
	GtkWidget *filter_bar;
	GtkWidget *preview;
	GKeyFile *kf;
	gboolean preview_visible = FALSE;
	gboolean split_enabled = FALSE;

	if (!filer_window || !filer_window->modern_mode || filer_window->modern_embedded ||
	    !filer_window->modern_content_box || !filer_window->view || !filer_window->scrollbar)
		return;
	if (modern_split_state(filer_window))
		return;

	state = g_new0(ModernSplitState, 1);
	state->host = filer_window;
	state->active = filer_window;
	state->last_position = 0;
	paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
	left_outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_style_context_add_class(gtk_widget_get_style_context(left_outer), "rox-modern-split-pane");
	left_content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	filter_bar = modern_filter_bar_new(filer_window);
	gtk_box_pack_start(GTK_BOX(left_outer), left_content, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(left_outer), filter_bar, FALSE, FALSE, 0);

	view = GTK_WIDGET(filer_window->view);
	scrollbar = filer_window->scrollbar;
	g_object_ref(view);
	g_object_ref(scrollbar);
	if (gtk_widget_get_parent(view))
		gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(view)), view);
	if (gtk_widget_get_parent(scrollbar))
		gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(scrollbar)), scrollbar);
	gtk_box_pack_start(GTK_BOX(left_content), view, TRUE, TRUE, 0);
	gtk_box_pack_end(GTK_BOX(left_content), scrollbar, FALSE, TRUE, 0);
	g_object_unref(view);
	g_object_unref(scrollbar);
	filer_window->view_hbox = GTK_BOX(left_content);

	gtk_paned_pack1(GTK_PANED(paned), left_outer, TRUE, FALSE);
	gtk_box_pack_start(GTK_BOX(filer_window->modern_content_box), paned, TRUE, TRUE, 0);
	preview = modern_preview_box_new(filer_window);
	gtk_box_pack_end(GTK_BOX(filer_window->modern_content_box), preview, FALSE, FALSE, 0);

	state->paned = paned;
	state->left_outer = left_outer;
	state->left_content = left_content;
	modern_split_update_active_style(state);
	g_object_set_data_full(G_OBJECT(filer_window->window), "rox-modern-split-state",
		state, modern_split_state_free);
	g_signal_connect(filer_window->window, "set-focus",
		G_CALLBACK(modern_split_focus_changed), state);

	kf = modern_session_load();
	if (g_key_file_has_key(kf, MODERN_SESSION_GROUP, "PreviewVisible", NULL))
		preview_visible = g_key_file_get_boolean(kf, MODERN_SESSION_GROUP, "PreviewVisible", NULL);
	if (g_key_file_has_key(kf, MODERN_SESSION_GROUP, "SplitEnabled", NULL))
		split_enabled = g_key_file_get_boolean(kf, MODERN_SESSION_GROUP, "SplitEnabled", NULL);
	state->last_position = g_key_file_get_integer(kf, MODERN_SESSION_GROUP, "SplitPosition", NULL);
	g_key_file_unref(kf);

	gtk_widget_show(paned);
	gtk_widget_show(left_outer);
	gtk_widget_show(left_content);
	gtk_widget_show(view);
	gtk_widget_show(scrollbar);
	gtk_widget_hide(filter_bar);
	if (preview_visible)
		modern_preview_set_visible(filer_window, TRUE);
	if (split_enabled)
		g_idle_add(modern_split_restore_idle, filer_window);

	/* filer_add_widgets() focused the file view before this function moved it
	 * into the Modern workspace. Re-acquire focus after all reparenting so
	 * unmodified typing continues to drive ROX type-ahead selection. */
	g_idle_add(modern_restore_file_view_focus_idle, filer_window);
}

static void menu_new_window(GtkMenuItem *item, FilerWindow *filer_window)
{
	(void) item;
	filer_opendir(filer_window->sym_path, filer_window, NULL);
}

static void menu_close(GtkMenuItem *item, FilerWindow *filer_window)
{
	(void) item;
	gtk_widget_destroy(filer_window->window);
}

static void menu_options(GtkMenuItem *item, FilerWindow *filer_window)
{
	(void) item;
	(void) filer_window;

	/* 2.13.0-5: use exactly the same Options entry point as Classic.
	 * Besides keeping ROX's window accounting consistent, this prevents
	 * Modern from drifting into a separate Options-window behaviour. */
	menu_show_options(NULL, 0, NULL);
}

static void menu_bookmarks(GtkMenuItem *item, FilerWindow *filer_window)
{
	(void) item;
	bookmarks_show_menu(filer_window);
}

static void modern_open_smb(GtkWidget *widget, FilerWindow *filer_window)
{
	(void) widget;
	rox_smb_open_dialog(filer_window);
}

static void modern_shared_folders(GtkWidget *widget, FilerWindow *filer_window)
{
	(void) widget;
	samba_share_show_manager(filer_window ? GTK_WINDOW(filer_window->window) : NULL);
}

static GtkWidget *append_menu(GtkWidget *bar, const gchar *label)
{
	GtkWidget *root = gtk_menu_item_new_with_mnemonic(label);
	GtkWidget *menu = gtk_menu_new();
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(root), menu);
	gtk_menu_shell_append(GTK_MENU_SHELL(bar), root);
	return menu;
}

static GtkWidget *append_item(GtkWidget *menu, const gchar *label,
		GCallback callback, FilerWindow *filer_window)
{
	GtkWidget *item = gtk_menu_item_new_with_mnemonic(label);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	if (callback)
		g_signal_connect(item, "activate", callback, filer_window);
	return item;
}

static void modern_sidebar_set_visible(FilerWindow *filer_window, gboolean visible)
{
	if (!filer_window || !filer_window->modern_sidebar)
		return;
	if (visible)
		gtk_widget_show_all(filer_window->modern_sidebar);
	else
		gtk_widget_hide(filer_window->modern_sidebar);
	if (filer_window->modern_sidebar_menu_item &&
	    GTK_IS_CHECK_MENU_ITEM(filer_window->modern_sidebar_menu_item) &&
	    gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(filer_window->modern_sidebar_menu_item)) != visible)
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(filer_window->modern_sidebar_menu_item), visible);
}

static void modern_toggle_sidebar(GtkCheckMenuItem *item, FilerWindow *filer_window)
{
	if (!filer_window || !filer_window->modern_sidebar)
		return;
	modern_sidebar_set_visible(filer_window,
		item ? gtk_check_menu_item_get_active(item) : !gtk_widget_get_visible(filer_window->modern_sidebar));
}

static void modern_breadcrumbs_toggled(GtkCheckMenuItem *item, FilerWindow *filer_window)
{
	ModernSplitState *split;
	gboolean enabled;
	if (!filer_window || !item)
		return;
	enabled = gtk_check_menu_item_get_active(item);
	filer_window->modern_breadcrumbs_enabled = enabled;
	modern_location_show_default(filer_window);
	split = modern_split_state(filer_window);
	if (split && split->right)
	{
		split->right->modern_breadcrumbs_enabled = enabled;
		modern_location_show_default(split->right);
	}
}

static void modern_save_session(GtkWidget *widget, FilerWindow *filer_window)
{
	ModernTabsState *state;
	ModernSplitState *split;
	GKeyFile *kf;
	gchar **paths;
	guint i;
	gint width = 0, height = 0;
	gint active = 0;
	gboolean maximized = FALSE;
	(void) widget;
	if (!filer_window || !filer_window->modern_mode || !filer_window->window)
		return;
	/* Only the current session owner writes the shared Modern session.
	 * This prevents a secondary Modern window from overwriting the main
	 * window's tabs/geometry when it is closed first. */
	if (modern_session_owner != filer_window)
		return;
	state = modern_tabs_state(filer_window);
	if (state && state->active)
		modern_tab_capture_state(state->active);
	kf = modern_session_load();
	gtk_window_get_size(GTK_WINDOW(filer_window->window), &width, &height);
	if (gtk_widget_get_window(filer_window->window))
		maximized = (gdk_window_get_state(gtk_widget_get_window(filer_window->window)) & GDK_WINDOW_STATE_MAXIMIZED) != 0;
	g_key_file_set_integer(kf, MODERN_SESSION_GROUP, "Width", width);
	g_key_file_set_integer(kf, MODERN_SESSION_GROUP, "Height", height);
	g_key_file_set_integer(kf, MODERN_SESSION_GROUP, "GeometryVersion", 76);
	g_key_file_set_boolean(kf, MODERN_SESSION_GROUP, "Maximized", maximized);
	g_key_file_set_boolean(kf, MODERN_SESSION_GROUP, "SidebarVisible",
		filer_window->modern_sidebar ? gtk_widget_get_visible(filer_window->modern_sidebar) : TRUE);
	if (filer_window->modern_paned)
		g_key_file_set_integer(kf, MODERN_SESSION_GROUP, "SidebarWidth",
			gtk_paned_get_position(GTK_PANED(filer_window->modern_paned)));
	g_key_file_set_boolean(kf, MODERN_SESSION_GROUP, "PreviewVisible",
		filer_window->modern_preview_box ? gtk_widget_get_visible(filer_window->modern_preview_box) : FALSE);
	g_key_file_set_boolean(kf, MODERN_SESSION_GROUP, "UseBreadcrumbs",
		filer_window->modern_breadcrumbs_enabled);
	split = modern_split_state(filer_window);
	if (split)
	{
		g_key_file_set_boolean(kf, MODERN_SESSION_GROUP, "SplitEnabled", split->enabled);
		g_key_file_set_integer(kf, MODERN_SESSION_GROUP, "SplitPosition",
			split->paned ? gtk_paned_get_position(GTK_PANED(split->paned)) : split->last_position);
		if (split->right && split->right->sym_path)
			g_key_file_set_string(kf, MODERN_SESSION_GROUP, "SplitRightPath", split->right->sym_path);
	}
	g_key_file_set_integer(kf, MODERN_SESSION_GROUP, "SidebarLayoutVersion", 81);
	g_key_file_set_integer(kf, MODERN_SESSION_GROUP, "TabLayoutVersion", 80);
	if (state && state->tabs && state->tabs->len) {
		paths = g_new0(gchar *, state->tabs->len);
		for (i = 0; i < state->tabs->len; i++) {
			ModernTab *tab = g_ptr_array_index(state->tabs, i);
			paths[i] = tab->path ? tab->path : (gchar *) home_dir;
			if (tab == state->active) active = (gint) i;
		}
		g_key_file_set_string_list(kf, MODERN_SESSION_GROUP, "Tabs",
			(const gchar * const *) paths, state->tabs->len);
		g_key_file_set_integer(kf, MODERN_SESSION_GROUP, "ActiveTab", active);
		g_free(paths);
	}
	modern_session_write(kf);
	g_key_file_unref(kf);
}

static gboolean modern_promote_session_owner_idle(gpointer data)
{
	(void) data;
	/* Promotion is deferred until GTK returns to the main loop.  During a
	 * whole-application shutdown all windows are normally destroyed before
	 * this idle runs, so a secondary window cannot overwrite the saved main
	 * session merely because it happened to be destroyed last. */
	if (!modern_session_owner && modern_windows)
		modern_session_owner = modern_windows->data;
	return G_SOURCE_REMOVE;
}

static void modern_window_destroy(GtkWidget *widget, FilerWindow *filer_window)
{
	gboolean was_owner;
	ModernSplitState *split;
	(void) widget;
	if (!filer_window)
		return;

	was_owner = modern_session_owner == filer_window;
	if (was_owner)
		modern_save_session(NULL, filer_window);

	/* Do not leave the invisible backend of an F3 pane alive after its host
	 * closes. Move its content back under its own hidden window first so GTK
	 * destroys the widgets and FilerWindow in their normal ownership order. */
	split = modern_split_state(filer_window);
	if (split && split->right && split->right->window)
	{
		GtkWidget *right_hbox = GTK_WIDGET(split->right->view_hbox);
		if (right_hbox && split->right->toplevel_vbox && gtk_widget_get_parent(right_hbox))
		{
			g_object_ref(right_hbox);
			gtk_container_remove(GTK_CONTAINER(gtk_widget_get_parent(right_hbox)), right_hbox);
			gtk_box_pack_start(split->right->toplevel_vbox, right_hbox, TRUE, TRUE, 0);
			g_object_unref(right_hbox);
		}
		gtk_widget_destroy(split->right->window);
		split->right = NULL;
	}

	modern_windows = g_list_remove(modern_windows, filer_window);
	if (was_owner) {
		modern_session_owner = NULL;
		if (modern_windows)
			g_idle_add(modern_promote_session_owner_idle, NULL);
	}
}

static void modern_restore_window_state(FilerWindow *filer_window)
{
	GKeyFile *kf;
	gint width, height;
	gint geometry_version;
	gboolean maximized;
	if (!filer_window || !filer_window->window) return;
	if (modern_session_owner && modern_session_owner != filer_window)
		return;
	kf = modern_session_load();
	width = g_key_file_get_integer(kf, MODERN_SESSION_GROUP, "Width", NULL);
	height = g_key_file_get_integer(kf, MODERN_SESSION_GROUP, "Height", NULL);
	geometry_version = g_key_file_get_integer(kf, MODERN_SESSION_GROUP, "GeometryVersion", NULL);
	maximized = g_key_file_get_boolean(kf, MODERN_SESSION_GROUP, "Maximized", NULL);
	/* 2.12.2-76 deliberately resets geometry inherited from the buggy
	 * pre-76 Modern implementation once.  After 76 saves the session, the
	 * user's exact width/height becomes authoritative forever. */
	if (geometry_version != 76 || width <= 0 || height <= 0) {
		width = MODERN_DEFAULT_WIDTH;
		height = MODERN_DEFAULT_HEIGHT;
		maximized = FALSE;
	}
	gtk_window_set_default_size(GTK_WINDOW(filer_window->window), width, height);
	if (maximized)
		gtk_window_maximize(GTK_WINDOW(filer_window->window));
	g_key_file_unref(kf);
}

static void modern_show_help_files(GtkWidget *widget, FilerWindow *filer_window)
{
	menu_rox_help(filer_window, HELP_DIR, widget);
}

/* 2.12.2-82: el menu Help de Moderno no ofrecia Acerca de.  Se reutiliza el
 * mismo dialogo nativo que usa Clasico en lugar de duplicarlo. */
static void modern_show_about(GtkWidget *widget, FilerWindow *filer_window)
{
	menu_rox_help(filer_window, HELP_ABOUT, widget);
}

static GtkWidget *build_menubar(FilerWindow *filer_window)
{
	GtkWidget *bar = gtk_menu_bar_new();
	GtkWidget *menu;

	menu = append_menu(bar, _("File"));
	append_item(menu, _("New Window"), G_CALLBACK(menu_new_window), filer_window);
	append_item(menu, _("New Tab"), G_CALLBACK(modern_new_tab), filer_window);
	append_item(menu, _("Duplicate Tab"), G_CALLBACK(modern_duplicate_tab), filer_window);
	append_item(menu, _("Close Tab"), G_CALLBACK(modern_close_current_tab), filer_window);
	append_item(menu, _("Close Other Tabs"), G_CALLBACK(modern_close_other_tabs), filer_window);
	append_item(menu, _("Close Tabs to the Right"), G_CALLBACK(modern_close_tabs_right), filer_window);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	append_item(menu, _("Close"), G_CALLBACK(menu_close), filer_window);

	menu = append_menu(bar, _("Edit"));
	append_item(menu, _("Copy to Other Pane (Shift+F5)"), G_CALLBACK(modern_copy_other_pane), filer_window);
	append_item(menu, _("Move to Other Pane (Shift+F6)"), G_CALLBACK(modern_move_other_pane), filer_window);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	append_item(menu, _("Options..."), G_CALLBACK(menu_options), filer_window);

	menu = append_menu(bar, _("View"));
	{
		GtkWidget *sidebar_item = gtk_check_menu_item_new_with_mnemonic(_("Show Sidebar"));
		GtkWidget *preview_item = gtk_check_menu_item_new_with_mnemonic(_("Show Preview Pane"));
		GtkWidget *breadcrumbs_item = gtk_check_menu_item_new_with_mnemonic(_("Breadcrumb Location Bar"));
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(sidebar_item), TRUE);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), sidebar_item);
		g_signal_connect(sidebar_item, "toggled", G_CALLBACK(modern_toggle_sidebar), filer_window);
		filer_window->modern_sidebar_menu_item = sidebar_item;
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), preview_item);
		g_signal_connect(preview_item, "toggled", G_CALLBACK(modern_preview_toggled), filer_window);
		filer_window->modern_preview_menu_item = preview_item;
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(breadcrumbs_item),
			filer_window->modern_breadcrumbs_enabled);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), breadcrumbs_item);
		g_signal_connect(breadcrumbs_item, "toggled", G_CALLBACK(modern_breadcrumbs_toggled), filer_window);
		filer_window->modern_breadcrumb_menu_item = breadcrumbs_item;
	}
	append_item(menu, _("Split View (F3)"), G_CALLBACK(modern_split_menu), filer_window);
	append_item(menu, _("Quick Filter (Ctrl+I)"), G_CALLBACK(modern_filter_button), filer_window);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	append_item(menu, _("Icon View"), G_CALLBACK(modern_icons_view), filer_window);
	append_item(menu, _("Detailed View"), G_CALLBACK(modern_details_view), filer_window);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
	append_item(menu, _("Scan"), G_CALLBACK(reload_dir), filer_window);
	append_item(menu, _("Search"), G_CALLBACK(search_dir), filer_window);

	menu = append_menu(bar, _("Go"));
	filer_window->modern_menu_back = append_item(menu, _("Back"), G_CALLBACK(go_back), filer_window);
	filer_window->modern_menu_forward = append_item(menu, _("Forward"), G_CALLBACK(go_forward), filer_window);
	append_item(menu, _("Up"), G_CALLBACK(go_up), filer_window);
	append_item(menu, _("Home"), G_CALLBACK(go_home), filer_window);

	menu = append_menu(bar, _("Samba"));
	append_item(menu, _("Connect to SMB Share..."), G_CALLBACK(modern_open_smb), filer_window);
	{
		GtkWidget *shared = append_item(menu, _("Shared Folders..."),
			G_CALLBACK(modern_shared_folders), filer_window);
		gtk_widget_set_sensitive(shared, samba_share_available());
	}

	menu = append_menu(bar, _("Bookmarks"));
	append_item(menu, _("Bookmarks"), G_CALLBACK(menu_bookmarks), filer_window);

	menu = append_menu(bar, _("Help"));
	append_item(menu, _("Show Help Files"), G_CALLBACK(modern_show_help_files), filer_window);
	append_item(menu, _("About"), G_CALLBACK(modern_show_about), filer_window);

	return bar;
}

void modern_ui_build(FilerWindow *filer_window, GtkWidget *vbox)
{
	GtkWidget *menubar;
	GtkWidget *nav;
	GtkWidget *button;
	GtkWidget *location;
	GKeyFile *kf;

	g_return_if_fail(filer_window != NULL);
	g_return_if_fail(GTK_IS_BOX(vbox));

	modern_install_style(filer_window->window);

	/* 2.14.0-3 restores the original editable location bar by default.
	 * Breadcrumbs are an optional presentation mode remembered per Modern
	 * session, not a permanent replacement for Rox-Filer2's path entry. */
	kf = modern_session_load();
	filer_window->modern_breadcrumbs_enabled =
		g_key_file_has_key(kf, MODERN_SESSION_GROUP, "UseBreadcrumbs", NULL) &&
		g_key_file_get_boolean(kf, MODERN_SESSION_GROUP, "UseBreadcrumbs", NULL);
	g_key_file_unref(kf);

	/* Track Modern windows separately so only one window owns the persisted
	 * session at a time.  Additional windows remain normal independent ROX
	 * windows and cannot overwrite the main restored session accidentally. */
	modern_windows = g_list_append(modern_windows, filer_window);
	if (!modern_session_owner)
		modern_session_owner = filer_window;

	menubar = build_menubar(filer_window);
	gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);
	filer_window->modern_menubar = menubar;

	nav = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_container_set_border_width(GTK_CONTAINER(nav), 5);

	button = modern_nav_button("go-previous", _("Back"));
	g_signal_connect(button, "clicked", G_CALLBACK(go_back), filer_window);
	gtk_box_pack_start(GTK_BOX(nav), button, FALSE, FALSE, 0);
	filer_window->modern_back = button;

	button = modern_nav_button("go-next", _("Forward"));
	g_signal_connect(button, "clicked", G_CALLBACK(go_forward), filer_window);
	gtk_box_pack_start(GTK_BOX(nav), button, FALSE, FALSE, 0);
	filer_window->modern_forward = button;

	button = modern_nav_button("go-up", _("Up"));
	g_signal_connect(button, "clicked", G_CALLBACK(go_up), filer_window);
	gtk_box_pack_start(GTK_BOX(nav), button, FALSE, FALSE, 0);

	button = modern_nav_button("user-home", _("Home"));
	g_signal_connect(button, "clicked", G_CALLBACK(go_home), filer_window);
	gtk_box_pack_start(GTK_BOX(nav), button, FALSE, FALSE, 0);

	button = modern_nav_button("view-refresh", _("Reload"));
	g_signal_connect(button, "clicked", G_CALLBACK(reload_dir), filer_window);
	gtk_box_pack_start(GTK_BOX(nav), button, FALSE, FALSE, 0);

	location = modern_location_widget_new(filer_window, FALSE);
	gtk_box_pack_start(GTK_BOX(nav), location, TRUE, TRUE, 4);

	button = icon_button("window-close-symbolic", _("Close Window"));
	gtk_widget_set_size_request(button, 48, 48);
	gtk_widget_set_no_show_all(button, TRUE);
	g_signal_connect(button, "clicked", G_CALLBACK(modern_close_window), filer_window);
	gtk_box_pack_end(GTK_BOX(nav), button, FALSE, FALSE, 0);
	filer_window->modern_close = button;

	/* Keep one clear search action in the toolbar, as in the original Modern
	 * design. Quick Filter lives on Ctrl+I/View and Preview on F11/View. */
	button = icon_button("system-search", _("Search"));
	gtk_widget_set_size_request(button, 48, 48);
	g_signal_connect(button, "clicked", G_CALLBACK(search_dir), filer_window);
	gtk_box_pack_end(GTK_BOX(nav), button, FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(vbox), nav, FALSE, FALSE, 0);
	filer_window->modern_navbar = nav;

	filer_window->modern_tabbar = modern_tabs_build(filer_window);

	gtk_widget_show_all(menubar);
	gtk_widget_show_all(nav);
	modern_restore_window_state(filer_window);
	g_signal_connect(filer_window->window, "destroy", G_CALLBACK(modern_window_destroy), filer_window);
	modern_ui_update_navigation(filer_window);
}


/* -------------------------------------------------------------------------
 * Modern Places sidebar
 *
 * This is deliberately a view of the existing Rox-Filer2 backend.  It does
 * not implement alternative Desktop, Trash or directory semantics.  Desktop
 * comes from desktop.c, Trash comes from trash.c, and the remaining standard
 * user folders come from GLib's XDG user-directory API.
 */

static GtkWidget *places_row_new(const gchar *icon_name, const gchar *label,
                                 const gchar *path)
{
	GtkWidget *row;
	GtkWidget *box;
	GtkWidget *image;
	GtkWidget *text;

	row = gtk_list_box_row_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(row), "rox-modern-sidebar-row");
	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_container_set_border_width(GTK_CONTAINER(box), 5);

	image = modern_sidebar_icon(icon_name, 20);
	gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);

	text = gtk_label_new(label);
	gtk_label_set_xalign(GTK_LABEL(text), 0.0);
	gtk_label_set_ellipsize(GTK_LABEL(text), PANGO_ELLIPSIZE_END);
	gtk_box_pack_start(GTK_BOX(box), text, TRUE, TRUE, 0);

	gtk_container_add(GTK_CONTAINER(row), box);
	g_object_set_data_full(G_OBJECT(row), "rox-modern-path", g_strdup(path), g_free);
	return row;
}

static gboolean places_has_path(GtkWidget *list, const gchar *path)
{
	GList *rows;
	GList *iter;
	gboolean found = FALSE;

	if (!path || !*path)
		return TRUE;

	rows = gtk_container_get_children(GTK_CONTAINER(list));
	for (iter = rows; iter; iter = iter->next) {
		const gchar *row_path = g_object_get_data(G_OBJECT(iter->data), "rox-modern-path");
		if (g_strcmp0(row_path, path) == 0) {
			found = TRUE;
			break;
		}
	}
	g_list_free(rows);
	return found;
}

static void places_add_path(GtkWidget *list, const gchar *icon_name,
                            const gchar *label, const gchar *path)
{
	GtkWidget *row;

	if (!path || !*path || places_has_path(list, path))
		return;

	row = places_row_new(icon_name, label, path);
	gtk_container_add(GTK_CONTAINER(list), row);
}

static void places_add_xdg(GtkWidget *list, GUserDirectory directory,
                           const gchar *icon_name, const gchar *fallback_leaf)
{
	const gchar *path;
	gchar *fallback = NULL;
	gchar *label;

	path = g_get_user_special_dir(directory);
	if (!path || !*path || !g_file_test(path, G_FILE_TEST_IS_DIR)) {
		if (home_dir && fallback_leaf && *fallback_leaf) {
			fallback = g_build_filename(home_dir, fallback_leaf, NULL);
			if (g_file_test(fallback, G_FILE_TEST_IS_DIR))
				path = fallback;
			else
				path = NULL;
		}
	}
	if (!path || !*path) {
		g_free(fallback);
		return;
	}

	/* Use the actual directory basename so translated/custom XDG directories
	 * keep their local name.  Puppy-style systems without user-dirs.conf get
	 * the conventional existing $HOME fallback instead of silently losing the
	 * Places entry. */
	label = g_filename_display_basename(path);
	places_add_path(list, icon_name, label, path);
	g_free(label);
	g_free(fallback);
}

static void modern_bookmarks_refresh(FilerWindow *filer_window)
{
	GList *children, *iter;
	GPtrArray *entries;
	guint i;
	GtkWidget *row;

	if (!filer_window || !filer_window->modern_bookmarks_list)
		return;
	children = gtk_container_get_children(GTK_CONTAINER(filer_window->modern_bookmarks_list));
	for (iter = children; iter; iter = iter->next)
		gtk_widget_destroy(GTK_WIDGET(iter->data));
	g_list_free(children);

	entries = bookmarks_get_entries();
	for (i = 0; entries && i < entries->len; i++)
	{
		RoxBookmarkInfo *info = g_ptr_array_index(entries, i);
		if (!info || !info->path || !*info->path)
			continue;
		row = places_row_new("starred", info->title && *info->title ? info->title : info->path,
			info->path);
		gtk_container_add(GTK_CONTAINER(filer_window->modern_bookmarks_list), row);
	}
	if (entries)
		g_ptr_array_free(entries, TRUE);

	/* Management stays in the existing Bookmarks menu.  The sidebar only
	 * shows actual favourite locations so it keeps the compact pre-2.14 look. */
	gtk_widget_show_all(filer_window->modern_bookmarks_list);
}

static void places_row_activated(GtkListBox *box, GtkListBoxRow *row,
                                 FilerWindow *filer_window)
{
	const gchar *path;
	FilerWindow *target;

	(void) box;
	target = modern_nav_target(filer_window);
	if (g_object_get_data(G_OBJECT(row), "rox-modern-smb")) {
		rox_smb_open_dialog(target);
		return;
	}
	if (g_object_get_data(G_OBJECT(row), "rox-modern-bookmark-add")) {
		if (target && target->sym_path)
			bookmarks_add_path(target->sym_path);
		modern_bookmarks_refresh(filer_window);
		return;
	}
	if (g_object_get_data(G_OBJECT(row), "rox-modern-bookmark-edit")) {
		bookmarks_edit();
		return;
	}
	path = g_object_get_data(G_OBJECT(row), "rox-modern-path");
	if (path && *path && target)
		filer_change_to(target, path, NULL);
}

static gboolean modern_places_button_press(GtkWidget *widget, GdkEventButton *event,
		FilerWindow *filer_window)
{
	GtkListBoxRow *row;
	const gchar *path;
	ModernSplitState *state;
	FilerWindow *active;
	FilerWindow *other;

	if (!filer_window || !event || event->type != GDK_BUTTON_PRESS || event->button != 2)
		return FALSE;
	row = gtk_list_box_get_row_at_y(GTK_LIST_BOX(widget), (gint) event->y);
	if (!row)
		return FALSE;
	path = g_object_get_data(G_OBJECT(row), "rox-modern-path");
	if (!path || !*path)
		return FALSE;

	state = modern_split_state(filer_window);
	if (!state)
		return FALSE;
	/* Capture the pane that was active before opening F3.  modern_split_toggle()
	 * deliberately gives focus to the new right pane, but a middle-click on a
	 * Place means “open this in the other pane”, so the original pane must
	 * remain the reference. */
	active = modern_nav_target(filer_window);
	if (!state->enabled)
		modern_split_toggle(filer_window);
	if (!state->enabled || !state->right)
		return FALSE;
	other = modern_split_other_pane(filer_window, active);
	if (!other)
		return FALSE;
	filer_change_to(other, path, NULL);
	if (other->view)
		gtk_widget_grab_focus(GTK_WIDGET(other->view));
	return TRUE;
}

static void modern_ui_select_current_place(FilerWindow *filer_window)
{
	GList *rows;
	GList *iter;
	FilerWindow *target;

	if (!filer_window || !filer_window->modern_places_list)
		return;
	target = modern_nav_target(filer_window);
	if (!target || !target->sym_path)
		return;

	rows = gtk_container_get_children(GTK_CONTAINER(filer_window->modern_places_list));
	for (iter = rows; iter; iter = iter->next) {
		GtkListBoxRow *row = GTK_LIST_BOX_ROW(iter->data);
		const gchar *path = g_object_get_data(G_OBJECT(row), "rox-modern-path");
		if (path && g_strcmp0(path, target->sym_path) == 0) {
			gtk_list_box_select_row(GTK_LIST_BOX(filer_window->modern_places_list), row);
			g_list_free(rows);
			return;
		}
	}
	gtk_list_box_unselect_all(GTK_LIST_BOX(filer_window->modern_places_list));
	g_list_free(rows);
}


/* -------------------------------------------------------------------------
 * Modern Devices sidebar
 *
 * Device discovery, icon choice, mount/unmount/eject and open semantics are
 * provided by drives.c. The Modern UI only renders that shared model.
 */

typedef struct
{
	RoxDriveInfo *drive;
} ModernDriveRow;

static void modern_drive_row_free(gpointer data)
{
	ModernDriveRow *row = data;
	if (!row)
		return;
	rox_drive_info_free(row->drive);
	g_free(row);
}

typedef struct
{
	gboolean eject;
} ModernDriveQuickAsync;

static void modern_drive_quick_done(GObject *source_object, GAsyncResult *result,
		gpointer user_data)
{
	ModernDriveQuickAsync *ctx = user_data;
	gchar *error_text = NULL;
	gboolean ok;
	(void) source_object;

	ok = ctx->eject ? rox_drive_eject_finish(result, &error_text)
	                : rox_drive_unmount_finish(result, &error_text);
	if (!ok)
		report_error("%s", error_text ? error_text :
			(ctx->eject ? _("The device could not be ejected.")
			            : _("The partition could not be unmounted.")));
	else
	{
		mount_update(TRUE);
		filer_update_all();
	}
	g_free(error_text);
	g_free(ctx);
}

static void modern_drive_quick_clicked(GtkButton *button, gpointer data)
{
	ModernDriveRow *row = data;
	ModernDriveQuickAsync *ctx;
	(void) button;

	if (!row || !row->drive)
		return;
	ctx = g_new0(ModernDriveQuickAsync, 1);
	/* 2.13.0-9: never power off a removable parent disk from a partition
	 * quick action. Optical rows may still perform a real Eject. */
	ctx->eject = row->drive->optical && rox_drive_can_eject(row->drive);
	if (ctx->eject)
		rox_drive_eject_async(row->drive, modern_drive_quick_done, ctx);
	else
		rox_drive_unmount_async(row->drive, modern_drive_quick_done, ctx);
}

static GtkWidget *devices_row_new(const RoxDriveInfo *drive)
{
	GtkWidget *row;
	GtkWidget *box;
	GtkWidget *image;
	GtkWidget *label;
	GtkWidget *quick_button;
	GtkWidget *quick_image;
	gchar *mountpoint;
	const gchar *status_text;
	ModernDriveRow *data;

	row = gtk_list_box_row_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(row), "rox-modern-sidebar-row");
	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_widget_set_hexpand(box, TRUE);
	gtk_container_set_border_width(GTK_CONTAINER(box), 5);
	image = modern_sidebar_icon(rox_drive_icon_name(drive), 20);
	gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
	label = gtk_label_new(rox_drive_display_name(drive));
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
	gtk_widget_set_hexpand(label, TRUE);
	gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);

	data = g_new0(ModernDriveRow, 1);
	data->drive = rox_drive_info_copy(drive);
	g_object_set_data_full(G_OBJECT(row), "rox-modern-drive", data,
		modern_drive_row_free);

	mountpoint = rox_drive_current_mountpoint(drive);
	status_text = mountpoint ? _("Mounted") : _("Not mounted");
	/* 2.13.0-7: use the same quick eject/unmount arrow as Desktop.  Its
	 * presence is intentionally also the visual mounted-state indicator. */
	if (mountpoint && !drive->foreign)
	{
		quick_button = gtk_button_new();
		gtk_button_set_relief(GTK_BUTTON(quick_button), GTK_RELIEF_NONE);
		gtk_widget_set_size_request(quick_button, 24, 24);
		gtk_style_context_add_class(gtk_widget_get_style_context(quick_button),
			"rox-drive-quick");
		quick_image = gtk_image_new_from_icon_name("media-eject-symbolic",
			GTK_ICON_SIZE_MENU);
		if (!gtk_icon_theme_has_icon(gtk_icon_theme_get_default(),
			"media-eject-symbolic"))
			gtk_image_set_from_icon_name(GTK_IMAGE(quick_image), "media-eject",
				GTK_ICON_SIZE_MENU);
		gtk_container_add(GTK_CONTAINER(quick_button), quick_image);
		/* 2.13.0-9: partition rows unmount only; optical media may eject. */
		gtk_widget_set_tooltip_text(quick_button,
			drive->optical && rox_drive_can_eject(drive) ? _("Eject") : _("Unmount"));
		g_signal_connect(quick_button, "clicked",
			G_CALLBACK(modern_drive_quick_clicked), data);
		gtk_box_pack_end(GTK_BOX(box), quick_button, FALSE, FALSE, 0);
	}
	/* Keep the drive name readable in a narrow sidebar. The mounted state is
	 * still available via the arrow and tooltip. */
	gtk_widget_set_tooltip_text(row, status_text);
	gtk_widget_set_tooltip_text(label, status_text);
	g_free(mountpoint);
	gtk_container_add(GTK_CONTAINER(row), box);
	return row;
}


static GtkWidget *devices_section_row_new(const gchar *text)
{
	GtkWidget *row;
	GtkWidget *label;
	PangoAttrList *attrs;

	row = gtk_list_box_row_new();
	gtk_widget_set_sensitive(row, FALSE);
	label = gtk_label_new(text);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_widget_set_margin_start(label, 8);
	gtk_widget_set_margin_end(label, 8);
	gtk_widget_set_margin_top(label, 8);
	gtk_widget_set_margin_bottom(label, 3);
	attrs = pango_attr_list_new();
	pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
	gtk_label_set_attributes(GTK_LABEL(label), attrs);
	pango_attr_list_unref(attrs);
	gtk_container_add(GTK_CONTAINER(row), label);
	return row;
}

static void modern_devices_clear(GtkWidget *list)
{
	GList *rows = gtk_container_get_children(GTK_CONTAINER(list));
	GList *iter;
	for (iter = rows; iter; iter = iter->next)
		gtk_widget_destroy(GTK_WIDGET(iter->data));
	g_list_free(rows);
}

static void modern_devices_show_initial_scan(GtkWidget *list)
{
	GtkWidget *row;
	GtkWidget *box;
	GtkWidget *spinner;
	GtkWidget *label;
	GList *rows;

	/* Do not blank an already-populated Devices list while a refresh runs.
	 * Existing rows remain usable until the shared scan publishes a new
	 * snapshot.  Only an empty/new list needs the initial progress row. */
	rows = gtk_container_get_children(GTK_CONTAINER(list));
	if (rows)
	{
		g_list_free(rows);
		return;
	}
	g_list_free(rows);
	gtk_container_add(GTK_CONTAINER(list),
		places_row_new("drive-harddisk", _("File System"), "/"));
	row = gtk_list_box_row_new();
	gtk_widget_set_sensitive(row, FALSE);
	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_container_set_border_width(GTK_CONTAINER(box), 5);
	spinner = gtk_spinner_new();
	gtk_spinner_start(GTK_SPINNER(spinner));
	gtk_box_pack_start(GTK_BOX(box), spinner, FALSE, FALSE, 0);
	label = gtk_label_new(_("Scanning"));
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_box_pack_start(GTK_BOX(box), label, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(row), box);
	gtk_container_add(GTK_CONTAINER(list), row);
	gtk_widget_show_all(list);
}

/* Modern windows share one drive scan.  Network/FUSE enumeration may block in
 * the kernel, so starting one GTask per filer window can exhaust GLib's worker
 * pool and starve unrelated SMB tasks.  Subscribers are weak refs owned by the
 * GTK main thread; a single result fans out to every live Devices list. */
static void modern_devices_monitor_changed(GPtrArray *drives,
        const GError *error, gpointer user_data)
{
    GtkWidget *list = GTK_WIDGET(user_data);
    guint j;

    if (!list || !GTK_IS_WIDGET(list) || !gtk_widget_get_parent(list))
        return;
    if (!drives) {
        /* Keep the previous valid contents on transient scan failures. */
        if (error)
            g_warning("Unable to refresh Modern devices: %s", error->message);
        return;
    }
    modern_devices_clear(list);
    gtk_container_add(GTK_CONTAINER(list),
        places_row_new("drive-harddisk", _("File System"), "/"));

    /* Keep real partitions first. Managed image loops are deliberately
     * separated and are the only loop devices Rox-Filer2 exposes. */
    for (j = 0; j < drives->len; j++) {
        RoxDriveInfo *drive = g_ptr_array_index(drives, j);
        if (drive->network || drive->managed_image || drive->portable)
            continue;
        gtk_container_add(GTK_CONTAINER(list), devices_row_new(drive));
    }

    {
        gboolean have_portable = FALSE;
        for (j = 0; j < drives->len; j++) {
            RoxDriveInfo *drive = g_ptr_array_index(drives, j);
            if (!drive->portable)
                continue;
            if (!have_portable) {
                gtk_container_add(GTK_CONTAINER(list),
                    devices_section_row_new(_("Portable Devices")));
                have_portable = TRUE;
            }
            gtk_container_add(GTK_CONTAINER(list), devices_row_new(drive));
        }
    }

    for (j = 0; j < drives->len; j++) {
        RoxDriveInfo *drive = g_ptr_array_index(drives, j);
        if (drive->managed_image) {
            guint k;
            gtk_container_add(GTK_CONTAINER(list),
                devices_section_row_new(_("Mounted Images")));
            for (k = j; k < drives->len; k++) {
                RoxDriveInfo *image_drive = g_ptr_array_index(drives, k);
                if (image_drive->managed_image)
                    gtk_container_add(GTK_CONTAINER(list),
                        devices_row_new(image_drive));
            }
            break;
        }
    }
    gtk_widget_show_all(list);
}

static void modern_devices_request_scan(GtkWidget *list)
{
    if (!list || !GTK_IS_WIDGET(list))
        return;
    modern_devices_show_initial_scan(list);
    rox_drives_monitor_subscribe(G_OBJECT(list), modern_devices_monitor_changed, list);
}

static void modern_ui_refresh_devices(FilerWindow *filer_window)
{
	if (!filer_window || !filer_window->modern_devices_list)
		return;
	modern_devices_request_scan(filer_window->modern_devices_list);
}

typedef struct
{
	FilerWindow *filer_window;
	gboolean open_after_mount;
} ModernDriveMountAsync;

static void modern_drive_mount_done(GObject *source_object, GAsyncResult *result,
		gpointer user_data)
{
	ModernDriveMountAsync *ctx = user_data;
	gchar *error_text = NULL;
	gchar *mountpoint;
	(void) source_object;

	mountpoint = rox_drive_mount_finish(result, &error_text);
	if (!mountpoint)
		report_error("%s", error_text ? error_text :
			_("The partition could not be mounted."));
	else
	{
		mount_update(TRUE);
		filer_update_all();
		if (ctx->open_after_mount && ctx->filer_window && filer_exists(ctx->filer_window))
			filer_change_to(ctx->filer_window, mountpoint, NULL);
	}
	g_free(mountpoint);
	g_free(error_text);
	g_free(ctx);
}

typedef struct
{
	FilerWindow *filer_window;
	gboolean eject;
} ModernDriveActionAsync;

static void modern_drive_action_done(GObject *source_object, GAsyncResult *result,
		gpointer user_data)
{
	ModernDriveActionAsync *ctx = user_data;
	gchar *error_text = NULL;
	gboolean ok;
	(void) source_object;

	ok = ctx->eject ? rox_drive_eject_finish(result, &error_text)
	                : rox_drive_unmount_finish(result, &error_text);
	if (!ok)
		report_error("%s", error_text ? error_text :
			(ctx->eject ? _("The device could not be ejected.")
			            : _("The partition could not be unmounted.")));
	else
	{
		mount_update(TRUE);
		filer_update_all();
	}
	g_free(error_text);
	g_free(ctx);
}

static void devices_row_activated(GtkListBox *box, GtkListBoxRow *row,
		FilerWindow *filer_window)
{
	ModernDriveRow *data;
	const gchar *path;
	gchar *mountpoint;
	FilerWindow *target;
	(void) box;
	target = modern_nav_target(filer_window);

	path = g_object_get_data(G_OBJECT(row), "rox-modern-path");
	if (path)
	{
		filer_change_to(target, path, NULL);
		return;
	}
	data = g_object_get_data(G_OBJECT(row), "rox-modern-drive");
	if (!data || !data->drive)
		return;
	mountpoint = rox_drive_current_mountpoint(data->drive);
	if (mountpoint)
	{
		filer_change_to(target, mountpoint, NULL);
		g_free(mountpoint);
		return;
	}
	if (data->drive->network)
	{
		report_error("%s", _("This network resource is no longer mounted."));
		return;
	}
	{
		ModernDriveMountAsync *ctx = g_new0(ModernDriveMountAsync, 1);
		ctx->filer_window = target;
		ctx->open_after_mount = TRUE;
		rox_drive_mount_async(data->drive, modern_drive_mount_done, ctx);
	}
}

typedef enum
{
	MODERN_DRIVE_OPEN,
	MODERN_DRIVE_MOUNT,
	MODERN_DRIVE_UNMOUNT,
	MODERN_DRIVE_EJECT
} ModernDriveCommand;

typedef struct
{
	FilerWindow *filer_window;
	RoxDriveInfo *drive;
	ModernDriveCommand command;
} ModernDriveMenuAction;

static void modern_drive_menu_action_free(gpointer data)
{
	ModernDriveMenuAction *action = data;
	if (!action)
		return;
	rox_drive_info_free(action->drive);
	g_free(action);
}

static void modern_drive_menu_run(GtkMenuItem *item, gpointer data)
{
	ModernDriveMenuAction *action = data;
	(void) item;

	if (!action || !action->drive || !action->filer_window)
		return;
	if (action->command == MODERN_DRIVE_OPEN || action->command == MODERN_DRIVE_MOUNT)
	{
		gchar *mountpoint = rox_drive_current_mountpoint(action->drive);
		if (mountpoint)
		{
			if (action->command == MODERN_DRIVE_OPEN)
				filer_change_to(action->filer_window, mountpoint, NULL);
			g_free(mountpoint);
			return;
		}
		if (action->drive->network)
		{
			report_error("%s", _("This network resource is no longer mounted."));
			return;
		}
		{
			ModernDriveMountAsync *ctx = g_new0(ModernDriveMountAsync, 1);
			ctx->filer_window = action->filer_window;
			ctx->open_after_mount = action->command == MODERN_DRIVE_OPEN;
			rox_drive_mount_async(action->drive, modern_drive_mount_done, ctx);
		}
		return;
	}

	if (action->command == MODERN_DRIVE_UNMOUNT ||
	    action->command == MODERN_DRIVE_EJECT)
	{
		ModernDriveActionAsync *ctx = g_new0(ModernDriveActionAsync, 1);
		ctx->filer_window = action->filer_window;
		ctx->eject = action->command == MODERN_DRIVE_EJECT;
		if (ctx->eject)
			rox_drive_eject_async(action->drive, modern_drive_action_done, ctx);
		else
			rox_drive_unmount_async(action->drive, modern_drive_action_done, ctx);
	}
}

static GtkWidget *modern_drive_menu_item(GtkWidget *menu, const gchar *label,
		FilerWindow *filer_window, const RoxDriveInfo *drive,
		ModernDriveCommand command)
{
	GtkWidget *item = gtk_menu_item_new_with_label(label);
	ModernDriveMenuAction *action = g_new0(ModernDriveMenuAction, 1);
	action->filer_window = filer_window;
	action->drive = rox_drive_info_copy(drive);
	action->command = command;
	g_object_set_data_full(G_OBJECT(item), "rox-modern-drive-menu-action", action,
		modern_drive_menu_action_free);
	g_signal_connect(item, "activate", G_CALLBACK(modern_drive_menu_run), action);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	return item;
}

static gboolean devices_button_press(GtkWidget *list, GdkEventButton *event,
		FilerWindow *filer_window)
{
	GtkListBoxRow *row;
	ModernDriveRow *data;
	GtkWidget *menu;
	GtkWidget *mount_item;
	GtkWidget *unmount_item;
	FilerWindow *target;
	gchar *mountpoint;
	gboolean mounted;

	if (!event || event->type != GDK_BUTTON_PRESS || event->button != 3)
		return FALSE;
	row = gtk_list_box_get_row_at_y(GTK_LIST_BOX(list), (gint) event->y);
	if (!row)
		return FALSE;
	data = g_object_get_data(G_OBJECT(row), "rox-modern-drive");
	if (!data || !data->drive)
		return FALSE;

	mountpoint = rox_drive_current_mountpoint(data->drive);
	mounted = mountpoint != NULL;
	g_free(mountpoint);
	target = modern_nav_target(filer_window);
	menu = rox_menu_new();
	modern_drive_menu_item(menu, _("Open"), target, data->drive, MODERN_DRIVE_OPEN);
	mount_item = modern_drive_menu_item(menu, _("Mount"), target,
		data->drive, MODERN_DRIVE_MOUNT);
	unmount_item = modern_drive_menu_item(menu, _("Unmount"), target,
		data->drive, MODERN_DRIVE_UNMOUNT);
	gtk_widget_set_sensitive(mount_item, !mounted && !data->drive->foreign);
	gtk_widget_set_sensitive(unmount_item, mounted && !data->drive->foreign);
	if (rox_drive_can_eject(data->drive))
	{
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
		modern_drive_menu_item(menu, _("Eject"), target, data->drive,
			MODERN_DRIVE_EJECT);
	}
	g_signal_connect_swapped(menu, "selection-done", G_CALLBACK(gtk_widget_destroy), menu);
	gtk_menu_attach_to_widget(GTK_MENU(menu), list, NULL);
	gtk_widget_show_all(menu);
	gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);
	return TRUE;
}

void modern_ui_attach_sidebar(FilerWindow *filer_window, GtkWidget *view_hbox)
{
	GtkWidget *sidebar;
	GtkWidget *content;
	GtkWidget *heading;
	GtkWidget *list;
	GtkWidget *bookmarks_list;
	GtkWidget *devices_heading;
	GtkWidget *devices_list;
	GtkWidget *network_heading;
	GtkWidget *network_list;
	gchar *desktop_path;
	GError *error = NULL;
	gchar *trash_path;

	g_return_if_fail(filer_window != NULL);
	g_return_if_fail(GTK_IS_BOX(view_hbox));
	if (!filer_window->modern_mode)
		return;

	/* The Modern reference design has one clean navigation sidebar. */
	sidebar = gtk_scrolled_window_new(NULL, NULL);
	gtk_style_context_add_class(gtk_widget_get_style_context(sidebar), "rox-modern-sidebar");
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sidebar),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_size_request(sidebar, 250, -1);
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(sidebar), GTK_SHADOW_NONE);

	content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
	gtk_container_set_border_width(GTK_CONTAINER(content), 6);
	gtk_container_add(GTK_CONTAINER(sidebar), content);

	heading = gtk_label_new(_("Places"));
	{
		PangoAttrList *attrs = pango_attr_list_new();
		pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
		gtk_label_set_attributes(GTK_LABEL(heading), attrs);
		pango_attr_list_unref(attrs);
	}
	gtk_style_context_add_class(gtk_widget_get_style_context(heading), "rox-modern-sidebar-heading");
	gtk_label_set_xalign(GTK_LABEL(heading), 0.0);
	gtk_widget_set_margin_start(heading, 8);
	gtk_widget_set_margin_top(heading, 5);
	gtk_widget_set_margin_bottom(heading, 3);
	gtk_box_pack_start(GTK_BOX(content), heading, FALSE, FALSE, 0);

	list = gtk_list_box_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(list), "rox-modern-sidebar-list");
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
	gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(list), TRUE);
	gtk_widget_add_events(list, GDK_BUTTON_PRESS_MASK);
	g_signal_connect(list, "button-press-event", G_CALLBACK(modern_places_button_press), filer_window);
	gtk_box_pack_start(GTK_BOX(content), list, FALSE, FALSE, 0);

	places_add_path(list, "user-home", _("Home"), home_dir);
	desktop_path = desktop_dup_directory();
	places_add_path(list, "user-desktop", _("Desktop"), desktop_path);
	g_free(desktop_path);
	places_add_xdg(list, G_USER_DIRECTORY_DOCUMENTS, "folder-documents", "Documents");
	places_add_xdg(list, G_USER_DIRECTORY_DOWNLOAD, "folder-download", "Downloads");
	places_add_xdg(list, G_USER_DIRECTORY_MUSIC, "folder-music", "Music");
	places_add_xdg(list, G_USER_DIRECTORY_PICTURES, "folder-pictures", "Pictures");
	places_add_xdg(list, G_USER_DIRECTORY_VIDEOS, "folder-videos", "Videos");
	places_add_xdg(list, G_USER_DIRECTORY_TEMPLATES, "folder-templates", "Templates");

	trash_path = rox_trash_open_path(&error);
	if (trash_path) {
		places_add_path(list, "user-trash", _("Trash"), trash_path);
		g_free(trash_path);
	} else {
		g_clear_error(&error);
	}
	g_signal_connect(list, "row-activated", G_CALLBACK(places_row_activated), filer_window);

	bookmarks_list = gtk_list_box_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(bookmarks_list), "rox-modern-sidebar-list");
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(bookmarks_list), GTK_SELECTION_NONE);
	gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(bookmarks_list), TRUE);
	gtk_widget_add_events(bookmarks_list, GDK_BUTTON_PRESS_MASK);
	g_signal_connect(bookmarks_list, "button-press-event", G_CALLBACK(modern_places_button_press), filer_window);
	g_signal_connect(bookmarks_list, "row-activated", G_CALLBACK(places_row_activated), filer_window);
	gtk_box_pack_start(GTK_BOX(content), bookmarks_list, FALSE, FALSE, 0);
	filer_window->modern_bookmarks_list = bookmarks_list;
	modern_bookmarks_refresh(filer_window);

	devices_heading = gtk_label_new(_("Devices"));
	{
		PangoAttrList *attrs = pango_attr_list_new();
		pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
		gtk_label_set_attributes(GTK_LABEL(devices_heading), attrs);
		pango_attr_list_unref(attrs);
	}
	gtk_style_context_add_class(gtk_widget_get_style_context(devices_heading), "rox-modern-sidebar-heading");
	gtk_label_set_xalign(GTK_LABEL(devices_heading), 0.0);
	gtk_widget_set_margin_start(devices_heading, 8);
	gtk_widget_set_margin_top(devices_heading, 12);
	gtk_widget_set_margin_bottom(devices_heading, 3);
	gtk_box_pack_start(GTK_BOX(content), devices_heading, FALSE, FALSE, 0);

	devices_list = gtk_list_box_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(devices_list), "rox-modern-sidebar-list");
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(devices_list), GTK_SELECTION_SINGLE);
	gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(devices_list), TRUE);
	gtk_widget_add_events(devices_list, GDK_BUTTON_PRESS_MASK);
	g_signal_connect(devices_list, "row-activated", G_CALLBACK(devices_row_activated), filer_window);
	g_signal_connect(devices_list, "button-press-event", G_CALLBACK(devices_button_press), filer_window);
	gtk_box_pack_start(GTK_BOX(content), devices_list, FALSE, FALSE, 0);
	filer_window->modern_devices_list = devices_list;
	modern_ui_refresh_devices(filer_window);

	network_heading = gtk_label_new(_("Network"));
	{
		PangoAttrList *attrs = pango_attr_list_new();
		pango_attr_list_insert(attrs, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
		gtk_label_set_attributes(GTK_LABEL(network_heading), attrs);
		pango_attr_list_unref(attrs);
	}
	gtk_style_context_add_class(gtk_widget_get_style_context(network_heading), "rox-modern-sidebar-heading");
	gtk_label_set_xalign(GTK_LABEL(network_heading), 0.0);
	gtk_widget_set_margin_start(network_heading, 8);
	gtk_widget_set_margin_top(network_heading, 12);
	gtk_widget_set_margin_bottom(network_heading, 3);
	gtk_box_pack_start(GTK_BOX(content), network_heading, FALSE, FALSE, 0);

	network_list = gtk_list_box_new();
	gtk_style_context_add_class(gtk_widget_get_style_context(network_list), "rox-modern-sidebar-list");
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(network_list), GTK_SELECTION_NONE);
	gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(network_list), TRUE);
	{
		GtkWidget *network_row = places_row_new("network-workgroup", _("Browse Network"), NULL);
		g_object_set_data(G_OBJECT(network_row), "rox-modern-smb", GINT_TO_POINTER(1));
		gtk_container_add(GTK_CONTAINER(network_list), network_row);
	}
	g_signal_connect(network_list, "row-activated", G_CALLBACK(places_row_activated), filer_window);
	gtk_box_pack_start(GTK_BOX(content), network_list, FALSE, FALSE, 0);

	filer_window->modern_tree_view = NULL;

	{
		GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
		GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
		GtkWidget *content_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
		GKeyFile *kf = modern_session_load();
		gint sidebar_width = g_key_file_get_integer(kf, MODERN_SESSION_GROUP, "SidebarWidth", NULL);
		gint sidebar_layout_version = g_key_file_get_integer(kf, MODERN_SESSION_GROUP, "SidebarLayoutVersion", NULL);
		gboolean sidebar_visible = TRUE;
		if (g_key_file_has_key(kf, MODERN_SESSION_GROUP, "SidebarVisible", NULL))
			sidebar_visible = g_key_file_get_boolean(kf, MODERN_SESSION_GROUP, "SidebarVisible", NULL);
		gtk_paned_pack1(GTK_PANED(paned), sidebar, FALSE, FALSE);
		gtk_paned_pack2(GTK_PANED(paned), right, TRUE, FALSE);
		gtk_box_pack_start(GTK_BOX(view_hbox), paned, TRUE, TRUE, 0);
		if (sidebar_layout_version != 81)
			sidebar_width = 250;
		gtk_paned_set_position(GTK_PANED(paned),
			CLAMP(sidebar_width >= 190 ? sidebar_width : 250, 190, 420));
		if (filer_window->modern_tabbar)
			gtk_box_pack_start(GTK_BOX(right), filer_window->modern_tabbar, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(right), content_row, TRUE, TRUE, 0);
		filer_window->modern_paned = paned;
		filer_window->modern_content_box = content_row;
		filer_window->view_hbox = GTK_BOX(content_row);
		g_key_file_unref(kf);
		gtk_widget_show(paned);
		gtk_widget_show(content_row);
		gtk_widget_show(right);
		if (filer_window->modern_tabbar)
			gtk_widget_show_all(filer_window->modern_tabbar);
		if (sidebar_visible)
			gtk_widget_show_all(sidebar);
		else
			gtk_widget_hide(sidebar);
	}

	filer_window->modern_sidebar = sidebar;
	filer_window->modern_places_list = list;
	if (filer_window->modern_sidebar_menu_item)
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(filer_window->modern_sidebar_menu_item),
			gtk_widget_get_visible(sidebar));
	modern_ui_select_current_place(filer_window);
}

void modern_ui_update_path(FilerWindow *filer_window)
{
	FilerWindow *host;
	ModernSplitState *state;
	if (!filer_window || !filer_window->modern_mode)
		return;
	modern_filter_clear_for_navigation(filer_window);
	if (filer_window->modern_path_entry && filer_window->sym_path &&
	    g_strcmp0(gtk_entry_get_text(GTK_ENTRY(filer_window->modern_path_entry)),
		filer_window->sym_path) != 0)
		gtk_entry_set_text(GTK_ENTRY(filer_window->modern_path_entry), filer_window->sym_path);
	modern_breadcrumb_rebuild(filer_window);
	if (!filer_window->modern_embedded)
	{
		modern_tabs_sync_current(filer_window);
		modern_bookmarks_refresh(filer_window);
		modern_ui_select_current_place(filer_window);
	}
	else
	{
		host = g_object_get_data(G_OBJECT(filer_window->window), "rox-modern-split-host");
		state = host ? modern_split_state(host) : NULL;
		if (host && state && state->active == filer_window)
			modern_ui_select_current_place(host);
	}
}

void modern_ui_update_navigation(FilerWindow *filer_window)
{
	gboolean back;
	gboolean forward;
	FilerWindow *chrome = filer_window;
	ModernSplitState *state;

	if (!filer_window || !filer_window->modern_mode)
		return;
	if (filer_window->modern_embedded)
	{
		chrome = g_object_get_data(G_OBJECT(filer_window->window), "rox-modern-split-host");
		state = chrome ? modern_split_state(chrome) : NULL;
		if (!chrome || !state || state->active != filer_window)
			return;
	}

	back = filer_history_can_back(filer_window);
	forward = filer_history_can_forward(filer_window);
	if (chrome->modern_back)
		gtk_widget_set_sensitive(chrome->modern_back, back);
	if (chrome->modern_forward)
		gtk_widget_set_sensitive(chrome->modern_forward, forward);
	if (chrome->modern_menu_back)
		gtk_widget_set_sensitive(chrome->modern_menu_back, back);
	if (chrome->modern_menu_forward)
		gtk_widget_set_sensitive(chrome->modern_menu_forward, forward);
	if (chrome->modern_close) {
		if (toolbar_close_button_enabled())
			gtk_widget_show(chrome->modern_close);
		else
			gtk_widget_hide(chrome->modern_close);
	}
}
