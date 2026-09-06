#ifndef _ROX_SMB_H
#define _ROX_SMB_H

#include <gtk/gtk.h>

struct _FilerWindow;

void rox_smb_open_dialog(struct _FilerWindow *source_window);
gboolean rox_smb_compiled_with_libsmbclient(void);
gboolean rox_smb_unmount_path(const gchar *mountpoint, gchar **error_text);
gboolean rox_smb_mountpoint_is_managed(const gchar *mountpoint);

#endif
