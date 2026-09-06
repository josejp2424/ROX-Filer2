/*
 * Rox-Filer2 optional SMB connector.
 *
 * SMB is deliberately exposed to the filer as a local mount, not as a VFS.
 * This keeps dir.c, diritem.c, fscache.c, actions, DnD and MIME handling on
 * their existing POSIX paths. libsmbclient, when available, is used only to
 * validate the remote share before mount.cifs is invoked.
 */

#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <glib/gstdio.h>

#ifdef HAVE_LIBSMBCLIENT
#include <libsmbclient.h>
#endif

#include "global.h"
#include "filer.h"
#include "choices.h"
#include "gui_support.h"
#include "i18n.h"
#include "mount.h"
#include "support.h"
#include "smb.h"
#include "drives_monitor.h"

typedef struct {
    GtkWidget *dialog;
    GtkWidget *server;
    GtkWidget *share;
    GtkWidget *share_combo;
    GtkWidget *list_button;
    GtkWidget *user;
    GtkWidget *domain;
    GtkWidget *password;
    GtkWidget *options;
    GtkWidget *spinner;
    FilerWindow *source_window;
    gboolean busy;
    gboolean dead;
    gboolean listing;
} SmbDialog;

typedef struct {
    gchar *server;
    gchar *share;
    gchar *user;
    gchar *domain;
    gchar *password;
    gchar *options;
    GPtrArray *shares;
    gchar *mountpoint;
    gchar *error_text;
} SmbJob;


static gchar *smb_runtime_base(void);
static GMutex smb_mount_mutex;
static void smb_save_connection(const SmbJob *job);

static void smb_secure_clear(gchar *text, gsize length)
{
    volatile gchar *p = (volatile gchar *) text;

    if (!p)
        return;
    while (length-- > 0)
        *p++ = '\0';
}

static void smb_secure_free(gchar *text)
{
    if (!text)
        return;
    smb_secure_clear(text, strlen(text));
    g_free(text);
}

static void smb_cleanup_mount_dirs(const gchar *mountpoint)
{
    gchar *point_canon, *parent;

    if (!rox_smb_mountpoint_is_managed(mountpoint))
        return;

    point_canon = g_canonicalize_filename(mountpoint, NULL);
    parent = g_path_get_dirname(point_canon);
    g_rmdir(point_canon);
    g_rmdir(parent);
    g_free(parent);
    g_free(point_canon);
}

static gboolean smb_component_valid(const gchar *text)
{
    const gchar *p;
    if (!text || !*text)
        return FALSE;
    for (p = text; *p; p++)
        if (*p == '/' || *p == '\\' || *p == '\n' || *p == '\r')
            return FALSE;
    return strcmp(text, ".") != 0 && strcmp(text, "..") != 0;
}

static gchar *smb_find_program(const gchar *name)
{
    static const gchar *system_dirs[] = { "/sbin", "/usr/sbin", "/bin", "/usr/bin", NULL };
    gchar *path;
    gint i;

    path = g_find_program_in_path(name);
    if (path)
        return path;
    for (i = 0; system_dirs[i]; i++) {
        path = g_build_filename(system_dirs[i], name, NULL);
        if (g_file_test(path, G_FILE_TEST_IS_EXECUTABLE))
            return path;
        g_free(path);
    }
    return NULL;
}

static gchar *smb_runtime_base(void)
{
    if (geteuid() == 0)
        return g_strdup("/mnt/smb");

    if (g_get_user_runtime_dir() && *g_get_user_runtime_dir())
        return g_build_filename(g_get_user_runtime_dir(), "rox-filer2", "smb", NULL);

    return g_strdup_printf("/tmp/rox-filer2-%lu/smb", (gulong) geteuid());
}

gboolean rox_smb_mountpoint_is_managed(const gchar *mountpoint)
{
    gchar *base, *base_canon, *point_canon, *prefix;
    gchar *parent = NULL, *grandparent = NULL;
    gboolean managed = FALSE;

    if (!mountpoint || !*mountpoint)
        return FALSE;
    base = smb_runtime_base();
    base_canon = g_canonicalize_filename(base, NULL);
    point_canon = g_canonicalize_filename(mountpoint, NULL);
    prefix = g_strconcat(base_canon, G_DIR_SEPARATOR_S, NULL);
    if (!g_str_has_prefix(point_canon, prefix))
        goto out;
    parent = g_path_get_dirname(point_canon);
    grandparent = g_path_get_dirname(parent);
    managed = g_strcmp0(grandparent, base_canon) == 0;
out:
    g_free(grandparent);
    g_free(parent);
    g_free(prefix);
    g_free(point_canon);
    g_free(base_canon);
    g_free(base);
    return managed;
}

