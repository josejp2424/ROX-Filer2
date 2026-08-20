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

/* menu.c - code for handling the popup menus */


/*
 * Modificado por josejp2424 (2026):
 * port a GTK3 y adaptaciones específicas de esta versión.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <gio/gio.h>

#include "global.h"

#include "main.h"
#include "menu.h"
#include "run.h"
#include "action.h"
#include "filer.h"
#include "pixmaps.h"
#include "type.h"
#include "support.h"
#include "gui_support.h"
#include "options.h"
#include "choices.h"
#include "gtksavebox.h"
#include "mount.h"
#include "minibuffer.h"
#include "i18n.h"
#include "dir.h"
#include "diritem.h"
#include "appmenu.h"
#include "usericons.h"
#include "infobox.h"
#include "view_iface.h"
#include "display.h"
#include "bookmarks.h"
#include "panel.h"
#include "bulk_rename.h"
#include "xtypes.h"
#include "log.h"
#include "dnd.h"
#include "desktop.h"
#include "filer_pair.h"
#include "search_integration.h"
#include "xdg_apps.h"
#include "custom_actions.h"
#include "trash.h"

static gboolean input_trace_enabled(void)
{
	const gchar *value = g_getenv("ROX_TRACE_INPUT");
	return value && *value && strcmp(value, "0") != 0;
}

#define INPUT_TRACE(...) G_STMT_START { \
	if (input_trace_enabled()) { \
		g_printerr("ROX-INPUT menu: "); \
		g_printerr(__VA_ARGS__); \
		g_printerr("\n"); \
	} \
} G_STMT_END

typedef enum {
	FILE_DUPLICATE_ITEM,
	FILE_RENAME_ITEM,
	FILE_LINK_ITEM,
	FILE_OPEN_FILE,
	FILE_PROPERTIES,
	FILE_COPY_TO_BACKGROUNDS,
	FILE_RUN_ACTION,
	FILE_SET_ICON,
	FILE_TRASH,
	FILE_DELETE,
	FILE_USAGE,
	FILE_CHMOD_ITEMS,
	FILE_FIND,
	FILE_SET_TYPE,
	FILE_COPY_TO_CLIPBOARD,
	FILE_CUT_TO_CLIPBOARD,
#if defined(HAVE_GETXATTR) || defined(HAVE_ATTROPEN)
	FILE_XATTRS,
#endif
} FileOp;

typedef void (*ActionFn)(GList *paths,
			 const char *dest_dir, const char *leaf, int quiet);
typedef void MenuCallback(GtkWidget *widget, gpointer data);

typedef gboolean (*SaveCb)(GObject *savebox,
			   const gchar *current, const gchar *new);

typedef enum {
	TERMINAL_RUN_NONE = 0,
	TERMINAL_RUN_DIRECT,
	TERMINAL_RUN_SHELL,
	TERMINAL_RUN_BASH,
	TERMINAL_RUN_ASH,
	TERMINAL_RUN_DASH,
	TERMINAL_RUN_ZSH,
	TERMINAL_RUN_KSH,
	TERMINAL_RUN_FISH,
	TERMINAL_RUN_PYTHON,
	TERMINAL_RUN_PERL,
	TERMINAL_RUN_RUBY,
	TERMINAL_RUN_LUA,
	TERMINAL_RUN_TCL,
	TERMINAL_RUN_PHP,
	TERMINAL_RUN_NODE,
	TERMINAL_RUN_AWK,
	TERMINAL_RUN_SED,
	TERMINAL_RUN_SHEBANG,
	TERMINAL_RUN_APPIMAGE_NEEDS_EXEC
} TerminalRunMode;

GClosure	*new_xterm_here_closure = NULL;
GtkAccelGroup	*filer_keys = NULL;
static gboolean filer_keys_need_init = TRUE;

static GtkWidget *popup_menu = NULL;	/* Currently open menu */

static gint updating_menu = 0;		/* Non-zero => ignore activations */
static Option o_menu_iconsize, o_menu_xterm, o_menu_xterm_grave, o_menu_quick;
static Option o_menu_hide_unavailable;

/* clipboard targets */
static const GtkTargetEntry clipboard_targets[] = {
	{"text/uri-list", 0, TARGET_URI_LIST},
	{"x-special/gnome-copied-files", 0, TARGET_GNOME_COPIED_FILES},
};
static GtkClipboard *clipboard;
static const char *clipboard_action = NULL;
static GList *selected_paths = NULL;

/* Rox-Filer2 2.12.2-26: a Cut operation is represented visually in every
 * open filer view and on the native desktop.  Keep the rendering state in
 * one place: the clipboard owner maintained by menu.c. */
static void menu_clipboard_visuals_changed(void)
{
	GList *node;

	for (node = all_filer_windows; node; node = node->next)
	{
		FilerWindow *filer_window = node->data;
		if (filer_window && filer_window->view)
			gtk_widget_queue_draw(GTK_WIDGET(filer_window->view));
	}
	desktop_refresh_cut_state();
}

/* Static prototypes */

static void save_menus(void);
static void menu_closed(GtkWidget *widget);
static void shade_file_menu_items(gboolean shaded);
static GtkWidget *savebox_show(const gchar *action, const gchar *path,
			 MaskedPixmap *image, SaveCb callback,
			 GdkDragAction dnd_action);
static gint save_to_file(GObject *savebox,
			 const gchar *pathname, gpointer data);
static gboolean action_with_leaf(ActionFn action,
				 const gchar *current, const gchar *new);
static gboolean link_cb(GObject *savebox,
			const gchar *initial, const gchar *path);
static void select_nth_item(GtkMenuShell *shell, int n);
static void new_file_type(gchar *templ);
static GList *set_keys_button(Option *option, xmlNode *node, guchar *label);

/* Note that for most of these callbacks none of the arguments are used. */

static void view_type(gpointer data, guint action, GtkWidget *widget);

/* (action used in these three - DetailsType) */
static void change_size(gpointer data, guint action, GtkWidget *widget);
static void change_size_auto(gpointer data, guint action, GtkWidget *widget);
static void set_with(gpointer data, guint action, GtkWidget *widget);

static void set_sort(gpointer data, guint action, GtkWidget *widget);
static void reverse_sort(gpointer data, guint action, GtkWidget *widget);

static void filter_directories(gpointer data, guint action, GtkWidget *widget);
static void hidden(gpointer data, guint action, GtkWidget *widget);
static void show_thumbs(gpointer data, guint action, GtkWidget *widget);
static void refresh(gpointer data, guint action, GtkWidget *widget);
static void save_settings(gpointer data, guint action, GtkWidget *widget);

static void file_op(gpointer data, guint action, GtkWidget *widget);
/* Agregado por josejp2424 (2026): integración nativa para copiar imágenes
 * a /usr/share/backgrounds y seleccionarlas como wallpaper. */
static gboolean item_is_wallpaper_image(const DirItem *item);
static void copy_image_to_backgrounds(const gchar *source_path);

static void select_all(gpointer data, guint action, GtkWidget *widget);
static void clear_selection(gpointer data, guint action, GtkWidget *widget);
static void invert_selection(gpointer data, guint action, GtkWidget *widget);
static void new_directory(gpointer data, guint action, GtkWidget *widget);
static void new_file(gpointer data, guint action, GtkWidget *widget);
static void customise_new(gpointer data, guint action, GtkWidget *widget);
static void xterm_here(gpointer data, guint action, GtkWidget *widget);
/* Agregado por josejp2424: integración de terminal para carpetas,
 * binarios, scripts shell, Python y AppImage. */
static void open_terminal_selected(gpointer data, guint action, GtkWidget *widget);
static void run_in_terminal(gpointer data, guint action, GtkWidget *widget);
static void new_xterm_here(void);
static TerminalRunMode terminal_run_mode_for_item(const gchar *path,
						   const DirItem *item);
static gboolean spawn_terminal_runner(const gchar *working_dir,
					  const gchar *path,
					  TerminalRunMode mode);
static gboolean terminal_build_argv(gboolean execute_command,
				    const gchar *command_path,
				    GPtrArray **argv_out);
static gchar *terminal_create_runner(const gchar *path, const gchar *working_dir,
					 TerminalRunMode mode);
static void menu_options_changed(void);
static void search_current_folders(gpointer data, guint action, GtkWidget *widget);
static void choose_application_selected(gpointer data, guint action, GtkWidget *widget);
static void add_file_action_selected(gpointer data, guint action, GtkWidget *widget);
static void add_selected_bookmark(gpointer data, guint action, GtkWidget *widget);
static void restore_selected_from_trash(gpointer data, guint action, GtkWidget *widget);
static void open_paired_windows(gpointer data, guint action, GtkWidget *widget);
static void realign_paired_windows(gpointer data, guint action, GtkWidget *widget);

static void open_parent_same(gpointer data, guint action, GtkWidget *widget);
static void open_parent(gpointer data, guint action, GtkWidget *widget);
static void home_directory(gpointer data, guint action, GtkWidget *widget);
static void show_bookmarks(gpointer data, guint action, GtkWidget *widget);
static void show_log(gpointer data, guint action, GtkWidget *widget);
static void new_window(gpointer data, guint action, GtkWidget *widget);
/* static void new_user(gpointer data, guint action, GtkWidget *widget); */
static void close_window(gpointer data, guint action, GtkWidget *widget);
static void follow_symlinks(gpointer data, guint action, GtkWidget *widget);

/* (action used in this - MiniType) */
static void mini_buffer(gpointer data, guint action, GtkWidget *widget);
static void resize(gpointer data, guint action, GtkWidget *widget);

/* clipboard */
static void clipboard_get(GtkClipboard *clipboard, GtkSelectionData *selection_data, guint info, gpointer user_data);
static void clipboard_clear(GtkClipboard *clipboard, gpointer user_data);
static void paste_from_clipboard(gpointer data, guint action, GtkWidget *widget);

#define MENUS_NAME "menus2"

static GtkWidget	*filer_menu;		/* The popup filer menu */
static GtkWidget	*filer_file_item;	/* The File '' label */
static GtkWidget	*filer_file_menu;	/* The File '' menu */
static GtkWidget    *filer_paste_item;      /* Paste in the main menu */
static GtkWidget    *filer_file_cut_item;   /* Cut in quick file menu */
static GtkWidget    *filer_file_copy_item;  /* Copy in quick file menu */
static GtkWidget    *filer_file_paste_item; /* Paste in quick file menu */
static GtkWidget    *filer_add_bookmark_item;
static GtkWidget    *filer_restore_item;
static GtkWidget    *filer_move_to_trash_item;
static GtkWidget	*file_shift_item;	/* Shift Open label */
static GtkWidget    *filer_duplicate_item;
static GtkWidget    *filer_link_item;
static GtkWidget    *filer_shift_open_item;
static GtkWidget    *filer_set_run_action_item;
static GtkWidget    *filer_open_with_item;
static GtkWidget    *filer_set_icon_item;
static GtkWidget	*filer_auto_size_menu;	/* The Automatic item */
static GtkWidget	*filer_hidden_menu;	/* The Show Hidden item */
static GtkWidget	*filer_filter_dirs_menu;/* The Filter Dirs item */
static GtkWidget	*filer_reverse_menu;	/* The Reversed item */
static GtkWidget	*filer_thumb_menu;	/* The Show Thumbs item */
static GtkWidget	*filer_new_window;	/* The New Window item */
static GtkWidget    *filer_new_menu;        /* The New submenu */
static GtkWidget    *filer_follow_sym;      /* Follow symbolic links item */
static GtkWidget    *filer_set_type;        /* Set type item */
static GtkWidget    *filer_open_terminal_here; /* Open terminal in selected folder */
static GtkWidget    *filer_run_in_terminal;    /* Run selected executable/script */
static GtkWidget    *filer_copy_to_backgrounds; /* Copiar imagen y aplicar wallpaper */
static GtkWidget    *filer_search_item;
static GtkWidget    *filer_pair_open_item;
static GtkWidget    *filer_pair_realign_item;
#if defined(HAVE_GETXATTR) || defined(HAVE_ATTROPEN)
static GtkWidget	*filer_xattrs;	/* Extended attributes item */
#endif

typedef struct {
	GtkWidget *menu;
	GtkWidget *cut;
	GtkWidget *copy;
	GtkWidget *paste;
	GtkWidget *add_bookmark;
	GtkWidget *restore;
	GtkWidget *move_to_trash;
	GtkWidget *duplicate;
	GtkWidget *link;
	GtkWidget *shift_open;
	GtkWidget *shift_label;
	GtkWidget *set_run_action;
	GtkWidget *open_with;
	GtkWidget *set_icon;
	GtkWidget *set_type;
	GtkWidget *open_terminal_here;
	GtkWidget *run_in_terminal;
	GtkWidget *copy_to_backgrounds;
	GtkWidget *search;
#if defined(HAVE_GETXATTR) || defined(HAVE_ATTROPEN)
	GtkWidget *xattrs;
#endif
} FileContextWidgets;

static FileContextWidgets base_file_context;
static GtkWidget *transient_file_context_menu = NULL;

#undef N_
#define N_(x) x

