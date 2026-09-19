/*
 * Rox-Filer2 privileged drive helper
 * Copyright (C) 2026 josejp2424 and Rox-Filer2 contributors.
 *
 * Rox-Filer2 invokes this helper through pkexec only when the filer runs as
 * a regular user. Root/Puppy sessions keep the direct mount path.
 *
 * Security properties:
 *   - no shell is used;
 *   - only fixed storage operations are accepted;
 *   - block-device arguments are canonicalised and validated;
 *   - external tools are resolved only from fixed system directories.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <mntent.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *const system_dirs[] = {
    "/usr/bin", "/bin", "/usr/sbin", "/sbin", "/usr/local/bin",
    "/usr/local/sbin", NULL
};

static char *find_program(const char *name)
{
    size_t i;
    char candidate[PATH_MAX];

    if (!name || !*name || strchr(name, '/'))
        return NULL;
    for (i = 0; system_dirs[i]; i++) {
        if (snprintf(candidate, sizeof(candidate), "%s/%s",
                     system_dirs[i], name) >= (int) sizeof(candidate))
            continue;
        if (access(candidate, X_OK) == 0)
            return strdup(candidate);
    }
    return NULL;
}

static bool wait_ok(pid_t pid)
{
    int status;

    while (waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR)
            continue;
        return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool run_program(char *const argv[])
{
    pid_t pid;

    if (!argv || !argv[0])
        return false;
    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "fork: %s\n", strerror(errno));
        return false;
    }
    if (pid == 0) {
        execv(argv[0], argv);
        fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
        _exit(127);
    }
    return wait_ok(pid);
}

static char *run_capture_stdout(char *const argv[])
{
    int pfd[2];
    pid_t pid;
    char *buf = NULL;
    size_t used = 0, cap = 0;
    bool ok;

    if (pipe(pfd) != 0)
        return NULL;
    pid = fork();
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return NULL;
    }
    if (pid == 0) {
        close(pfd[0]);
        if (dup2(pfd[1], STDOUT_FILENO) < 0) {
            close(pfd[1]);
            _exit(127);
        }
        close(pfd[1]);
        execv(argv[0], argv);
        close(STDOUT_FILENO);
        _exit(127);
    }

    close(pfd[1]);
    for (;;) {
        char chunk[512];
        ssize_t n = read(pfd[0], chunk, sizeof(chunk));
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (n == 0)
            break;
        if (used + (size_t) n + 1 > cap) {
            size_t newcap = cap ? cap * 2 : 1024;
            char *tmp;
            while (newcap < used + (size_t) n + 1)
                newcap *= 2;
            tmp = realloc(buf, newcap);
            if (!tmp) {
                free(buf);
                close(pfd[0]);
                (void) wait_ok(pid);
                return NULL;
            }
            buf = tmp;
            cap = newcap;
        }
        if (!buf) {
            close(pfd[0]);
            (void) wait_ok(pid);
            return NULL;
        }
        memcpy(buf + used, chunk, (size_t) n);
        used += (size_t) n;
    }
    close(pfd[0]);
    ok = wait_ok(pid);
    if (!ok) {
        free(buf);
        return NULL;
    }
    if (!buf)
        return strdup("");
    buf[used] = '\0';
    while (used > 0 && (buf[used - 1] == '\n' || buf[used - 1] == '\r' ||
                        buf[used - 1] == ' ' || buf[used - 1] == '\t'))
        buf[--used] = '\0';
    return buf;
}

static bool canonical_block_device(const char *input, char out[PATH_MAX])
{
    struct stat st;

    if (!input || !realpath(input, out)) {
        fprintf(stderr, "Invalid block device '%s': %s\n",
                input ? input : "", strerror(errno));
        return false;
    }
    if (strncmp(out, "/dev/", 5) != 0 || stat(out, &st) != 0 ||
        !S_ISBLK(st.st_mode)) {
        fprintf(stderr, "Refusing non-block device '%s'\n", input);
        return false;
    }
    return true;
}

static char *mountpoint_for_device(const char *device)
{
    FILE *fp;
    struct mntent entbuf;
    struct mntent *ent;
    char buf[8192];
    char dev_real[PATH_MAX];

    if (!canonical_block_device(device, dev_real))
        return NULL;
    fp = setmntent("/proc/self/mounts", "r");
    if (!fp)
        return NULL;
    while ((ent = getmntent_r(fp, &entbuf, buf, sizeof(buf))) != NULL) {
        char entry_real[PATH_MAX];
        if (ent->mnt_fsname[0] == '/' && realpath(ent->mnt_fsname, entry_real) &&
            strcmp(entry_real, dev_real) == 0) {
            char *result = strdup(ent->mnt_dir);
            endmntent(fp);
            return result;
        }
    }
    endmntent(fp);
    return NULL;
}

static uid_t invoking_uid(void)
{
    const char *text = getenv("PKEXEC_UID");
    char *end = NULL;
    unsigned long value;

    if (!text || !*text)
        return 0;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno || !end || *end || value > (unsigned long) ((uid_t) -1))
        return 0;
    return (uid_t) value;
}

static gid_t invoking_gid(uid_t uid)
{
    struct passwd *pw;

    if (uid == 0)
        return 0;
    pw = getpwuid(uid);
    return pw ? pw->pw_gid : (gid_t) uid;
}

static char *filesystem_type(const char *device)
{
    char *blkid = find_program("blkid");
    char *result;
    char *argv[7];

    if (!blkid)
        return NULL;
    argv[0] = blkid;
    argv[1] = "-o";
    argv[2] = "value";
    argv[3] = "-s";
    argv[4] = "TYPE";
    argv[5] = (char *) device;
    argv[6] = NULL;
    result = run_capture_stdout(argv);
    free(blkid);
    return result;
}

static bool mount_device(const char *input)
{
    char device[PATH_MAX];
    char target[PATH_MAX];
    const char *base;
    char *mount = NULL;
    char *fstype = NULL;
    char options[160];
    char *existing = NULL;
    uid_t uid;
    gid_t gid;
    bool made = false;
    bool ok;

    if (!canonical_block_device(input, device))
        return false;
    existing = mountpoint_for_device(device);
    if (existing) {
        puts(existing);
        free(existing);
        return true;
    }

    base = strrchr(device, '/');
    base = base ? base + 1 : device;
    if (!*base || snprintf(target, sizeof(target), "/mnt/%s", base) >= (int) sizeof(target)) {
        fprintf(stderr, "Invalid mount target for '%s'\n", device);
        return false;
    }
    if (mkdir("/mnt", 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "Unable to create /mnt: %s\n", strerror(errno));
        return false;
    }
    if (mkdir(target, 0755) == 0)
        made = true;
    else if (errno != EEXIST) {
        fprintf(stderr, "Unable to create '%s': %s\n", target, strerror(errno));
        return false;
    }

    mount = find_program("mount");
    if (!mount) {
        fprintf(stderr, "Required program 'mount' was not found.\n");
        if (made)
            (void) rmdir(target);
        return false;
    }

    uid = invoking_uid();
    gid = invoking_gid(uid);
    fstype = filesystem_type(device);
    options[0] = '\0';

    /* FAT/exFAT/NTFS do not carry Unix ownership.  Mount them for the user
     * who authenticated with pkexec instead of leaving everything root-owned. */
    if (uid != 0 && fstype &&
        (!strcmp(fstype, "vfat") || !strcmp(fstype, "msdos") ||
         !strcmp(fstype, "exfat") || !strcmp(fstype, "ntfs") ||
         !strcmp(fstype, "ntfs3"))) {
        snprintf(options, sizeof(options), "uid=%lu,gid=%lu,umask=022",
                 (unsigned long) uid, (unsigned long) gid);
    }

    if (options[0]) {
        char *argv[] = {mount, "-o", options, device, target, NULL};
        ok = run_program(argv);
    } else {
        char *argv[] = {mount, device, target, NULL};
        ok = run_program(argv);
    }

    if (!ok && made)
        (void) rmdir(target);
    if (ok)
        puts(target);
    free(fstype);
    free(mount);
    return ok;
}