static gboolean smb_options_valid(const gchar *options)
{
    gchar **parts;
    gint i;
    gboolean ok = TRUE;

    if (!options || !*options)
        return TRUE;
    if (strchr(options, '\n') || strchr(options, '\r'))
        return FALSE;

    parts = g_strsplit(options, ",", -1);
    for (i = 0; parts[i]; i++) {
        gchar *item = g_strstrip(parts[i]);
        gchar *equals = strchr(item, '=');
        gchar *key;

        if (!equals)
            continue;
        key = g_ascii_strdown(item, (gssize) (equals - item));
        if (g_strcmp0(key, "credentials") == 0 ||
                g_strcmp0(key, "cred") == 0 ||
                g_strcmp0(key, "password") == 0 ||
                g_strcmp0(key, "username") == 0 ||
                g_strcmp0(key, "user") == 0)
            ok = FALSE;
        g_free(key);
        if (!ok)
            break;
    }
    g_strfreev(parts);
    return ok;
}

static gchar *smb_connections_path(gboolean create)
{
    if (create)
        return choices_find_xdg_path_save("smb.ini", PROJECT, SITE, TRUE);
    return choices_find_xdg_path_load("smb.ini", PROJECT, SITE);
}

static void smb_save_connection(const SmbJob *job)
{
    GKeyFile *kf;
    gchar *path, *data;
    gsize length;

    if (!job || !job->server || !job->share)
        return;
    path = smb_connections_path(TRUE);
    if (!path)
        return;
    kf = g_key_file_new();
    g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL);
    g_key_file_set_string(kf, "Last", "Server", job->server);
    g_key_file_set_string(kf, "Last", "Share", job->share);
    g_key_file_set_string(kf, "Last", "User", job->user ? job->user : "");
    g_key_file_set_string(kf, "Last", "Domain", job->domain ? job->domain : "");
    data = g_key_file_to_data(kf, &length, NULL);
    if (data) {
        if (g_file_set_contents(path, data, length, NULL))
            g_chmod(path, 0600);
        g_free(data);
    }
    g_key_file_unref(kf);
    g_free(path);
}

static void smb_load_last_connection(SmbDialog *sd)
{
    GKeyFile *kf;
    gchar *path, *value;

    path = smb_connections_path(FALSE);
    if (!path)
        return;
    kf = g_key_file_new();
    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, NULL))
        goto out;
    value = g_key_file_get_string(kf, "Last", "Server", NULL);
    if (value) { gtk_entry_set_text(GTK_ENTRY(sd->server), value); g_free(value); }
    value = g_key_file_get_string(kf, "Last", "Share", NULL);
    if (value) { gtk_entry_set_text(GTK_ENTRY(sd->share), value); g_free(value); }
    value = g_key_file_get_string(kf, "Last", "User", NULL);
    if (value) { gtk_entry_set_text(GTK_ENTRY(sd->user), value); g_free(value); }
    value = g_key_file_get_string(kf, "Last", "Domain", NULL);
    if (value) { gtk_entry_set_text(GTK_ENTRY(sd->domain), value); g_free(value); }
out:
    g_key_file_unref(kf);
    g_free(path);
}

static gboolean smb_write_all(gint fd, const gchar *data, gsize length,
        GError **error)
{
    gsize done = 0;

    while (done < length) {
        ssize_t written = write(fd, data + done, length - done);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "%s", g_strerror(errno));
            return FALSE;
        }
        if (written == 0) {
            g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                    "%s", _("The command failed."));
            return FALSE;
        }
        done += (gsize) written;
    }
    return TRUE;
}

static gboolean smb_write_credentials(const gchar *user, const gchar *domain,
        const gchar *password, gchar **path_out, GError **error)
{
    gchar *tmpl = g_build_filename(g_get_tmp_dir(), "rox-filer2-smb-XXXXXX", NULL);
    gint fd = g_mkstemp(tmpl);
    GString *content;
    gboolean ok;

    if (fd < 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                "%s", g_strerror(errno));
        g_free(tmpl);
        return FALSE;
    }

    if (fchmod(fd, 0600) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                "%s", g_strerror(errno));
        close(fd);
        g_unlink(tmpl);
        g_free(tmpl);
        return FALSE;
    }

    content = g_string_new(NULL);
    g_string_append_printf(content, "username=%s\npassword=%s\n",
            user ? user : "", password ? password : "");
    if (domain && *domain)
        g_string_append_printf(content, "domain=%s\n", domain);
    ok = smb_write_all(fd, content->str, content->len, error);
    smb_secure_clear(content->str, content->len);
    g_free(g_string_free(content, FALSE));
    if (close(fd) != 0 && ok) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                "%s", g_strerror(errno));
        ok = FALSE;
    }
    if (!ok) {
        g_unlink(tmpl);
        g_free(tmpl);
        return FALSE;
    }
    *path_out = tmpl;
    return TRUE;
}

