/*
 * Rox-Filer2 image mounter
 * Copyright (C) 2026 josejp2424 and Rox-Filer2 contributors.
 *
 * This module deliberately does not use GVfs.  Root sessions (the normal
 * Puppy model) use losetup + mount directly.  Normal-user sessions use
 * udisksctl when it is available.  All images are mounted read-only.
 *
 * The host still provides the Linux kernel and its filesystem drivers.
 */

#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>

#include "global.h"
#include "filer.h"
#include "gui_support.h"
#include "i18n.h"
#include "image_mounter.h"
#include "modern_ui.h"

#define IMAGE_MOUNTER_GROUP "ImageMounter"
#define IMAGE_MOUNTER_ROOT  "/media/rox-filer2-images"

typedef enum {
    IMAGE_MOUNT_METHOD_NONE = 0,
    IMAGE_MOUNT_METHOD_ROOT,
    IMAGE_MOUNT_METHOD_UDISKS
} ImageMountMethod;

typedef struct {
    gchar *image;
    gchar *loopdev;
    gchar *blockdev;
    gchar *mountpoint;
    ImageMountMethod method;
} ImageMountState;

static void image_mount_state_clear(ImageMountState *state)
{
    if (!state)
        return;
    g_free(state->image);
    g_free(state->loopdev);
    g_free(state->blockdev);
    g_free(state->mountpoint);
    memset(state, 0, sizeof(*state));
}

static gchar *strip_output(gchar *text)
{
    gchar *start;

    if (!text)
        return NULL;
    start = g_strstrip(text);
    if (start != text)
        memmove(text, start, strlen(start) + 1);
    return text;
}

static gchar *helper_path(const gchar *name);

/* argv[0] es el nombre del programa; se sustituye por su ruta absoluta y se
 * lanza sin G_SPAWN_SEARCH_PATH.  Devuelve FALSE si el ayudante no existe en
 * ninguno de los directorios de sistema. */
static gboolean command_run(const gchar **argv_in, gchar **stdout_text,
                            gchar **stderr_text)
{
    GError *error = NULL;
    gchar *out = NULL;
    gchar *err = NULL;
    gchar *program = NULL;
    gchar **argv = NULL;
    gint status = 0;
    gboolean spawned;
    gboolean ok = FALSE;
    guint n;

    if (!argv_in || !argv_in[0])
        return FALSE;

    program = helper_path(argv_in[0]);
    if (!program) {
        if (stderr_text)
            *stderr_text = g_strdup_printf(
                _("Required program '%s' was not found."), argv_in[0]);
        if (stdout_text)
            *stdout_text = NULL;
        return FALSE;
    }

    for (n = 0; argv_in[n]; n++)
        ;
    argv = g_new0(gchar *, n + 1);
    argv[0] = program;
    for (n = 1; argv_in[n]; n++)
        argv[n] = (gchar *) argv_in[n];

    spawned = g_spawn_sync(NULL, argv, NULL, G_SPAWN_DEFAULT,
                           NULL, NULL, &out, &err, &status, &error);
    g_free(argv);
    g_free(program);
    if (spawned)
        ok = g_spawn_check_wait_status(status, &error);

    if (!ok && error) {
        if (!err || !*err) {
            g_free(err);
            err = g_strdup(error->message);
        }
        g_error_free(error);
    }

    strip_output(out);
    strip_output(err);

    if (stdout_text)
        *stdout_text = out;
    else
        g_free(out);

    if (stderr_text)
        *stderr_text = err;
    else
        g_free(err);

    return ok;
}

/* 2.12.2-85: los ayudantes privilegiados se resuelven por ruta absoluta.
 *
 * Hasta -84 todo se lanzaba con G_SPAWN_SEARCH_PATH, de modo que losetup,
 * mount, umount, blkid, partx y blockdev se buscaban en $PATH y se
 * ejecutaban como root.  Un directorio escribible antes de /sbin en el PATH
 * bastaba para ejecutar codigo arbitrario con privilegios.  Ahora se busca
 * solo en los directorios de sistema habituales. */
static const gchar *const helper_dirs[] = {
    "/sbin", "/usr/sbin", "/bin", "/usr/bin", "/usr/local/sbin",
    "/usr/local/bin", NULL
};

static gchar *helper_path(const gchar *name)
{
    guint i;

    if (!name || !*name || strchr(name, '/'))
        return NULL;

    for (i = 0; helper_dirs[i]; i++) {
        gchar *candidate = g_build_filename(helper_dirs[i], name, NULL);
        if (g_file_test(candidate, G_FILE_TEST_IS_EXECUTABLE))
            return candidate;
        g_free(candidate);
    }
    return NULL;
}

static gboolean program_exists(const gchar *name)
{
    gchar *path = helper_path(name);
    gboolean exists = path != NULL;
    g_free(path);
    return exists;
}

static gchar *canonical_image_path(const gchar *path)
{
    if (!path || !*path)
        return NULL;
    return g_canonicalize_filename(path, NULL);
}

static gchar *state_directory(void)
{
    /* Use XDG_RUNTIME_DIR only when the session really provides it.
     * g_get_user_runtime_dir() may fall back to a persistent cache path,
     * which is wrong for loop/mount bookkeeping that must be transient. */
    const gchar *runtime = g_getenv("XDG_RUNTIME_DIR");
    gchar *base;
    gchar *dir;

    if (runtime && *runtime && g_path_is_absolute(runtime)) {
        base = g_strdup(runtime);
    } else {
        base = g_strdup_printf("%s/rox-filer2-%lu",
                               g_get_tmp_dir(), (gulong) getuid());
    }

    dir = g_build_filename(base, "rox-filer2", "image-mounter", NULL);
    g_free(base);

    if (g_mkdir_with_parents(dir, 0700) != 0) {
        g_free(dir);
        return NULL;
    }
    (void) chmod(dir, 0700);
    return dir;
}

static gchar *state_path_for_image(const gchar *path)
{
    gchar *canonical = canonical_image_path(path);
    gchar *hash;
    gchar *dir;
    gchar *name;
    gchar *result;

    if (!canonical)
        return NULL;

    hash = g_compute_checksum_for_string(G_CHECKSUM_SHA256, canonical, -1);
    dir = state_directory();
    name = g_strconcat(hash, ".ini", NULL);
    result = dir ? g_build_filename(dir, name, NULL) : NULL;

    g_free(name);
    g_free(dir);
    g_free(hash);
    g_free(canonical);
    return result;
}

