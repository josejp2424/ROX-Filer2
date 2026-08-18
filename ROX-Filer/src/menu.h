/*
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _MENU_H
#define _MENU_H

/* 'action's for menu_rox_help */
enum {HELP_ABOUT, HELP_DIR, HELP_MANUAL};

typedef enum menu_icon_style {
  MIS_NONE, MIS_SMALL, MIS_LARGE,
  MIS_HUGE_UNUSED,
  MIS_CURRENT, /* As per current filer window */
  MIS_DEFAULT
} MenuIconStyle;

extern GtkAccelGroup	*filer_keys;

void menu_init(void);

RoxItemFactory *menu_create(RoxItemFactoryEntry *def, int n_entries,
			    const gchar *name, GtkAccelGroup *keys);
void menu_set_items_shaded(GtkWidget *menu, gboolean shaded, int from, int n);
void position_menu(GtkMenu *menu, gint *x, gint *y,
		   gboolean  *push_in, gpointer data);
void show_popup_menu(GtkWidget *menu, GdkEvent *event, int item);

gboolean ensure_filer_menu(void);
void show_filer_menu(FilerWindow *filer_window,
		     GdkEvent *event,
		     ViewIter *item);
void menu_popdown(void);
/* Shared terminal launcher used by filer actions and desktop Console. */
gboolean menu_open_terminal_at(const gchar *directory);
/* Hidden diagnostic entry point used by the bundled test script. */
gboolean menu_diagnose_run_in_terminal(const gchar *path);
gboolean menu_diagnose_rename_dialog(const gchar *path);
/* Shared helpers used by the native ROX Desktop. */
void menu_set_clipboard_paths(GList *paths, gboolean cut);
gboolean menu_rename_path(const gchar *path, GtkWindow *parent);
/* For "New" toolbar button */
void show_new_directory(FilerWindow *filer_window);
void show_new_file(FilerWindow *filer_window);
GtkWidget *create_menu_new(FilerWindow *filer_window);
GtkWidget *create_menu_new_for_path(const gchar *path, GtkWindow *parent);
GtkWidget *prepare_menu_new(FilerWindow *filer_window);
void show_menu_new(FilerWindow *filer_window);

/* Public menu handlers */
void menu_rox_help(gpointer data, guint action, GtkWidget *widget);
void menu_show_options(gpointer data, guint action, GtkWidget *widget);
void open_home(gpointer data, guint action, GtkWidget *widget);
void menu_show_shift_action(GtkWidget *menu_item, DirItem *item, gboolean next);

#endif /* _MENU_H */