static RoxItemFactoryEntry filer_menu_def[] = {
{N_("Display"),			NULL, NULL, 0, "<Branch>", "video-display"},
{">" N_("Icons View"),   	NULL, view_type, VIEW_TYPE_COLLECTION, "<IconItem>", "view-grid"},
{">" N_("Icons, With..."),	NULL, NULL, 0, "<Branch>", "view-list-details"},
{">>" N_("Sizes"),		NULL, set_with, DETAILS_SIZE, "<IconItem>", "view-list-details"},
{">>" N_("Permissions"),	NULL, set_with, DETAILS_PERMISSIONS, "<IconItem>", "dialog-password"},
{">>" N_("Types"),		NULL, set_with, DETAILS_TYPE, "<IconItem>", "text-x-generic"},
{">>" N_("Times"),		NULL, set_with, DETAILS_TIMES, "<IconItem>", "appointment-new"},
{">" N_("List View"),   	NULL, view_type, VIEW_TYPE_DETAILS, "<IconItem>", ROX_ICON_SHOW_DETAILS},
{">",				NULL, NULL, 0, "<Separator>"},
{">" N_("Bigger Icons"),   	"equal", change_size, 1, "<IconItem>", ROX_ICON_ZOOM_IN},
{">" N_("Smaller Icons"),   	"minus", change_size, -1, "<IconItem>", ROX_ICON_ZOOM_OUT},
{">" N_("Automatic"),   	NULL, change_size_auto, 0, "<ToggleItem>", ROX_ICON_ZOOM_FIT},
{">",				NULL, NULL, 0, "<Separator>"},
{">" N_("Sort by Name"),	NULL, set_sort, SORT_NAME, "<IconItem>", "view-sort-ascending"},
{">" N_("Sort by Type"),	NULL, set_sort, SORT_TYPE, "<IconItem>", "view-sort-ascending"},
{">" N_("Sort by Date (atime)"),	NULL, set_sort, SORT_DATEA, "<IconItem>", "view-sort-ascending"},
{">" N_("Sort by Date (ctime)"),	NULL, set_sort, SORT_DATEC, "<IconItem>", "view-sort-ascending"},
{">" N_("Sort by Date (mtime)"),	NULL, set_sort, SORT_DATEM, "<IconItem>", "view-sort-ascending"},
{">" N_("Sort by Size"),	NULL, set_sort, SORT_SIZE, "<IconItem>", "view-sort-ascending"},
{">" N_("Sort by Owner"),	NULL, set_sort, SORT_OWNER, "<IconItem>", "system-users"},
{">" N_("Sort by Group"),	NULL, set_sort, SORT_GROUP, "<IconItem>", "system-users"},
{">" N_("Reversed"),		NULL, reverse_sort, 0, "<ToggleItem>", "view-sort-descending"},
{">",				NULL, NULL, 0, "<Separator>"},
{">" N_("Show Hidden"),   	"<Ctrl>H", hidden, 0, "<ToggleItem>", ROX_ICON_SHOW_HIDDEN},
{">" N_("Filter Files..."),   	NULL, mini_buffer, MINI_FILTER, "<IconItem>", ROX_ICON_FIND},
{">" N_("Filter Directories With Files"),	NULL, filter_directories, 0, "<ToggleItem>", ROX_ICON_DIRECTORY},
{">" N_("Show Thumbnails"),	NULL, show_thumbs, 0, "<ToggleItem>", "image-x-generic"},
{">" N_("Refresh"),		"F5", refresh, 0, "<IconItem>", ROX_ICON_REFRESH},
{">" N_("Save Current Display Settings..."),	 NULL, save_settings, 0, "<IconItem>", ROX_ICON_SAVE},
{N_("File"),			NULL, NULL, 0, "<Branch>", "text-x-generic"},
/* Buscar en la carpeta seleccionada queda al comienzo del menú. Para archivos
 * no compatibles se oculta dinámicamente en show_filer_menu(). */
{">" N_("Search in This Folder..."), "<Ctrl>F", search_current_folders, 0, "<IconItem>", "edit-find"},
{">" N_("Add to Bookmarks"),	NULL, add_selected_bookmark, 0, "<IconItem>", ROX_ICON_BOOKMARKS},
{">",				NULL, NULL, 0, "<Separator>"},
/* Rox-Filer2: keep clipboard operations visible in the quick file menu too. */
{">" N_("Cut"),			NULL, file_op, FILE_CUT_TO_CLIPBOARD, "<IconItem>", ROX_ICON_CUT},
{">" N_("Copy"),			NULL, file_op, FILE_COPY_TO_CLIPBOARD, "<IconItem>", ROX_ICON_COPY},
{">" N_("Paste"),			NULL, paste_from_clipboard, 0, "<IconItem>", ROX_ICON_PASTE},
{">",				NULL, NULL, 0, "<Separator>"},
{">" N_("Duplicate..."),	"<Ctrl>D", file_op, FILE_DUPLICATE_ITEM, "<IconItem>", ROX_ICON_COPY},
{">" N_("Rename..."),		"F2", file_op, FILE_RENAME_ITEM, "<IconItem>", "document-edit"},
{">" N_("Link..."),		NULL, file_op, FILE_LINK_ITEM, "<IconItem>", ROX_ICON_SYMLINK},
/* Rox-Filer2: Restore is visible only while browsing the freedesktop Trash. */
{">" N_("Restore"),		NULL, restore_selected_from_trash, 0, "<IconItem>", "edit-undo"},
/* Modificado por josejp2424 (2026): Delete usa la papelera estándar y
 * Shift+Delete conserva el borrado permanente tradicional. */
{">" N_("Move to Trash"),	"Delete", file_op, FILE_TRASH, "<IconItem>", ROX_ICON_TRASH},
{">" N_("Delete Permanently..."), "<Shift>Delete", file_op, FILE_DELETE, "<IconItem>", ROX_ICON_DELETE},
{">",				NULL, NULL, 0, "<Separator>"},
{">" N_("Shift Open"),   	NULL, file_op, FILE_OPEN_FILE, "<IconItem>", ROX_ICON_OPEN},
/* Modificado por josejp2424 (2026): las aplicaciones MIME principales ya se
 * muestran directamente arriba. Este elemento abre siempre el selector XDG,
 * incluso cuando la caché MIME de la distribución está incompleta. */
{">" N_("Open With..."),	NULL, choose_application_selected, 0, "<IconItem>", ROX_ICON_EXECUTE},
{">" N_("Add File Action..."), NULL, add_file_action_selected, 0, "<IconItem>", "list-add"},
/* Agregado por josejp2424 (2026): visible sólo para imágenes compatibles y
 * situado inmediatamente debajo de Open With. */
{">" N_("Copy to Backgrounds..."), NULL, file_op, FILE_COPY_TO_BACKGROUNDS, "<IconItem>", "preferences-desktop-wallpaper"},
{">",				NULL, NULL, 0, "<Separator>"},
/* Agregado por josejp2424: opciones de terminal en el menú contextual. */
{">" N_("Open Terminal Here"),	NULL, open_terminal_selected, 0, "<IconItem>", ROX_ICON_TERMINAL},
{">" N_("Run in Terminal"),	NULL, run_in_terminal, 0, "<IconItem>", ROX_ICON_TERMINAL},
{">",				NULL, NULL, 0, "<Separator>"},
{">" N_("Set Default Application..."),	"asterisk", file_op, FILE_RUN_ACTION, "<IconItem>", ROX_ICON_EXECUTE},
{">" N_("Set Icon..."),		NULL, file_op, FILE_SET_ICON, "<IconItem>", "image-x-generic"},
#if defined(HAVE_GETXATTR) || defined(HAVE_ATTROPEN)
{">" N_("Extended attributes..."),		NULL, file_op, FILE_XATTRS, "<IconItem>", ROX_ICON_XATTR},
#endif
{">" N_("Properties"),		"<Ctrl>P", file_op, FILE_PROPERTIES, "<IconItem>", ROX_ICON_PROPERTIES},
{">" N_("Count"),		NULL, file_op, FILE_USAGE, "<IconItem>", "accessories-calculator"},
{">" N_("Set Type..."),		NULL, file_op, FILE_SET_TYPE, "<IconItem>", "text-x-generic"},
{">" N_("Permissions"),		NULL, file_op, FILE_CHMOD_ITEMS, "<IconItem>", "dialog-password"},
{">",				NULL, NULL, 0, "<Separator>"},
{N_("Select"),	    		NULL, NULL, 0, "<Branch>", ROX_ICON_SELECT},
{">" N_("Select All"),	    	"<Ctrl>A", select_all, 0, "<IconItem>", ROX_ICON_SELECT},
{">" N_("Clear Selection"),	NULL, clear_selection, 0, "<IconItem>", ROX_ICON_CLEAR},
{">" N_("Invert Selection"),	NULL, invert_selection, 0, "<IconItem>", ROX_ICON_SELECT},
{">" N_("Select by Name..."),	"period", mini_buffer, MINI_SELECT_BY_NAME, "<IconItem>", ROX_ICON_FIND},
{">" N_("Select If..."),	"<Shift>question", mini_buffer, MINI_SELECT_IF, "<IconItem>", ROX_ICON_FIND},
{N_("Options..."),		NULL, menu_show_options, 0, "<IconItem>", ROX_ICON_PREFERENCES},
{"",				NULL, NULL, 0, "<Separator>"},
{N_("Cut"),			"<Ctrl>X", file_op, FILE_CUT_TO_CLIPBOARD, "<IconItem>", ROX_ICON_CUT},
{N_("Copy"),			"<Ctrl>C", file_op, FILE_COPY_TO_CLIPBOARD, "<IconItem>", ROX_ICON_COPY},
{N_("Paste"),			"<Ctrl>V", paste_from_clipboard, 0, "<IconItem>", ROX_ICON_PASTE},
{"",				NULL, NULL, 0, "<Separator>"},
/* New debe seguir siendo un submenú; el icono se alinea en gui_support.c. */
{N_("New"),			NULL, NULL, 0, "<Branch>", ROX_ICON_ADD},
{">" N_("Directory"),		"Insert", new_directory, 0, "<IconItem>", "folder-new"},
{">" N_("Blank file"),		NULL, new_file, 0, "<IconItem>", ROX_ICON_NEW},
{">" N_("Customise Menu..."),	NULL, customise_new, 0, "<IconItem>", ROX_ICON_PREFERENCES},
{N_("Window"),			NULL, NULL, 0, "<Branch>", "window-new"},
{">" N_("Parent, New Window"), 	NULL, open_parent, 0, "<IconItem>", ROX_ICON_GO_UP},
{">" N_("Parent, Same Window"), NULL, open_parent_same, 0, "<IconItem>", ROX_ICON_GO_UP},
{">" N_("New Window"),		NULL, new_window, 0, "<IconItem>", "window-new"},
{">" N_("Home Directory"),	"<Ctrl>Home", home_directory, 0, "<IconItem>", ROX_ICON_HOME},
{">" N_("Show Bookmarks"),	"<Ctrl>B", show_bookmarks, 0, "<IconItem>", ROX_ICON_BOOKMARKS},
{">" N_("Show Log"),		NULL, show_log, 0, "<IconItem>", ROX_ICON_INFO},
{">" N_("Follow Symbolic Links"),	NULL, follow_symlinks, 0, "<IconItem>", ROX_ICON_SYMLINK},
{">" N_("Resize Window"),	"<Ctrl>E", resize, 0, "<IconItem>", "view-fullscreen"},
{">",				NULL, NULL, 0, "<Separator>"},
{">" N_("Open Paired Windows"), NULL, open_paired_windows, 0, "<IconItem>", "window-new"},
{">" N_("Realign Paired Windows"), NULL, realign_paired_windows, 0, "<IconItem>", "view-restore"},
/* {">" N_("New, As User..."),	NULL, new_user, 0, NULL}, */

{">" N_("Close Window"),	"<Ctrl>Q", close_window, 0, "<IconItem>", ROX_ICON_CLOSE},
{">",				NULL, NULL, 0, "<Separator>"},
{">" N_("Enter Path..."),	"slash", mini_buffer, MINI_PATH, "<IconItem>", ROX_ICON_JUMP_TO},
{">" N_("Shell Command..."),	"<Shift>exclam", mini_buffer, MINI_SHELL, "<IconItem>", ROX_ICON_TERMINAL},
{">" N_("Open Terminal Here"),	"F4", xterm_here, FALSE, "<IconItem>", ROX_ICON_TERMINAL},
{">" N_("Switch to Terminal"),	NULL, xterm_here, TRUE, "<IconItem>", ROX_ICON_TERMINAL},
};


#define GET_MENU_ITEM(var, menu)	\
		var = rox_item_factory_get_widget(item_factory,	"<" menu ">");

#define GET_SMENU_ITEM(var, menu, sub)	\
	do {				\
		tmp = g_strdup_printf("<" menu ">/%s", _(sub));		\
		var = rox_item_factory_get_widget(item_factory,	tmp); 	\
		g_free(tmp);		\
	} while (0)

#define GET_SSMENU_ITEM(var, menu, sub, subsub)	\
	do {				\
		tmp = g_strdup_printf("<" menu ">/%s/%s", _(sub), _(subsub)); \
		var = rox_item_factory_get_widget(item_factory,	tmp); 	\
		g_free(tmp);		\
	} while (0)

/* Returns TRUE if the keys were installed (first call only) */
/* Rox-Filer2 r102:
 * The historical File/Dir menu was created as a submenu by the ROX item
 * factory. Reusing that submenu directly as a root popup can leave stale
 * prelight/selected states on some X11/XLibre GTK3 themes. Build a genuine
 * standalone GtkMenu while keeping the original menu items and callbacks. */
static GtkWidget *menu_promote_submenu_to_root(GtkWidget *submenu)
{
	GtkWidget *root;
	GList *children, *iter;

	g_return_val_if_fail(GTK_IS_MENU(submenu), submenu);

	root = gtk_menu_new();
	children = gtk_container_get_children(GTK_CONTAINER(submenu));

	for (iter = children; iter; iter = iter->next)
	{
		GtkWidget *child = GTK_WIDGET(iter->data);

		g_object_ref(child);
		gtk_container_remove(GTK_CONTAINER(submenu), child);
		gtk_menu_shell_append(GTK_MENU_SHELL(root), child);
		g_object_unref(child);
	}
	g_list_free(children);

	return root;
}

static void file_context_capture_current(FileContextWidgets *ctx)
{
	g_return_if_fail(ctx != NULL);

	ctx->menu = filer_file_menu;
	ctx->cut = filer_file_cut_item;
	ctx->copy = filer_file_copy_item;
	ctx->paste = filer_file_paste_item;
	ctx->add_bookmark = filer_add_bookmark_item;
	ctx->restore = filer_restore_item;
	ctx->move_to_trash = filer_move_to_trash_item;
	ctx->duplicate = filer_duplicate_item;
	ctx->link = filer_link_item;
	ctx->shift_open = filer_shift_open_item;
	ctx->shift_label = file_shift_item;
	ctx->set_run_action = filer_set_run_action_item;
	ctx->open_with = filer_open_with_item;
	ctx->set_icon = filer_set_icon_item;
	ctx->set_type = filer_set_type;
	ctx->open_terminal_here = filer_open_terminal_here;
	ctx->run_in_terminal = filer_run_in_terminal;
	ctx->copy_to_backgrounds = filer_copy_to_backgrounds;
	ctx->search = filer_search_item;
#if defined(HAVE_GETXATTR) || defined(HAVE_ATTROPEN)
	ctx->xattrs = filer_xattrs;
#endif
}

static void file_context_apply(const FileContextWidgets *ctx)
{
	g_return_if_fail(ctx != NULL);

	filer_file_menu = ctx->menu;
	filer_file_cut_item = ctx->cut;
	filer_file_copy_item = ctx->copy;
	filer_file_paste_item = ctx->paste;
	filer_add_bookmark_item = ctx->add_bookmark;
	filer_restore_item = ctx->restore;
	filer_move_to_trash_item = ctx->move_to_trash;
	filer_duplicate_item = ctx->duplicate;
	filer_link_item = ctx->link;
	filer_shift_open_item = ctx->shift_open;
	file_shift_item = ctx->shift_label;
	filer_set_run_action_item = ctx->set_run_action;
	filer_open_with_item = ctx->open_with;
	filer_set_icon_item = ctx->set_icon;
	filer_set_type = ctx->set_type;
	filer_open_terminal_here = ctx->open_terminal_here;
	filer_run_in_terminal = ctx->run_in_terminal;
	filer_copy_to_backgrounds = ctx->copy_to_backgrounds;
	filer_search_item = ctx->search;
#if defined(HAVE_GETXATTR) || defined(HAVE_ATTROPEN)
	filer_xattrs = ctx->xattrs;
#endif
}

static GtkWidget *file_context_lookup(RoxItemFactory *factory,
				      const gchar *label)
{
	gchar *path;
	GtkWidget *widget;

	path = g_strdup_printf("<filer-context>/%s/%s", _("File"), _(label));
	widget = rox_item_factory_get_widget(factory, path);
	g_free(path);
	return widget;
}

static gboolean file_context_build_fresh(FileContextWidgets *ctx)
{
	RoxItemFactory *factory;
	RoxItemFactoryEntry *translated;
	GtkWidget *attach;
	gchar *file_path;
	guint n_entries;

	g_return_val_if_fail(ctx != NULL, FALSE);
	memset(ctx, 0, sizeof(*ctx));

	/* Build a genuinely new set of GtkMenuItems for every right-click.
	 * No accelerator group is attached to this private menu; the normal
	 * filer accelerator group remains owned by the persistent main menu. */
	factory = rox_item_factory_new(GTK_TYPE_MENU, "<filer-context>", NULL);
	n_entries = G_N_ELEMENTS(filer_menu_def);
	translated = translate_entries(filer_menu_def, n_entries);
	rox_item_factory_create_items(factory, n_entries, translated, NULL);
	free_translated_entries(translated, n_entries);

	file_path = g_strdup_printf("<filer-context>/%s", _("File"));
	ctx->menu = rox_item_factory_get_widget(factory, file_path);
	g_free(file_path);
	if (!GTK_IS_MENU(ctx->menu)) {
		rox_item_factory_free(factory);
		memset(ctx, 0, sizeof(*ctx));
		return FALSE;
	}

	/* Keep the File submenu alive while the disposable factory destroys the
	 * unused Display/Select/Window/main-menu widgets around it. */
	g_object_ref(ctx->menu);
	attach = gtk_menu_get_attach_widget(GTK_MENU(ctx->menu));
	if (GTK_IS_MENU_ITEM(attach))
		gtk_menu_item_set_submenu(GTK_MENU_ITEM(attach), NULL);

	ctx->cut = file_context_lookup(factory, "Cut");
	ctx->copy = file_context_lookup(factory, "Copy");
	ctx->paste = file_context_lookup(factory, "Paste");
	ctx->add_bookmark = file_context_lookup(factory, "Add to Bookmarks");
	ctx->restore = file_context_lookup(factory, "Restore");
	ctx->move_to_trash = file_context_lookup(factory, "Move to Trash");
	ctx->duplicate = file_context_lookup(factory, "Duplicate...");
	ctx->link = file_context_lookup(factory, "Link...");
	ctx->shift_open = file_context_lookup(factory, "Shift Open");
	ctx->set_run_action = file_context_lookup(factory, "Set Default Application...");
	ctx->open_with = file_context_lookup(factory, "Open With...");
	ctx->set_icon = file_context_lookup(factory, "Set Icon...");
	ctx->set_type = file_context_lookup(factory, "Set Type...");
	ctx->open_terminal_here = file_context_lookup(factory, "Open Terminal Here");
	ctx->run_in_terminal = file_context_lookup(factory, "Run in Terminal");
	ctx->copy_to_backgrounds = file_context_lookup(factory, "Copy to Backgrounds...");
	ctx->search = file_context_lookup(factory, "Search in This Folder...");
#if defined(HAVE_GETXATTR) || defined(HAVE_ATTROPEN)
	ctx->xattrs = file_context_lookup(factory, "Extended attributes...");
#endif
	ctx->shift_label = menu_item_get_label_widget(ctx->shift_open);

	/* Hidden-by-default entries must survive show_all(), exactly like the
	 * persistent menu created in ensure_filer_menu(). */
	gtk_widget_set_no_show_all(ctx->copy_to_backgrounds, TRUE);
	gtk_widget_set_no_show_all(ctx->open_with, TRUE);
	gtk_widget_set_no_show_all(ctx->open_terminal_here, TRUE);
	gtk_widget_set_no_show_all(ctx->run_in_terminal, TRUE);
	gtk_widget_set_no_show_all(ctx->search, TRUE);
	gtk_widget_set_no_show_all(ctx->add_bookmark, TRUE);
	gtk_widget_set_no_show_all(ctx->restore, TRUE);
	gtk_widget_set_no_show_all(ctx->move_to_trash, TRUE);

	gtk_widget_hide(ctx->copy_to_backgrounds);
	gtk_widget_hide(ctx->open_with);
	gtk_widget_hide(ctx->add_bookmark);
	gtk_widget_hide(ctx->restore);

	rox_item_factory_free(factory);
	return TRUE;
}

static void file_context_destroy_transient(GtkWidget *menu)
{
	if (!menu)
		return;

	file_context_apply(&base_file_context);
	transient_file_context_menu = NULL;

	gtk_widget_destroy(menu);
	g_object_unref(menu);
}