/* /proc/self/mounts escapes whitespace and backslashes using octal sequences. */
static gchar *mount_field_unescape(const gchar *field)
{
    GString *out;
    const gchar *p;

    if (!field)
        return NULL;
    out = g_string_sized_new(strlen(field));
    for (p = field; *p; p++) {
        if (*p == '\\' && p[1] && p[2] && p[3] &&
            /* 2.12.2-85: son secuencias octales; hasta -84 se aceptaban
             * 8 y 9, que producian valores fuera de rango. */
            p[1] >= '0' && p[1] <= '7' &&
            p[2] >= '0' && p[2] <= '7' &&
            p[3] >= '0' && p[3] <= '7') {
            gint value = (p[1] - '0') * 64 + (p[2] - '0') * 8 + (p[3] - '0');
            g_string_append_c(out, (gchar) value);
            p += 3;
        } else {
            g_string_append_c(out, *p);
        }
    }
    return g_string_free(out, FALSE);
}

static gchar *find_mountpoint_for_device(const gchar *device)
{
    gchar *contents = NULL;
    gchar **lines = NULL;
    gchar *result = NULL;
    gsize length = 0;
    guint i;

    if (!device || !*device)
        return NULL;
    if (!g_file_get_contents("/proc/self/mounts", &contents, &length, NULL))
        return NULL;

    lines = g_strsplit(contents, "\n", -1);
    for (i = 0; lines[i]; i++) {
        gchar **fields;
        gchar *source;
        gchar *target;

        if (!*lines[i])
            continue;
        fields = g_strsplit(lines[i], " ", 4);
        if (!fields[0] || !fields[1]) {
            g_strfreev(fields);
            continue;
        }
        source = mount_field_unescape(fields[0]);
        target = mount_field_unescape(fields[1]);
        if (g_strcmp0(source, device) == 0) {
            result = target;
            g_free(source);
            g_strfreev(fields);
            break;
        }
        g_free(source);
        g_free(target);
        g_strfreev(fields);
    }

    g_strfreev(lines);
    g_free(contents);
    return result;
}

static gboolean mountpoint_is_active(const gchar *mountpoint)
{
    gchar *contents = NULL;
    gchar **lines = NULL;
    gboolean active = FALSE;
    gsize length = 0;
    guint i;

    if (!mountpoint || !*mountpoint)
        return FALSE;
    if (!g_file_get_contents("/proc/self/mounts", &contents, &length, NULL))
        return FALSE;

    lines = g_strsplit(contents, "\n", -1);
    for (i = 0; lines[i]; i++) {
        gchar **fields;
        gchar *target;

        if (!*lines[i])
            continue;
        fields = g_strsplit(lines[i], " ", 4);
        if (!fields[1]) {
            g_strfreev(fields);
            continue;
        }
        target = mount_field_unescape(fields[1]);
        if (g_strcmp0(target, mountpoint) == 0) {
            active = TRUE;
            g_free(target);
            g_strfreev(fields);
            break;
        }
        g_free(target);
        g_strfreev(fields);
    }

    g_strfreev(lines);
    g_free(contents);
    return active;
}

static gboolean save_state(const ImageMountState *state)
{
    GKeyFile *key;
    gchar *state_path;
    gchar *data;
    gsize len;
    GError *error = NULL;
    gboolean ok;

    state_path = state_path_for_image(state->image);
    if (!state_path)
        return FALSE;

    key = g_key_file_new();
    g_key_file_set_string(key, IMAGE_MOUNTER_GROUP, "image", state->image);
    g_key_file_set_string(key, IMAGE_MOUNTER_GROUP, "loopdev", state->loopdev);
    g_key_file_set_string(key, IMAGE_MOUNTER_GROUP, "blockdev", state->blockdev);
    g_key_file_set_string(key, IMAGE_MOUNTER_GROUP, "mountpoint", state->mountpoint);
    g_key_file_set_integer(key, IMAGE_MOUNTER_GROUP, "method", (gint) state->method);
    data = g_key_file_to_data(key, &len, NULL);
    ok = g_file_set_contents(state_path, data, (gssize) len, &error);
    if (ok)
        (void) chmod(state_path, 0600);
    else if (error)
        g_error_free(error);

    g_free(data);
    g_key_file_unref(key);
    g_free(state_path);
    return ok;
}

static gboolean load_state(const gchar *path, ImageMountState *state)
{
    GKeyFile *key;
    gchar *state_path;
    GError *error = NULL;
    gboolean ok = FALSE;

    memset(state, 0, sizeof(*state));
    state_path = state_path_for_image(path);
    if (!state_path)
        return FALSE;

    key = g_key_file_new();
    if (!g_key_file_load_from_file(key, state_path, G_KEY_FILE_NONE, &error)) {
        if (error)
            g_error_free(error);
        goto out;
    }

    state->image = g_key_file_get_string(key, IMAGE_MOUNTER_GROUP, "image", NULL);
    state->loopdev = g_key_file_get_string(key, IMAGE_MOUNTER_GROUP, "loopdev", NULL);
    state->blockdev = g_key_file_get_string(key, IMAGE_MOUNTER_GROUP, "blockdev", NULL);
    state->mountpoint = g_key_file_get_string(key, IMAGE_MOUNTER_GROUP, "mountpoint", NULL);
    state->method = (ImageMountMethod) g_key_file_get_integer(key,
                            IMAGE_MOUNTER_GROUP, "method", NULL);

    ok = state->image && state->loopdev && state->blockdev && state->mountpoint &&
         state->method != IMAGE_MOUNT_METHOD_NONE;
out:
    g_key_file_unref(key);
    g_free(state_path);
    if (!ok)
        image_mount_state_clear(state);
    return ok;
}

static void remove_state_file(const gchar *path)
{
    gchar *state_path = state_path_for_image(path);
    if (state_path) {
        (void) g_unlink(state_path);
        g_free(state_path);
    }
}

/* 2.12.2-85: todo punto de montaje que vayamos a desmontar o borrar tiene
 * que estar dentro del arbol que gestionamos.
 *
 * Hasta -84, image_mounter_unmount() tomaba "mountpoint" del archivo de
 * estado y se lo pasaba a umount(8) como root, y despues hacia g_rmdir()
 * sobre esa misma ruta, sin comprobar nada.  Un .ini heredado, corrupto o
 * escrito a medias bastaba para que Rox-Filer2 desmontara y borrara un
 * directorio ajeno.  Verificado en pruebas: con mountpoint=/tmp/victima
 * el modulo desmontaba ese tmpfs y eliminaba el directorio. */
static gboolean mountpoint_is_ours(const gchar *mountpoint)
{
    static const gchar *prefix = IMAGE_MOUNTER_ROOT "/";

    if (!mountpoint || !*mountpoint)
        return FALSE;
    if (!g_str_has_prefix(mountpoint, prefix))
        return FALSE;
    /* Ningun componente ".." puede escapar del arbol gestionado. */
    if (strstr(mountpoint, "/../") || g_str_has_suffix(mountpoint, "/.."))
        return FALSE;
    return mountpoint[strlen(prefix)] != '\0';
}

