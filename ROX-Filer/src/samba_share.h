#ifndef ROX_SAMBA_SHARE_H
#define ROX_SAMBA_SHARE_H

#include <gtk/gtk.h>

/* Runtime-only Samba usershare integration.  Rox-Filer2 never links against
 * Samba for this feature: the `net` and optional `testparm` helpers are
 * discovered when the action is used. */
gboolean samba_share_available(void);
gboolean samba_share_path_is_shared(const gchar *path);
const gchar *samba_share_emblem_icon(void);
void samba_share_cache_invalidate(void);
void samba_share_show_dialog(GtkWindow *parent, const gchar *path);
void samba_share_show_manager(GtkWindow *parent);

#endif /* ROX_SAMBA_SHARE_H */