gboolean ensure_filer_menu(void)
{
	GList			*items;
	guchar			*tmp;
	GtkWidget		*item;
	RoxItemFactory  	*item_factory;

	if (!filer_keys_need_init)
		return FALSE;
	filer_keys_need_init = FALSE;

	item_factory = menu_create(filer_menu_def,
		sizeof(filer_menu_def) / sizeof(*filer_menu_def),
		"<filer>", filer_keys);

	GET_MENU_ITEM(filer_menu, "filer");
	GET_SMENU_ITEM(filer_file_menu, "filer", "File");
	GET_SMENU_ITEM(filer_paste_item, "filer", "Paste");
	GET_SSMENU_ITEM(filer_file_cut_item, "filer", "File", "Cut");
	GET_SSMENU_ITEM(filer_file_copy_item, "filer", "File", "Copy");
	GET_SSMENU_ITEM(filer_file_paste_item, "filer", "File", "Paste");
	GET_SSMENU_ITEM(filer_duplicate_item, "filer", "File", "Duplicate...");
	GET_SSMENU_ITEM(filer_link_item, "filer", "File", "Link...");
	GET_SSMENU_ITEM(filer_shift_open_item, "filer", "File", "Shift Open");
	GET_SSMENU_ITEM(filer_set_run_action_item, "filer", "File", "Set Default Application...");
	GET_SSMENU_ITEM(filer_open_with_item, "filer", "File", "Open With...");
	GET_SSMENU_ITEM(filer_set_icon_item, "filer", "File", "Set Icon...");
	GET_SSMENU_ITEM(filer_hidden_menu, "filer", "Display", "Show Hidden");
	GET_SSMENU_ITEM(filer_filter_dirs_menu, "filer", "Display", "Filter Directories With Files");
	GET_SSMENU_ITEM(filer_reverse_menu, "filer", "Display", "Reversed");
	GET_SSMENU_ITEM(filer_auto_size_menu, "filer", "Display", "Automatic");
	GET_SSMENU_ITEM(filer_thumb_menu, "filer", "Display",
							"Show Thumbnails");
	GET_SSMENU_ITEM(item, "filer", "File", "Set Type...");
	filer_set_type = item; /* Modificado por josejp2424: conservar el GtkMenuItem GTK3 */
	GET_SSMENU_ITEM(item, "filer", "File", "Open Terminal Here");
	filer_open_terminal_here = item;
	GET_SSMENU_ITEM(item, "filer", "File", "Run in Terminal");
	filer_run_in_terminal = item;
	GET_SSMENU_ITEM(item, "filer", "File", "Copy to Backgrounds...");
	filer_copy_to_backgrounds = item;
	GET_SSMENU_ITEM(item, "filer", "File", "Search in This Folder...");
	filer_search_item = item;
	GET_SSMENU_ITEM(filer_add_bookmark_item, "filer", "File", "Add to Bookmarks");
	GET_SSMENU_ITEM(filer_restore_item, "filer", "File", "Restore");
	GET_SSMENU_ITEM(filer_move_to_trash_item, "filer", "File", "Move to Trash");
	GET_SSMENU_ITEM(item, "filer", "Window", "Open Paired Windows");
	filer_pair_open_item = item;
	GET_SSMENU_ITEM(item, "filer", "Window", "Realign Paired Windows");
	filer_pair_realign_item = item;
	/* Modificado por josejp2424 (2026): show_popup_menu() usa show_all();
	 * no-show-all permite ocultar esta acción para archivos no compatibles. */
	gtk_widget_set_no_show_all(filer_copy_to_backgrounds, TRUE);
	gtk_widget_set_no_show_all(filer_open_with_item, TRUE);
	gtk_widget_hide(filer_open_with_item);
	gtk_widget_hide(filer_copy_to_backgrounds);
	gtk_widget_set_no_show_all(filer_open_terminal_here, TRUE);
	gtk_widget_set_no_show_all(filer_run_in_terminal, TRUE);
	gtk_widget_set_no_show_all(filer_search_item, TRUE);
	gtk_widget_set_no_show_all(filer_add_bookmark_item, TRUE);
	gtk_widget_set_no_show_all(filer_restore_item, TRUE);
	gtk_widget_set_no_show_all(filer_move_to_trash_item, TRUE);
	gtk_widget_hide(filer_add_bookmark_item);
	gtk_widget_hide(filer_restore_item);
	gtk_widget_set_no_show_all(filer_pair_open_item, TRUE);
	gtk_widget_set_no_show_all(filer_pair_realign_item, TRUE);

#if defined(HAVE_GETXATTR) || defined(HAVE_ATTROPEN)
	GET_SSMENU_ITEM(item, "filer", "File", "Extended attributes...");
	filer_xattrs = item; /* Modificado por josejp2424: conservar el GtkMenuItem GTK3 */
#endif

	GET_SMENU_ITEM(filer_new_menu, "filer", "New");
	GET_SSMENU_ITEM(item, "filer", "Window", "Follow Symbolic Links");
	filer_follow_sym = item; /* Modificado por josejp2424: conservar el GtkMenuItem GTK3 */

	/* File '' label... */
	items = gtk_container_get_children(GTK_CONTAINER(filer_menu));
	filer_file_item = menu_item_get_label_widget(GTK_WIDGET(g_list_nth(items, 1)->data));
	g_list_free(items);

	/* Promote the old File/Dir submenu to a real root popup.
	 * The GtkMenuItems themselves are moved, so callbacks, accelerators and
	 * references such as filer_restore_item remain unchanged. */
	{
		GtkWidget *file_parent = gtk_widget_get_parent(filer_file_item);

		if (GTK_IS_MENU_ITEM(file_parent))
		{
			GtkWidget *old_file_menu = filer_file_menu;
			GtkWidget *standalone;

			g_object_ref(old_file_menu);
			standalone = menu_promote_submenu_to_root(old_file_menu);
			gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_parent), NULL);
			gtk_widget_set_no_show_all(file_parent, TRUE);
			gtk_widget_hide(file_parent);
			filer_file_menu = standalone;
			g_object_unref(old_file_menu);
		}
	}

	/* Shift Open... label: obtenerlo por ruta, sin depender del orden del menú. */
	file_shift_item = menu_item_get_label_widget(filer_shift_open_item);

	/* Keep the persistent File menu only as the canonical/base widget set.
	 * Item context popups use disposable, freshly-created widgets. */
	file_context_capture_current(&base_file_context);

	GET_SSMENU_ITEM(item, "filer", "Window", "New Window");
	filer_new_window = item; /* Modificado por josejp2424: conservar el GtkMenuItem GTK3 */

	g_signal_connect(filer_menu, "selection-done",
			G_CALLBACK(menu_closed), NULL);
	g_signal_connect(filer_file_menu, "selection-done",
			G_CALLBACK(menu_closed), NULL);

	if (o_menu_xterm_grave.int_value)
	{
		new_xterm_here_closure = g_cclosure_new(G_CALLBACK(new_xterm_here),
												FALSE, NULL);
		gtk_accel_group_connect(filer_keys, gdk_keyval_from_name("grave"),
			0, 0, new_xterm_here_closure);
	}

	g_signal_connect(filer_keys, "accel_changed",
			G_CALLBACK(save_menus), NULL);

	return TRUE;
}

void menu_init(void)
{
	char			*menurc;

	menurc = choices_find_xdg_path_load(MENUS_NAME, PROJECT, SITE);
	if (menurc)
	{
		gtk_accel_map_load(menurc);
		g_free(menurc);
	}

	/* Rox-Filer2: one terminal preference is shared by Open Terminal Here,
	 * Run in Terminal and the desktop Console icon. An empty preference means
	 * automatic selection: defaultterminal, x-terminal-emulator, xterm, urxvt. */
	option_add_string(&o_menu_xterm, "menu_xterm", "");
	option_add_int(&o_menu_xterm_grave, "menu_xterm_grave", TRUE);
	option_add_int(&o_menu_iconsize, "menu_iconsize", MIS_SMALL);
	option_add_int(&o_menu_quick, "menu_quick", TRUE);
	option_add_int(&o_menu_hide_unavailable, "menu_hide_unavailable", TRUE);
	option_add_saver(save_menus);

	option_register_widget("menu-set-keys", set_keys_button);

	filer_keys = gtk_accel_group_new();

	option_add_notify(menu_options_changed);

	clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
}

/* Name is in the form "<panel>" */
RoxItemFactory *menu_create(RoxItemFactoryEntry *def, int n_entries,
			    const gchar *name, GtkAccelGroup *keys)
{
	RoxItemFactory  	*item_factory;
	RoxItemFactoryEntry	*translated;

	if (!keys)
	{
		keys = gtk_accel_group_new();
		gtk_accel_group_lock(keys);
	}

	item_factory = rox_item_factory_new(GTK_TYPE_MENU, name, keys);

	translated = translate_entries(def, n_entries);
	rox_item_factory_create_items(item_factory, n_entries,
					translated, NULL);
	free_translated_entries(translated, n_entries);

	return item_factory;
}

/* Prevent the user from setting a short-cut on this item */
static void menuitem_no_shortcuts(GtkWidget *item)
{
	/* XXX */
#if 0
	GtkMenuItem *menuitem = GTK_MENU_ITEM(item);

	_gtk_widget_set_accel_path(item, NULL, NULL);
	null_g_free(&menuitem->accel_path);
#endif
}

/* Shade items that only work on single files */
static void shade_file_menu_items(gboolean shaded)
{
	if (filer_duplicate_item)
		gtk_widget_set_sensitive(filer_duplicate_item, !shaded);
	if (filer_link_item)
		gtk_widget_set_sensitive(filer_link_item, !shaded);
	if (filer_shift_open_item)
		gtk_widget_set_sensitive(filer_shift_open_item, !shaded);
	if (filer_set_run_action_item)
		gtk_widget_set_sensitive(filer_set_run_action_item, !shaded);
	if (filer_set_icon_item)
		gtk_widget_set_sensitive(filer_set_icon_item, !shaded);
#if defined(HAVE_GETXATTR) || defined(HAVE_ATTROPEN)
	if (filer_xattrs)
		gtk_widget_set_sensitive(filer_xattrs, !shaded);
#endif
}

/* 'data' is an array of three ints:
 * [ pointer_x, pointer_y, item_under_pointer ]
 */
void position_menu(GtkMenu *menu, gint *x, gint *y,
		   gboolean  *push_in, gpointer data)
{
	int		*pos = (int *) data;
	GtkRequisition	minimum, natural;
	GList		*items, *next;
	int		y_shift = 0;
	int		item = pos[2];

	next = items = gtk_container_get_children(GTK_CONTAINER(menu));

	while (item >= 0 && next)
	{
		GtkRequisition child_minimum, child_natural;
		int h;

		gtk_widget_get_preferred_size(GTK_WIDGET(next->data),
					      &child_minimum, &child_natural);
		h = MAX(child_minimum.height, child_natural.height);

		if (item > 0)
			y_shift += h;
		else
			y_shift += h / 2;

		next = next->next;
		item--;
	}

	g_list_free(items);

	gtk_widget_get_preferred_size(GTK_WIDGET(menu), &minimum, &natural);

	*x = pos[0] - (natural.width * 7 / 8);
	*y = pos[1] - y_shift;

	*x = CLAMP(*x, 0, MAX(0, screen_width - natural.width));
	*y = CLAMP(*y, 0, MAX(0, screen_height - natural.height));

	*push_in = FALSE;
}

static GtkWidget *make_directory_menu_item(DirItem *ditem, const char *label,
				MenuIconStyle style)
{
	GtkWidget *item;

	if (style != MIS_NONE && di_image(ditem))
	{
		GdkPixbuf *pixbuf;
		MaskedPixmap *image;

		image = di_image(ditem);

		switch (style)
		{
			case MIS_LARGE:
				pixbuf = image->pixbuf;
				break;
			default:
				if (!image->sm_pixbuf)
					pixmap_make_small(image);
				pixbuf = image->sm_pixbuf;
				break;
		}

		item = menu_item_new_with_pixbuf(label, pixbuf);
		/* TODO: Find a way to allow short-cuts */
		menuitem_no_shortcuts(item);
		gtk_widget_show_all(item);
	}
	else
		item = gtk_menu_item_new_with_label(label);

	return item;
}

typedef struct {
	CallbackFn func;
	gchar *path;
	FilerWindow *target_filer;
} MenuDirActivation;

static void menu_dir_activation_free(gpointer data, GClosure *closure)
{
	MenuDirActivation *activation = data;
	(void)closure;
	if (!activation)
		return;
	g_free(activation->path);
	g_free(activation);
}

static void menu_dir_activate_for_filer(GtkMenuItem *item, gpointer data)
{
	MenuDirActivation *activation = data;
	FilerWindow *previous;
	(void)item;

	if (!activation || !activation->func)
		return;
	previous = window_with_focus;
	window_with_focus = activation->target_filer;
	activation->func(activation->path);
	window_with_focus = previous;
}

static GList *menu_from_dir(GtkWidget *menu, GHashTable *menu_entries,
			    const gchar *dir_name,
			    MenuIconStyle style, CallbackFn func,
			    gboolean separator, gboolean strip_ext,
			    gboolean recurse, FilerWindow *target_filer)
{
	GList *widgets = NULL;
	DirItem *ditem;
	int i;
	GtkWidget *item;
	char *dname = NULL;
	GPtrArray *names;

	dname = pathdup(dir_name);

	names = list_dir(dname);
	if (!names)
		goto out;

	for (i = 0; i < names->len; i++)
	{
		char	*leaf = names->pdata[i];
		gchar	*fname;

		if (separator)
		{
			item = gtk_menu_item_new();
			widgets = g_list_append(widgets, item);
			if (menu)
				gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
			separator = FALSE;
		}

		fname = g_strconcat(dname, "/", leaf, NULL);

		/* Strip off extension, if any */
		if (strip_ext)
		{
			char	*dot;
			dot = strchr(leaf, '.');
			if (dot)
				*dot = '\0';
		}

		if (menu_entries && g_hash_table_contains(menu_entries, leaf))
		{
			g_free(fname);
			g_free(leaf);
			continue;
		}

		ditem = diritem_new((const guchar *) "");
		diritem_restat((const guchar *) fname, ditem, NULL);

		item = make_directory_menu_item(ditem, leaf, style);

		if (menu_entries)
			g_hash_table_add(menu_entries, g_strdup(leaf));

		g_free(leaf);

		/* If it is a directory (but NOT an AppDir) and we are
		 * recursing then set up a sub menu.
		 */
		if (recurse && ditem->base_type == TYPE_DIRECTORY &&
			   !(ditem->flags & ITEM_FLAG_APPDIR))
		{
			GtkWidget *sub;
			GList *new_widgets;

			sub = rox_menu_new();
			new_widgets = menu_from_dir(sub, menu_entries, fname, style, func,
						separator, strip_ext, TRUE, target_filer);
			g_list_free(new_widgets);
			gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), sub);
		}
		else if (target_filer)
		{
			MenuDirActivation *activation = g_new0(MenuDirActivation, 1);
			activation->func = func;
			activation->path = fname;
			activation->target_filer = target_filer;
			g_signal_connect_data(item, "activate",
				G_CALLBACK(menu_dir_activate_for_filer), activation,
				menu_dir_activation_free, 0);
			fname = NULL;
		}
		else
			g_signal_connect_swapped(item, "activate",
					G_CALLBACK(func), fname);

		diritem_free(ditem);

		if (menu)
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		if (fname)
			g_signal_connect_swapped(item, "destroy",
					G_CALLBACK(g_free), fname);

		widgets = g_list_append(widgets, item);
	}

	g_ptr_array_free(names, TRUE);
out:
	g_free(dname);

	return widgets;
}

/* Scan the user template directory and the templates bundled with ROX-Filer.
 * User templates are added first and override bundled templates with the same
 * visible name. This keeps the New menu useful on a fresh installation while
 * still allowing complete user customisation. */
static void update_new_files_menu(MenuIconStyle style)
{
	static GList *widgets = NULL;
	gchar *user_dir = NULL;
	gchar *bundled_dir = NULL;
	GHashTable *entries;
	gboolean need_separator = TRUE;
	GList *added;

	if (widgets)
	{
		GList *next;
		for (next = widgets; next; next = next->next)
			gtk_widget_destroy((GtkWidget *) next->data);
		g_list_free(widgets);
		widgets = NULL;
	}

	entries = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	user_dir = choices_find_xdg_path_load("Templates", "", SITE);
	if (user_dir)
	{
		added = menu_from_dir(filer_new_menu, entries, user_dir, style,
				(CallbackFn) new_file_type, need_separator, TRUE, FALSE, NULL);
		if (added)
			need_separator = FALSE;
		widgets = g_list_concat(widgets, added);
	}

	bundled_dir = g_build_filename(app_dir, "Templates", NULL);
	if (g_file_test(bundled_dir, G_FILE_TEST_IS_DIR))
	{
		added = menu_from_dir(filer_new_menu, entries, bundled_dir, style,
				(CallbackFn) new_file_type, need_separator, TRUE, FALSE, NULL);
		widgets = g_list_concat(widgets, added);
	}

	g_hash_table_destroy(entries);
	g_free(bundled_dir);
	g_free(user_dir);
	gtk_widget_show_all(filer_new_menu);
}

/* 'item' is the number of the item to appear under the pointer. */
void show_popup_menu(GtkWidget *menu, GdkEvent *event, int item)
{
	g_return_if_fail(GTK_IS_MENU(menu));

	gtk_widget_show_all(menu);
	gtk_menu_shell_deselect(GTK_MENU_SHELL(menu));
	gtk_menu_popup_at_pointer(GTK_MENU(menu), event);
	if (item >= 0)
		select_nth_item(GTK_MENU_SHELL(menu), item);
}

/* Hide the popup menu, if any */
void menu_popdown(void)
{
	if (popup_menu)
		gtk_menu_popdown(GTK_MENU(popup_menu));
}

static MenuIconStyle get_menu_icon_style(void)
{
	MenuIconStyle mis;
	int display;

	mis = o_menu_iconsize.int_value;

	switch (mis)
	{
		case MIS_NONE: case MIS_SMALL: case MIS_LARGE:
			return mis;
		default:
			break;
	}

	if (mis == MIS_CURRENT && window_with_focus)
	{
		switch (window_with_focus->display_style)
		{
			case HUGE_ICONS:
			case LARGE_ICONS:
				return MIS_LARGE;
			case SMALL_ICONS:
				return MIS_SMALL;
			default:
				break;
		}
	}

	display = o_display_size.int_value;
	switch (display)
	{
		case HUGE_ICONS:
		case LARGE_ICONS:
			return MIS_LARGE;
		case SMALL_ICONS:
			return MIS_SMALL;
		default:
			break;
	}

	return MIS_SMALL;
}

static void clipboardcb(
		GtkClipboard *clipboard,
		GtkSelectionData *data,
		gpointer p)
{
	if (gtk_selection_data_get_length(data) > 0)
	{
		gtk_widget_set_sensitive(filer_paste_item, TRUE);
		if (filer_file_paste_item)
			gtk_widget_set_sensitive(filer_file_paste_item, TRUE);
	}
	else if (GPOINTER_TO_INT(p))
		gtk_clipboard_request_contents(
				clipboard, text_uri_list, clipboardcb, GUINT_TO_POINTER(0));
}