static gboolean suffix_is(const gchar *path, const gchar *suffix)
{
    gsize path_len;
    gsize suffix_len;

    if (!path || !suffix)
        return FALSE;
    path_len = strlen(path);
    suffix_len = strlen(suffix);
    if (path_len < suffix_len)
        return FALSE;
    return g_ascii_strcasecmp(path + path_len - suffix_len, suffix) == 0;
}

gboolean image_mounter_can_handle(const gchar *path)
{
    gboolean uncertain = FALSE;
    gchar *content_type;
    gboolean supported = FALSE;

    if (!path || !g_file_test(path, G_FILE_TEST_IS_REGULAR))
        return FALSE;

    if (suffix_is(path, ".iso") || suffix_is(path, ".sfs") ||
        suffix_is(path, ".squashfs") || suffix_is(path, ".sqfs") ||
        suffix_is(path, ".sqsh") || suffix_is(path, ".img"))
        return TRUE;

    content_type = g_content_type_guess(path, NULL, 0, &uncertain);
    if (content_type) {
        supported = g_content_type_is_a(content_type, "application/vnd.squashfs") ||
                    g_content_type_is_a(content_type, "application/vnd.efi.iso") ||
                    g_content_type_is_a(content_type, "application/x-cd-image") ||
                    g_content_type_is_a(content_type, "application/x-iso9660-image") ||
                    g_content_type_is_a(content_type, "application/vnd.efi.img") ||
                    g_content_type_is_a(content_type, "application/x-raw-disk-image");
        g_free(content_type);
    }
    return supported;
}

static gboolean image_is_partitionable_raw(const gchar *path)
{
    return suffix_is(path, ".img");
}

gboolean image_mounter_is_mounted(const gchar *path, gchar **mountpoint)
{
    ImageMountState state;
    gboolean active;

    if (mountpoint)
        *mountpoint = NULL;
    if (!load_state(path, &state))
        return FALSE;

    active = mountpoint_is_active(state.mountpoint);
    if (active && mountpoint)
        *mountpoint = g_strdup(state.mountpoint);
    if (!active)
        remove_state_file(path);
    image_mount_state_clear(&state);
    return active;
}

static gchar *blkid_value(const gchar *device, const gchar *field)
{
    gchar *out = NULL;
    gchar *err = NULL;
    const gchar *argv[] = {
        "blkid", "-p", "-o", "value",
        "-s", field, device, NULL
    };

    if (!program_exists("blkid"))
        return NULL;
    if (!command_run(argv, &out, &err)) {
        g_free(out);
        out = NULL;
    }
    g_free(err);
    return out;
}

static guint64 block_size_bytes(const gchar *device)
{
    gchar *name;
    gchar *path;
    gchar *contents = NULL;
    guint64 sectors = 0;

    name = g_path_get_basename(device);
    path = g_build_filename("/sys/class/block", name, "size", NULL);
    if (g_file_get_contents(path, &contents, NULL, NULL))
        sectors = g_ascii_strtoull(contents, NULL, 10);
    g_free(contents);
    g_free(path);
    g_free(name);
    return sectors * 512ULL;
}

/* 2.12.2-85: numero real de particion a partir de /dev/loopNpM. */
static guint partition_number(const gchar *device)
{
    const gchar *p;

    if (!device)
        return 0;
    p = strrchr(device, 'p');
    if (!p || !g_ascii_isdigit(p[1]))
        return 0;
    return (guint) g_ascii_strtoull(p + 1, NULL, 10);
}

/* Hasta -84 esto ordenaba con g_strcmp0, es decir lexicograficamente, de
 * modo que loop0p10 quedaba entre loop0p1 y loop0p2.  Como la etiqueta del
 * combo usaba ademas el indice del array y no el numero real, el usuario
 * elegia "Partition 2" y montaba /dev/loop0p10. */
static gint partition_name_compare(gconstpointer a, gconstpointer b)
{
    const gchar *const *sa = a;
    const gchar *const *sb = b;
    guint na = partition_number(*sa);
    guint nb = partition_number(*sb);

    if (na != nb)
        return na < nb ? -1 : 1;
    return g_strcmp0(*sa, *sb);
}

static GPtrArray *loop_partitions(const gchar *loopdev)
{
    GPtrArray *parts;
    gchar *loop_name;
    gchar *sys_dir;
    GDir *dir;
    const gchar *entry;

    parts = g_ptr_array_new_with_free_func(g_free);
    loop_name = g_path_get_basename(loopdev);
    sys_dir = g_build_filename("/sys/class/block", loop_name, NULL);
    dir = g_dir_open(sys_dir, 0, NULL);
    if (!dir)
        goto out;

    while ((entry = g_dir_read_name(dir)) != NULL) {
        gchar *dev;
        if (!g_str_has_prefix(entry, loop_name))
            continue;
        if (entry[strlen(loop_name)] != 'p')
            continue;
        if (!g_ascii_isdigit(entry[strlen(loop_name) + 1]))
            continue;
        dev = g_build_filename("/dev", entry, NULL);
        if (g_file_test(dev, G_FILE_TEST_EXISTS))
            g_ptr_array_add(parts, dev);
        else
            g_free(dev);
    }
    g_dir_close(dir);
out:
    g_free(sys_dir);
    g_free(loop_name);
    g_ptr_array_sort(parts, partition_name_compare);
    return parts;
}

static gboolean partition_lists_equal(GPtrArray *a, GPtrArray *b)
{
    guint i;

    if (!a || !b || a->len != b->len)
        return FALSE;
    for (i = 0; i < a->len; i++) {
        if (g_strcmp0(g_ptr_array_index(a, i),
                      g_ptr_array_index(b, i)) != 0)
            return FALSE;
    }
    return TRUE;
}

static GPtrArray *wait_for_partitions(const gchar *loopdev)
{
    GPtrArray *stable = NULL;
    guint stable_checks = 0;
    guint tries;

    /* A partitioned IMG may expose p1 before p2/p3 have appeared in sysfs.
     * Do not accept the first non-empty snapshot: wait until the complete
     * set is unchanged for several polls.  This avoids auto-mounting p1 of
     * a multi-partition image simply because it appeared first. */
    for (tries = 0; tries < 20; tries++) {
        GPtrArray *current = loop_partitions(loopdev);

        if (partition_lists_equal(stable, current)) {
            stable_checks++;
            g_ptr_array_free(current, TRUE);
        } else {
            if (stable)
                g_ptr_array_free(stable, TRUE);
            stable = current;
            stable_checks = 1;
        }

        /* Five identical non-empty snapshots span roughly 400 ms.  The work
         * happens off the GTK thread, so favour correctness over an early
         * auto-mount decision. */
        if (stable && stable->len > 0 && stable_checks >= 5)
            break;

        g_usleep(100000);
    }

    if (!stable)
        stable = g_ptr_array_new_with_free_func(g_free);
    return stable;
}

