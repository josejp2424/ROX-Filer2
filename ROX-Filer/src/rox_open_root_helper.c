/*
 * Rox-Filer2 fixed Open-as-root helper.
 *
 * 2.13.0-9: launched only through pkexec from the normal-user context menu.
 * It accepts exactly one existing directory, resolves it with realpath() and
 * starts the fixed package-owned Rox-Filer2 binary.  It never invokes a shell
 * and never accepts an executable path from the caller.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define ROX_FILER_BINARY "/usr/lib/rox-filer2/ROX-Filer"

static void usage(const char *argv0)
{
    fprintf(stderr, "Usage: %s DIRECTORY\n", argv0);
}

static bool validate_directory(const char *input, char resolved[PATH_MAX])
{
    struct stat st;

    if (!input || input[0] != '/') {
        fprintf(stderr, "Directory must be an absolute path.\n");
        return false;
    }
    if (!realpath(input, resolved)) {
        fprintf(stderr, "Could not resolve '%s': %s\n", input, strerror(errno));
        return false;
    }
    if (stat(resolved, &st) != 0) {
        fprintf(stderr, "Could not stat '%s': %s\n", resolved, strerror(errno));
        return false;
    }
    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Not a directory: %s\n", resolved);
        return false;
    }
    return true;
}

static void preserve_caller_xauthority(void)
{
    const char *current = getenv("XAUTHORITY");
    const char *uid_text;
    char *end = NULL;
    unsigned long raw_uid;
    struct passwd *caller;
    char candidate[PATH_MAX];

    /* pkexec with allow_gui=true preserves XAUTHORITY when the session sets
     * it explicitly. Traditional X11 sessions often leave it unset and rely
     * on ~/.Xauthority, so derive that path from PKEXEC_UID before HOME is
     * switched to /root. */
    if (current && *current)
        return;

    uid_text = getenv("PKEXEC_UID");
    if (!uid_text || !*uid_text)
        return;
    errno = 0;
    raw_uid = strtoul(uid_text, &end, 10);
    if (errno || !end || *end != '\0' || raw_uid > (unsigned long)((uid_t)-1))
        return;

    caller = getpwuid((uid_t)raw_uid);
    if (!caller || !caller->pw_dir || !*caller->pw_dir)
        return;
    if (snprintf(candidate, sizeof(candidate), "%s/.Xauthority", caller->pw_dir) >=
            (int)sizeof(candidate))
        return;
    if (access(candidate, R_OK) == 0)
        (void)setenv("XAUTHORITY", candidate, 1);
}

static void prepare_root_environment(void)
{
    struct passwd *pw;

    preserve_caller_xauthority();
    pw = getpwuid(0);

    if (pw && pw->pw_dir && *pw->pw_dir)
        (void)setenv("HOME", pw->pw_dir, 1);
    (void)setenv("USER", "root", 1);
    (void)setenv("LOGNAME", "root", 1);

    /* The action is intentionally X11/XLibre/XWayland only.  polkit's
     * allow_gui annotation preserves DISPLAY/XAUTHORITY for this fixed helper.
     * Never use xhost and never point root at the user's Wayland runtime dir. */
    (void)setenv("GDK_BACKEND", "x11", 1);
    (void)setenv("ROX_DESKTOP_BACKEND", "x11", 1);
    (void)unsetenv("WAYLAND_DISPLAY");
    (void)unsetenv("XDG_RUNTIME_DIR");
}

int main(int argc, char **argv)
{
    char resolved[PATH_MAX];
    pid_t pid;

    if (geteuid() != 0) {
        fprintf(stderr, "rox-open-root-helper must be run as root through pkexec.\n");
        return 1;
    }
    if (argc != 2) {
        usage(argv[0]);
        return 2;
    }
    if (!getenv("DISPLAY") || !*getenv("DISPLAY")) {
        fprintf(stderr, "An X11/XLibre or XWayland DISPLAY is required.\n");
        return 1;
    }
    if (access(ROX_FILER_BINARY, X_OK) != 0) {
        fprintf(stderr, "Required program '%s' was not found or is not executable.\n",
                ROX_FILER_BINARY);
        return 1;
    }
    if (!validate_directory(argv[1], resolved))
        return 1;

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "fork failed: %s\n", strerror(errno));
        return 1;
    }
    if (pid > 0)
        return 0;

    (void)setsid();
    prepare_root_environment();
    if (chdir(resolved) != 0) {
        fprintf(stderr, "Could not enter '%s': %s\n", resolved, strerror(errno));
        _exit(1);
    }

    execl(ROX_FILER_BINARY, "ROX-Filer", "-n", resolved, (char *)NULL);
    fprintf(stderr, "Could not start '%s': %s\n", ROX_FILER_BINARY, strerror(errno));
    _exit(1);
}