/* iter->peek() is the clicked item, or NULL if none */
void show_filer_menu(FilerWindow *filer_window, GdkEvent *event, ViewIter *iter)
{
	DirItem		*file_item = NULL;
	int		n_selected;
	int             n_added = 0;
	gboolean        item_context;
	gboolean        in_trash;
	FileContextWidgets transient_context;

	g_return_if_fail(event != NULL);

	n_selected = view_count_selected(filer_window->view);

	INPUT_TRACE("show_filer_menu enter event_type=%d selected=%d iter_item=%s",
		event->type, n_selected,
		(iter && iter->peek(iter)) ? (const char *) iter->peek(iter)->leafname : "<none>");

	ensure_filer_menu();
	INPUT_TRACE("menu widgets root=%p file=%p",
		(void *) filer_menu, (void *) filer_file_menu);

	updating_menu++;

	/* Remove dynamic MIME tools and previous AppMenu, if any. */
	xdg_apps_remove_mime_tools();
	appmenu_remove();

	window_with_focus = filer_window;

	/* Right-clicking an unselected item must act on that item, not on a
	 * previous selection somewhere else in the view. Preserve a deliberate
	 * multi-selection only when the clicked item is already part of it. */
	item_context = iter && iter->peek(iter) != NULL;
	if (item_context && !view_get_selected(filer_window->view, iter))
		view_select_only(filer_window->view, iter);
	if (event->type == GDK_KEY_PRESS && n_selected > 0)
		item_context = TRUE;

	if (item_context)
	{
		if (file_context_build_fresh(&transient_context))
		{
			transient_file_context_menu = transient_context.menu;
			file_context_apply(&transient_context);
			g_signal_connect(transient_file_context_menu, "selection-done",
				G_CALLBACK(menu_closed), NULL);
			INPUT_TRACE("fresh file context menu=%p",
				(void *) transient_file_context_menu);
		}
		else
		{
			/* Extremely defensive fallback: keep the persistent menu usable
			 * if fresh construction ever fails. */
			memset(&transient_context, 0, sizeof(transient_context));
			file_context_apply(&base_file_context);
		}
	}

	filer_window->temp_item_selected = FALSE;
	n_selected = view_count_selected(filer_window->view);
	in_trash = rox_trash_filer_is_trash(filer_window);

	/* Cut/Copy are valid for one or many selected items, but not for an
	 * empty background click.  Keep the quick file menu consistent with the
	 * top-level Ctrl+X/Ctrl+C actions. */
	if (filer_file_cut_item)
		gtk_widget_set_sensitive(filer_file_cut_item, n_selected > 0);
	if (filer_file_copy_item)
		gtk_widget_set_sensitive(filer_file_copy_item, n_selected > 0);

	/* Determine whether to shade "Paste" without depending on its
	 * numeric position in the main menu. */
	gtk_widget_set_sensitive(filer_paste_item, FALSE);
	if (filer_file_paste_item)
		gtk_widget_set_sensitive(filer_file_paste_item, FALSE);
	gtk_clipboard_request_contents(
			clipboard, gnome_copied_files, clipboardcb, GUINT_TO_POINTER(1));


	{
		GtkWidget	*file_label;
		GString		*buffer;
		DirItem		*item;

		file_label = filer_file_item;
		gtk_check_menu_item_set_active(
				GTK_CHECK_MENU_ITEM(filer_thumb_menu),
				filer_window->show_thumbs);
		gtk_check_menu_item_set_active(
				GTK_CHECK_MENU_ITEM(filer_hidden_menu),
				filer_window->show_hidden);
		gtk_check_menu_item_set_active(
				GTK_CHECK_MENU_ITEM(filer_filter_dirs_menu),
				filer_window->filter_directories);
		gtk_check_menu_item_set_active(
				GTK_CHECK_MENU_ITEM(filer_reverse_menu),
				filer_window->sort_order != GTK_SORT_ASCENDING);
		gtk_check_menu_item_set_active(
			GTK_CHECK_MENU_ITEM(filer_auto_size_menu),
			filer_window->display_style_wanted == AUTO_SIZE_ICONS);
		buffer = g_string_new(NULL);

		if (o_menu_hide_unavailable.int_value)
		{
			gtk_widget_hide(filer_open_terminal_here);
			gtk_widget_hide(filer_run_in_terminal);
		}
		else
		{
			gtk_widget_show(filer_open_terminal_here);
			gtk_widget_show(filer_run_in_terminal);
			gtk_widget_set_sensitive(filer_open_terminal_here, FALSE);
			gtk_widget_set_sensitive(filer_run_in_terminal, FALSE);
		}
		gtk_widget_hide(filer_copy_to_backgrounds);
		gtk_widget_hide(filer_open_with_item);
		gtk_widget_hide(filer_search_item);
		gtk_widget_hide(filer_add_bookmark_item);
		gtk_widget_hide(filer_restore_item);
		if (in_trash)
			gtk_widget_hide(filer_move_to_trash_item);
		else
			gtk_widget_show(filer_move_to_trash_item);
		gtk_widget_hide(filer_pair_open_item);
		gtk_widget_hide(filer_pair_realign_item);
		if (filer_pair_is_enabled()) {
			gtk_widget_show(filer_pair_open_item);
			gtk_widget_show(filer_pair_realign_item);
		}
		if (search_integration_available(filer_window))
		{
			gtk_menu_item_set_label(GTK_MENU_ITEM(filer_search_item),
				n_selected > 1 ? _("Search in Selected Folders...") :
				_("Search in This Folder..."));
			gtk_widget_show(filer_search_item);
		}

		/* Applications associated with the MIME type are inserted directly at
		 * the start of this menu below. Open With remains a normal, reliable
		 * command which opens the complete chooser for files and directories. */
		if (n_selected > 0)
			gtk_widget_show(filer_open_with_item);

		switch (n_selected)
		{
			case 0:
				g_string_assign(buffer, _("Next Click"));
				shade_file_menu_items(FALSE);
				break;
			case 1:
				item = filer_selected_item(filer_window);
				if (item->base_type == TYPE_UNKNOWN)
					dir_update_item(filer_window->directory,
							item->leafname);
				shade_file_menu_items(FALSE);
				file_item = filer_selected_item(filer_window);
				if (item_is_wallpaper_image(file_item))
					gtk_widget_show(filer_copy_to_backgrounds);
				if (file_item && file_item->base_type == TYPE_DIRECTORY &&
				    !in_trash)
					gtk_widget_show(filer_add_bookmark_item);
				if (in_trash) {
					gtk_menu_item_set_label(GTK_MENU_ITEM(filer_restore_item), _("Restore"));
					gtk_widget_show(filer_restore_item);
				}
				{
					const gchar *terminal_path = (const gchar *) make_path(
						filer_window->sym_path, file_item->leafname);
					if (file_item->base_type == TYPE_DIRECTORY ||
					    g_file_test(terminal_path, G_FILE_TEST_IS_DIR))
					{
						gtk_widget_show(filer_open_terminal_here);
						gtk_widget_set_sensitive(filer_open_terminal_here, TRUE);
					}
					else if (terminal_run_mode_for_item(terminal_path, file_item) != TERMINAL_RUN_NONE)
					{
						gtk_widget_show(filer_run_in_terminal);
						gtk_widget_set_sensitive(filer_run_in_terminal, TRUE);
					}
				}
				g_string_printf(buffer, _("%s '%s'"),
					basetype_name(file_item),
					g_utf8_validate(file_item->leafname,
							-1, NULL)
						? file_item->leafname
						: _("(bad utf-8)"));
				if (!can_set_run_action(file_item) && filer_set_run_action_item)
					gtk_widget_set_sensitive(filer_set_run_action_item, FALSE);
				break;
			default:
				shade_file_menu_items(TRUE);
				g_string_printf(buffer, _("%d items"),
						 n_selected);
				if (in_trash) {
					gtk_menu_item_set_label(GTK_MENU_ITEM(filer_restore_item),
						_("Restore Selected"));
					gtk_widget_show(filer_restore_item);
				}
				break;
		}
		gtk_label_set_text(GTK_LABEL(file_label), buffer->str);
		g_string_free(buffer, TRUE);

		menu_show_shift_action(file_shift_item, file_item,
					n_selected == 0);
		if (file_item)
			n_added = appmenu_add(make_path(filer_window->sym_path,
							file_item->leafname),
						file_item, filer_file_menu);

		/* Keep MIME applications and matching file actions in the foreground,
		 * as in classic ROX, while using XDG/GIO underneath. */
		if (n_selected > 0)
		{
			GList *mime_paths = filer_selected_items(filer_window);
			n_added += xdg_apps_add_mime_tools(filer_file_menu, mime_paths,
				GTK_WINDOW(filer_window->window));
			destroy_glist(&mime_paths);
		}
	}

	update_new_files_menu(get_menu_icon_style());

	gtk_widget_set_sensitive(filer_new_window,
			!o_unique_filer_windows.int_value);
	gtk_widget_set_sensitive(filer_follow_sym,
		strcmp(filer_window->sym_path, filer_window->real_path) != 0);
	gtk_widget_set_sensitive(filer_set_type,
				 xattr_supported(filer_window->real_path));
#if defined(HAVE_GETXATTR) || defined(HAVE_ATTROPEN)
	gtk_widget_set_sensitive(filer_xattrs,
				xattr_supported(filer_window->real_path) &&
				n_selected <= 1 &&
				(file_item ? access((const char *) make_path(filer_window->real_path,
						file_item->leafname), R_OK) : 0) == 0);
#endif

	/* One visible context menu only. Item popups use a freshly-created
	 * GtkMenu/GtkMenuItem tree; background clicks keep the persistent main
	 * filer menu. */
	popup_menu = item_context ? filer_file_menu : filer_menu;

	updating_menu--;

	INPUT_TRACE("popup chosen=%p quick=%d n_added=%d",
		(void *) popup_menu, o_menu_quick.int_value, n_added);
	show_popup_menu(popup_menu, event, -1);
}

static void menu_closed(GtkWidget *widget)
{
	gboolean transient_context;

	if (window_with_focus == NULL || widget != popup_menu)
		return;			/* Close panel item chosen? */

	transient_context = (widget == transient_file_context_menu);
	popup_menu = NULL;

	if (window_with_focus->temp_item_selected)
	{
		view_clear_selection(window_with_focus->view);
		window_with_focus->temp_item_selected = FALSE;
	}

	/* Dynamic items belong to the current popup; destroy them before the
	 * transient GtkMenu itself. */
	xdg_apps_remove_mime_tools();
	appmenu_remove();

	if (transient_context)
		file_context_destroy_transient(widget);
}

static void target_callback(FilerWindow *filer_window,
			ViewIter *iter,
			gpointer action)
{
	g_return_if_fail(filer_window != NULL);

	window_with_focus = filer_window;

	/* Don't grab the primary selection */
	filer_window->temp_item_selected = TRUE;

	view_wink_item(filer_window->view, iter);
	view_select_only(filer_window->view, iter);
	file_op(NULL, GPOINTER_TO_INT(action), NULL);

	view_clear_selection(filer_window->view);
	filer_window->temp_item_selected = FALSE;
}

/* Set the text of the 'Shift Open...' menu item.
 * If icon is NULL, reset the text and also shade it, unless 'next'.
 */
void menu_show_shift_action(GtkWidget *menu_item, DirItem *item, gboolean next)
{
	guchar		*shift_action = NULL;

	if (item)
	{
		if (item->flags & ITEM_FLAG_MOUNT_POINT)
		{
			if (item->flags & ITEM_FLAG_MOUNTED)
				shift_action = N_("Unmount");
			else
				shift_action = N_("Open unmounted");
		}
		else if (item->flags & ITEM_FLAG_SYMLINK)
			shift_action = N_("Show Target");
		else if (item->base_type == TYPE_DIRECTORY)
			shift_action = N_("Look Inside");
		else if (item->base_type == TYPE_FILE)
			shift_action = N_("Open As Text");
	}
	gtk_label_set_text(GTK_LABEL(menu_item),
			shift_action ? _(shift_action)
				     : _("Shift Open"));
	gtk_widget_set_sensitive(menu_item, shift_action != NULL || next);
}

/* Actions */

static void view_type(gpointer data, guint action, GtkWidget *widget)
{
	ViewType view_type = (ViewType) action;

	g_return_if_fail(window_with_focus != NULL);

	if (view_type == VIEW_TYPE_COLLECTION)
		display_set_layout(window_with_focus,
				window_with_focus->display_style_wanted,
				DETAILS_NONE, FALSE);

	filer_set_view_type(window_with_focus, (ViewType) action);
}

static void change_size(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	display_change_size(window_with_focus, action == 1);
}

static void change_size_auto(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	if (updating_menu)
		return;

	if (window_with_focus->display_style_wanted == AUTO_SIZE_ICONS)
	{
		DisplayStyle actual = window_with_focus->display_style;
		display_set_layout(window_with_focus, actual,
				   window_with_focus->details_type, FALSE);
		display_set_default_size(actual);
	}
	else
	{
		display_set_layout(window_with_focus, AUTO_SIZE_ICONS,
				   window_with_focus->details_type, FALSE);
		display_set_default_size(AUTO_SIZE_ICONS);
	}
}

static void set_with(gpointer data, guint action, GtkWidget *widget)
{
	DisplayStyle size;

	g_return_if_fail(window_with_focus != NULL);

	size = window_with_focus->display_style_wanted;

	filer_set_view_type(window_with_focus, VIEW_TYPE_COLLECTION);
	display_set_layout(window_with_focus, size, action, FALSE);
}

static void set_sort(gpointer data, guint action, GtkWidget *widget)
{
	if (updating_menu)
		return;

	g_return_if_fail(window_with_focus != NULL);

	display_set_sort_type(window_with_focus, action, GTK_SORT_ASCENDING);
}

static void reverse_sort(gpointer data, guint action, GtkWidget *widget)
{
	GtkSortType order;

	if (updating_menu)
		return;

	g_return_if_fail(window_with_focus != NULL);

	order = window_with_focus->sort_order;
	if (order == GTK_SORT_ASCENDING)
		order = GTK_SORT_DESCENDING;
	else
		order = GTK_SORT_ASCENDING;

	display_set_sort_type(window_with_focus, window_with_focus->sort_type,
			      order);
}

static void hidden(gpointer data, guint action, GtkWidget *widget)
{
	if (updating_menu)
		return;

	g_return_if_fail(window_with_focus != NULL);

	display_set_hidden(window_with_focus,
			   !window_with_focus->show_hidden);
}

static void filter_directories(gpointer data, guint action, GtkWidget *widget)
{
	if (updating_menu)
		return;

	g_return_if_fail(window_with_focus != NULL);

	display_set_filter_directories(window_with_focus,
			   !window_with_focus->filter_directories);
}

static void show_thumbs(gpointer data, guint action, GtkWidget *widget)
{
	if (updating_menu)
		return;

	g_return_if_fail(window_with_focus != NULL);

	display_set_thumbs(window_with_focus, !window_with_focus->show_thumbs);
}

static void refresh(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_refresh(window_with_focus);
}

static void save_settings(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_save_settings(window_with_focus);
}

/* Agregado por josejp2424 (2026): acciones separadas para papelera y
 * borrado permanente, manteniendo funciones pequeñas al estilo de ROX. */
static void move_to_trash(FilerWindow *filer_window)
{
	GList *paths = filer_selected_items(filer_window);
	action_trash(paths);
	destroy_glist(&paths);
}

static void delete_permanently(FilerWindow *filer_window)
{
	GList *paths = filer_selected_items(filer_window);
	action_delete_permanently(paths);
	destroy_glist(&paths);
}

static void usage(FilerWindow *filer_window)
{
	GList *paths;
	paths = filer_selected_items(filer_window);
	action_usage(paths);
	destroy_glist(&paths);
}

static void chmod_items(FilerWindow *filer_window)
{
	GList *paths;
	paths = filer_selected_items(filer_window);
	action_chmod(paths, FALSE, NULL);
	destroy_glist(&paths);
}

static void set_type_items(FilerWindow *filer_window)
{
	GList *paths, *p;
	int npass=0, nfail=0;

	paths = filer_selected_items(filer_window);
	for(p=paths; p; p=g_list_next(p)) {
		if(xattr_supported((const char *) p->data))
			npass++;
		else
			nfail++;
	}
	if(npass==0)
		report_error(_("Extended attributes, used to store types, are not supported for this "
				"file or files.\n"
				"This may be due to lack of support from the filesystem or the C library, "
				"or it may simply be that the filesystem needs to be mounted with "
				"the right mount option ('user_xattr' on Linux)."));
	else if(nfail>0)
		report_error(_("Setting type not supported for some of these files"));
	if(npass>0)
		action_settype(paths, FALSE, NULL);
	destroy_glist(&paths);
}

static void find(FilerWindow *filer_window)
{
	GList *paths;
	paths = filer_selected_items(filer_window);
	action_find(paths);
	destroy_glist(&paths);
}

static gboolean last_symlink_check_relative = TRUE;

/* This creates a new savebox widget, and allows the user to pick a new path
 * for the file.
 * Once the new path has been picked, the callback will be called with
 * both the current and new paths.
 * NOTE: This function unrefs 'image'!
 */
static GtkWidget *savebox_show(const gchar *action, const gchar *path,
			 MaskedPixmap *image, SaveCb callback,
			 GdkDragAction dnd_action)
{
	GtkWidget *savebox = NULL;
	GtkWidget *check_relative = NULL;

	g_return_val_if_fail(image != NULL, NULL);

	savebox = gtk_savebox_new(action);
	gtk_savebox_set_action(GTK_SAVEBOX(savebox), dnd_action);

	if (callback == link_cb)
	{
		check_relative = gtk_check_button_new_with_mnemonic(
							_("_Relative link"));
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_relative),
					     last_symlink_check_relative);

		gtk_widget_set_can_focus(check_relative, FALSE); /* Modificado por josejp2424: GTK3 */
		gtk_widget_set_tooltip_text(check_relative, _("If on, the symlink will store the path from the "
			"symlink to the target file. Use this if the symlink "
			"and the target will be moved together.\n"
			"If off, the path from the root directory is stored - "
			"use this if the symlink may move but the target will "
			"stay put."));
		gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(savebox))),
				check_relative, FALSE, TRUE, 0);
		gtk_widget_show(check_relative);
	}

	g_signal_connect(savebox, "save_to_file",
				G_CALLBACK(save_to_file), NULL);

	g_object_set_data_full(G_OBJECT(savebox), "current_path",
				g_strdup(path), g_free);
	g_object_set_data(G_OBJECT(savebox), "action_callback", callback);
	g_object_set_data(G_OBJECT(savebox), "check_relative", check_relative);

	gtk_window_set_title(GTK_WINDOW(savebox), action);
	/* En algunos gestores de ventanas ligeros el diálogo de renombrado podía
	 * abrirse detrás de la ventana del filer y parecer que no existía. Mantenerlo
	 * asociado a la ventana activa y presentarlo explícitamente. */
	if (window_with_focus && GTK_IS_WINDOW(window_with_focus->window))
	{
		gtk_window_set_transient_for(GTK_WINDOW(savebox),
			GTK_WINDOW(window_with_focus->window));
		gtk_window_set_destroy_with_parent(GTK_WINDOW(savebox), TRUE);
		gtk_window_set_position(GTK_WINDOW(savebox), GTK_WIN_POS_CENTER_ON_PARENT);
	}

	if (g_utf8_validate(path, -1, NULL))
		gtk_savebox_set_pathname(GTK_SAVEBOX(savebox), path);
	else
	{
		gchar *u8, *dir, *base;
		dir = g_path_get_dirname(path);
		base = g_path_get_basename(path);
		u8 = to_utf8(base);
		gtk_savebox_set_pathname(GTK_SAVEBOX(savebox),
				make_path(dir, u8));
		g_free(u8);
		g_free(dir);
		g_free(base);
	}
	gtk_savebox_set_icon(GTK_SAVEBOX(savebox), image->pixbuf);
	g_object_unref(image);

	gtk_widget_show(savebox);
	gtk_window_present(GTK_WINDOW(savebox));
	return savebox;
}

static gint save_to_file(GObject *savebox,
			 const gchar *pathname, gpointer data)
{
	SaveCb		callback;
	const gchar	*current_path;

	callback = g_object_get_data(savebox, "action_callback");
	current_path = g_object_get_data(savebox, "current_path");

	g_return_val_if_fail(callback != NULL, GTK_XDS_SAVE_ERROR);
	g_return_val_if_fail(current_path != NULL, GTK_XDS_SAVE_ERROR);

	return callback(savebox, current_path, pathname)
			? GTK_XDS_SAVED : GTK_XDS_SAVE_ERROR;
}

static gboolean copy_cb(GObject *savebox,
			const gchar *current, const gchar *new)
{
	return action_with_leaf(action_copy, current, new);
}