#ifdef HAVE_LIBSMBCLIENT
typedef struct {
    const gchar *user;
    const gchar *domain;
    const gchar *password;
} SmbAuth;

static GPrivate smb_auth_private = G_PRIVATE_INIT(NULL);

static void smb_auth_cb(SMBCCTX *ctx, const char *server, const char *share,
        char *workgroup, int wglen, char *username, int unlen,
        char *password, int pwlen)
{
    SmbAuth *auth = g_private_get(&smb_auth_private);
    (void) ctx; (void) server; (void) share;
    if (!auth)
        return;
    if (auth->domain && *auth->domain)
        g_strlcpy(workgroup, auth->domain, wglen);
    g_strlcpy(username, auth->user ? auth->user : "", unlen);
    g_strlcpy(password, auth->password ? auth->password : "", pwlen);
}

static GMutex smbclient_mutex;

static gboolean smb_preflight(const gchar *url, const gchar *user,
        const gchar *domain, const gchar *password, gchar **error_text)
{
    SMBCCTX *ctx = NULL;
    SMBCFILE *dh = NULL;
    smbc_opendir_fn opendir_fn;
    smbc_closedir_fn closedir_fn;
    SmbAuth auth = { user, domain, password };
    gboolean ok = FALSE;

    /* libsmbclient does not export smbc_thread_posix() in supported Samba
     * ABIs. Serialize preflights instead; this is sufficient because the
     * connector never needs concurrent directory probes. */
    g_mutex_lock(&smbclient_mutex);
    ctx = smbc_new_context();
    if (!ctx) {
        *error_text = g_strdup("libsmbclient: smbc_new_context failed");
        goto out;
    }
    g_private_set(&smb_auth_private, &auth);
    smbc_setFunctionAuthDataWithContext(ctx, smb_auth_cb);
    if (!smbc_init_context(ctx)) {
        *error_text = g_strdup("libsmbclient: smbc_init_context failed");
        goto out;
    }
    opendir_fn = smbc_getFunctionOpendir(ctx);
    closedir_fn = smbc_getFunctionClosedir(ctx);
    if (!opendir_fn || !closedir_fn) {
        *error_text = g_strdup("libsmbclient: directory API unavailable");
        goto out;
    }
    dh = opendir_fn(ctx, url);
    if (!dh) {
        *error_text = g_strdup_printf("libsmbclient: %s", g_strerror(errno));
        goto out;
    }
    closedir_fn(ctx, dh);
    dh = NULL;
    ok = TRUE;

out:
    g_private_set(&smb_auth_private, NULL);
    if (ctx)
        smbc_free_context(ctx, 1);
    g_mutex_unlock(&smbclient_mutex);
    return ok;
}
static gboolean smb_list_shares(const gchar *server, const gchar *user,
        const gchar *domain, const gchar *password, GPtrArray **shares_out,
        gchar **error_text)
{
    SMBCCTX *ctx = NULL;
    SMBCFILE *dh = NULL;
    smbc_opendir_fn opendir_fn;
    smbc_closedir_fn closedir_fn;
    smbc_readdir_fn readdir_fn;
    struct smbc_dirent *entry;
    gchar *url;
    SmbAuth auth = { user, domain, password };
    GPtrArray *shares = NULL;
    gboolean ok = FALSE;

    *shares_out = NULL;
    *error_text = NULL;
    url = g_strdup_printf("smb://%s/", server);
    g_mutex_lock(&smbclient_mutex);
    ctx = smbc_new_context();
    if (!ctx) { *error_text = g_strdup("libsmbclient: smbc_new_context failed"); goto out; }
    g_private_set(&smb_auth_private, &auth);
    smbc_setFunctionAuthDataWithContext(ctx, smb_auth_cb);
    if (!smbc_init_context(ctx)) { *error_text = g_strdup("libsmbclient: smbc_init_context failed"); goto out; }
    opendir_fn = smbc_getFunctionOpendir(ctx);
    closedir_fn = smbc_getFunctionClosedir(ctx);
    readdir_fn = smbc_getFunctionReaddir(ctx);
    if (!opendir_fn || !closedir_fn || !readdir_fn) {
        *error_text = g_strdup("libsmbclient: directory API unavailable"); goto out;
    }
    dh = opendir_fn(ctx, url);
    if (!dh) { *error_text = g_strdup_printf("libsmbclient: %s", g_strerror(errno)); goto out; }
    shares = g_ptr_array_new_with_free_func(g_free);
    while ((entry = readdir_fn(ctx, dh)) != NULL) {
        if (entry->smbc_type != SMBC_FILE_SHARE || !*entry->name ||
                strcmp(entry->name, ".") == 0 || strcmp(entry->name, "..") == 0)
            continue;
        g_ptr_array_add(shares, g_strdup(entry->name));
    }
    closedir_fn(ctx, dh);
    dh = NULL;
    *shares_out = shares;
    shares = NULL;
    ok = TRUE;
out:
    if (dh && closedir_fn) closedir_fn(ctx, dh);
    if (shares) g_ptr_array_unref(shares);
    g_private_set(&smb_auth_private, NULL);
    if (ctx) smbc_free_context(ctx, 1);
    g_mutex_unlock(&smbclient_mutex);
    g_free(url);
    return ok;
}
#endif

