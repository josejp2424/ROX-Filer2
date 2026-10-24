/*
 * Rox-Filer2 2.14.0-5: real byte progress for copy and move.
 *
 * cp(1) returns as soon as the data is in the kernel page cache; on a slow
 * USB stick the kernel then keeps writing long after the action window has
 * closed, and the old bar advanced per top-level item (one ISO: 0 -> 100).
 *
 * Runs inside the action child process (see start_action() in action.c).
 * cp/mv are started with fork/exec and, while they run, the bytes they wrote
 * are confirmed on the device with sync_file_range().  FUSE block filesystems
 * (exfat-fuse, ntfs-3g) are measured with the device's sectors-written
 * counter instead.  tp_finish() then flushes only the destination filesystem
 * before the operation is reported done, so 100% means "safe to remove".
 *
 * No threads: waits that can block for long (fdatasync on FUSE, syncfs) run
 * in a short-lived grandchild so progress keeps flowing to the window.
 */

#ifndef TRANSFER_PROGRESS_H
#define TRANSFER_PROGRESS_H

#include <glib.h>

/* Report progress: total < 0 while still counting. */
typedef void (*TPSendFunc)(gint64 done, gint64 total, gboolean flushing);

/* Start a tracked operation writing into dest_dir. */
void tp_begin(const char *dest_dir, TPSendFunc send);
/* Count the bytes 'path' will write (copy, or a move to another filesystem). */
void tp_add_source(const char *path);
/* All sources counted: report the total. */
void tp_counted(void);
gboolean tp_active(void);
/* TRUE if a move of 'path' into the destination is a rename (no bytes). */
gboolean tp_same_device(const char *path);

/* Run argv (cp or mv) and track what it writes under track_path (a file or a
 * folder, or NULL).  Returns an error string like fork_exec_wait(), or NULL
 * on success.  Messages printed on stderr by a command that exits 0 (e.g. mv
 * reporting it could not keep the owner on FAT) are returned in *warnings
 * instead of being treated as a failure.  g_free() both. */
char *tp_run(const char **argv, const char *track_path, char **warnings);

/* For commands run elsewhere (rsync via popen): set what they write, call
 * tp_pump() whenever possible while they run, tp_track_end() after exit. */
void tp_track_begin(const char *const *paths);
void tp_pump(void);
void tp_track_end(void);

/* cp -af as support.c:copy_file(), minus ownership when the destination
 * filesystem cannot store Unix owners. */
char *tp_copy_file(const char *from, const char *to, char **warnings);

/* Confirm what is left and flush the destination filesystem. */
void tp_finish(void);

/* FAT, exFAT, NTFS, SMB, FUSE...: no Unix owners. */
gboolean tp_dest_lacks_ownership(const char *dir);

#endif /* TRANSFER_PROGRESS_H */
