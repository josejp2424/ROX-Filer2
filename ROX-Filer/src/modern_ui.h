#ifndef _MODERN_UI_H
#define _MODERN_UI_H

#include <gtk/gtk.h>

void modern_ui_build(FilerWindow *filer_window, GtkWidget *vbox);
void modern_ui_attach_sidebar(FilerWindow *filer_window, GtkWidget *view_hbox);
void modern_ui_update_path(FilerWindow *filer_window);
void modern_ui_update_navigation(FilerWindow *filer_window);
void modern_ui_open_path_in_new_tab(FilerWindow *filer_window, const gchar *path);
gboolean modern_ui_open_path_as_tab_if_configured(FilerWindow *source, const gchar *path);
FilerWindow *modern_ui_primary_window(void);

#endif /* _MODERN_UI_H */
