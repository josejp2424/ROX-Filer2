#ifndef ROX_TRASH_H
#define ROX_TRASH_H

#include <gtk/gtk.h>
#include <gio/gio.h>

#include "filer.h"

/* Standard Freedesktop Trash helpers shared by the filer and ROX Desktop. */
const gchar *rox_trash_icon_name(void);
gchar *rox_trash_files_dir(void);
gchar *rox_trash_info_dir(void);
gboolean rox_trash_is_empty(void);
gboolean rox_trash_file(GFile *file, GError **error);
void rox_trash_open(FilerWindow *source_window);
gboolean rox_trash_filer_is_trash(FilerWindow *filer_window);
void rox_trash_restore_selected(FilerWindow *filer_window);
void rox_trash_empty(GtkWindow *parent);
GtkToolItem *rox_trash_toolbar_button_new(FilerWindow *filer_window);
GFileMonitor *rox_trash_monitor_new(GError **error);

#endif
