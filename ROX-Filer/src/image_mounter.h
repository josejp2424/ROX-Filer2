/*
 * Rox-Filer2 image mounter
 * Copyright (C) 2026 josejp2424 and Rox-Filer2 contributors.
 *
 * Native image mounting helper shared by the Classic and Modern interfaces.
 */

#ifndef _IMAGE_MOUNTER_H
#define _IMAGE_MOUNTER_H

#include <gtk/gtk.h>

/* Keep this header independent from global.h.  FilerWindow is forward
 * declared so image_mounter.h can be included safely from menu.c and the
 * image-mounter implementation without depending on global include order. */
struct _FilerWindow;
typedef struct _FilerWindow FilerWindow;

/* Supported directly from the filer context menu: ISO, Puppy SFS,
 * SquashFS/SQFS and raw IMG images. */
gboolean image_mounter_can_handle(const gchar *path);

/* Returns TRUE only for mounts created by Rox-Filer2 which are still active.
 * mountpoint, when requested, must be freed with g_free(). */
gboolean image_mounter_is_mounted(const gchar *path, gchar **mountpoint);

/* Mount read-only and open the resulting directory in Rox-Filer2.  Modern
 * windows use a new tab; Classic opens a normal filer window. */
gboolean image_mounter_mount(const gchar *path,
                             GtkWindow *parent,
                             FilerWindow *source_window);

/* Open or unmount a mount previously created by image_mounter_mount(). */
gboolean image_mounter_open(const gchar *path, FilerWindow *source_window);
gboolean image_mounter_unmount(const gchar *path, GtkWindow *parent);

#endif /* _IMAGE_MOUNTER_H */