static bool unmount_device(const char *input)
{
    char device[PATH_MAX];
    char *mountpoint;
    char *umount;
    bool ok;

    if (!canonical_block_device(input, device))
        return false;
    mountpoint = mountpoint_for_device(device);
    if (!mountpoint)
        return true;
    umount = find_program("umount");
    if (!umount) {
        fprintf(stderr, "Required program 'umount' was not found.\n");
        free(mountpoint);
        return false;
    }
    {
        char *argv[] = {umount, mountpoint, NULL};
        ok = run_program(argv);
    }
    if (ok && !strncmp(mountpoint, "/mnt/", 5))
        (void) rmdir(mountpoint);
    free(umount);
    free(mountpoint);
    return ok;
}

static bool eject_or_poweroff(const char *input, bool optical)
{
    char device[PATH_MAX];
    char *eject = NULL;
    char *udisksctl = NULL;
    bool ok = false;

    if (!canonical_block_device(input, device))
        return false;
    eject = find_program("eject");
    udisksctl = find_program("udisksctl");

    if (optical && eject) {
        char *argv[] = {eject, device, NULL};
        ok = run_program(argv);
    }
    if (!ok && udisksctl) {
        char *argv[] = {udisksctl, "power-off", "-b", device, NULL};
        ok = run_program(argv);
    }
    if (!ok && !optical && eject) {
        char *argv[] = {eject, device, NULL};
        ok = run_program(argv);
    }
    if (!ok && !eject && !udisksctl)
        fprintf(stderr, "Neither 'eject' nor 'udisksctl' was found.\n");

    free(eject);
    free(udisksctl);
    return ok;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
            "Usage: %s mount-device DEVICE\n"
            "       %s unmount-device DEVICE\n"
            "       %s eject-optical DEVICE\n"
            "       %s power-off DEVICE\n",
            argv0, argv0, argv0, argv0);
}

int main(int argc, char **argv)
{
    if (geteuid() != 0) {
        fprintf(stderr, "rox-mount-helper must be run as root through pkexec.\n");
        return 126;
    }
    if (argc != 3) {
        usage(argv[0]);
        return 2;
    }

    if (!strcmp(argv[1], "mount-device"))
        return mount_device(argv[2]) ? 0 : 1;
    if (!strcmp(argv[1], "unmount-device"))
        return unmount_device(argv[2]) ? 0 : 1;
    if (!strcmp(argv[1], "eject-optical"))
        return eject_or_poweroff(argv[2], true) ? 0 : 1;
    if (!strcmp(argv[1], "power-off"))
        return eject_or_poweroff(argv[2], false) ? 0 : 1;

    usage(argv[0]);
    return 2;
}