gboolean rox_smb_compiled_with_libsmbclient(void)
{
#ifdef HAVE_LIBSMBCLIENT
    return TRUE;
#else
    return FALSE;
#endif
}

static gboolean smb_spawn_mount(gchar *mount_cifs, gchar *remote,
        gchar *mountpoint, gchar *options, gchar **stderr_text,
        gint *status, GError **error)
{
    gboolean ok = FALSE;

    if (geteuid() == 0) {
        gchar *argv_direct[] = { mount_cifs, remote, mountpoint,
            (gchar *) "-o", options, NULL };
        ok = rox_spawn_sync(NULL, argv_direct, NULL, 0, NULL, NULL, NULL,
                stderr_text, status, error);
        return ok && g_spawn_check_wait_status(*status, NULL);
    }

    /* A normal user cannot normally invoke mount.cifs directly. Avoid a
     * guaranteed failing child and use sudo only when the administrator has
     * explicitly configured it. A broad NOPASSWD rule for mount.cifs is not
     * recommended because it effectively grants root-equivalent mount power. */
    {
        gchar *sudo_prog = smb_find_program("sudo");
        if (sudo_prog) {
            gchar *argv_sudo[] = { sudo_prog, (gchar *) "-n", mount_cifs,
                remote, mountpoint, (gchar *) "-o", options, NULL };
            ok = rox_spawn_sync(NULL, argv_sudo, NULL, 0, NULL, NULL, NULL,
                    stderr_text, status, error);
            g_free(sudo_prog);
            return ok && g_spawn_check_wait_status(*status, NULL);
        }
    }

    if (error && !*error)
        g_set_error(error, G_SPAWN_ERROR, G_SPAWN_ERROR_NOENT,
                "%s", _("SMB mounting needs root privileges or a configured privilege helper."));
    return FALSE;
}

