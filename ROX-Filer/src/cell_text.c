/*
 * ROX-Filer (GTK3)
 *
 * cell_text.c - GTK3-compatible cell renderer helper.
 *
 * The historical custom GtkCellRendererText subclass accessed private
 * renderer members.  The GTK3 details view uses the standard renderer through
 * public properties such as "text", "foreground-rgba", "weight" and "font".
 */

/*
 * Modificado por josejp2424 (2026):
 * port a GTK3 y adaptaciones específicas de esta versión.
 */

#include "cell_text.h"
#include <gtk/gtk.h>

GtkCellRenderer *cell_text_new(void)
{
    GtkCellRenderer *cell = gtk_cell_renderer_text_new();

    /* Keep a small, ROX-like padding by default. Callers may override. */
    g_object_set(G_OBJECT(cell),
                 "xpad", 2,
                 "ypad", 0,
                 NULL);

    return cell;
}