static gchar *partition_description(const gchar *device, guint index)
{
    gchar *fstype = blkid_value(device, "TYPE");
    gchar *label = blkid_value(device, "LABEL");
    guint64 size = block_size_bytes(device);
    gchar *size_text = size ? g_format_size(size) : NULL;
    gchar *result;
    /* 2.12.2-85: numerar por la particion real, no por la posicion en la
     * lista; con numeracion no contigua (p1, p2, p5) el indice mentia. */
    guint number = partition_number(device);

    if (number == 0)
        number = index + 1;

    if (!fstype || !*fstype) {
        g_free(fstype);
        fstype = g_strdup(_("Unknown filesystem"));
    }

    if (label && *label && size_text)
        result = g_strdup_printf(_("Partition %u — %s — %s — %s"),
                                 number, fstype, size_text, label);
    else if (label && *label)
        result = g_strdup_printf(_("Partition %u — %s — %s"),
                                 number, fstype, label);
    else if (size_text)
        result = g_strdup_printf(_("Partition %u — %s — %s"),
                                 number, fstype, size_text);
    else
        result = g_strdup_printf(_("Partition %u — %s"), number, fstype);

    g_free(fstype);
    g_free(label);
    g_free(size_text);
    return result;
}

/* 2.12.2-85: reemplazada por choose_partition_from_labels(), que recibe
 * las descripciones ya calculadas en el hilo de trabajo en vez de
 * invocar blkid(8) mientras se construye el combo. */

static gchar *extract_loop_device(const gchar *text)
{
    const gchar *p;
    const gchar *end;

    if (!text)
        return NULL;
    p = strstr(text, "/dev/loop");
    while (p) {
        end = p + strlen("/dev/loop");
        if (g_ascii_isdigit(*end)) {
            while (g_ascii_isdigit(*end))
                end++;
            return g_strndup(p, end - p);
        }
        p = strstr(p + 1, "/dev/loop");
    }
    return NULL;
}

static gchar *setup_loop_root(const gchar *image, gchar **error_text)
{
    gchar *out = NULL;
    gchar *err = NULL;
    gchar *loopdev = NULL;
    const gchar *argv[] = {
        "losetup", "--find", "--show",
        "--read-only", "--partscan", image, NULL
    };

    if (!program_exists("losetup")) {
        if (error_text)
            *error_text = g_strdup_printf(_("Required program '%s' was not found."), "losetup");
        return NULL;
    }

    /* Modern util-linux: one atomic read-only setup with partition scan. */
    if (command_run(argv, &out, &err)) {
        loopdev = extract_loop_device(out);
        if (!loopdev && out && g_str_has_prefix(out, "/dev/loop"))
            loopdev = g_strdup(out);
    }

    /* Older Puppy releases may provide a small losetup without --show or
     * --partscan.  Keep the historical two-step setup as a compatibility
     * fallback.  The mount itself remains read-only even if -r is missing. */
    if (!loopdev) {
        gchar *free_out = NULL;
        gchar *free_err = NULL;
        const gchar *find_argv[] = {"losetup", "-f", NULL};

        if (command_run(find_argv, &free_out, &free_err) &&
            free_out && g_str_has_prefix(free_out, "/dev/loop")) {
            gchar *setup_err = NULL;
            const gchar *setup_ro[] = {
                "losetup", "-r", free_out,
                image, NULL
            };
            const gchar *setup_rw[] = {
                "losetup", free_out, image, NULL
            };

            if (command_run(setup_ro, NULL, &setup_err)) {
                loopdev = g_strdup(free_out);
            } else {
                g_clear_pointer(&setup_err, g_free);
                if (command_run(setup_rw, NULL, &setup_err)) {
                    /* BusyBox variants without -r: request kernel RO state
                     * when blockdev is available. mount(8) still gets -o ro. */
                    if (program_exists("blockdev")) {
                        const gchar *ro_argv[] = {
                            "blockdev", "--setro",
                            free_out, NULL
                        };
                        (void) command_run(ro_argv, NULL, NULL);
                    }
                    loopdev = g_strdup(free_out);
                }
            }
            if (!loopdev && setup_err) {
                g_free(err);
                err = g_strdup(setup_err);
            }
            g_free(setup_err);
        }
        if (!loopdev && free_err && *free_err) {
            g_free(err);
            err = g_strdup(free_err);
        }
        g_free(free_out);
        g_free(free_err);
    }

    if (!loopdev && error_text)
        *error_text = g_strdup(err && *err ? err :
                               _("Unable to determine the loop device created for this image."));
    g_free(out);
    g_free(err);
    return loopdev;
}

static gchar *setup_loop_udisks(const gchar *image, gchar **error_text)
{
    gchar *out = NULL;
    gchar *err = NULL;
    gchar *loopdev = NULL;
    const gchar *argv[] = {
        "udisksctl", "loop-setup",
        "--read-only", "--file", image, NULL
    };

    if (!command_run(argv, &out, &err)) {
        if (error_text)
            *error_text = g_strdup(err && *err ? err : _("Unknown error"));
        g_free(out);
        g_free(err);
        return NULL;
    }
    loopdev = extract_loop_device(out);
    if (!loopdev && error_text)
        *error_text = g_strdup(_("Unable to determine the loop device created for this image."));
    g_free(out);
    g_free(err);
    return loopdev;
}

static void detach_loop_root(const gchar *loopdev)
{
    const gchar *argv[] = {"losetup", "-d", loopdev, NULL};
    if (loopdev && program_exists("losetup"))
        (void) command_run(argv, NULL, NULL);
}

static void detach_loop_udisks(const gchar *loopdev)
{
    const gchar *argv[] = {
        "udisksctl", "loop-delete",
        "--block-device", loopdev, NULL
    };
    if (loopdev && program_exists("udisksctl"))
        (void) command_run(argv, NULL, NULL);
}