static gboolean smb_mount(const gchar *server, const gchar *share,
        const gchar *user, const gchar *domain, const gchar *password,
        const gchar *advanced_options, gchar **mountpoint_out,
        gchar **error_text)
{
    gchar *mount_cifs = smb_find_program("mount.cifs");
    gchar *base = NULL, *mountpoint = NULL, *remote = NULL;
    gchar *credentials = NULL, *options = NULL;
    gchar *stderr_text = NULL;
    gint status = 0;
    GError *error = NULL;
    gboolean ok = FALSE;
    gboolean created_mountpoint = FALSE;
    gchar *preflight_error = NULL;

    *mountpoint_out = NULL;
    *error_text = NULL;

    /* 2.12.2-82: hasta -81 la validacion vivia solo en los manejadores del
     * dialogo.  Cualquier futuro llamador (marcadores, control remoto,
     * automontaje) habria podido pasar ".." y salirse del arbol gestionado.
     * La comprobacion pertenece aqui, junto al montaje. */
    if (!smb_component_valid(server) || !smb_component_valid(share) ||
            !smb_options_valid(advanced_options)) {
        *error_text = g_strdup(_("Enter a valid SMB server and share name."));
        g_free(mount_cifs);
        return FALSE;
    }

    g_mutex_lock(&smb_mount_mutex);
    if (!mount_cifs) {
        *error_text = g_strdup(_("mount.cifs was not found. Install cifs-utils to use SMB shares."));
        g_mutex_unlock(&smb_mount_mutex);
        return FALSE;
    }

    base = smb_runtime_base();
    mountpoint = g_build_filename(base, server, share, NULL);
    remote = g_strdup_printf("//%s/%s", server, share);
    if (!g_file_test(mountpoint, G_FILE_TEST_IS_DIR))
        created_mountpoint = TRUE;
    if (g_mkdir_with_parents(mountpoint, 0700) != 0) {
        *error_text = g_strdup_printf(_("Unable to create SMB mount point '%s'."), mountpoint);
        goto out;
    }

    if (mount_is_mounted((const guchar *) mountpoint, NULL, NULL)) {
        *mountpoint_out = g_strdup(mountpoint);
        goto out;
    }

    if (user && *user) {
        if (!smb_write_credentials(user, domain, password, &credentials, &error)) {
            *error_text = g_strdup(error->message);
            g_clear_error(&error);
            goto out;
        }
        options = g_strdup_printf("credentials=%s,uid=%lu,gid=%lu,iocharset=utf8%s%s",
                credentials, (gulong) getuid(), (gulong) getgid(),
                advanced_options && *advanced_options ? "," : "",
                advanced_options && *advanced_options ? advanced_options : "");
    } else {
        options = g_strdup_printf("guest,uid=%lu,gid=%lu,iocharset=utf8%s%s",
                (gulong) getuid(), (gulong) getgid(),
                advanced_options && *advanced_options ? "," : "",
                advanced_options && *advanced_options ? advanced_options : "");
    }

    ok = smb_spawn_mount(mount_cifs, remote, mountpoint, options,
            &stderr_text, &status, &error);
    if (!ok) {
#ifdef HAVE_LIBSMBCLIENT
        gchar *url = g_strdup_printf("smb://%s/%s", server, share);
        /* Probe only after mount.cifs fails.  Successful mounts pay no
         * second network round-trip, while failures still get an independent
         * libsmbclient diagnostic. */
        smb_preflight(url, user, domain, password, &preflight_error);
        g_free(url);
#endif
        if (error) {
            *error_text = preflight_error
                ? g_strdup_printf("%s\n%s: %s", error->message,
                        _("SMB probe"), preflight_error)
                : g_strdup(error->message);
        } else if (stderr_text && *stderr_text) {
            *error_text = preflight_error
                ? g_strdup_printf("%s\n%s: %s", stderr_text,
                        _("SMB probe"), preflight_error)
                : g_strdup(stderr_text);
        } else {
            *error_text = preflight_error
                ? g_strdup_printf("%s\n%s: %s", _("The command failed."),
                        _("SMB probe"), preflight_error)
                : g_strdup(_("The command failed."));
        }
        g_clear_error(&error);
        goto out;
    }

    *mountpoint_out = g_strdup(mountpoint);

out:
    if (credentials) {
        g_unlink(credentials);
        g_free(credentials);
    }
    if (!*mountpoint_out && created_mountpoint)
        smb_cleanup_mount_dirs(mountpoint);
    g_free(stderr_text);
    g_free(options);
    g_free(remote);
    g_free(mountpoint);
    g_free(base);
    g_free(mount_cifs);
    g_free(preflight_error);
    g_mutex_unlock(&smb_mount_mutex);
    return *mountpoint_out != NULL;
}

gboolean rox_smb_unmount_path(const gchar *mountpoint, gchar **error_text)
{
    gchar *umount_prog;
    gchar *stderr_text = NULL;
    gint status = 0;
    GError *error = NULL;
    gboolean ok;

    if (error_text)
        *error_text = NULL;
    if (!mountpoint || !*mountpoint)
        return FALSE;
    if (!mount_is_mounted((const guchar *) mountpoint, NULL, NULL))
        return TRUE;

    umount_prog = smb_find_program("umount");
    if (!umount_prog) {
        if (error_text)
            *error_text = g_strdup(_("The command failed."));
        return FALSE;
    }

    {
        gchar *argv[] = { umount_prog, (gchar *) mountpoint, NULL };
        ok = rox_spawn_sync(NULL, argv, NULL, 0, NULL, NULL, NULL,
                &stderr_text, &status, &error);
    }
    if (!(ok && g_spawn_check_wait_status(status, NULL)) && geteuid() != 0) {
        gchar *sudo_prog = smb_find_program("sudo");
        if (sudo_prog) {
            gchar *argv[] = { sudo_prog, (gchar *) "-n", umount_prog,
                (gchar *) mountpoint, NULL };
            g_clear_error(&error);
            g_clear_pointer(&stderr_text, g_free);
            status = 0;
            ok = rox_spawn_sync(NULL, argv, NULL, 0, NULL, NULL, NULL,
                    &stderr_text, &status, &error);
            g_free(sudo_prog);
        }
    }

    ok = ok && g_spawn_check_wait_status(status, NULL);
    if (!ok && error_text) {
        if (error)
            *error_text = g_strdup(error->message);
        else if (stderr_text && *stderr_text)
            *error_text = g_strdup(stderr_text);
        else
            *error_text = g_strdup(_("The command failed."));
    }
    if (ok) {
        smb_cleanup_mount_dirs(mountpoint);
        rox_drives_monitor_request_scan();
    }
    g_clear_error(&error);
    g_free(stderr_text);
    g_free(umount_prog);
    return ok;
}

