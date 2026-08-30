/*
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef __VIEW_COLLECTION_H__
/*
 * Modificado por josejp2424 (2026):
 * port a GTK3 y adaptaciones específicas de esta versión.
 */

#define __VIEW_COLLECTION_H__

#include <gtk/gtk.h>

typedef struct _ViewCollectionClass ViewCollectionClass;

#define VIEW_COLLECTION(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST((obj), view_collection_get_type(), ViewCollection))

GtkWidget *view_collection_new(FilerWindow *filer_window);
GType view_collection_get_type(void);

#endif /* __VIEW_COLLECTION_H__ */