static gboolean action_with_leaf(ActionFn action,
				 const gchar *current, const gchar *new)
{
	const char	*leaf;
	char		*new_dir;
	GList		*local_paths;

	if (new[0] != '/')
	{
		report_error(_("New pathname is not absolute"));
		return FALSE;
	}

	if (new[strlen(new) - 1] == '/')
	{
		new_dir = g_strdup(new);
		leaf = NULL;
	}
	else
	{
		const gchar *slash;

		slash = strrchr(new, '/');
		new_dir = g_strndup(new, slash - new);
		leaf = slash + 1;
	}

	local_paths = g_list_append(NULL, (gchar *) current);
	action(local_paths, new_dir, leaf, -1);
	g_list_free(local_paths);

	g_free(new_dir);

	return TRUE;
}

/* Open a savebox to act on the selected file.
 * Call 'callback' later to perform the operation.
 */
static void src_dest_action_item(const gchar *path, MaskedPixmap *image,
			 const gchar *action, SaveCb callback,
			 GdkDragAction dnd_action)
{
	g_object_ref(image);
	savebox_show(action, path, image, callback, dnd_action);
}

static gboolean rename_cb(GObject *savebox,
			  const gchar *current, const gchar *new)
{
	gboolean result;
	(void)savebox;
	rox_debug_log("RENAME", "current=%s new=%s",
		current ? current : "", new ? new : "");
	result = action_with_leaf(action_move, current, new);
	rox_debug_log("RENAME", "result=%s", result ? "ok" : "failed");
	return result;
}

static gboolean link_cb(GObject *savebox,
			const gchar *initial, const gchar *path)
{
	GtkToggleButton *check_relative;
	struct stat info;
	int	err;
	gchar	*link_path;

	check_relative = g_object_get_data(savebox, "check_relative");

	last_symlink_check_relative = gtk_toggle_button_get_active(check_relative);

	if (last_symlink_check_relative)
		link_path = get_relative_path(path, initial);
	else
		link_path = g_strdup(initial);

	if (mc_lstat(path, &info) == 0 && S_ISLNK(info.st_mode))
	{
		GtkWidget *box, *button;
		gint ans;

		box = gtk_message_dialog_new(NULL, 0, GTK_MESSAGE_QUESTION,
				GTK_BUTTONS_CANCEL,
				_("Symlink from '%s' already exists. "
				"Replace it with a link to '%s'?"),
				path, link_path);

		gtk_window_set_position(GTK_WINDOW(box), GTK_WIN_POS_CENTER);

		button = button_new_mixed(ROX_ICON_YES, _("_Replace"));
		gtk_widget_show(button);
		gtk_widget_set_can_default(button, TRUE); /* Modificado por josejp2424: GTK3 */
		gtk_dialog_add_action_widget(GTK_DIALOG(box),
					     button, GTK_RESPONSE_OK);
		gtk_dialog_set_default_response(GTK_DIALOG(box),
						GTK_RESPONSE_OK);

		ans = gtk_dialog_run(GTK_DIALOG(box));
		gtk_widget_destroy(box);

		if (ans != GTK_RESPONSE_OK)
		{
			g_free(link_path);
			return FALSE;
		}

		unlink(path);
	}

	err = symlink(link_path, path);
	g_free(link_path);

	if (err)
	{
		report_error("symlink: %s", g_strerror(errno));
		return FALSE;
	}

	dir_check_this(path);

	return TRUE;
}

static void run_action(DirItem *item)
{
	if (can_set_run_action(item))
		type_set_handler_dialog(item->mime_type);
	else
		report_error(
			_("You can only set the default application for a "
			"regular file"));
}

void open_home(gpointer data, guint action, GtkWidget *widget)
{
	filer_opendir(home_dir, NULL, NULL);
}

static void select_all(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	window_with_focus->temp_item_selected = FALSE;
	view_select_all(window_with_focus->view);
}

static void clear_selection(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	window_with_focus->temp_item_selected = FALSE;
	view_clear_selection(window_with_focus->view);
}

static gboolean invert_cb(ViewIter *iter, gpointer data)
{
	return !view_get_selected((ViewIface *) data, iter);
}

static void invert_selection(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	window_with_focus->temp_item_selected = FALSE;

	view_select_if(window_with_focus->view, invert_cb,
		       window_with_focus->view);
}

void menu_show_options(gpointer data, guint action, GtkWidget *widget)
{
	GtkWidget *win;

	win = options_show();

	if (win)
	{
		number_of_windows++;
		g_signal_connect(win, "destroy",
				G_CALLBACK(one_less_window), NULL);
	}
}

static void new_savebox_remember_filer(GtkWidget *savebox, FilerWindow *target)
{
	/* Only remember real filer windows.  The desktop New menu uses a tiny
	 * synthetic target which is intentionally destroyed with the menu. */
	if (savebox && target && filer_exists(target))
		g_object_set_data(G_OBJECT(savebox), "rox-new-filer", target);
}

static FilerWindow *new_savebox_filer(GObject *savebox)
{
	FilerWindow *target;
	target = savebox ? g_object_get_data(savebox, "rox-new-filer") : NULL;
	return target && filer_exists(target) ? target : NULL;
}

static gboolean new_directory_cb(GObject *savebox,
				 const gchar *initial, const gchar *path)
{
	if (mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO))
	{
		report_error("mkdir: %s", g_strerror(errno));
		return FALSE;
	}

	dir_check_this(path);

	{
		FilerWindow *target = new_savebox_filer(savebox);
		if (target)
		{
			gchar *leaf = strrchr(path, '/');
			if (leaf)
				display_set_autoselect(target, leaf + 1);
		}
	}

	return TRUE;
}

void show_new_directory(FilerWindow *filer_window)
{
	window_with_focus = filer_window;
	new_directory(NULL, 0, NULL);
}

static void new_directory(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	{
		FilerWindow *target = window_with_focus;
		GtkWidget *savebox = savebox_show(_("Create"),
			make_path(target->sym_path, _("NewDir")),
			type_to_icon(inode_directory), new_directory_cb,
			GDK_ACTION_COPY);
		new_savebox_remember_filer(savebox, target);
	}
}

static gboolean new_file_cb(GObject *savebox,
			    const gchar *initial, const gchar *path)
{
	int fd;

	fd = open(path, O_CREAT | O_EXCL, 0666);

	if (fd == -1)
	{
		report_error(_("Error creating '%s': %s"),
				path, g_strerror(errno));
		return FALSE;
	}

	if (close(fd))
		report_error(_("Error creating '%s': %s"),
				path, g_strerror(errno));

	dir_check_this(path);

	{
		FilerWindow *target = new_savebox_filer(savebox);
		if (target)
		{
			gchar *leaf = strrchr(path, '/');
			if (leaf)
				display_set_autoselect(target, leaf + 1);
		}
	}

	return TRUE;
}

void show_new_file(FilerWindow *filer_window)
{
	window_with_focus = filer_window;
	new_file(NULL, 0, NULL);
}

static void new_file(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	{
		FilerWindow *target = window_with_focus;
		GtkWidget *savebox = savebox_show(_("Create"),
			make_path(target->sym_path, _("NewFile")),
			type_to_icon(text_plain), new_file_cb, GDK_ACTION_COPY);
		new_savebox_remember_filer(savebox, target);
	}
}

static gboolean new_file_type_cb(GObject *savebox,
			         const gchar *initial, const gchar *path)
{
	const gchar *leaf;
	const gchar *template_path;
	gchar *rtempl, *dest, *base;
	GList *paths;

	/* The exact selected template is stored on the savebox. This supports
	 * both user templates and the templates bundled in ROX-Filer. */
	template_path = g_object_get_data(savebox, "template_path");
	if (!template_path || !g_file_test(template_path, G_FILE_TEST_EXISTS))
	{
		base = g_path_get_basename(initial);
		report_error(
		_("Error creating file: could not find the template for %s"),
				base);
		g_free(base);
		return FALSE;
	}
	rtempl = pathdup(template_path);

	base = g_path_get_basename(path);
	dest = g_path_get_dirname(path);
	leaf = base;
	paths = g_list_append(NULL, rtempl);

	action_copy(paths, dest, leaf, TRUE);

	g_list_free(paths);
	g_free(dest);
	g_free(rtempl);

	{
		FilerWindow *target = new_savebox_filer(savebox);
		if (target)
			display_set_autoselect(target, leaf);
	}
	g_free(base);

	return TRUE;
}

static void new_file_type(gchar *templ)
{
	const gchar *leaf;
	MIME_type *type;
	gchar *base;

	g_return_if_fail(window_with_focus != NULL);

	base = g_path_get_basename(templ);
	leaf = base;
	type = type_get_type(templ);

	{
		FilerWindow *target = window_with_focus;
		GtkWidget *savebox = savebox_show(_("Create"),
			make_path(target->sym_path, leaf),
			type_to_icon(type),
			new_file_type_cb, GDK_ACTION_COPY);
		if (savebox)
		{
			g_object_set_data_full(G_OBJECT(savebox), "template_path",
				g_strdup(templ), g_free);
			new_savebox_remember_filer(savebox, target);
		}
	}
	g_free(base);
}

static void new_menu_directory_activate(GtkMenuItem *item, gpointer data)
{
	FilerWindow *previous = window_with_focus;
	(void)item;
	show_new_directory((FilerWindow *)data);
	window_with_focus = previous;
}

static void new_menu_file_activate(GtkMenuItem *item, gpointer data)
{
	FilerWindow *previous = window_with_focus;
	(void)item;
	show_new_file((FilerWindow *)data);
	window_with_focus = previous;
}

static void new_menu_customise_activate(GtkMenuItem *item, gpointer data)
{
	(void)item;
	(void)data;
	customise_new(NULL, 0, NULL);
}

GtkWidget *create_menu_new(FilerWindow *filer_window)
{
	GtkWidget *menu;
	GtkWidget *item;
	GHashTable *entries;
	gchar *user_dir;
	gchar *bundled_dir;
	GList *added;
	gboolean separator = TRUE;

	menu = rox_menu_new();

	item = menu_item_new_with_icon(_("Directory"), "folder-new");
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	g_signal_connect(item, "activate",
		G_CALLBACK(new_menu_directory_activate), filer_window);

	item = menu_item_new_with_icon(_("Blank file"), ROX_ICON_NEW);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	g_signal_connect(item, "activate",
		G_CALLBACK(new_menu_file_activate), filer_window);

	/* Rox-Filer2 2.12.2-11: keep the toolbar New menu in exactly the same
	 * order as the context-menu New submenu: built-in actions first, then
	 * the separator and template entries. */
	item = menu_item_new_with_icon(_("Customise Menu..."),
		ROX_ICON_PREFERENCES);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	g_signal_connect(item, "activate",
		G_CALLBACK(new_menu_customise_activate), filer_window);

	entries = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	user_dir = choices_find_xdg_path_load("Templates", "", SITE);
	if (user_dir)
	{
		added = menu_from_dir(menu, entries, user_dir,
			get_menu_icon_style(), (CallbackFn)new_file_type,
			separator, TRUE, FALSE, filer_window);
		if (added)
			separator = FALSE;
		g_list_free(added);
	}
	bundled_dir = g_build_filename(app_dir, "Templates", NULL);
	if (g_file_test(bundled_dir, G_FILE_TEST_IS_DIR))
	{
		added = menu_from_dir(menu, entries, bundled_dir,
			get_menu_icon_style(), (CallbackFn)new_file_type,
			separator, TRUE, FALSE, filer_window);
		g_list_free(added);
	}
	g_hash_table_destroy(entries);
	g_free(bundled_dir);
	g_free(user_dir);

	gtk_widget_show_all(menu);
	return menu;
}

static void new_menu_fake_filer_free(gpointer data)
{
	FilerWindow *fake = data;

	if (!fake)
		return;
	if (window_with_focus == fake)
		window_with_focus = NULL;
	g_free(fake->sym_path);
	g_free(fake->real_path);
	g_free(fake);
}

/* Rox-Filer2 2.12.2-15: the native desktop has no FilerWindow, but it should
 * expose exactly the same New menu and Templates support as a filer window.
 * A tiny target object supplies the destination path to the existing New
 * callbacks without opening a hidden filer window. */
GtkWidget *create_menu_new_for_path(const gchar *path, GtkWindow *parent)
{
	FilerWindow *fake;
	GtkWidget *menu;

	g_return_val_if_fail(path != NULL, NULL);
	fake = g_new0(FilerWindow, 1);
	fake->window = parent ? GTK_WIDGET(parent) : NULL;
	fake->sym_path = g_strdup(path);
	fake->real_path = pathdup(path);

	menu = create_menu_new(fake);
	g_object_set_data_full(G_OBJECT(menu), "rox-new-target-filer", fake,
	                       new_menu_fake_filer_free);
	return menu;
}

GtkWidget *prepare_menu_new(FilerWindow *filer_window)
{
	window_with_focus = filer_window;
	ensure_filer_menu();
	update_new_files_menu(get_menu_icon_style());
	return filer_new_menu;
}

void show_menu_new(FilerWindow *filer_window)
{
	GtkWidget *menu = prepare_menu_new(filer_window);
	show_popup_menu(menu, NULL, 1);
}

static void customise_new(gpointer data, guint action, GtkWidget *widget)
{
	(void) data;
	(void) action;
	(void) widget;
	GPtrArray	*path;
	guchar		*save;
	GString		*dirs;
	int		i;

	dirs = g_string_new(NULL);

	path = choices_list_xdg_dirs("", SITE);
	for (i = 0; i < path->len; i++)
	{
		guchar *old = (guchar *) path->pdata[i];

		g_string_append(dirs, old);
		g_string_append(dirs, "/Templates\n");
	}
	choices_free_list(path);

	save = choices_find_xdg_path_save("", "Templates", SITE, TRUE);
	if (save)
		mkdir(save, 0777);

	info_message(
		_("Any files placed in your Templates directories will "
		"appear on the `New' menu. Choosing one of them will make "
		"a copy of it as the new file.\n\n"
		"The following directories contain templates:\n\n%s\n%s\n"),
		dirs->str,
		save ? _("I'll show you your Templates directory now; you "
			 "should place any template files you want inside it.")
		     : _("Your CHOICESPATH variable setting prevents "
			 "customisations - sorry."));

	g_string_free(dirs, TRUE);

	if (save)
		filer_opendir(save, NULL, NULL);
}


static void choose_application_selected(gpointer data, guint action, GtkWidget *widget)
{
	GList *paths;

	(void)data; (void)action; (void)widget;
	if (!window_with_focus)
		return;

	paths = filer_selected_items(window_with_focus);
	if (!paths)
		return;
	xdg_apps_choose_for_paths(paths, GTK_WINDOW(window_with_focus->window), FALSE);
	destroy_glist(&paths);
}

static void add_file_action_selected(gpointer data, guint action, GtkWidget *widget)
{
	GList *paths;

	(void)data; (void)action; (void)widget;
	if (!window_with_focus)
		return;

	paths = filer_selected_items(window_with_focus);
	if (!paths)
		return;
	custom_actions_add_for_paths(paths, GTK_WINDOW(window_with_focus->window));
	destroy_glist(&paths);
}

static void add_selected_bookmark(gpointer data, guint action, GtkWidget *widget)
{
	DirItem *item;
	const gchar *path;

	(void) data;
	(void) action;
	(void) widget;

	if (!window_with_focus ||
	    view_count_selected(window_with_focus->view) != 1)
		return;

	item = filer_selected_item(window_with_focus);
	if (!item || item->base_type != TYPE_DIRECTORY)
		return;

	path = (const gchar *) make_path(window_with_focus->sym_path,
					   item->leafname);
	bookmarks_add_path(path);
}

static void restore_selected_from_trash(gpointer data, guint action, GtkWidget *widget)
{
	(void) data;
	(void) action;
	(void) widget;

	if (window_with_focus)
		rox_trash_restore_selected(window_with_focus);
}

static void search_current_folders(gpointer data, guint action, GtkWidget *widget)
{
	(void)data; (void)action; (void)widget;
	if (window_with_focus)
		search_integration_launch(window_with_focus);
}

static void open_paired_windows(gpointer data, guint action, GtkWidget *widget)
{
	(void)data; (void)action; (void)widget;
	filer_pair_open(window_with_focus, NULL, NULL);
}

static void realign_paired_windows(gpointer data, guint action, GtkWidget *widget)
{
	(void)data; (void)action; (void)widget;
	filer_pair_realign();
}

static void menu_options_changed(void)
{
	if (o_menu_xterm_grave.has_changed)
	{
		if (o_menu_xterm_grave.int_value)
		{
			new_xterm_here_closure = g_cclosure_new(G_CALLBACK(new_xterm_here),
													FALSE, NULL);
			gtk_accel_group_connect(filer_keys,
				gdk_keyval_from_name("grave"),
				0, 0,
				new_xterm_here_closure);
		}
		else
		{
			gtk_accel_group_disconnect(filer_keys, new_xterm_here_closure);
		}
	}
}

static gboolean path_has_suffix_case(const gchar *path, const gchar *suffix)
{
	gsize path_len;
	gsize suffix_len;

	if (!path || !suffix)
		return FALSE;

	path_len = strlen(path);
	suffix_len = strlen(suffix);
	if (suffix_len > path_len)
		return FALSE;

	return g_ascii_strcasecmp(path + path_len - suffix_len, suffix) == 0;
}

static gchar *path_read_shebang(const gchar *path)
{
	gchar buffer[4097];
	gint fd;
	ssize_t got;
	gchar *line_end;
	gchar *line;

	if (!path)
		return NULL;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return NULL;
	got = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);
	if (got < 2)
		return NULL;

	buffer[got] = '\0';
	/* Accept a UTF-8 BOM for scripts copied from editors. */
	line = buffer;
	if (got >= 5 && (guchar) line[0] == 0xef &&
	    (guchar) line[1] == 0xbb && (guchar) line[2] == 0xbf)
		line += 3;
	if (line[0] != '#' || line[1] != '!')
		return NULL;

	line_end = strpbrk(line, "\r\n");
	if (line_end)
		*line_end = '\0';
	line = g_strstrip(line + 2);
	return *line ? g_strdup(line) : NULL;
}

static gboolean path_has_shebang(const gchar *path)
{
	gchar *line = path_read_shebang(path);
	gboolean result = line != NULL;
	g_free(line);
	return result;
}

static gboolean terminal_program_available(const gchar *program)
{
	gchar *found;
	gboolean available;

	if (!program || !*program)
		return FALSE;
	if (g_path_is_absolute(program))
		return access(program, X_OK) == 0;

	found = g_find_program_in_path(program);
	available = found != NULL;
	g_free(found);
	return available;
}