static void smb_job_free(gpointer data)
{
    SmbJob *job = data;
    if (!job)
        return;
    g_free(job->server);
    g_free(job->share);
    g_free(job->user);
    g_free(job->domain);
    smb_secure_free(job->password);
    g_free(job->options);
    g_free(job->mountpoint);
    g_free(job->error_text);
    if (job->shares) g_ptr_array_unref(job->shares);
    g_free(job);
}

static void smb_mount_worker(GTask *task, gpointer source_object,
        gpointer task_data, GCancellable *cancellable)
{
    SmbJob *job = task_data;
    gboolean ok;
    (void) source_object;
    (void) cancellable;

    ok = smb_mount(job->server, job->share, job->user, job->domain,
            job->password, job->options, &job->mountpoint, &job->error_text);
    g_task_return_boolean(task, ok);
}

static void smb_unmount_worker(GTask *task, gpointer source_object,
        gpointer task_data, GCancellable *cancellable)
{
    const gchar *mountpoint = task_data;
    gboolean ok;

    (void) source_object;
    (void) cancellable;
    ok = rox_smb_unmount_path(mountpoint, NULL);
    g_task_return_boolean(task, ok);
}

static void smb_unmount_async_cleanup(const gchar *mountpoint)
{
    GTask *task;

    if (!mountpoint || !*mountpoint)
        return;
    task = g_task_new(NULL, NULL, NULL, NULL);
    g_task_set_task_data(task, g_strdup(mountpoint), g_free);
    g_task_run_in_thread(task, smb_unmount_worker);
    g_object_unref(task);
}

static void smb_mount_finished(GObject *source, GAsyncResult *result,
        gpointer data)
{
    GtkWidget *dialog = GTK_WIDGET(data);
    SmbDialog *sd = g_object_get_data(G_OBJECT(dialog), "rox-smb-dialog");
    GTask *task = G_TASK(result);
    SmbJob *job = g_task_get_task_data(task);
    GError *error = NULL;
    gboolean ok;
    FilerWindow *source_window;

    (void) source;
    ok = g_task_propagate_boolean(task, &error);
    if (!sd || sd->dead) {
        if (ok && job->mountpoint)
            smb_unmount_async_cleanup(job->mountpoint);
        g_clear_error(&error);
        g_object_unref(dialog);
        return;
    }

    sd->busy = FALSE;
    gtk_spinner_stop(GTK_SPINNER(sd->spinner));
    gtk_widget_hide(sd->spinner);
    gtk_window_set_deletable(GTK_WINDOW(sd->dialog), TRUE);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(sd->dialog), GTK_RESPONSE_ACCEPT, TRUE);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(sd->dialog), GTK_RESPONSE_CANCEL, TRUE);
    gtk_widget_set_sensitive(sd->list_button, TRUE);

    if (!ok) {
        delayed_error(_("Unable to connect to SMB share:\n%s"),
                job->error_text ? job->error_text :
                (error ? error->message : _("Unknown error")));
        g_clear_error(&error);
        g_object_unref(dialog);
        return;
    }

    source_window = sd->source_window;
    mount_user_mount(job->mountpoint);
    rox_drives_monitor_request_scan();
    gtk_widget_destroy(sd->dialog);
    smb_save_connection(job);
    if (source_window && filer_exists(source_window))
        filer_opendir(job->mountpoint, source_window, NULL);
    g_clear_error(&error);
    g_object_unref(dialog);
}

static void smb_dialog_destroy(GtkWidget *widget, gpointer data)
{
    SmbDialog *sd = data;

    (void) widget;
    sd->dead = TRUE;
    sd->dialog = NULL;
    sd->spinner = NULL;
    sd->source_window = NULL;
}

static void smb_list_worker(GTask *task, gpointer source_object,
        gpointer task_data, GCancellable *cancellable)
{
    SmbJob *job = task_data;
    gboolean ok = FALSE;
    (void) source_object; (void) cancellable;
#ifdef HAVE_LIBSMBCLIENT
    ok = smb_list_shares(job->server, job->user, job->domain, job->password,
            &job->shares, &job->error_text);
#else
    job->error_text = g_strdup(_("This build has no libsmbclient support."));
#endif
    g_task_return_boolean(task, ok);
}

