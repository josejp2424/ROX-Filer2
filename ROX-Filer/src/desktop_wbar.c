/*
 * Agregado por josejp2424 (2026): mantener el pixmap raíz de X11 sincronizado
 * con ROX Desktop y actualizar wbar cuando ya se encuentra ejecutándose.
 *
 * La lógica toma como referencia la implementación probada de EssoraWM:
 * detectar PIDs reales en /proc, ignorar zombis, detener completamente la
 * instancia anterior, volver a aplicar el wallpaper y arrancar una sola wbar.
 * Todo se ejecuta fuera del hilo GTK para no bloquear menús ni el escritorio.
 */
#include "config.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include <gtk/gtk.h>
#include <gio/gio.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

#include "global.h"
#include "support.h"
#include "desktop_wbar.h"
#include "desktop.h"

typedef struct {
    gchar *path;
    gchar *mode;
    guint generation;
} DesktopWbarUpdate;

static GMutex update_lock;
static gint update_generation;

static gboolean desktop_wbar_refresh_main(gpointer data)
{
    (void)data;
    desktop_refresh_after_environment_change();
    return G_SOURCE_REMOVE;
}

static void desktop_wbar_update_free(gpointer data)
{
    DesktopWbarUpdate *update = data;
    if (!update)
        return;
    g_free(update->path);
    g_free(update->mode);
    g_free(update);
}

static gboolean process_is_live_wbar(pid_t pid)
{
    gchar path[64];
    gchar buffer[256];
    FILE *fp;
    gchar *close_paren;
    gchar state = 0;
    gsize length;

    if (pid <= 1 || pid == getpid())
        return FALSE;

    g_snprintf(path, sizeof(path), "/proc/%ld/comm", (long)pid);
    fp = fopen(path, "r");
    if (!fp)
        return FALSE;
    if (!fgets(buffer, sizeof(buffer), fp)) {
        fclose(fp);
        return FALSE;
    }
    fclose(fp);
    length = strlen(buffer);
    while (length > 0 && g_ascii_isspace(buffer[length - 1]))
        buffer[--length] = '\0';
    if (strcmp(buffer, "wbar") != 0)
        return FALSE;

    g_snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid);
    fp = fopen(path, "r");
    if (!fp)
        return FALSE;
    if (!fgets(buffer, sizeof(buffer), fp)) {
        fclose(fp);
        return FALSE;
    }
    fclose(fp);
    close_paren = strrchr(buffer, ')');
    if (close_paren && close_paren[1] == ' ' && close_paren[2])
        state = close_paren[2];
    return state != 'Z';
}

static gint collect_wbar_processes(pid_t *pids, gint max_count)
{
    DIR *dir;
    struct dirent *entry;
    gint count = 0;

    dir = opendir("/proc");
    if (!dir)
        return 0;
    while ((entry = readdir(dir)) != NULL) {
        const gchar *read = entry->d_name;
        pid_t pid;

        if (!*read)
            continue;
        while (*read && g_ascii_isdigit(*read))
            read++;
        if (*read)
            continue;
        pid = (pid_t)g_ascii_strtoll(entry->d_name, NULL, 10);
        if (!process_is_live_wbar(pid))
            continue;
        if (pids && count < max_count)
            pids[count] = pid;
        count++;
    }
    closedir(dir);
    return count;
}

static void wait_for_wbar_exit(gint attempts)
{
    gint i;
    for (i = 0; i < attempts && collect_wbar_processes(NULL, 0) > 0; i++)
        g_usleep(100000);
}

static void stop_wbar_processes(void)
{
    pid_t pids[64];
    gint count;
    gint i;

    count = collect_wbar_processes(pids, G_N_ELEMENTS(pids));
    for (i = 0; i < count && i < (gint)G_N_ELEMENTS(pids); i++)
        if (kill(pids[i], SIGTERM) != 0 && errno != ESRCH)
            g_warning("Unable to stop wbar process %ld: %s",
                      (long)pids[i], g_strerror(errno));
    wait_for_wbar_exit(15);

    count = collect_wbar_processes(pids, G_N_ELEMENTS(pids));
    for (i = 0; i < count && i < (gint)G_N_ELEMENTS(pids); i++)
        if (kill(pids[i], SIGKILL) != 0 && errno != ESRCH)
            g_warning("Unable to terminate wbar process %ld: %s",
                      (long)pids[i], g_strerror(errno));
    wait_for_wbar_exit(15);
}