static gchar *safe_mount_name(const gchar *image, const gchar *blockdev)
{
    gchar *base = g_path_get_basename(image);
    gchar *block = g_path_get_basename(blockdev);
    GString *safe = g_string_new(NULL);
    const guchar *p;
    gchar *result;

    for (p = (const guchar *) base; *p && safe->len < 72; p++) {
        if (g_ascii_isalnum(*p) || *p == '-' || *p == '_' || *p == '.')
            g_string_append_c(safe, (gchar) *p);
        else
            g_string_append_c(safe, '_');
    }
    if (safe->len == 0)
        g_string_assign(safe, "image");
    result = g_strdup_printf("%s-%s", safe->str, block);
    g_string_free(safe, TRUE);
    g_free(base);
    g_free(block);
    return result;
}

static gchar *mount_root_block(const gchar *image, const gchar *blockdev,
                               gchar **error_text)
{
    gchar *name;
    gchar *mountpoint;
    gchar *err = NULL;
    const gchar *argv[] = {
        "mount", "-o", "ro,nosuid,nodev",
        blockdev, NULL, NULL
    };

    if (!program_exists("mount")) {
        if (error_text)
            *error_text = g_strdup_printf(_("Required program '%s' was not found."), "mount");
        return NULL;
    }
    if (g_mkdir_with_parents(IMAGE_MOUNTER_ROOT, 0755) != 0) {
        if (error_text)
            *error_text = g_strdup_printf(_("Unable to create mount directory '%s': %s"),
                                          IMAGE_MOUNTER_ROOT, g_strerror(errno));
        return NULL;
    }

    name = safe_mount_name(image, blockdev);
    mountpoint = g_build_filename(IMAGE_MOUNTER_ROOT, name, NULL);
    g_free(name);
    if (g_mkdir_with_parents(mountpoint, 0755) != 0) {
        if (error_text)
            *error_text = g_strdup_printf(_("Unable to create mount directory '%s': %s"),
                                          mountpoint, g_strerror(errno));
        g_free(mountpoint);
        return NULL;
    }

    argv[4] = mountpoint;
    if (!command_run(argv, NULL, &err)) {
        if (error_text)
            *error_text = g_strdup(err && *err ? err : _("Unknown error"));
        g_free(err);
        (void) g_rmdir(mountpoint);
        g_free(mountpoint);
        return NULL;
    }
    g_free(err);
    return mountpoint;
}

static gchar *mount_udisks_block(const gchar *blockdev, gchar **error_text)
{
    gchar *out = NULL;
    gchar *err = NULL;
    gchar *mountpoint = NULL;
    const gchar *argv[] = {
        "udisksctl", "mount",
        "--block-device", blockdev, NULL
    };

    if (!command_run(argv, &out, &err)) {
        if (error_text)
            *error_text = g_strdup(err && *err ? err : _("Unknown error"));
        g_free(out);
        g_free(err);
        return NULL;
    }

    /* Prefer the kernel's view over localized udisksctl output. */
    mountpoint = find_mountpoint_for_device(blockdev);
    if (!mountpoint && out) {
        gchar *at = strstr(out, " at ");
        if (at) {
            gchar *end;
            mountpoint = g_strdup(at + 4);
            g_strstrip(mountpoint);
            end = mountpoint + strlen(mountpoint);
            if (end > mountpoint && end[-1] == '.')
                end[-1] = '\0';
        }
    }
    if (!mountpoint && error_text)
        *error_text = g_strdup(_("The image was mounted, but its mount point could not be found."));

    g_free(out);
    g_free(err);
    return mountpoint;
}

/* ------------------------------------------------------------------
 * 2.12.2-86: montaje y desmontaje asincronos y lifetime seguro.
 *
 * Hasta -84 todo el modulo corria con g_spawn_sync() en el hilo principal
 * de GTK, y wait_for_partitions() ademas hacia una espera activa de 20
 * iteraciones de 50 ms.  Medido: montar un .sfs tardaba 14 ms, pero montar
 * un .img cuyas particiones no aparecen de inmediato bloqueaba la interfaz
 * 1027 ms, y hasta unos 2 s cuando partx(8) estaba instalado.  El modulo
 * SMB ya hacia esto bien con GTask; este ahora sigue el mismo patron.
 *
 * El trabajo se reparte en dos fases porque la eleccion de particion
 * necesita volver al hilo principal para mostrar el dialogo:
 *
 *   fase 1 (hilo)    losetup + deteccion de particiones + descripciones
 *   -> hilo principal: dialogo de particion si hay mas de una
 *   fase 2 (hilo)    mount
 *   -> hilo principal: guardar estado y abrir el directorio
 * ------------------------------------------------------------------ */

typedef struct {
    gchar            *image;        /* ruta canonica */
    GtkWidget        *parent_widget;/* referencia fuerte; puede destruirse */
    gulong            parent_destroy_handler;
    gboolean          parent_destroyed;
    GtkWidget        *progress;     /* dialogo con spinner */
    GtkWidget        *source_widget;/* referencia fuerte a la ventana origen */
    ImageMountMethod  method;
    gchar            *loopdev;
    gchar            *blockdev;
    gchar            *mountpoint;
    gchar            *error_text;
    GPtrArray        *parts;        /* gchar* dispositivos */
    GPtrArray        *labels;       /* gchar* descripciones */
} MountJob;

static void widget_slot_destroyed(GtkWidget *widget, gpointer data)
{
    GtkWidget **slot = data;

    if (slot && *slot == widget)
        *slot = NULL;
}

static void mount_parent_destroyed(GtkWidget *widget, gpointer data)
{
    MountJob *job = data;

    (void) widget;
    job->parent_destroyed = TRUE;
}

static void mount_job_set_progress(MountJob *job, GtkWidget *progress)
{
    job->progress = progress;
    if (job->progress)
        g_signal_connect(job->progress, "destroy",
                         G_CALLBACK(widget_slot_destroyed), &job->progress);
}

static FilerWindow *mount_job_source_window(MountJob *job)
{
    if (!job || !job->source_widget)
        return NULL;

    /* filer.c clears this object data before freeing FilerWindow.  Keeping a
     * reference to the GtkWindow therefore gives us a race-free lifetime
     * test without ever retaining a raw FilerWindow pointer. */
    return g_object_get_data(G_OBJECT(job->source_widget), "filer_window");
}

static GtkWindow *mount_job_parent(MountJob *job)
{
    FilerWindow *source_window = mount_job_source_window(job);

    if (source_window && GTK_IS_WINDOW(job->source_widget))
        return GTK_WINDOW(job->source_widget);
    if (job && job->parent_widget && !job->parent_destroyed &&
        GTK_IS_WINDOW(job->parent_widget))
        return GTK_WINDOW(job->parent_widget);
    return NULL;
}