static void smb_list_finished(GObject *source, GAsyncResult *result, gpointer data)
{
    GtkWidget *dialog = GTK_WIDGET(data);
    SmbDialog *sd = g_object_get_data(G_OBJECT(dialog), "rox-smb-dialog");
    GTask *task = G_TASK(result);
    SmbJob *job = g_task_get_task_data(task);
    GError *error = NULL;
    gboolean ok;
    guint i;
    (void) source;

    ok = g_task_propagate_boolean(task, &error);
    if (!sd || sd->dead) { g_clear_error(&error); g_object_unref(dialog); return; }
    sd->listing = FALSE;
    gtk_widget_set_sensitive(sd->list_button, !sd->busy);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(sd->dialog),
            GTK_RESPONSE_ACCEPT, !sd->busy);
    if (!ok) {
        delayed_error(_("Unable to list SMB shares:\n%s"),
                job->error_text ? job->error_text :
                (error ? error->message : _("Unknown error")));
        g_clear_error(&error);
        g_object_unref(dialog);
        return;
    }
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(sd->share_combo));
    for (i = 0; job->shares && i < job->shares->len; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(sd->share_combo),
                g_ptr_array_index(job->shares, i));
    if (job->shares && job->shares->len > 0)
        gtk_combo_box_set_active(GTK_COMBO_BOX(sd->share_combo), 0);
    g_clear_error(&error);
    g_object_unref(dialog);
}

static void smb_list_clicked(GtkButton *button, gpointer data)
{
    SmbDialog *sd = data;
    SmbJob *job;
    GTask *task;
    const gchar *server;
    (void) button;

    if (sd->busy || sd->listing)
        return;
    server = gtk_entry_get_text(GTK_ENTRY(sd->server));
    if (!smb_component_valid(server)) {
        delayed_error(_("Enter a valid SMB server and share name."));
        return;
    }
    job = g_new0(SmbJob, 1);
    job->server = g_strdup(server);
    job->user = g_strdup(gtk_entry_get_text(GTK_ENTRY(sd->user)));
    job->domain = g_strdup(gtk_entry_get_text(GTK_ENTRY(sd->domain)));
    job->password = g_strdup(gtk_entry_get_text(GTK_ENTRY(sd->password)));
    sd->listing = TRUE;
    gtk_widget_set_sensitive(sd->list_button, FALSE);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(sd->dialog),
            GTK_RESPONSE_ACCEPT, FALSE);
    task = g_task_new(NULL, NULL, smb_list_finished, g_object_ref(sd->dialog));
    g_task_set_task_data(task, job, smb_job_free);
    g_task_run_in_thread(task, smb_list_worker);
    g_object_unref(task);
}

static void smb_dialog_response(GtkDialog *dialog, gint response, gpointer data)
{
    SmbDialog *sd = data;
    const gchar *server, *share, *user, *domain, *password, *advanced_options;
    SmbJob *job;
    GTask *task;

    if (sd->busy)
        return;
    if (response != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy(GTK_WIDGET(dialog));
        return;
    }
    if (sd->listing)
        return;

    server = gtk_entry_get_text(GTK_ENTRY(sd->server));
    share = gtk_entry_get_text(GTK_ENTRY(sd->share));
    user = gtk_entry_get_text(GTK_ENTRY(sd->user));
    domain = gtk_entry_get_text(GTK_ENTRY(sd->domain));
    password = gtk_entry_get_text(GTK_ENTRY(sd->password));
    advanced_options = gtk_entry_get_text(GTK_ENTRY(sd->options));

    if (!smb_component_valid(server) || !smb_component_valid(share)) {
        delayed_error(_("Enter a valid SMB server and share name."));
        return;
    }
    if (!smb_options_valid(advanced_options)) {
        delayed_error(_("Advanced SMB options cannot contain credentials or password fields."));
        return;
    }

    job = g_new0(SmbJob, 1);
    job->server = g_strdup(server);
    job->share = g_strdup(share);
    job->user = g_strdup(user);
    job->domain = g_strdup(domain);
    job->password = g_strdup(password);
    job->options = g_strdup(advanced_options);

    sd->busy = TRUE;
    gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_ACCEPT, FALSE);
    gtk_dialog_set_response_sensitive(dialog, GTK_RESPONSE_CANCEL, FALSE);
    gtk_widget_set_sensitive(sd->list_button, FALSE);
    gtk_window_set_deletable(GTK_WINDOW(dialog), FALSE);
    gtk_widget_show(sd->spinner);
    gtk_spinner_start(GTK_SPINNER(sd->spinner));

    task = g_task_new(NULL, NULL, smb_mount_finished, g_object_ref(sd->dialog));
    g_task_set_task_data(task, job, smb_job_free);
    g_task_run_in_thread(task, smb_mount_worker);
    g_object_unref(task);
}