static gboolean spawn_and_wait(gchar **argv)
{
    gint status = 0;
    GError *error = NULL;
    gboolean ok;

    ok = rox_spawn_sync(NULL, argv, NULL,
                      G_SPAWN_SEARCH_PATH |
                      G_SPAWN_STDOUT_TO_DEV_NULL |
                      G_SPAWN_STDERR_TO_DEV_NULL,
                      NULL, NULL, NULL, NULL, &status, &error);
    if (!ok) {
        g_clear_error(&error);
        return FALSE;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static gboolean apply_root_wallpaper(const gchar *path, const gchar *mode)
{
    gchar *program;
    gboolean ok = FALSE;

    program = g_find_program_in_path("hsetroot");
    if (program) {
        const gchar *option = "-fill";
        gchar *argv[4];
        if (!g_strcmp0(mode, "center"))
            option = "-center";
        else if (!g_strcmp0(mode, "tile"))
            option = "-tile";
        argv[0] = program;
        argv[1] = (gchar *)option;
        argv[2] = (gchar *)path;
        argv[3] = NULL;
        ok = spawn_and_wait(argv);
        g_free(program);
        if (ok)
            return TRUE;
    }

    program = g_find_program_in_path("feh");
    if (program) {
        const gchar *option = "--bg-fill";
        gchar *argv[4];
        if (!g_strcmp0(mode, "fit"))
            option = "--bg-max";
        else if (!g_strcmp0(mode, "stretch"))
            option = "--bg-scale";
        else if (!g_strcmp0(mode, "center"))
            option = "--bg-center";
        else if (!g_strcmp0(mode, "tile"))
            option = "--bg-tile";
        argv[0] = program;
        argv[1] = (gchar *)option;
        argv[2] = (gchar *)path;
        argv[3] = NULL;
        ok = spawn_and_wait(argv);
        g_free(program);
        if (ok)
            return TRUE;
    }

    program = g_find_program_in_path("xwallpaper");
    if (program) {
        const gchar *option = "--zoom";
        gchar *argv[4];
        if (!g_strcmp0(mode, "fit"))
            option = "--maximize";
        else if (!g_strcmp0(mode, "stretch"))
            option = "--stretch";
        else if (!g_strcmp0(mode, "center"))
            option = "--center";
        else if (!g_strcmp0(mode, "tile"))
            option = "--tile";
        argv[0] = program;
        argv[1] = option;
        argv[2] = (gchar *)path;
        argv[3] = NULL;
        ok = spawn_and_wait(argv);
        g_free(program);
    }
    return ok;
}

static gboolean start_wbar(void)
{
    pid_t pid;

    /* Modificado por josejp2424 (2026): conservar el mismo arranque que
     * EssoraWM. La nueva instancia queda en una sesión independiente y no
     * hereda los descriptores de ROX-Filer. */
    pid = fork();
    if (pid < 0) {
        g_warning("Unable to fork wbar: %s", g_strerror(errno));
        return FALSE;
    }
    if (pid == 0) {
        gint nullfd;

        setsid();
        nullfd = open("/dev/null", O_RDWR);
        if (nullfd >= 0) {
            dup2(nullfd, STDIN_FILENO);
            dup2(nullfd, STDOUT_FILENO);
            dup2(nullfd, STDERR_FILENO);
            if (nullfd > STDERR_FILENO)
                close(nullfd);
        }
        execlp("wbar", "wbar", (char *)NULL);
        _exit(127);
    }
    return TRUE;
}

static void desktop_wbar_update_thread(GTask *task, gpointer source_object,
                                       gpointer task_data,
                                       GCancellable *cancellable)
{
    DesktopWbarUpdate *update = task_data;
    gint before;
    (void)source_object;
    (void)cancellable;

    g_mutex_lock(&update_lock);
    if ((gint)update->generation != g_atomic_int_get(&update_generation)) {
        g_mutex_unlock(&update_lock);
        g_task_return_boolean(task, TRUE);
        return;
    }

    /* wbar toma su pseudo-transparencia del pixmap raíz, no de la ventana
     * _NET_WM_WINDOW_TYPE_DESKTOP. Mantener ambos fondos sincronizados. */
    apply_root_wallpaper(update->path, update->mode);
    before = collect_wbar_processes(NULL, 0);
    if (before > 0) {
        stop_wbar_processes();
        apply_root_wallpaper(update->path, update->mode);
        g_usleep(250000);
        if (collect_wbar_processes(NULL, 0) > 0)
            stop_wbar_processes();
        start_wbar();
    }
    g_mutex_unlock(&update_lock);

    /* Recalcular el escritorio cuando wbar termina de reiniciarse. */
    g_main_context_invoke(NULL, desktop_wbar_refresh_main, NULL);
    g_task_return_boolean(task, TRUE);
}

void desktop_wbar_wallpaper_changed(const gchar *path, const gchar *mode)
{
#ifdef GDK_WINDOWING_X11
    GdkDisplay *display = gdk_display_get_default();
    GTask *task;
    DesktopWbarUpdate *update;

    if (!path || !*path || !display || !GDK_IS_X11_DISPLAY(display))
        return;

    update = g_new0(DesktopWbarUpdate, 1);
    update->path = g_strdup(path);
    update->mode = g_strdup(mode ? mode : "fill");
    update->generation = (guint)(g_atomic_int_add(&update_generation, 1) + 1);

    task = g_task_new(NULL, NULL, NULL, NULL);
    g_task_set_task_data(task, update, desktop_wbar_update_free);
    g_task_run_in_thread(task, desktop_wbar_update_thread);
    g_object_unref(task);
#else
    (void)path;
    (void)mode;
#endif
}