static void mount_job_free(MountJob *job)
{
    if (!job)
        return;
    if (job->progress)
        gtk_widget_destroy(job->progress);
    if (job->parent_widget) {
        if (job->parent_destroy_handler)
            g_signal_handler_disconnect(job->parent_widget,
                                        job->parent_destroy_handler);
        g_object_unref(job->parent_widget);
    }
    if (job->source_widget)
        g_object_unref(job->source_widget);
    g_free(job->image);
    g_free(job->loopdev);
    g_free(job->blockdev);
    g_free(job->mountpoint);
    g_free(job->error_text);
    if (job->parts)
        g_ptr_array_free(job->parts, TRUE);
    if (job->labels)
        g_ptr_array_free(job->labels, TRUE);
    g_free(job);
}

static void open_mountpoint(const gchar *mountpoint, FilerWindow *source_window)
{
    if (source_window && source_window->modern_mode)
        modern_ui_open_path_in_new_tab(source_window, mountpoint);
    else
        filer_opendir(mountpoint, NULL, NULL);
}

static void job_open_mountpoint(MountJob *job)
{
    FilerWindow *source_window = mount_job_source_window(job);

    if (source_window && source_window->modern_mode)
        modern_ui_open_path_in_new_tab(source_window, job->mountpoint);
    else
        filer_opendir(job->mountpoint, NULL, NULL);
}

static GtkWidget *progress_dialog_new(GtkWindow *parent, const gchar *text)
{
    GtkWidget *dialog;
    GtkWidget *box;
    GtkWidget *spinner;
    GtkWidget *label;

    dialog = gtk_dialog_new_with_buttons(_("Image Mounter"), parent,
                 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, NULL, NULL);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    gtk_window_set_deletable(GTK_WINDOW(dialog), FALSE);

    box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(box), 18);
    gtk_box_set_spacing(GTK_BOX(box), 12);

    spinner = gtk_spinner_new();
    gtk_widget_set_size_request(spinner, 32, 32);
    gtk_spinner_start(GTK_SPINNER(spinner));
    gtk_box_pack_start(GTK_BOX(box), spinner, FALSE, FALSE, 0);

    label = gtk_label_new(text);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

    gtk_widget_show_all(dialog);
    return dialog;
}

static void job_detach_loop(MountJob *job)
{
    if (!job->loopdev)
        return;
    if (job->method == IMAGE_MOUNT_METHOD_ROOT)
        detach_loop_root(job->loopdev);
    else
        detach_loop_udisks(job->loopdev);
}

/* ---------------- fase 1: loop + particiones (hilo) ---------------- */

static void mount_phase1_thread(GTask *task, gpointer source, gpointer data,
                                GCancellable *cancellable)
{
    MountJob *job = data;
    GPtrArray *parts;
    guint i;

    (void) source; (void) cancellable;

    if (geteuid() == 0) {
        job->method = IMAGE_MOUNT_METHOD_ROOT;
        job->loopdev = setup_loop_root(job->image, &job->error_text);
    } else if (program_exists("udisksctl")) {
        job->method = IMAGE_MOUNT_METHOD_UDISKS;
        job->loopdev = setup_loop_udisks(job->image, &job->error_text);
    } else {
        job->error_text = g_strdup(
            _("Image mounting requires root privileges or udisksctl (udisks2)."));
        g_task_return_boolean(task, FALSE);
        return;
    }

    if (!job->loopdev) {
        g_task_return_boolean(task, FALSE);
        return;
    }

    /* Las ISO y las SquashFS son imagenes de un sistema de archivos
     * completo aunque una ISO hibrida exponga tabla de particiones.  Solo
     * en los .img crudos descendemos a loopXpN. */
    if (!image_is_partitionable_raw(job->image)) {
        job->blockdev = g_strdup(job->loopdev);
        g_task_return_boolean(task, TRUE);
        return;
    }

    /* A raw .img may also contain one filesystem directly, with no partition
     * table at all.  When blkid is available, avoid an unnecessary partition
     * wait for that common case. */
    if (program_exists("blkid")) {
        gchar *pttype = blkid_value(job->loopdev, "PTTYPE");
        if (!pttype || !*pttype) {
            g_free(pttype);
            job->blockdev = g_strdup(job->loopdev);
            g_task_return_boolean(task, TRUE);
            return;
        }
        g_free(pttype);
    }

    parts = wait_for_partitions(job->loopdev);
    if (parts->len == 0 && job->method == IMAGE_MOUNT_METHOD_ROOT &&
        program_exists("partx")) {
        const gchar *partx_argv[] = {"partx", "-a", job->loopdev, NULL};
        (void) command_run(partx_argv, NULL, NULL);
        g_ptr_array_free(parts, TRUE);
        parts = wait_for_partitions(job->loopdev);
    }

    if (parts->len == 0) {
        g_ptr_array_free(parts, TRUE);
        job->blockdev = g_strdup(job->loopdev);
        g_task_return_boolean(task, TRUE);
        return;
    }

    if (parts->len == 1) {
        job->blockdev = g_strdup(g_ptr_array_index(parts, 0));
        g_ptr_array_free(parts, TRUE);
        g_task_return_boolean(task, TRUE);
        return;
    }

    /* blkid(8) tambien es lento: las descripciones se calculan aqui, no en
     * el hilo principal mientras se construye el combo. */
    job->parts = parts;
    job->labels = g_ptr_array_new_with_free_func(g_free);
    for (i = 0; i < parts->len; i++)
        g_ptr_array_add(job->labels,
                        partition_description(g_ptr_array_index(parts, i), i));

    g_task_return_boolean(task, TRUE);
}

/* ---------------- fase 2: mount (hilo) ---------------- */

static void mount_phase2_thread(GTask *task, gpointer source, gpointer data,
                                GCancellable *cancellable)
{
    MountJob *job = data;

    (void) source; (void) cancellable;

    g_clear_pointer(&job->error_text, g_free);
    if (job->method == IMAGE_MOUNT_METHOD_ROOT)
        job->mountpoint = mount_root_block(job->image, job->blockdev,
                                           &job->error_text);
    else
        job->mountpoint = mount_udisks_block(job->blockdev, &job->error_text);

    g_task_return_boolean(task, job->mountpoint != NULL);
}

static void mount_cleanup_thread(GTask *task, gpointer source, gpointer data,
                                 GCancellable *cancellable)
{
    MountJob *job = data;
    (void) source; (void) cancellable;
    job_detach_loop(job);
    g_task_return_boolean(task, TRUE);
}

static void mount_cleanup_done(GObject *source, GAsyncResult *res, gpointer data)
{
    (void) source; (void) res;
    mount_job_free((MountJob *) data);
}