static GtkWidget *smb_row(GtkWidget *grid, gint row, const gchar *label,
        gboolean password)
{
    GtkWidget *l = gtk_label_new(label);
    GtkWidget *entry = gtk_entry_new();
    gtk_label_set_xalign(GTK_LABEL(l), 0.0);
    gtk_widget_set_hexpand(entry, TRUE);
    if (password)
        gtk_entry_set_visibility(GTK_ENTRY(entry), FALSE);
    gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), entry, 1, row, 1, 1);
    return entry;
}

void rox_smb_open_dialog(FilerWindow *source_window)
{
    SmbDialog *sd = g_new0(SmbDialog, 1);
    GtkWidget *area, *grid, *info;

    sd->source_window = source_window;
    sd->dialog = gtk_dialog_new_with_buttons(_("Connect to SMB Share"),
            source_window ? GTK_WINDOW(source_window->window) : NULL,
            GTK_DIALOG_DESTROY_WITH_PARENT,
            _("Cancel"), GTK_RESPONSE_CANCEL,
            _("Connect"), GTK_RESPONSE_ACCEPT, NULL);
    gtk_window_set_default_size(GTK_WINDOW(sd->dialog), 430, -1);
    gtk_dialog_set_default_response(GTK_DIALOG(sd->dialog), GTK_RESPONSE_ACCEPT);

    area = gtk_dialog_get_content_area(GTK_DIALOG(sd->dialog));
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 14);
    gtk_box_pack_start(GTK_BOX(area), grid, TRUE, TRUE, 0);

    info = gtk_label_new(_("SMB shares are mounted locally so ROX keeps using its normal file operations."));
    gtk_label_set_line_wrap(GTK_LABEL(info), TRUE);
    gtk_label_set_xalign(GTK_LABEL(info), 0.0);
    gtk_grid_attach(GTK_GRID(grid), info, 0, 0, 2, 1);
    sd->server = smb_row(grid, 1, _("Server"), FALSE);
    {
        GtkWidget *l = gtk_label_new(_("Share"));
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_label_set_xalign(GTK_LABEL(l), 0.0);
        sd->share_combo = gtk_combo_box_text_new_with_entry();
        sd->share = gtk_bin_get_child(GTK_BIN(sd->share_combo));
        gtk_widget_set_hexpand(sd->share_combo, TRUE);
        sd->list_button = gtk_button_new_with_label(_("List shares"));
        gtk_box_pack_start(GTK_BOX(box), sd->share_combo, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(box), sd->list_button, FALSE, FALSE, 0);
        gtk_grid_attach(GTK_GRID(grid), l, 0, 2, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), box, 1, 2, 1, 1);
    }
    sd->user = smb_row(grid, 3, _("User"), FALSE);
    sd->domain = smb_row(grid, 4, _("Domain"), FALSE);
    sd->password = smb_row(grid, 5, _("Password"), TRUE);
    sd->options = smb_row(grid, 6, _("Advanced mount options"), FALSE);
    gtk_widget_set_tooltip_text(sd->options,
            _("Examples: vers=1.0, port=445, sec=ntlmssp, noperm"));
    sd->spinner = gtk_spinner_new();
    gtk_widget_set_halign(sd->spinner, GTK_ALIGN_CENTER);
    gtk_grid_attach(GTK_GRID(grid), sd->spinner, 0, 7, 2, 1);

    gtk_entry_set_activates_default(GTK_ENTRY(sd->server), TRUE);
    gtk_entry_set_activates_default(GTK_ENTRY(sd->share), TRUE);
    gtk_entry_set_activates_default(GTK_ENTRY(sd->password), TRUE);
    gtk_entry_set_activates_default(GTK_ENTRY(sd->options), TRUE);
    smb_load_last_connection(sd);
    g_signal_connect(sd->list_button, "clicked", G_CALLBACK(smb_list_clicked), sd);
    gtk_widget_show_all(sd->dialog);
    gtk_widget_hide(sd->spinner);
    g_signal_connect(sd->dialog, "destroy", G_CALLBACK(smb_dialog_destroy), sd);
    g_signal_connect(sd->dialog, "response", G_CALLBACK(smb_dialog_response), sd);
    g_object_set_data_full(G_OBJECT(sd->dialog), "rox-smb-dialog", sd, g_free);
}