/* Scripts sin shebang no declaran un intérprete. Para los tipos de shell,
 * usar el shell de la sesión (normalmente Bash en Essora/Puppy), luego Bash
 * si está disponible y finalmente /bin/sh. Los scripts con #! conservan
 * siempre el intérprete explícito. */
static gchar *terminal_default_shell(void)
{
	const gchar *configured = g_getenv("SHELL");
	gchar *found;

	if (configured && *configured && terminal_program_available(configured))
		return g_strdup(configured);
	found = g_find_program_in_path("bash");
	if (found)
		return found;
	if (access("/bin/sh", X_OK) == 0)
		return g_strdup("/bin/sh");
	return g_strdup("sh");
}

/* Validate the interpreter named by #! before opening a terminal. This turns
 * a silent-looking failure into a precise error, including /usr/bin/env forms. */
static gboolean terminal_shebang_available(const gchar *path)
{
	gchar *shebang = path_read_shebang(path);
	gchar **argv = NULL;
	gint argc = 0;
	GError *error = NULL;
	const gchar *program = NULL;
	gint i;
	gboolean available = FALSE;

	if (!shebang || !g_shell_parse_argv(shebang, &argc, &argv, &error) ||
	    argc < 1)
	{
		delayed_error(_("Unable to parse the script interpreter '%s': %s"),
			shebang ? shebang : "",
			error ? error->message : _("Unknown error"));
		goto out;
	}

	program = argv[0];
	{
		gchar *base = g_path_get_basename(program);
		gboolean uses_env = g_strcmp0(base, "env") == 0;
		g_free(base);
		if (uses_env)
		{
			/* Find the command after env options and optional NAME=VALUE pairs. */
			program = NULL;
			for (i = 1; i < argc; i++)
			{
				if (g_strcmp0(argv[i], "-S") == 0)
					continue;
				if (argv[i][0] == '-' && !program)
					continue;
				if (strchr(argv[i], '=') && !program)
					continue;
				program = argv[i];
				break;
			}
		}
	}

	if (!program || !*program)
	{
		delayed_error(_("Unable to parse the script interpreter '%s': %s"),
			shebang, _("Unknown error"));
		goto out;
	}

	available = terminal_program_available(program);
	if (!available)
		delayed_error(_("Program %s not found - deleted?"), program);

out:
	g_clear_error(&error);
	g_strfreev(argv);
	g_free(shebang);
	return available;
}

static gboolean terminal_arg_is_exec_marker(const gchar *arg)
{
	return arg &&
		(strcmp(arg, "-e") == 0 ||
		 strcmp(arg, "--execute") == 0 ||
		 strcmp(arg, "-x") == 0 ||
		 strcmp(arg, "--") == 0);
}

static gboolean terminal_name_is(const gchar *name, const gchar *candidate)
{
	return name && candidate && g_ascii_strcasecmp(name, candidate) == 0;
}

/* Parse the configured terminal once and adapt the command separator to the
 * terminal family. The executed payload is a temporary script path, which is
 * accepted both by terminals that expect argv and those that expect a single
 * command after -e. */
static gboolean terminal_command_program_available(const gchar *command)
{
	gchar **parsed = NULL;
	gint argc = 0;
	GError *error = NULL;
	gchar *found = NULL;
	gboolean available = FALSE;

	if (!command || !*command)
		return FALSE;
	if (!g_shell_parse_argv(command, &argc, &parsed, &error))
	{
		rox_debug_log("TERMINAL", "invalid configured command=%s error=%s",
			command, error ? error->message : "");
		g_clear_error(&error);
		return FALSE;
	}
	if (argc > 0 && parsed && parsed[0] && *parsed[0])
	{
		if (strchr(parsed[0], '/'))
			available = access(parsed[0], X_OK) == 0;
		else
		{
			found = g_find_program_in_path(parsed[0]);
			available = found != NULL;
			g_free(found);
		}
	}
	g_strfreev(parsed);
	return available;
}

static gchar *terminal_resolve_command(void)
{
	static const gchar * const fallbacks[] = {
		"defaultterminal",
		"x-terminal-emulator",
		"xterm",
		"urxvt",
		NULL
	};
	const gchar *diagnostic_terminal = g_getenv("ROX_DIAGNOSTIC_TERMINAL");
	gchar *saved_command = NULL;
	const gchar *configured;
	gchar *result = NULL;
	guint i;

	if (g_getenv("ROX_DIAGNOSTIC") && diagnostic_terminal &&
	    *diagnostic_terminal)
		return g_strdup(diagnostic_terminal);

	/* The desktop and filer can be different long-running processes.  Read
	 * the last saved value each time a terminal is requested so a change in
	 * Options applies immediately after OK, even in an already-running
	 * desktop process. */
	saved_command = option_get_saved("menu_xterm");
	configured = saved_command ? saved_command :
		(const gchar *) o_menu_xterm.value;

	if (configured && *configured)
	{
		if (terminal_command_program_available(configured))
		{
			rox_debug_log("TERMINAL", "selected configured terminal=%s", configured);
			result = g_strdup(configured);
			g_free(saved_command);
			return result;
		}
		rox_debug_log("TERMINAL",
			"configured terminal unavailable=%s; using automatic fallbacks",
			configured);
	}
	g_free(saved_command);

	for (i = 0; fallbacks[i]; i++)
	{
		gchar *found = g_find_program_in_path(fallbacks[i]);
		if (!found)
			continue;
		g_free(found);
		rox_debug_log("TERMINAL", "selected fallback terminal=%s", fallbacks[i]);
		return g_strdup(fallbacks[i]);
	}

	return NULL;
}

static gboolean terminal_build_argv(gboolean execute_command,
				    const gchar *command_path,
				    GPtrArray **argv_out)
{
	gchar *terminal_command = terminal_resolve_command();
	gchar **parsed = NULL;
	gint argc = 0;
	GError *error = NULL;
	GPtrArray *argv;
	gchar *program_name;
	gint i;

	g_return_val_if_fail(argv_out != NULL, FALSE);
	*argv_out = NULL;

	if (!terminal_command || !*terminal_command)
	{
		delayed_error(_("No suitable terminal emulator was found. Configure one in Options > Desktop, or install defaultterminal, x-terminal-emulator, xterm or urxvt."));
		g_free(terminal_command);
		return FALSE;
	}

	if (!g_shell_parse_argv(terminal_command, &argc, &parsed, &error))
	{
		delayed_error(_("Failed to parse terminal command '%s':\n%s"),
			terminal_command, error ? error->message : "");
		if (error)
			g_error_free(error);
		g_free(terminal_command);
		return FALSE;
	}

	if (argc < 1 || !parsed || !parsed[0] || !*parsed[0])
	{
		g_strfreev(parsed);
		delayed_error(_("Terminal command is empty."));
		g_free(terminal_command);
		return FALSE;
	}

	argv = g_ptr_array_new_with_free_func(g_free);
	for (i = 0; i < argc; i++)
		g_ptr_array_add(argv, g_strdup(parsed[i]));
	g_strfreev(parsed);

	program_name = g_path_get_basename((const gchar *) g_ptr_array_index(argv, 0));

	if (!execute_command)
	{
		/* A user may have saved "xterm -e" in older ROX versions. That is
		 * valid for Run in Terminal, but opening an empty terminal must not
		 * leave a dangling execute switch. */
		while (argv->len > 1 && terminal_arg_is_exec_marker(
			(const gchar *) g_ptr_array_index(argv, argv->len - 1)))
			g_ptr_array_remove_index(argv, argv->len - 1);
	}
	else if (command_path && *command_path)
	{
		if (argv->len > 0 && terminal_arg_is_exec_marker(
			(const gchar *) g_ptr_array_index(argv, argv->len - 1)))
		{
			/* Respect an explicit -e, -x, --execute or -- configured by the
			 * user and append only the runner. */
		}
		else if (terminal_name_is(program_name, "gnome-terminal") ||
			 terminal_name_is(program_name, "mate-terminal") ||
			 terminal_name_is(program_name, "kgx") ||
			 terminal_name_is(program_name, "ptyxis"))
		{
			g_ptr_array_add(argv, g_strdup("--"));
		}
		else if (terminal_name_is(program_name, "xfce4-terminal") ||
			 terminal_name_is(program_name, "terminator"))
		{
			g_ptr_array_add(argv, g_strdup("-x"));
		}
		else if (terminal_name_is(program_name, "wezterm"))
		{
			gboolean has_start = FALSE;
			for (i = 1; i < (gint) argv->len; i++)
				if (terminal_name_is((const gchar *) g_ptr_array_index(argv, i), "start"))
					has_start = TRUE;
			if (!has_start)
				g_ptr_array_add(argv, g_strdup("start"));
			g_ptr_array_add(argv, g_strdup("--"));
		}
		else if (!(terminal_name_is(program_name, "foot") ||
			   terminal_name_is(program_name, "kitty")))
		{
			/* xterm, rxvt/urxvt, lxterminal, konsole, qterminal,
			 * alacritty, defaultterminal and x-terminal-emulator. */
			g_ptr_array_add(argv, g_strdup("-e"));
		}

		g_ptr_array_add(argv, g_strdup(command_path));
	}

	g_free(program_name);
	g_free(terminal_command);
	g_ptr_array_add(argv, NULL);
	*argv_out = argv;
	return TRUE;
}

static gchar *terminal_create_runner(const gchar *path, const gchar *working_dir,
					 TerminalRunMode mode)
{
	const gchar *command = NULL;
	gchar *dynamic_command = NULL;
	gchar *quoted_target;
	gchar *quoted_working_dir;
	gchar *quoted_message;
	gchar *contents;
	gchar *runner_path = NULL;
	GError *error = NULL;
	gint fd;
	FILE *stream;
	gboolean write_failed;

	switch (mode)
	{
		case TERMINAL_RUN_DIRECT:
			command = "\"$target\"";
			break;
		case TERMINAL_RUN_SHELL:
		{
			gchar *shell = terminal_default_shell();
			gchar *quoted_shell = g_shell_quote(shell);
			dynamic_command = g_strdup_printf("%s \"$target\"", quoted_shell);
			command = dynamic_command;
			g_free(quoted_shell);
			g_free(shell);
			break;
		}
		case TERMINAL_RUN_BASH:
			command = "bash \"$target\"";
			break;
		case TERMINAL_RUN_ASH:
			command = "ash \"$target\"";
			break;
		case TERMINAL_RUN_DASH:
			command = "dash \"$target\"";
			break;
		case TERMINAL_RUN_ZSH:
			command = "zsh \"$target\"";
			break;
		case TERMINAL_RUN_KSH:
			command = "ksh \"$target\"";
			break;
		case TERMINAL_RUN_FISH:
			command = "fish \"$target\"";
			break;
		case TERMINAL_RUN_PYTHON:
			command = "python3 \"$target\"";
			break;
		case TERMINAL_RUN_PERL:
			command = "perl \"$target\"";
			break;
		case TERMINAL_RUN_RUBY:
			command = "ruby \"$target\"";
			break;
		case TERMINAL_RUN_LUA:
			command = "lua \"$target\"";
			break;
		case TERMINAL_RUN_TCL:
			command = "tclsh \"$target\"";
			break;
		case TERMINAL_RUN_PHP:
			command = "php \"$target\"";
			break;
		case TERMINAL_RUN_NODE:
			command = "node \"$target\"";
			break;
		case TERMINAL_RUN_AWK:
			command = "awk -f \"$target\"";
			break;
		case TERMINAL_RUN_SED:
			command = "sed -f \"$target\"";
			break;
		case TERMINAL_RUN_SHEBANG:
		{
			gchar *shebang = path_read_shebang(path);
			gchar **interpreter_argv = NULL;
			gint interpreter_argc = 0;
			GString *builder;
			gint i;

			if (!shebang || !g_shell_parse_argv(shebang,
					&interpreter_argc, &interpreter_argv, &error) ||
			    interpreter_argc < 1)
			{
				delayed_error(_("Unable to parse the script interpreter '%s': %s"),
					shebang ? shebang : "",
					error ? error->message : _("Unknown error"));
				g_clear_error(&error);
				g_free(shebang);
				g_strfreev(interpreter_argv);
				return NULL;
			}

			builder = g_string_new(NULL);
			for (i = 0; i < interpreter_argc; i++)
			{
				gchar *quoted = g_shell_quote(interpreter_argv[i]);
				if (i > 0)
					g_string_append_c(builder, ' ');
				g_string_append(builder, quoted);
				g_free(quoted);
			}
			g_string_append(builder, " \"$target\"");
			dynamic_command = g_string_free(builder, FALSE);
			command = dynamic_command;
			g_free(shebang);
			g_strfreev(interpreter_argv);
			break;
		}
		default:
			return NULL;
	}

	fd = g_file_open_tmp("rox-run-terminal-XXXXXX", &runner_path, &error);
	if (fd < 0)
	{
		delayed_error("%s", error ? error->message : "Unable to create terminal runner");
		g_clear_error(&error);
		g_free(dynamic_command);
		return NULL;
	}

	quoted_target = g_shell_quote(path);
	quoted_working_dir = g_shell_quote(working_dir && *working_dir
		? working_dir : g_get_home_dir());
	quoted_message = g_shell_quote(_("Script completed. Press RETURN to close the terminal."));
	contents = g_strdup_printf(
		"#!/bin/sh\n"
		"rm -f -- \"$0\"\n"
		"target=%s\n"
		"workdir=%s\n"
		"message=%s\n"
		"if ! cd -- \"$workdir\"; then\n"
		"  printf 'Unable to enter script directory: %%s\n' \"$workdir\" >&2\n"
		"  status=1\n"
		"else\n"
		"  %s\n"
		"  status=$?\n"
		"fi\n"
		"printf '\\n%%s\\n' \"$message\"\n"
		"IFS= read -r answer || true\n"
		"exit \"$status\"\n",
		quoted_target, quoted_working_dir, quoted_message, command);
	g_free(quoted_target);
	g_free(quoted_working_dir);
	g_free(quoted_message);
	g_free(dynamic_command);

	stream = fdopen(fd, "w");
	if (!stream)
	{
		close(fd);
		unlink(runner_path);
		delayed_error("Unable to write terminal runner: %s", g_strerror(errno));
		g_free(contents);
		g_free(runner_path);
		return NULL;
	}
	write_failed = fputs(contents, stream) == EOF;
	if (fclose(stream) != 0)
		write_failed = TRUE;
	if (write_failed)
	{
		unlink(runner_path);
		delayed_error("Unable to write terminal runner: %s", g_strerror(errno));
		g_free(contents);
		g_free(runner_path);
		return NULL;
	}
	g_free(contents);

	if (chmod(runner_path, 0700) != 0)
	{
		unlink(runner_path);
		delayed_error("Unable to make terminal runner executable: %s", g_strerror(errno));
		g_free(runner_path);
		return NULL;
	}

	return runner_path;
}

static gboolean remove_terminal_runner_later(gpointer data)
{
	gchar *path = data;
	if (path)
	{
		unlink(path);
		g_free(path);
	}
	return G_SOURCE_REMOVE;
}

static TerminalRunMode terminal_run_mode_for_item(const gchar *path,
					   const DirItem *item)
{
	if (!path || !item)
		return TERMINAL_RUN_NONE;
	if (item->base_type != TYPE_FILE &&
	    !g_file_test(path, G_FILE_TEST_IS_REGULAR))
		return TERMINAL_RUN_NONE;

	if (item->mime_type == application_x_desktop)
		return TERMINAL_RUN_NONE;

	/* AppImages must keep their native executable loader. */
	if (path_has_suffix_case(path, ".AppImage"))
		return access(path, X_OK) == 0
			? TERMINAL_RUN_DIRECT : TERMINAL_RUN_APPIMAGE_NEEDS_EXEC;

	/* Parse the interpreter ourselves even when the file is executable. This
	 * handles CRLF shebangs and /usr/bin/env -S more reliably than asking the
	 * kernel to interpret every script directly. */
	if (path_has_shebang(path))
		return TERMINAL_RUN_SHEBANG;

	/* Sin shebang, la extensión es solamente un respaldo. No ejecutar un
	 * archivo .bash con /bin/sh: los bashisms fallan aunque el script sea válido. */
	if (path_has_suffix_case(path, ".bash"))
		return TERMINAL_RUN_BASH;
	if (path_has_suffix_case(path, ".ash"))
		return TERMINAL_RUN_ASH;
	if (path_has_suffix_case(path, ".dash"))
		return TERMINAL_RUN_DASH;
	if (path_has_suffix_case(path, ".zsh"))
		return TERMINAL_RUN_ZSH;
	if (path_has_suffix_case(path, ".ksh"))
		return TERMINAL_RUN_KSH;
	if (path_has_suffix_case(path, ".fish"))
		return TERMINAL_RUN_FISH;
	if (item->mime_type == application_x_shellscript ||
	    path_has_suffix_case(path, ".sh"))
		return TERMINAL_RUN_SHELL;

	if ((item->mime_type &&
	     g_strcmp0(item->mime_type->media_type, "text") == 0 &&
	     item->mime_type->subtype &&
	     strstr(item->mime_type->subtype, "python") != NULL) ||
	    path_has_suffix_case(path, ".py") ||
	    path_has_suffix_case(path, ".pyw"))
		return TERMINAL_RUN_PYTHON;
	if (path_has_suffix_case(path, ".pl"))
		return TERMINAL_RUN_PERL;
	if (path_has_suffix_case(path, ".rb"))
		return TERMINAL_RUN_RUBY;
	if (path_has_suffix_case(path, ".lua"))
		return TERMINAL_RUN_LUA;
	if (path_has_suffix_case(path, ".tcl"))
		return TERMINAL_RUN_TCL;
	if (path_has_suffix_case(path, ".php"))
		return TERMINAL_RUN_PHP;
	if (path_has_suffix_case(path, ".js") ||
	    path_has_suffix_case(path, ".mjs") ||
	    path_has_suffix_case(path, ".cjs"))
		return TERMINAL_RUN_NODE;
	if (path_has_suffix_case(path, ".awk"))
		return TERMINAL_RUN_AWK;
	if (path_has_suffix_case(path, ".sed"))
		return TERMINAL_RUN_SED;

	/* Un texto ejecutable sin #! es normalmente un script shell antiguo.
	 * Ejecutarlo directamente termina en ENOEXEC; usar el shell de sesión.
	 * Los binarios nativos siguen ejecutándose directamente. */
	if (access(path, X_OK) == 0)
	{
		if (item->mime_type &&
		    g_strcmp0(item->mime_type->media_type, "text") == 0)
			return TERMINAL_RUN_SHELL;
		return TERMINAL_RUN_DIRECT;
	}

	return TERMINAL_RUN_NONE;
}