static void job_abort(MountJob *job)
{
    GTask *task;

    if (job->progress) {
        gtk_widget_destroy(job->progress);
        job->progress = NULL;
    }
    /* losetup -d tambien puede tardar: se hace fuera del hilo principal. */
    task = g_task_new(NULL, NULL, mount_cleanup_done, job);
    g_task_set_task_data(task, job, NULL);
    g_task_run_in_thread(task, mount_cleanup_thread);
    g_object_unref(task);
}

/* save_state() is intentionally performed on the GTK thread, but a failure
 * must not run umount/udisksctl synchronously there.  Roll the successful
 * mount back in a worker and verify that unmount really succeeded before
 * detaching the loop device. */
static void mount_state_rollback_thread(GTask *task, gpointer source,
                                        gpointer data,
                                        GCancellable *cancellable)
{
    MountJob *job = data;
    gboolean ok = FALSE;

    (void) source; (void) cancellable;
    g_clear_pointer(&job->error_text, g_free);

    if (job->method == IMAGE_MOUNT_METHOD_ROOT) {
        const gchar *argv[] = {"umount", job->mountpoint, NULL};

        if (!mountpoint_is_ours(job->mountpoint)) {
            job->error_text = g_strdup(_("Unknown error"));
        } else if (command_run(argv, NULL, &job->error_text)) {
            detach_loop_root(job->loopdev);
            (void) g_rmdir(job->mountpoint);
            ok = TRUE;
        }
    } else if (job->method == IMAGE_MOUNT_METHOD_UDISKS) {
        const gchar *argv[] = {"udisksctl", "unmount",
                               "--block-device", job->blockdev, NULL};
        if (command_run(argv, NULL, &job->error_text)) {
            detach_loop_udisks(job->loopdev);
            ok = TRUE;
        }
    } else {
        job->error_text = g_strdup(_("Unknown error"));
    }

    g_task_return_boolean(task, ok);
}

static void mount_state_rollback_done(GObject *source, GAsyncResult *res,
                                      gpointer data)
{
    MountJob *job = data;
    gboolean ok;
    gchar *image;
    gchar *msg;

    (void) source;
    ok = g_task_propagate_boolean(G_TASK(res), NULL);
    if (job->progress)
        gtk_widget_destroy(job->progress);

    image = g_strdup(job->image);
    if (ok) {
        mount_job_free(job);
        report_error(_("The image was mounted, but its state could not be "
                       "saved, so it was unmounted again: '%s'"), image);
        g_free(image);
        return;
    }

    msg = g_strdup(job->error_text && *job->error_text
                   ? job->error_text : _("Unknown error"));
    mount_job_free(job);
    /* This existing translated error is deliberately reused: no untranslated
     * runtime string is introduced by the safety rollback fix. */
    report_error(_("Unable to unmount image '%s': %s"), image, msg);
    g_free(image);
    g_free(msg);
}

/* ---------------- fase 2: resultado (hilo principal) ---------------- */

static void mount_phase2_done(GObject *source, GAsyncResult *res, gpointer data)
{
    MountJob *job = data;
    ImageMountState state;

    (void) source; (void) res;

    if (job->progress) {
        gtk_widget_destroy(job->progress);
        job->progress = NULL;
    }

    if (!job->mountpoint) {
        gchar *msg = g_strdup(job->error_text ? job->error_text
                                              : _("Unknown error"));
        gchar *image = g_strdup(job->image);
        job_abort(job);
        report_error(_("Unable to mount image '%s': %s"), image, msg);
        g_free(image);
        g_free(msg);
        return;
    }

    memset(&state, 0, sizeof(state));
    state.image = g_strdup(job->image);
    state.loopdev = g_strdup(job->loopdev);
    state.blockdev = g_strdup(job->blockdev);
    state.mountpoint = g_strdup(job->mountpoint);
    state.method = job->method;

    /* 2.12.2-85: hasta -84 el resultado de save_state() se descartaba con
     * (void).  Si fallaba, la imagen quedaba montada sin estado: no se
     * podia desmontar desde la interfaz, y montarla de nuevo creaba un
     * segundo loop y un segundo punto de montaje.  Ahora un fallo al
     * guardar deshace el montaje y se informa. */
    if (!save_state(&state)) {
        GTask *task;

        image_mount_state_clear(&state);
        if (!job->progress)
            mount_job_set_progress(job, progress_dialog_new(
                mount_job_parent(job), _("Unmounting image…")));
        task = g_task_new(NULL, NULL, mount_state_rollback_done, job);
        g_task_set_task_data(task, job, NULL);
        g_task_run_in_thread(task, mount_state_rollback_thread);
        g_object_unref(task);
        return;
    }
    image_mount_state_clear(&state);

    job_open_mountpoint(job);
    mount_job_free(job);
}

static void start_phase2(MountJob *job)
{
    GTask *task = g_task_new(NULL, NULL, mount_phase2_done, job);
    g_task_set_task_data(task, job, NULL);
    g_task_run_in_thread(task, mount_phase2_thread);
    g_object_unref(task);
}

/* ---------------- fase 1: resultado (hilo principal) ---------------- */

static gchar *choose_partition_from_labels(GtkWindow *parent,
                                           GPtrArray *parts,
                                           GPtrArray *labels)
{
    GtkWidget *dialog;
    GtkWidget *box;
    GtkWidget *label;
    GtkWidget *combo;
    gint response;
    gint active;
    guint i;
    gchar *chosen = NULL;

    dialog = gtk_dialog_new_with_buttons(_("Image Mounter"), parent,
                  GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                  _("Cancel"), GTK_RESPONSE_CANCEL,
                  _("Mount"), GTK_RESPONSE_ACCEPT,
                  NULL);
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 520, -1);

    box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(box), 14);
    label = gtk_label_new(_("This image contains multiple partitions. "
                            "Choose the partition to mount:"));
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 6);

    combo = gtk_combo_box_text_new();
    for (i = 0; i < labels->len; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo),
                                       g_ptr_array_index(labels, i));
    gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
    gtk_box_pack_start(GTK_BOX(box), combo, FALSE, FALSE, 6);
    gtk_widget_show_all(box);

    response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_ACCEPT) {
        active = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
        if (active >= 0 && (guint) active < parts->len)
            chosen = g_strdup(g_ptr_array_index(parts, active));
    }
    gtk_widget_destroy(dialog);
    return chosen;
}

