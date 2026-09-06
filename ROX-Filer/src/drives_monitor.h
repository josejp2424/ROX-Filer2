#ifndef _ROX_DRIVES_MONITOR_H
#define _ROX_DRIVES_MONITOR_H

#include <gio/gio.h>
#include "drives.h"

typedef void (*RoxDrivesMonitorFunc)(GPtrArray *drives,
                                     const GError *error,
                                     gpointer user_data);

void rox_drives_monitor_subscribe(GObject *owner,
                                  RoxDrivesMonitorFunc callback,
                                  gpointer user_data);
void rox_drives_monitor_request_scan(void);
GPtrArray *rox_drives_monitor_snapshot_copy(void);
RoxDriveInfo *rox_drives_monitor_find_by_device(const gchar *device);

#endif
