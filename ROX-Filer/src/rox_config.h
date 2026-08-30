/*
 * Agregado por josejp2424 (2026): configuración XDG centralizada para
 * ROX-Filer y el nuevo modo ROX Desktop.
 */
#ifndef ROX_CONFIG_H
#define ROX_CONFIG_H

#include <glib.h>

void rox_config_init(void);
const gchar *rox_config_dir(void);
const gchar *rox_config_file(const gchar *name);
GKeyFile *rox_config_load(const gchar *name);
gboolean rox_config_save(GKeyFile *key_file, const gchar *name, GError **error);

#endif