static void mount_phase1_done(GObject *source, GAsyncResult *res, gpointer data)
{
    MountJob *job = data;

    (void) source; (void) res;

    if (!job->loopdev) {
        gchar *msg = g_strdup(job->error_text ? job->error_text
                                              : _("Unknown error"));
        gchar *image = g_strdup(job->image);
        if (job->progress) {
            gtk_widget_destroy(job->progress);
            job->progress = NULL;
        }
        mount_job_free(job);
        report_error(_("Unable to create a loop device for '%s': %s"),
                     image, msg);
        g_free(image);
        g_free(msg);
        return;
    }

    if (job->parts && job->parts->len > 1) {
        if (job->progress) {
            gtk_widget_destroy(job->progress);
            job->progress = NULL;
        }
        job->blockdev = choose_partition_from_labels(mount_job_parent(job),
                                                     job->parts, job->labels);
        if (!job->blockdev) {
            job_abort(job);
            return;
        }
        mount_job_set_progress(job,
            progress_dialog_new(mount_job_parent(job), _("Mounting image…")));
    }

    if (!job->blockdev)
        job->blockdev = g_strdup(job->loopdev);

    start_phase2(job);
}

gboolean image_mounter_mount(const gchar *path, GtkWindow *parent,
                             FilerWindow *source_window)
{
    gchar *canonical;
    gchar *existing = NULL;
    MountJob *job;
    GTask *task;

    if (!image_mounter_can_handle(path)) {
        report_error("%s", _("Unsupported image format."));
        return FALSE;
    }

    canonical = canonical_image_path(path);
    if (!canonical)
        return FALSE;

    if (image_mounter_is_mounted(canonical, &existing)) {
        open_mountpoint(existing, source_window);
        g_free(existing);
        g_free(canonical);
        return TRUE;
    }

    job = g_new0(MountJob, 1);
    job->image = canonical;
    if (parent) {
        job->parent_widget = g_object_ref(GTK_WIDGET(parent));
        job->parent_destroy_handler =
            g_signal_connect(job->parent_widget, "destroy",
                             G_CALLBACK(mount_parent_destroyed), job);
    }
    if (source_window && source_window->window)
        job->source_widget = g_object_ref(source_window->window);
    mount_job_set_progress(job,
        progress_dialog_new(mount_job_parent(job), _("Mounting image…")));

    task = g_task_new(NULL, NULL, mount_phase1_done, job);
    g_task_set_task_data(task, job, NULL);
    g_task_run_in_thread(task, mount_phase1_thread);
    g_object_unref(task);
    return TRUE;
}

gboolean image_mounter_open(const gchar *path, FilerWindow *source_window)
{
    gchar *mountpoint = NULL;

    if (!image_mounter_is_mounted(path, &mountpoint)) {
        report_error("%s", _("The mounted image is no longer available."));
        return FALSE;
    }
    open_mountpoint(mountpoint, source_window);
    g_free(mountpoint);
    return TRUE;
}

/* ---------------- desmontaje asincrono ---------------- */

typedef struct {
    ImageMountState  state;
    GtkWidget       *progress;
    gchar           *error_text;
    gboolean         ok;
} UnmountJob;

static void unmount_job_set_progress(UnmountJob *job, GtkWidget *progress)
{
    job->progress = progress;
    if (job->progress)
        g_signal_connect(job->progress, "destroy",
                         G_CALLBACK(widget_slot_destroyed), &job->progress);
}

static void unmount_job_free(UnmountJob *job)
{
    if (!job)
        return;
    if (job->progress)
        gtk_widget_destroy(job->progress);
    image_mount_state_clear(&job->state);
    g_free(job->error_text);
    g_free(job);
}

static void unmount_thread(GTask *task, gpointer source, gpointer data,
                           GCancellable *cancellable)
{
    UnmountJob *job = data;

    (void) source; (void) cancellable;

    if (job->state.method == IMAGE_MOUNT_METHOD_ROOT) {
        const gchar *argv[] = {"umount", job->state.mountpoint, NULL};
        if (!command_run(argv, NULL, &job->error_text)) {
            g_task_return_boolean(task, FALSE);
            return;
        }
        detach_loop_root(job->state.loopdev);
        (void) g_rmdir(job->state.mountpoint);
    } else if (job->state.method == IMAGE_MOUNT_METHOD_UDISKS) {
        const gchar *argv[] = {"udisksctl", "unmount",
                               "--block-device", job->state.blockdev, NULL};
        if (!command_run(argv, NULL, &job->error_text)) {
            g_task_return_boolean(task, FALSE);
            return;
        }
        detach_loop_udisks(job->state.loopdev);
    } else {
        job->error_text = g_strdup(_("Unknown error"));
        g_task_return_boolean(task, FALSE);
        return;
    }

    job->ok = TRUE;
    g_task_return_boolean(task, TRUE);
}

static void unmount_done(GObject *source, GAsyncResult *res, gpointer data)
{
    UnmountJob *job = data;

    (void) source; (void) res;

    if (job->progress) {
        gtk_widget_destroy(job->progress);
        job->progress = NULL;
    }

    if (job->ok) {
        remove_state_file(job->state.image);
    } else {
        gchar *image = g_strdup(job->state.image);
        gchar *msg = g_strdup(job->error_text && *job->error_text
                              ? job->error_text : _("Unknown error"));
        unmount_job_free(job);
        report_error(_("Unable to unmount image '%s': %s"), image, msg);
        g_free(image);
        g_free(msg);
        return;
    }
    unmount_job_free(job);
}

gboolean image_mounter_unmount(const gchar *path, GtkWindow *parent)
{
    ImageMountState state;
    UnmountJob *job;
    GTask *task;

    if (!load_state(path, &state) || !mountpoint_is_active(state.mountpoint)) {
        image_mount_state_clear(&state);
        remove_state_file(path);
        report_error("%s", _("The mounted image is no longer available."));
        return FALSE;
    }

    /* 2.12.2-85: solo desmontamos y borramos dentro del arbol que
     * gestionamos.  Un archivo de estado heredado o corrupto no puede
     * hacer que umount(8) y g_rmdir() actuen sobre una ruta ajena. */
    if (state.method == IMAGE_MOUNT_METHOD_ROOT &&
        !mountpoint_is_ours(state.mountpoint)) {
        gchar *bad = g_strdup(state.mountpoint);
        image_mount_state_clear(&state);
        remove_state_file(path);
        report_error(_("Refusing to unmount '%s': it is outside the directory "
                       "Rox-Filer2 manages for images (%s)."),
                     bad, IMAGE_MOUNTER_ROOT);
        g_free(bad);
        return FALSE;
    }

    job = g_new0(UnmountJob, 1);
    job->state = state;              /* se transfiere la propiedad */
    unmount_job_set_progress(job,
        progress_dialog_new(parent, _("Unmounting image…")));

    task = g_task_new(NULL, NULL, unmount_done, job);
    g_task_set_task_data(task, job, NULL);
    g_task_run_in_thread(task, unmount_thread);
    g_object_unref(task);
    return TRUE;
}
