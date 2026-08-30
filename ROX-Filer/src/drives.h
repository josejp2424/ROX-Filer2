/*
 * ROX-Filer GTK3 partition and desktop drive integration.
 *
 * Agregado por josejp2424 (2026): modelo compartido de unidades para que
 * la barra de Particiones y ROX Desktop utilicen la misma detección,
 * montaje y selección de iconos del tema GTK.
 *
 * Copyright (C) 2026 josejp2424
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 */

#ifndef _ROX_DRIVES_H
#define _ROX_DRIVES_H

#include <gtk/gtk.h>
#include <gio/gio.h>

struct _FilerWindow;

typedef struct _RoxDriveInfo RoxDriveInfo;

struct _RoxDriveInfo
{
	gchar *name;
	gchar *device;
	gchar *label;
	gchar *fstype;
	gchar *mountpoint;
	gchar *size;
	gchar *type;
	gchar *transport;
	gchar *model;
	gboolean removable;
	gboolean optical;
	gboolean network;
	gboolean solid_state;
};

GtkToolItem *drives_toolbar_button_new(struct _FilerWindow *filer_window);

/* Agregado por josejp2424 (2026): API común usada por ROX Desktop y por la
 * GUI de Particiones. El GPtrArray devuelto libera sus RoxDriveInfo con
 * rox_drive_info_free(). */
GPtrArray *rox_drives_read(GError **error);
RoxDriveInfo *rox_drive_info_copy(const RoxDriveInfo *source);
void rox_drive_info_free(gpointer data);
RoxDriveInfo *rox_drive_find_by_device(const gchar *device, GError **error);

gchar *rox_drive_find_mountpoint(const gchar *device);
gchar *rox_drive_mount(const RoxDriveInfo *drive, gchar **error_text);
gboolean rox_drive_unmount(const RoxDriveInfo *drive, gchar **error_text);
gboolean rox_drive_eject(const RoxDriveInfo *drive, gchar **error_text);

/* Agregado por josejp2424 (2026): resolvedor único de iconos.
 * ROX Desktop y la GUI de Particiones deben usar este mismo GIcon para que
 * USB, SD/MMC, NVMe/SSD y medios ópticos nunca sigan caminos distintos. */
const gchar *rox_drive_icon_name(const RoxDriveInfo *drive);
GIcon *rox_drive_get_icon(const RoxDriveInfo *drive);
GtkWidget *rox_drive_icon_widget_new(const RoxDriveInfo *drive, gint size);
const gchar *rox_drive_display_name(const RoxDriveInfo *drive);

#endif /* _ROX_DRIVES_H */