static const gchar *terminal_run_mode_name(TerminalRunMode mode)
{
	switch (mode)
	{
		case TERMINAL_RUN_DIRECT: return "direct";
		case TERMINAL_RUN_SHELL: return "sh";
		case TERMINAL_RUN_BASH: return "bash";
		case TERMINAL_RUN_ASH: return "ash";
		case TERMINAL_RUN_DASH: return "dash";
		case TERMINAL_RUN_ZSH: return "zsh";
		case TERMINAL_RUN_KSH: return "ksh";
		case TERMINAL_RUN_FISH: return "fish";
		case TERMINAL_RUN_PYTHON: return "python";
		case TERMINAL_RUN_PERL: return "perl";
		case TERMINAL_RUN_RUBY: return "ruby";
		case TERMINAL_RUN_LUA: return "lua";
		case TERMINAL_RUN_TCL: return "tcl";
		case TERMINAL_RUN_PHP: return "php";
		case TERMINAL_RUN_NODE: return "node";
		case TERMINAL_RUN_AWK: return "awk";
		case TERMINAL_RUN_SED: return "sed";
		case TERMINAL_RUN_SHEBANG: return "shebang";
		case TERMINAL_RUN_APPIMAGE_NEEDS_EXEC: return "appimage-needs-exec";
		default: return "none";
	}
}

static gboolean spawn_terminal_runner(const gchar *working_dir,
					  const gchar *path,
					  TerminalRunMode mode)
{
	GPtrArray *argv = NULL;
	gchar *runner_path;
	gboolean success;

	rox_debug_log("TERMINAL", "path=%s mode=%s cwd=%s configured=%s",
		path ? path : "", terminal_run_mode_name(mode),
		working_dir ? working_dir : "",
		(const gchar *) o_menu_xterm.value ? (const gchar *) o_menu_xterm.value : "");

	if (mode == TERMINAL_RUN_SHEBANG && !terminal_shebang_available(path))
		return FALSE;

	{
		const gchar *required = NULL;
		gchar *found = NULL;

		switch (mode)
		{
			case TERMINAL_RUN_BASH: required = "bash"; break;
			case TERMINAL_RUN_ASH: required = "ash"; break;
			case TERMINAL_RUN_DASH: required = "dash"; break;
			case TERMINAL_RUN_ZSH: required = "zsh"; break;
			case TERMINAL_RUN_KSH: required = "ksh"; break;
			case TERMINAL_RUN_FISH: required = "fish"; break;
			case TERMINAL_RUN_PYTHON: required = "python3"; break;
			case TERMINAL_RUN_PERL: required = "perl"; break;
			case TERMINAL_RUN_RUBY: required = "ruby"; break;
			case TERMINAL_RUN_LUA: required = "lua"; break;
			case TERMINAL_RUN_TCL: required = "tclsh"; break;
			case TERMINAL_RUN_PHP: required = "php"; break;
			case TERMINAL_RUN_NODE: required = "node"; break;
			case TERMINAL_RUN_AWK: required = "awk"; break;
			case TERMINAL_RUN_SED: required = "sed"; break;
			default: break;
		}

		if (required)
		{
			found = g_find_program_in_path(required);
			if (!found)
			{
				delayed_error(_("Program %s not found - deleted?"), required);
				return FALSE;
			}
			g_free(found);
		}
	}

	runner_path = terminal_create_runner(path, working_dir, mode);
	if (!runner_path)
		return FALSE;

	rox_debug_log("TERMINAL", "runner=%s", runner_path);
	if (!terminal_build_argv(TRUE, runner_path, &argv))
	{
		unlink(runner_path);
		g_free(runner_path);
		return FALSE;
	}

	{
		gchar *joined = g_strjoinv(" | ", (gchar **) argv->pdata);
		gint child = rox_spawn(working_dir, (const gchar **) argv->pdata);
		success = child != 0;
		rox_debug_log("TERMINAL", "argv=%s spawn=%s pid=%d",
			joined ? joined : "", success ? "ok" : "failed", child);
		g_free(joined);
	}
	g_ptr_array_free(argv, TRUE);

	if (!success)
		unlink(runner_path);
	else
		g_timeout_add_seconds(600, remove_terminal_runner_later,
			g_strdup(runner_path));
	g_free(runner_path);
	return success;
}

gboolean menu_rename_path(const gchar *path, GtkWindow *parent)
{
	DirItem *item;
	MaskedPixmap *image;
	GtkWidget *dialog;
	gchar *leaf;

	if (!path || !g_file_test(path, G_FILE_TEST_EXISTS))
		return FALSE;

	leaf = g_path_get_basename(path);
	item = diritem_new((const guchar *) leaf);
	diritem_restat((const guchar *) path, item, NULL);
	g_free(leaf);

	image = di_image(item);
	if (!image)
	{
		diritem_free(item);
		return FALSE;
	}

	g_object_ref(image);
	dialog = savebox_show(_("Rename"), path, image, rename_cb,
	                      GDK_ACTION_MOVE);
	diritem_free(item);
	if (!dialog)
		return FALSE;

	if (parent)
	{
		gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
		gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
		gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER_ON_PARENT);
	}
	gtk_window_present(GTK_WINDOW(dialog));
	return TRUE;
}

gboolean menu_diagnose_rename_dialog(const gchar *path)
{
	DirItem *item;
	MaskedPixmap *image;
	GtkWidget *dialog;
	gchar *leaf;
	gboolean visible;
	gboolean realized;

	if (!path || !g_file_test(path, G_FILE_TEST_EXISTS))
	{
		g_printerr("DIAG_RENAME_ERROR=missing:%s\n", path ? path : "");
		return FALSE;
	}

	leaf = g_path_get_basename(path);
	item = diritem_new((const guchar *) leaf);
	diritem_restat((const guchar *) path, item, NULL);
	g_free(leaf);

	image = di_image(item);
	if (!image)
	{
		g_printerr("DIAG_RENAME_ERROR=no-icon:%s\n", path);
		diritem_free(item);
		return FALSE;
	}

	/* savebox_show() consumes one reference. */
	g_object_ref(image);
	dialog = savebox_show(_("Rename"), path, image, rename_cb,
		GDK_ACTION_MOVE);
	if (!dialog)
	{
		g_printerr("DIAG_RENAME_ERROR=no-dialog:%s\n", path);
		diritem_free(item);
		return FALSE;
	}

	while (gtk_events_pending())
		gtk_main_iteration();
	visible = gtk_widget_get_visible(dialog);
	realized = gtk_widget_get_realized(dialog);
	g_print("DIAG_RENAME_VISIBLE=%d\n", visible ? 1 : 0);
	g_print("DIAG_RENAME_REALIZED=%d\n", realized ? 1 : 0);
	g_print("DIAG_RENAME_TITLE=%s\n",
		gtk_window_get_title(GTK_WINDOW(dialog)));

	gtk_widget_destroy(dialog);
	while (gtk_events_pending())
		gtk_main_iteration();
	diritem_free(item);
	return visible && realized;
}

gboolean menu_diagnose_run_in_terminal(const gchar *path)
{
	DirItem item = {0};
	TerminalRunMode mode;
	gchar *directory;
	gboolean launched;

	if (!path || !g_file_test(path, G_FILE_TEST_IS_REGULAR))
	{
		g_printerr("DIAG_TERMINAL_ERROR=not-a-regular-file:%s\n",
			path ? path : "");
		return FALSE;
	}

	item.base_type = TYPE_FILE;
	item.mime_type = type_from_path(path);
	mode = terminal_run_mode_for_item(path, &item);
	g_print("DIAG_TERMINAL_PATH=%s\n", path);
	g_print("DIAG_TERMINAL_MODE=%s\n", terminal_run_mode_name(mode));
	{
		gchar *effective_terminal = terminal_resolve_command();
		g_print("DIAG_TERMINAL_COMMAND=%s\n",
			effective_terminal ? effective_terminal : "");
		g_free(effective_terminal);
	}

	if (mode == TERMINAL_RUN_NONE ||
	    mode == TERMINAL_RUN_APPIMAGE_NEEDS_EXEC)
	{
		g_print("DIAG_TERMINAL_RESULT=unsupported\n");
		return FALSE;
	}

	directory = g_path_get_dirname(path);
	launched = spawn_terminal_runner(directory, path, mode);
	g_free(directory);
	g_print("DIAG_TERMINAL_RESULT=%s\n", launched ? "ok" : "failed");
	return launched;
}

static void new_xterm_here()
{
	xterm_here(NULL, FALSE, NULL);
}

gboolean menu_open_terminal_at(const gchar *directory)
{
	GPtrArray *argv = NULL;
	gboolean success;

	g_return_val_if_fail(directory != NULL, FALSE);

	if (!terminal_build_argv(FALSE, NULL, &argv))
		return FALSE;

	success = rox_spawn(directory, (const gchar **) argv->pdata) != 0;
	g_ptr_array_free(argv, TRUE);
	return success;
}

static void open_terminal_directory(const gchar *directory, gboolean close_filer)
{
	gboolean success = menu_open_terminal_at(directory);

	if (success && close_filer && window_with_focus)
		gtk_widget_destroy(window_with_focus->window);
}

static void xterm_here(gpointer data, guint action, GtkWidget *widget)
{
	gchar *directory = NULL;

	g_return_if_fail(window_with_focus != NULL);

	/* F4 and the Window menu use the selected folder when there is exactly
	 * one; otherwise they preserve ROX's traditional current-directory action. */
	if (view_count_selected(window_with_focus->view) == 1)
	{
		DirItem *item = filer_selected_item(window_with_focus);
		if (item)
		{
			const gchar *selected_path = (const gchar *) make_path(
				window_with_focus->sym_path, item->leafname);
			if (item->base_type == TYPE_DIRECTORY ||
			    g_file_test(selected_path, G_FILE_TEST_IS_DIR))
				directory = g_strdup(selected_path);
		}
	}
	if (!directory)
		directory = g_strdup(window_with_focus->sym_path);

	open_terminal_directory(directory, action != 0);
	g_free(directory);
}

/* Agregado por josejp2424: abrir el terminal en la carpeta seleccionada. */
static void open_terminal_selected(gpointer data, guint action, GtkWidget *widget)
{
	DirItem *item;
	gchar *directory;

	g_return_if_fail(window_with_focus != NULL);

	if (view_count_selected(window_with_focus->view) != 1)
	{
		delayed_error(_("Select a single folder to open a terminal there."));
		return;
	}

	item = filer_selected_item(window_with_focus);
	if (!item)
		return;

	directory = g_strdup((const gchar *) make_path(
		window_with_focus->sym_path, item->leafname));
	if (item->base_type != TYPE_DIRECTORY &&
	    !g_file_test(directory, G_FILE_TEST_IS_DIR))
	{
		delayed_error(_("The selected item is not a folder."));
		g_free(directory);
		return;
	}
	open_terminal_directory(directory, FALSE);
	g_free(directory);
}

/* Agregado por josejp2424: ejecutar el elemento seleccionado en terminal. */
static void run_in_terminal(gpointer data, guint action, GtkWidget *widget)
{
	DirItem *item;
	gchar *path;
	gchar *directory;
	TerminalRunMode mode;

	g_return_if_fail(window_with_focus != NULL);

	if (view_count_selected(window_with_focus->view) != 1)
	{
		delayed_error(_("Select a single executable file or script to run in a terminal."));
		return;
	}

	item = filer_selected_item(window_with_focus);
	if (!item)
		return;
	path = g_strdup((const gchar *) make_path(
		window_with_focus->sym_path, item->leafname));
	mode = terminal_run_mode_for_item(path, item);

	if (mode == TERMINAL_RUN_APPIMAGE_NEEDS_EXEC)
	{
		delayed_error(_("The selected AppImage is not executable. Enable its execute permission first."));
		g_free(path);
		return;
	}
	if (mode == TERMINAL_RUN_NONE)
	{
		delayed_error(_("The selected item cannot be run in a terminal."));
		g_free(path);
		return;
	}

	directory = g_path_get_dirname(path);
	spawn_terminal_runner(directory, path, mode);
	g_free(directory);
	g_free(path);
}

static void home_directory(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_change_to(window_with_focus, home_dir, NULL);
}

static void show_bookmarks(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	bookmarks_show_menu(window_with_focus);
}

static void show_log(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	log_show_window();
}

static void follow_symlinks(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	if (strcmp(window_with_focus->real_path, window_with_focus->sym_path))
		filer_change_to(window_with_focus,
				window_with_focus->real_path, NULL);
	else
		delayed_error(_("This is already the canonical name "
				"for this directory."));
}

static void open_parent(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	filer_open_parent(window_with_focus);
}

static void open_parent_same(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	change_to_parent(window_with_focus);
}

static void resize(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	view_autosize(window_with_focus->view);
}

static void new_window(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	if (o_unique_filer_windows.int_value)
	{
		report_error(_("You can't open a second view onto "
			"this directory because the `Unique Windows' option "
			"is turned on in the Options window."));
	}
	else
		filer_opendir(window_with_focus->sym_path, window_with_focus, NULL);
}

static void close_window(gpointer data, guint action, GtkWidget *widget)
{
	g_return_if_fail(window_with_focus != NULL);

	if (!filer_window_delete(window_with_focus->window, NULL,
				 window_with_focus))
		gtk_widget_destroy(window_with_focus->window);
}

static void mini_buffer(gpointer data, guint action, GtkWidget *widget)
{
	MiniType type = (MiniType) action;

	g_return_if_fail(window_with_focus != NULL);

	/* Item needs to remain selected... */
	if (type == MINI_SHELL)
		window_with_focus->temp_item_selected = FALSE;

	minibuffer_show(window_with_focus, type);
}

/* Agregado por josejp2424: diálogo Acerca de nativo e integrado en el binario. */
static void show_rox_about_dialog(void)
{
	const gchar *authors[] = {
		_("Original ROX-Filer author: Thomas Leonard"),
		_("Original ROX-Filer contributors: ROX Desktop contributors"),
		_("Rox-Filer2 continuation and development: josejp2424"),
		_("Rox-Filer2 project maintainer: josejp2424"),
		NULL
	};
	GtkWindow *parent = NULL;
	GtkWidget *dialog;
	GdkPixbuf *logo = NULL;
	gchar *logo_path;

	if (window_with_focus && window_with_focus->window)
		parent = GTK_WINDOW(window_with_focus->window);

	dialog = gtk_about_dialog_new();
	gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(dialog), "Rox-Filer2");
	gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(dialog), VERSION);
	gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(dialog),
		_("Fast and lightweight file manager for X11 and Wayland, continued from ROX-Filer."));
	gtk_about_dialog_set_copyright(GTK_ABOUT_DIALOG(dialog),
		"Original ROX-Filer (C) 2005 Thomas Leonard and contributors\n"
		"Rox-Filer2 continuation (C) 2026 josejp2424");
	gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(dialog), authors);
	gtk_about_dialog_set_license(GTK_ABOUT_DIALOG(dialog),
		_("Rox-Filer2 is free software; you can redistribute it "
		  "and/or modify it under the terms of the GNU General Public License "
		  "as published by the Free Software Foundation; either version 2 "
		  "of the License, or (at your option) any later version."));
	gtk_about_dialog_set_wrap_license(GTK_ABOUT_DIALOG(dialog), TRUE);
	gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(dialog),
		"https://github.com/josejp2424/ROX-Filer-gtk3");
	gtk_about_dialog_set_website_label(GTK_ABOUT_DIALOG(dialog),
		_("Rox-Filer2 project"));

	/* Rox-Filer2 2.12.2-26: use the installed application icon by name so
	 * GTK selects the best hicolor size.  Keep the bundled legacy image as
	 * a fallback when the source tree is run before installation. */
	gtk_window_set_icon_name(GTK_WINDOW(dialog), "rox-filer2");
	if (gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), "rox-filer2")) {
		gtk_about_dialog_set_logo_icon_name(GTK_ABOUT_DIALOG(dialog),
			"rox-filer2");
	} else {
		logo_path = g_build_filename(app_dir, "ROX-Filer.png", NULL);
		logo = gdk_pixbuf_new_from_file_at_scale(logo_path, 96, 96, TRUE, NULL);
		g_free(logo_path);
		if (logo) {
			gtk_about_dialog_set_logo(GTK_ABOUT_DIALOG(dialog), logo);
			g_object_unref(logo);
		}
	}

	if (parent) {
		gtk_window_set_transient_for(GTK_WINDOW(dialog), parent);
		gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
		gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER_ON_PARENT);
	} else {
		gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
	}

	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

void menu_rox_help(gpointer data, guint action, GtkWidget *widget)
{
	if (action == HELP_ABOUT)
		show_rox_about_dialog();
	else if (action == HELP_DIR)
		filer_opendir((const char *) make_path(app_dir, "Help"), NULL, NULL);
	else if (action == HELP_MANUAL)
	{
		gchar *manual = NULL;

		if (current_lang)
		{
			manual = g_strconcat(app_dir, "/Help/Manual-",
					     current_lang, ".html", NULL);
			if (!file_exists(manual) && strchr(current_lang, '_'))
			{
				/* Try again without the territory */
				strcpy(strrchr(manual, '_'), ".html");
			}
			if (!file_exists(manual))
				null_g_free(&manual);
		}

		if (!manual)
			manual = g_strconcat(app_dir,
						"/Help/Manual.html", NULL);

		run_by_path(manual);

		g_free(manual);
	}
	else
		g_warning("Unknown help action %d\n", action);
}

/* Set n items from position 'from' in 'menu' to the 'shaded' state */
void menu_set_items_shaded(GtkWidget *menu, gboolean shaded, int from, int n)
{
	GList	*items, *item;

	items = gtk_container_get_children(GTK_CONTAINER(menu));

	item = g_list_nth(items, from);
	while (item && n--)
	{
		gtk_widget_set_sensitive(GTK_WIDGET(item->data), !shaded);
		item = item->next;
	}
	g_list_free(items);
}

static void save_menus(void)
{
	char	*menurc;

	menurc = choices_find_xdg_path_save(MENUS_NAME, PROJECT, SITE, TRUE);
	if (menurc)
	{
		gtk_accel_map_save(menurc);
		g_free(menurc);
	}
}

static void select_nth_item(GtkMenuShell *shell, int n)
{
	GList	  *items;
	GtkWidget *item;

	items = gtk_container_get_children(GTK_CONTAINER(shell));
	item = g_list_nth_data(items, n);

	g_return_if_fail(item != NULL);

	g_list_free(items);

	gtk_menu_shell_select_item(shell, item);
}

