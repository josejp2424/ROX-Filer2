/*
 * Agregado por josejp2424 (2026): configuración XDG centralizada para
 * ROX-Filer y el nuevo modo ROX Desktop.
 */
#include "config.h"
#include <glib.h>
#include <glib/gstdio.h>
#include "rox_config.h"

static gchar *config_dir_path;
static GHashTable *config_paths;

void rox_config_init(void)
{
    if (config_dir_path)
        return;

    /* Modificado por josejp2424 (2026): usar la ubicación tradicional de
     * configuración de ROX y mantener todos los ajustes nuevos dentro de
     * ROX-Filer. Para root resulta en
     * /root/.config/rox.sourceforge.net/ROX-Filer. */
    config_dir_path = g_build_filename(g_get_user_config_dir(),
                                       "rox.sourceforge.net",
                                       "ROX-Filer", NULL);
    config_paths = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    if (g_mkdir_with_parents(config_dir_path, 0700) != 0)
        g_warning("Unable to create ROX-Filer configuration directory: %s", config_dir_path);
}

const gchar *rox_config_dir(void)
{
    rox_config_init();
    return config_dir_path;
}

const gchar *rox_config_file(const gchar *name)
{
    gchar *path;
    rox_config_init();
    path = g_hash_table_lookup(config_paths, name);
    if (!path) {
        path = g_build_filename(config_dir_path, name, NULL);
        g_hash_table_insert(config_paths, g_strdup(name), path);
    }
    return path;
}

GKeyFile *rox_config_load(const gchar *name)
{
    GKeyFile *key_file = g_key_file_new();
    GError *error = NULL;
    const gchar *path = rox_config_file(name);

    if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_KEEP_COMMENTS, &error)) {
        if (!g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
            g_warning("Unable to read %s: %s", path, error->message);
        g_clear_error(&error);
    }
    return key_file;
}

gboolean rox_config_save(GKeyFile *key_file, const gchar *name, GError **error)
{
    gsize length = 0;
    gchar *data = g_key_file_to_data(key_file, &length, error);
    gboolean ok;
    if (!data)
        return FALSE;
    ok = g_file_set_contents(rox_config_file(name), data, length, error);
    g_free(data);
    return ok;
}