static void clipboard_get(GtkClipboard *clipboard, GtkSelectionData *selection_data, guint info, gpointer user_data)
{
	if (!selected_paths)
		return;

	GList *iter;
	switch (info) {
		case TARGET_URI_LIST:
		{
			gchar *tmp;
			gchar *data = g_strdup("");
			for (iter = selected_paths; iter; iter = iter->next)
			{
				tmp = data;
				data = g_strconcat(data, "file://", (gchar *)iter->data, "\n", NULL);
				g_free(tmp);
			}
			gtk_selection_data_set(selection_data, text_uri_list,
					8, (const guchar *) data, (gint) strlen(data));
			g_free(data);
			break;
		}
		case TARGET_GNOME_COPIED_FILES:
		{
			gchar *tmp;
			gchar *data = strdup(clipboard_action);
			for (iter = selected_paths; iter; iter = iter->next)
			{
				tmp = data;
				data = g_strconcat(data, "file://", (gchar *)iter->data, "\r\n", NULL);
				g_free(tmp);
			}
			gtk_selection_data_set(selection_data, gnome_copied_files,
					8, (const guchar *) data, (gint) strlen(data));
			g_free(data);
			break;
		}
		default:
			break;
	}
}

static void clipboard_clear(GtkClipboard *clipboard, gpointer user_data)
{
	(void)clipboard;
	(void)user_data;

	if (selected_paths)
		destroy_glist(&selected_paths);
	clipboard_action = NULL;
	menu_clipboard_visuals_changed();
}

gboolean menu_path_is_cut(const gchar *path)
{
	GList *node;
	gchar *candidate;

	if (!path || !*path || !clipboard_action ||
	    g_strcmp0(clipboard_action, "cut\n") != 0 || !selected_paths)
		return FALSE;

	for (node = selected_paths; node; node = node->next)
	{
		const gchar *stored = node->data;
		if (g_strcmp0(path, stored) == 0)
			return TRUE;
	}

	/* Normalise harmless /./ and /../ differences without resolving
	 * symlinks.  This keeps a cut item faded if the same directory is shown
	 * through an equivalent spelling. */
	candidate = g_canonicalize_filename(path, NULL);
	for (node = selected_paths; node; node = node->next)
	{
		gchar *stored = g_canonicalize_filename((const gchar *)node->data, NULL);
		gboolean same = g_strcmp0(candidate, stored) == 0;
		g_free(stored);
		if (same)
		{
			g_free(candidate);
			return TRUE;
		}
	}
	g_free(candidate);
	return FALSE;
}

gboolean menu_clipboard_has_files(void)
{
	GdkAtom copied_files;
	GdkAtom uri_list;

	if (!clipboard)
		clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
	copied_files = gdk_atom_intern_static_string("x-special/gnome-copied-files");
	uri_list = gdk_atom_intern_static_string("text/uri-list");

	return gtk_clipboard_wait_is_target_available(clipboard, copied_files) ||
	       gtk_clipboard_wait_is_target_available(clipboard, uri_list);
}

void menu_set_clipboard_paths(GList *paths, gboolean cut)
{
	GList *node;

	if (!clipboard)
		clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_clear(clipboard);

	clipboard_action = cut ? "cut\n" : "copy\n";
	selected_paths = NULL;
	for (node = paths; node; node = node->next)
	{
		const gchar *path = node->data;
		if (path && *path)
			selected_paths = g_list_append(selected_paths, g_strdup(path));
	}

	if (selected_paths)
		gtk_clipboard_set_with_data(clipboard, clipboard_targets, 2,
			clipboard_get, clipboard_clear, NULL);
	else
		clipboard_action = NULL;

	menu_clipboard_visuals_changed();
}

gboolean menu_paste_into_path(const gchar *dest_path)
{
	const gchar *error = NULL;
	gchar *dest_real_path;
	gboolean pasted = FALSE;
	gboolean are_copying = TRUE;
	gboolean ignore_no_local_paths = FALSE;

	if (!dest_path || !*dest_path)
		return FALSE;
	dest_real_path = pathdup(dest_path);

	if (!clipboard)
		clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);

	GtkSelectionData *selection =
				gtk_clipboard_wait_for_contents(clipboard, gnome_copied_files);
	if (selection == NULL)
		selection = gtk_clipboard_wait_for_contents(clipboard, text_uri_list);
	if (selection == NULL)
	{
		delayed_error(_("The clipboard is empty."));
		g_free(dest_real_path);
		return FALSE;
	}

	char **uri_list = gtk_selection_data_get_uris(selection);
	if (!uri_list)
	{
		const guchar *selection_bytes =
			gtk_selection_data_get_data(selection);
		const gint selection_length =
			gtk_selection_data_get_length(selection);

		if (!selection_bytes || selection_length <= 0)
		{
			gtk_selection_data_free(selection);
			delayed_error(_("The clipboard does not contain a file list."));
			g_free(dest_real_path);
			return FALSE;
		}

		char *tmp = g_strndup((const gchar *) selection_bytes,
				selection_length);
		uri_list = g_strsplit_set(tmp, "\r\n", -1);
		g_free(tmp);
	}

	/* Either one local URI, or a list. If everything in the list
	* isn't local then we are stuck.
	*/

	GList *local_paths = NULL;
	gchar **uri_iter;
	for (uri_iter = uri_list; *uri_iter; uri_iter++)
	{
		if (**uri_iter == '\0')
			continue;
		if (strcmp(*uri_iter, "copy") == 0)
			continue;
		if (strcmp(*uri_iter, "cut") == 0)
		{
			are_copying = FALSE;
			continue;
		}

		char *path = get_local_path((EscapedPath *) *uri_iter);
		if (path)
		{
			gchar *source_real_path = pathdup(path);
			gchar *source_dirname = g_path_get_dirname(source_real_path);
			if (strcmp(source_dirname, dest_real_path) == 0 &&
				are_copying == TRUE)
			{
				gchar *source_basename = g_path_get_basename(path);
				gchar *new_name = NULL;
				GList *one_path = NULL;

				ignore_no_local_paths = TRUE;
				one_path = g_list_append(one_path, path);

				new_name = g_strdup_printf(_("Copy of %s"), source_basename);
				int i = 2;
				struct stat dest_info;
				while (mc_lstat((char *) make_path(source_dirname, new_name), &dest_info) == 0)
				{
					g_free(new_name);
					new_name = g_strdup_printf(_("Copy(%d) of %s"), i, source_basename);
					i++;
				}
				action_copy(one_path, dest_path, new_name, -1);
				pasted = TRUE;

				g_free(new_name);
				g_free(source_basename);
				destroy_glist(&one_path);
			}
			else
				local_paths = g_list_append(local_paths, path);

			g_free(source_real_path);
			g_free(source_dirname);
		}
		else
			error = _("Some of these files are on a "
					"different machine - they will be "
					"ignored - sorry");
	}

	if (!local_paths)
	{
		if (ignore_no_local_paths == FALSE)
			error = _("None of these files are on the local "
				"machine - I can't operate on multiple "
				"remote files - sorry.");
	}
	else
	{
		if (are_copying == FALSE)
		{
			action_move(local_paths, dest_path, NULL, -1);
			pasted = TRUE;
			gtk_clipboard_clear(clipboard);
		}
		else
		{
			action_copy(local_paths, dest_path, NULL, -1);
			pasted = TRUE;
		}

		destroy_glist(&local_paths);
	}

	if (error)
		delayed_error(_("Error getting file list: %s"), error);

	g_strfreev(uri_list);
	gtk_selection_data_free(selection);
	g_free(dest_real_path);
	return pasted;
}

static void paste_from_clipboard(gpointer data, guint action, GtkWidget *unused)
{
	(void)data;
	(void)action;
	(void)unused;

	if (!window_with_focus)
		return;
	menu_paste_into_path((const gchar *)window_with_focus->sym_path);
}


static gboolean item_is_wallpaper_image(const DirItem *item)
{
	const gchar *subtype;

	if (!item || item->base_type != TYPE_FILE || !item->mime_type ||
	    g_strcmp0(item->mime_type->media_type, "image") != 0)
		return FALSE;

	subtype = item->mime_type->subtype;
	return g_strcmp0(subtype, "jpeg") == 0 ||
	       g_strcmp0(subtype, "png") == 0 ||
	       g_strcmp0(subtype, "svg+xml") == 0;
}

/* Agregado por josejp2424 (2026): reemplaza el antiguo script SendTo.
 * Copia la imagen de forma segura, pregunta antes de sobrescribir y ofrece
 * aplicarla inmediatamente mediante el propio ROX Desktop. */
static void copy_image_to_backgrounds(const gchar *source_path)
{
	const gchar *backgrounds_dir = "/usr/share/backgrounds";
	GtkWindow *parent = window_with_focus
		? GTK_WINDOW(window_with_focus->window) : NULL;
	GFile *source = NULL;
	GFile *destination = NULL;
	gchar *leaf = NULL;
	gchar *destination_path = NULL;
	GError *error = NULL;
	gboolean already_there = FALSE;
	gboolean overwrite = FALSE;
	GtkWidget *dialog;
	gint response;

	if (g_mkdir_with_parents(backgrounds_dir, 0755) != 0 && errno != EEXIST) {
		report_error(_("Unable to create backgrounds folder '%s': %s"),
		             backgrounds_dir, g_strerror(errno));
		return;
	}

	leaf = g_path_get_basename(source_path);
	destination_path = g_build_filename(backgrounds_dir, leaf, NULL);
	source = g_file_new_for_path(source_path);
	destination = g_file_new_for_path(destination_path);
	already_there = g_file_equal(source, destination);

	if (!already_there && g_file_query_exists(destination, NULL)) {
		dialog = gtk_message_dialog_new(parent,
			GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
			_("A file named '%s' already exists in the backgrounds folder."),
			leaf);
		gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER_ON_PARENT);
		gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
			"%s", _("Replace the existing file?"));
		gtk_dialog_add_button(GTK_DIALOG(dialog), _("Cancel"),
		                      GTK_RESPONSE_CANCEL);
		gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Replace"),
		                      GTK_RESPONSE_ACCEPT);
		gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
		response = gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
		if (response != GTK_RESPONSE_ACCEPT)
			goto out;
		overwrite = TRUE;
	}

	if (!already_there && !g_file_copy(source, destination,
		G_FILE_COPY_ALL_METADATA |
		(overwrite ? G_FILE_COPY_OVERWRITE : G_FILE_COPY_NONE),
		NULL, NULL, NULL, &error)) {
		report_error(_("Unable to copy '%s' to '%s': %s"),
		             source_path, destination_path,
		             error ? error->message : _("The command failed."));
		g_clear_error(&error);
		goto out;
	}

	dialog = gtk_message_dialog_new(parent,
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
		"%s", already_there
			? _("The image is already in the backgrounds folder.")
			: _("The image was copied to the backgrounds folder."));
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER_ON_PARENT);
	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
		"%s", _("Would you like to use it as the desktop wallpaper now?"));
	gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Not now"),
	                      GTK_RESPONSE_CANCEL);
	gtk_dialog_add_button(GTK_DIALOG(dialog), _("_Use as Wallpaper"),
	                      GTK_RESPONSE_ACCEPT);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
	response = gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);

	if (response == GTK_RESPONSE_ACCEPT &&
	    !desktop_set_wallpaper(destination_path, TRUE, &error)) {
		report_error(_("Unable to set wallpaper: %s"),
		             error ? error->message : _("The command failed."));
		g_clear_error(&error);
	}

out:
	if (source)
		g_object_unref(source);
	if (destination)
		g_object_unref(destination);
	g_free(destination_path);
	g_free(leaf);
}

static void file_op(gpointer data, guint action, GtkWidget *unused)
{
	DirItem	*item;
	const guchar *path;
	int	n_selected;
	ViewIter iter;

	g_return_if_fail(window_with_focus != NULL);

	n_selected = view_count_selected(window_with_focus->view);

	if (n_selected < 1)
	{
		const char *prompt;

		switch (action)
		{
			case FILE_DUPLICATE_ITEM:
				prompt = _("Duplicate ... ?");
				break;
			case FILE_RENAME_ITEM:
				prompt = _("Rename ... ?");
				break;
			case FILE_LINK_ITEM:
				prompt = _("Symlink ... ?");
				break;
			case FILE_OPEN_FILE:
				prompt = _("Shift Open ... ?");
				break;
			case FILE_PROPERTIES:
				prompt = _("Properties of ... ?");
				break;
			case FILE_COPY_TO_BACKGROUNDS:
				prompt = _("Copy ... to Backgrounds ?");
				break;
#if defined(HAVE_GETXATTR) || defined(HAVE_ATTROPEN)
			case FILE_XATTRS:
				prompt = _("Extended attributes of ... ?");
				break;
#endif
			case FILE_SET_TYPE:
				prompt = _("Set type of ... ?");
				break;
			case FILE_RUN_ACTION:
				prompt = _("Set default application for ... ?");
				break;
			case FILE_SET_ICON:
				prompt = _("Set icon for ... ?");
				break;
			case FILE_TRASH:
				prompt = _("Move ... to Trash ?");
				break;
			case FILE_DELETE:
				prompt = _("DELETE ... permanently ?");
				break;
			case FILE_USAGE:
				prompt = _("Count the size of ... ?");
				break;
			case FILE_CHMOD_ITEMS:
				prompt = _("Set permissions on ... ?");
				break;
			case FILE_FIND:
				prompt = _("Search inside ... ?");
				break;
			case FILE_COPY_TO_CLIPBOARD:
				prompt = _("Copy ... to clipboard ?");
				break;
			case FILE_CUT_TO_CLIPBOARD:
				prompt = _("Cut ... to clipboard ?");
				break;
			default:
				g_warning("Unknown action!");
				return;
		}
		filer_target_mode(window_with_focus, target_callback,
					GINT_TO_POINTER(action), prompt);
		return;
	}

	switch (action)
	{
		case FILE_TRASH:
			move_to_trash(window_with_focus);
			return;
		case FILE_DELETE:
			delete_permanently(window_with_focus);
			return;
		case FILE_USAGE:
			usage(window_with_focus);
			return;
		case FILE_CHMOD_ITEMS:
			chmod_items(window_with_focus);
			return;
		case FILE_SET_TYPE:
			set_type_items(window_with_focus);
			return;
		case FILE_FIND:
			find(window_with_focus);
			return;
		case FILE_PROPERTIES:
		{
			GList *items;

			items = filer_selected_items(window_with_focus);
			infobox_show_list(items);
			destroy_glist(&items);
			return;
		}
		case FILE_RENAME_ITEM:
			if (n_selected > 1)
			{
				GList *items = NULL;
				ViewIter iter;

				view_get_iter(window_with_focus->view, &iter, VIEW_ITER_SELECTED);
				while ((item = iter.next(&iter)))
					items = g_list_prepend(items, item->leafname);
				items = g_list_reverse(items);

				bulk_rename(window_with_focus->sym_path, items);
				g_list_free(items);
				return;
			}
			break;	/* Not a bulk rename... see below */

		case FILE_COPY_TO_CLIPBOARD:
		case FILE_CUT_TO_CLIPBOARD:
		{
			GList *paths = filer_selected_items(window_with_focus);
			menu_set_clipboard_paths(paths, action == FILE_CUT_TO_CLIPBOARD);
			destroy_glist(&paths);
			return;
		}
		default:
			break;
	}

	/* All the following actions require exactly one file selected */

	if (n_selected > 1)
	{
		report_error(_("You cannot do this to more than "
				"one item at a time"));
		return;
	}

	view_get_iter(window_with_focus->view, &iter, VIEW_ITER_SELECTED);

	item = iter.next(&iter);
	g_return_if_fail(item != NULL);
	/* iter may be passed to filer_openitem... */

	if (item->base_type == TYPE_UNKNOWN)
		item = dir_update_item(window_with_focus->directory,
					item->leafname);

	if (!item)
	{
		report_error(_("Item no longer exists!"));
		return;
	}

	path = make_path(window_with_focus->sym_path, item->leafname);

	switch (action)
	{
		case FILE_DUPLICATE_ITEM:
			src_dest_action_item((const gchar *) path, di_image(item),
					_("Duplicate"), copy_cb,
					GDK_ACTION_COPY);
			break;
		case FILE_RENAME_ITEM:
			rox_debug_log("RENAME", "show-dialog path=%s",
				(const gchar *) path);
			src_dest_action_item((const gchar *) path, di_image(item),
					_("Rename"), rename_cb,
					GDK_ACTION_MOVE);
			break;
		case FILE_LINK_ITEM:
			src_dest_action_item((const gchar *) path, di_image(item),
					_("Symlink"), link_cb,
					GDK_ACTION_LINK);
			break;
		case FILE_OPEN_FILE:
			filer_openitem(window_with_focus, &iter,
				OPEN_SAME_WINDOW | OPEN_SHIFT);
			break;
		case FILE_RUN_ACTION:
			run_action(item);
			break;
		case FILE_SET_ICON:
			icon_set_handler_dialog(item, path);
			break;
		case FILE_COPY_TO_BACKGROUNDS:
			if (!item_is_wallpaper_image(item)) {
				report_error("%s", _("Only JPEG, PNG and SVG images can be copied to the backgrounds folder."));
				break;
			}
			copy_image_to_backgrounds((const gchar *) path);
			break;
#if defined(HAVE_GETXATTR) || defined(HAVE_ATTROPEN)
		case FILE_XATTRS:
			if (access((const char *) path, R_OK) == 0)
				xattrs_browser(item, path);
			break;
#endif
		default:
			g_warning("Unknown action!");
			return;
	}
}

static void show_key_help(GtkWidget *button, gpointer data)
{
	gboolean can_change_accels;

	g_object_get(G_OBJECT(gtk_settings_get_default()),
		     "gtk-can-change-accels", &can_change_accels,
		     NULL);

	if (!can_change_accels)
	{
		info_message(_("User-definable shortcuts are disabled by the active GTK3 "
			"settings. Enable them with your desktop settings manager, "
			"or add this line under [Settings] in "
			"~/.config/gtk-3.0/settings.ini:\n\n"
			"gtk-can-change-accels = true"));
		return;
	}

	info_message(_("To set a keyboard short-cut for a menu item:\n\n"
	"- Open the menu over a filer window,\n"
	"- Move the pointer over the item you want to use,\n"
	"- Press the key you want attached to it.\n\n"
	"The key will appear next to the menu item and you can just press "
	"that key without opening the menu in future."));
}

static GList *set_keys_button(Option *option, xmlNode *node, guchar *label)
{
	GtkWidget *button, *box;

	g_return_val_if_fail(option == NULL, NULL);

	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	button = gtk_button_new_with_label(_("Set keyboard shortcuts"));
	gtk_widget_set_halign(button, GTK_ALIGN_START);
	gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
	gtk_box_pack_start(GTK_BOX(box), button, FALSE, FALSE, 0);
	g_signal_connect(button, "clicked", G_CALLBACK(show_key_help), NULL);

	return g_list_append(NULL, box);
}
