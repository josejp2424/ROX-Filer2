/*
 * Rox-Filer2 2.14.0-5: real byte progress for copy and move.
 * See transfer_progress.h for the design.
 */

#include "config.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <glib.h>

#include "transfer_progress.h"

/* Small enough that a 4 MB/s stick still reports twice per second, large
 * enough that a fast SSD does not drown in syscalls. */
#define TP_CHUNK	((off_t) 2 * 1024 * 1024)
#define TP_BUDGET_US	150000		/* confirming time per loop pass */
#define TP_SEND_US	200000		/* at most 5 messages per second */
#define TP_FUSE_MAGIC	0x65735546UL

typedef struct {
	char		*path;
	gint64		confirmed;
	gboolean	fuse;
	gboolean	in_pending;
	gboolean	writer_done;	/* the cp/mv that wrote it has exited */
} Pending;

static struct {
	gboolean	active;
	TPSendFunc	send;
	char		*dest_dir;
	dev_t		dest_dev;
	gboolean	dest_dev_ok;

	/* fuseblk reports the real block device in st_dev; its sysfs
	 * counter is what the device has actually received. */
	char		*fuse_stat_path;
	guint64		fuse_base_sectors;
	gboolean	fuse_dirty;	/* FUSE data written since last push */

	gint64		counting;
	gint64		total;		/* -1 while counting */
	gint64		done;		/* confirmed by sync_file_range() */
	gint64		fuse_logical;	/* written by cp to FUSE files */

	GHashTable	*seen;		/* "dev:ino" -> Pending* (owned) */
	GHashTable	*before;	/* "dev:ino" -> "size:mtime" already there */
	GPtrArray	*pending;	/* Pending* still to confirm */
	gint64		last_send_us;
	gint64		next_walk_us;
	gint64		visited;
	gboolean	flushing;
	pid_t		syncer;		/* grandchild: fdatasync/syncfs */
	GPtrArray	*tracks;	/* destinations the running command writes */
} tp;

/* ------------------------------------------------------------ helpers */

static gboolean magic_lacks_ownership(unsigned long magic)
{
	switch (magic)
	{
		case 0x4d44UL:		/* MSDOS_SUPER_MAGIC (vfat/msdos) */
		case 0x2011BAB0UL:	/* EXFAT_SUPER_MAGIC */
		case 0x5346544eUL:	/* NTFS_SB_MAGIC (legacy ntfs) */
		case 0x7366746eUL:	/* ntfs3 */
		case 0x65735546UL:	/* FUSE (ntfs-3g, exfat-fuse, sshfs) */
		case 0xFF534D42UL:	/* CIFS */
		case 0xFE534D42UL:	/* SMB2 */
		case 0x517BUL:		/* SMB */
		case 0x4244UL:		/* HFS */
		case 0x15013346UL:	/* UDF */
		case 0x786f4256UL:	/* VBOXSF */
			return TRUE;
		default:
			return FALSE;
	}
}

gboolean tp_dest_lacks_ownership(const char *dir)
{
	struct statfs fs;

	if (!dir || statfs(dir, &fs) != 0)
		return FALSE;	/* unknown: keep preserving ownership */
	return magic_lacks_ownership((unsigned long) fs.f_type);
}

static gboolean path_is_fuse(const char *path)
{
	struct statfs fs;

	return statfs(path, &fs) == 0 &&
		(unsigned long) fs.f_type == TP_FUSE_MAGIC;
}

/* Field 7 of /sys/dev/block/MAJ:MIN/stat: 512-byte sectors written. */
static gboolean read_sectors_written(const char *stat_path, guint64 *sectors)
{
	gchar *contents = NULL;
	gchar **fields;
	gboolean ok = FALSE;
	int i, n = 0;

	if (!g_file_get_contents(stat_path, &contents, NULL, NULL))
		return FALSE;
	fields = g_strsplit_set(g_strstrip(contents), " \t", -1);
	for (i = 0; fields[i]; i++)
	{
		if (!*fields[i])
			continue;
		if (n++ == 6)
		{
			*sectors = g_ascii_strtoull(fields[i], NULL, 10);
			ok = TRUE;
			break;
		}
	}
	g_strfreev(fields);
	g_free(contents);
	return ok;
}

static gint64 tp_done_now(void)
{
	gint64 done = tp.done;

	if (tp.fuse_stat_path)
	{
		guint64 now = 0;
		gint64 device = 0;

		if (read_sectors_written(tp.fuse_stat_path, &now) &&
		    now > tp.fuse_base_sectors)
			device = (gint64) (now - tp.fuse_base_sectors) * 512;
		done += MIN(tp.fuse_logical, device);
	}
	else
		done += tp.fuse_logical;

	if (tp.total < 0)
		return 0;
	/* Hard links and sparse files can make the two sums differ. */
	return CLAMP(done, 0, tp.total);
}

static void tp_send(gboolean force)
{
	gint64 now;

	if (!tp.active || !tp.send)
		return;
	now = g_get_monotonic_time();
	if (!force && now - tp.last_send_us < TP_SEND_US)
		return;
	tp.last_send_us = now;
	tp.send(tp_done_now(), tp.total, tp.flushing);
}

static void pending_free(gpointer data)
{
	Pending *p = data;

	g_free(p->path);
	g_free(p);
}

/* ------------------------------------------------------------ counting */

static gint64 count_tree(const char *path)
{
	struct stat st;
	DIR *dir;
	struct dirent *ent;
	gint64 total = 0;

	if (lstat(path, &st) != 0)
		return 0;
	if (S_ISREG(st.st_mode))
		return st.st_size;
	if (!S_ISDIR(st.st_mode))
		return 0;	/* symlinks are copied as links */

	dir = opendir(path);
	if (!dir)
		return 0;
	while ((ent = readdir(dir)))
	{
		gchar *child;

		if (strcmp(ent->d_name, ".") == 0 ||
		    strcmp(ent->d_name, "..") == 0)
			continue;
		child = g_build_filename(path, ent->d_name, NULL);
		total += count_tree(child);
		g_free(child);
	}
	closedir(dir);
	return total;
}

void tp_begin(const char *dest_dir, TPSendFunc send)
{
	struct stat st;

	if (tp.seen)
		g_hash_table_destroy(tp.seen);
	if (tp.before)
		g_hash_table_destroy(tp.before);
	if (tp.pending)
		g_ptr_array_free(tp.pending, TRUE);
	g_free(tp.dest_dir);
	g_free(tp.fuse_stat_path);
	if (tp.tracks)
		g_ptr_array_free(tp.tracks, TRUE);
	memset(&tp, 0, sizeof(tp));

	tp.active = TRUE;
	tp.send = send;
	tp.total = -1;
	tp.dest_dir = g_strdup(dest_dir);
	tp.seen = g_hash_table_new_full(g_str_hash, g_str_equal,
					g_free, pending_free);
	tp.before = g_hash_table_new_full(g_str_hash, g_str_equal,
					  g_free, g_free);
	tp.pending = g_ptr_array_new();

	if (dest_dir && stat(dest_dir, &st) == 0)
	{
		tp.dest_dev = st.st_dev;
		tp.dest_dev_ok = TRUE;
		if (path_is_fuse(dest_dir))
		{
			/* Baseline before any cp/mv starts.  Plain FUSE
			 * (sshfs) has an anonymous 0:N device and no sysfs
			 * entry: it falls back to counting what cp wrote. */
			char *stat_path = g_strdup_printf(
				"/sys/dev/block/%u:%u/stat",
				major(st.st_dev), minor(st.st_dev));
			if (read_sectors_written(stat_path,
						 &tp.fuse_base_sectors))
				tp.fuse_stat_path = stat_path;
			else
				g_free(stat_path);
		}
	}
	tp_send(TRUE);	/* "Calculating size..." */
}

gboolean tp_active(void)
{
	return tp.active;
}

void tp_add_source(const char *path)
{
	if (tp.active)
		tp.counting += count_tree(path);
}

void tp_counted(void)
{
	if (!tp.active)
		return;
	tp.total = tp.counting;
	tp_send(TRUE);
}

gboolean tp_same_device(const char *path)
{
	struct stat st;

	return tp.dest_dev_ok && lstat(path, &st) == 0 &&
		st.st_dev == tp.dest_dev;
}

/* ------------------------------------------------------------ tracking */

static gchar *file_key(const struct stat *st)
{
	return g_strdup_printf("%lu:%lu", (unsigned long) st->st_dev,
			       (unsigned long) st->st_ino);
}

static gchar *file_state(const struct stat *st)
{
	return g_strdup_printf("%lld:%lld.%09ld", (long long) st->st_size,
			       (long long) st->st_mtim.tv_sec,
			       (long) st->st_mtim.tv_nsec);
}

/* What is already in a destination before a command runs, so a merge does
 * not count files it leaves alone.  Not ctime: FAT/exFAT have none and
 * exfat-fuse reports ctime = mtime, which cp -a sets back to the source's. */
static void snapshot_tree(const char *path)
{
	struct stat st;
	DIR *dir;
	struct dirent *ent;

	if (lstat(path, &st) != 0)
		return;
	if (S_ISREG(st.st_mode))
	{
		gchar *key = file_key(&st);

		if (!g_hash_table_contains(tp.seen, key))
			g_hash_table_insert(tp.before, key, file_state(&st));
		else
			g_free(key);
		return;
	}
	if (!S_ISDIR(st.st_mode))
		return;
	dir = opendir(path);
	if (!dir)
		return;
	while ((ent = readdir(dir)))
	{
		gchar *child;

		if (strcmp(ent->d_name, ".") == 0 ||
		    strcmp(ent->d_name, "..") == 0)
			continue;
		child = g_build_filename(path, ent->d_name, NULL);
		snapshot_tree(child);
		g_free(child);
	}
	closedir(dir);
}

static void register_file(const char *path)
{
	struct stat st;
	gchar *key;
	Pending *p;

	if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode))
		return;

	key = file_key(&st);
	p = g_hash_table_lookup(tp.seen, key);
	if (!p)
	{
		const gchar *was = g_hash_table_lookup(tp.before, key);

		if (was)
		{
			gchar *now = file_state(&st);
			gboolean untouched = strcmp(was, now) == 0;

			g_free(now);
			if (untouched)
			{
				g_free(key);
				return;	/* already there, left alone */
			}
		}
	}
	if (p)
	{
		g_free(key);
		/* Same inode under a new name: rsync renamed its temporary
		 * ".name.XXXXXX" to the final name.  Keep what was confirmed. */
		if (strcmp(p->path, path) != 0)
		{
			g_free(p->path);
			p->path = g_strdup(path);
		}
		if (!p->in_pending && st.st_size != p->confirmed)
		{
			/* Written again (e.g. a merge overwrote it). */
			p->in_pending = TRUE;
			p->writer_done = FALSE;
			g_ptr_array_add(tp.pending, p);
		}
		return;
	}

	p = g_new0(Pending, 1);
	p->path = g_strdup(path);
	p->fuse = path_is_fuse(path);
	p->in_pending = TRUE;
	g_hash_table_insert(tp.seen, key, p);
	g_ptr_array_add(tp.pending, p);
}

static void walk_tree(const char *path)
{
	struct stat st;
	DIR *dir;
	struct dirent *ent;

	tp.visited++;
	if (lstat(path, &st) != 0)
		return;
	if (S_ISREG(st.st_mode))
	{
		register_file(path);
		return;
	}
	if (!S_ISDIR(st.st_mode))
		return;
	dir = opendir(path);
	if (!dir)
		return;
	while ((ent = readdir(dir)))
	{
		gchar *child;

		if (strcmp(ent->d_name, ".") == 0 ||
		    strcmp(ent->d_name, "..") == 0)
			continue;
		child = g_build_filename(path, ent->d_name, NULL);
		walk_tree(child);
		g_free(child);
	}
	closedir(dir);
}

/* rsync writes "<dir>/.<name>.XXXXXX" and renames it at the end; without
 * watching it a large file would only be confirmed after rsync exits. */
static void scan_temporaries(const char *path)
{
	gchar *dir = g_path_get_dirname(path);
	gchar *base = g_path_get_basename(path);
	gchar *prefix = g_strconcat(".", base, ".", NULL);
	DIR *d = opendir(dir);
	struct dirent *ent;

	if (d)
	{
		while ((ent = readdir(d)))
		{
			tp.visited++;
			if (g_str_has_prefix(ent->d_name, prefix))
			{
				gchar *tmp = g_build_filename(dir, ent->d_name, NULL);
				register_file(tmp);
				g_free(tmp);
			}
		}
		closedir(d);
	}
	g_free(prefix);
	g_free(base);
	g_free(dir);
}

/* Big folder trees are walked less often (4000 files -> 1 s).  Based on
 * the file count, not elapsed time, which includes device waits. */
static void scan_tracks(gboolean force)
{
	gint64 now = g_get_monotonic_time();
	guint i;

	if (!tp.tracks || (!force && now < tp.next_walk_us))
		return;
	tp.visited = 0;
	for (i = 0; i < tp.tracks->len; i++)
	{
		const char *path = g_ptr_array_index(tp.tracks, i);
		struct stat st;

		walk_tree(path);
		if (lstat(path, &st) != 0 || !S_ISDIR(st.st_mode))
			scan_temporaries(path);
	}
	tp.next_walk_us = now + MAX((gint64) 150000, tp.visited * 250);
}

/* Confirm one file for at most until 'deadline'. TRUE if it is complete. */
static gboolean confirm_one(Pending *p, gint64 deadline)
{
	struct stat st;
	int fd;

	fd = open(p->path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (fd < 0)
	{
		/* Gone or unreadable (mode 0200): it cannot be confirmed. */
		if (lstat(p->path, &st) == 0 && st.st_size > p->confirmed)
		{
			if (p->fuse)
				tp.fuse_logical += st.st_size - p->confirmed;
			else
				tp.done += st.st_size - p->confirmed;
			p->confirmed = st.st_size;
		}
		return p->writer_done;
	}
	if (fstat(fd, &st) != 0)
	{
		close(fd);
		return p->writer_done;
	}
	if (st.st_size < p->confirmed)
	{
		/* Truncated and rewritten. */
		if (p->fuse)
			tp.fuse_logical -= p->confirmed;
		else
			tp.done -= p->confirmed;
		p->confirmed = 0;
	}

	if (p->fuse)
	{
		/* The data is already with the daemon: count it here; the
		 * bar shows min(written, device) and a grandchild pushes it
		 * to the device (service_fuse()). */
		if (st.st_size > p->confirmed)
		{
			tp.fuse_logical += st.st_size - p->confirmed;
			p->confirmed = st.st_size;
			tp.fuse_dirty = TRUE;
		}
		close(fd);
		return p->writer_done;
	}

	while (p->confirmed < st.st_size)
	{
		off_t n = MIN(TP_CHUNK, st.st_size - p->confirmed);

		if (sync_file_range(fd, p->confirmed, n,
				    SYNC_FILE_RANGE_WAIT_BEFORE |
				    SYNC_FILE_RANGE_WRITE |
				    SYNC_FILE_RANGE_WAIT_AFTER) != 0)
		{
			/* Not supported here: one whole-file flush. */
			fdatasync(fd);
			tp.done += st.st_size - p->confirmed;
			p->confirmed = st.st_size;
			break;
		}
		p->confirmed += n;
		tp.done += n;
		tp_send(FALSE);
		if (g_get_monotonic_time() >= deadline)
			break;
	}
	close(fd);
	return p->writer_done && p->confirmed >= st.st_size;
}

static void confirm_pending(gint64 budget_us)
{
	gint64 deadline = g_get_monotonic_time() + budget_us;
	guint i = 0;

	while (i < tp.pending->len)
	{
		Pending *p = g_ptr_array_index(tp.pending, i);

		if (confirm_one(p, deadline))
		{
			p->in_pending = FALSE;
			g_ptr_array_remove_index(tp.pending, i);
		}
		else
			i++;
		if (g_get_monotonic_time() >= deadline)
			break;
	}
}

/* ------------------------------------------------------------ syncer */

static void reap_syncer(void)
{
	int status;

	if (tp.syncer > 0 && waitpid(tp.syncer, &status, WNOHANG) == tp.syncer)
		tp.syncer = 0;
}

/* A grandchild that only calls open() + fdatasync()/syncfs() + _exit():
 * the action child keeps reporting progress meanwhile. */
static void start_syncer(const char *path, gboolean whole_fs)
{
	pid_t pid;

	if (tp.syncer > 0 || !path)
		return;
	pid = fork();
	if (pid == 0)
	{
		int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);

		if (fd >= 0)
		{
			if (whole_fs)
				syncfs(fd);
			else
				fdatasync(fd);
		}
		_exit(0);
	}
	if (pid > 0)
		tp.syncer = pid;
}

static void wait_syncer(void)
{
	while (tp.syncer > 0)
	{
		reap_syncer();
		tp_send(FALSE);
		if (tp.syncer > 0)
			g_usleep(100000);
	}
}

/* Push FUSE data to the device while cp/rsync run.  syncfs() on fuseblk
 * also flushes the block device below (sync_blockdev), where exfat-fuse and
 * ntfs-3g leave their writes; no particular file is needed, which matters
 * because rsync renames its temporary file under our feet. */
static void service_fuse(void)
{
	reap_syncer();
	if (tp.syncer == 0 && tp.fuse_dirty && tp.dest_dir)
	{
		tp.fuse_dirty = FALSE;
		start_syncer(tp.dest_dir, TRUE);
	}
}

/* ------------------------------------------------------------ tracks */

void tp_track_begin(const char *const *paths)
{
	if (!tp.active)
		return;
	if (tp.tracks)
		g_ptr_array_free(tp.tracks, TRUE);
	tp.tracks = g_ptr_array_new_with_free_func(g_free);
	g_hash_table_remove_all(tp.before);
	for (; paths && *paths; paths++)
	{
		g_ptr_array_add(tp.tracks, g_strdup(*paths));
		snapshot_tree(*paths);
	}
	tp.next_walk_us = 0;
}

void tp_pump(void)
{
	if (!tp.active)
		return;
	scan_tracks(FALSE);
	confirm_pending(TP_BUDGET_US);
	service_fuse();
	tp_send(FALSE);
}

void tp_track_end(void)
{
	guint i;

	if (!tp.active)
		return;
	scan_tracks(TRUE);
	/* One writer at a time: whatever is pending is complete.  Do not wait
	 * for the device here: the next command starts at once and these
	 * files keep being confirmed meanwhile. */
	for (i = 0; i < tp.pending->len; i++)
		((Pending *) g_ptr_array_index(tp.pending, i))->writer_done = TRUE;
	if (tp.tracks)
		g_ptr_array_free(tp.tracks, TRUE);
	tp.tracks = NULL;
	tp_send(FALSE);
}

/* ------------------------------------------------------------ running */

static void drain(int fd, GString *buffer)
{
	char chunk[4096];
	ssize_t n;

	for (;;)
	{
		n = read(fd, chunk, sizeof(chunk));
		if (n > 0)
			g_string_append_len(buffer, chunk, n);
		else if (n < 0 && errno == EINTR)
			continue;
		else
			break;
	}
}

/* waitpid() is safe here: start_action() resets SIGCHLD to SIG_DFL in the
 * action child, so no reaper competes for the exit status.  Raw fork/exec
 * also avoids GLib's spawn helpers and the parent's child-reap mutex. */
char *tp_run(const char **argv, const char *track_path, char **warnings)
{
	int fds[2];
	int status = 0;
	pid_t pid;
	GString *err;
	gboolean tracking = tp.active && track_path;

	if (warnings)
		*warnings = NULL;
	if (pipe2(fds, O_CLOEXEC) != 0)
		return g_strdup(g_strerror(errno));

	pid = fork();
	if (pid < 0)
	{
		int e = errno;

		close(fds[0]);
		close(fds[1]);
		return g_strdup(g_strerror(e));
	}
	if (pid == 0)
	{
		int null = open("/dev/null", O_WRONLY | O_CLOEXEC);

		dup2(fds[1], STDERR_FILENO);
		if (null >= 0)
			dup2(null, STDOUT_FILENO);
		execvp(argv[0], (char **) argv);
		dprintf(STDERR_FILENO, "%s: %s\n", argv[0], strerror(errno));
		_exit(127);
	}

	close(fds[1]);
	fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
	err = g_string_new(NULL);
	if (tracking)
	{
		const char *tracks[] = { track_path, NULL };
		tp_track_begin(tracks);
	}

	for (;;)
	{
		struct pollfd pfd = { fds[0], POLLIN, 0 };
		pid_t r;

		drain(fds[0], err);
		tp_pump();

		r = waitpid(pid, &status, WNOHANG);
		if (r == pid)
			break;
		if (r < 0 && errno != EINTR)
		{
			status = -1;
			break;
		}
		poll(&pfd, 1, tp.active ? 50 : 200);
	}

	/* Everything the command printed. */
	fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) & ~O_NONBLOCK);
	drain(fds[0], err);
	close(fds[0]);

	if (tracking)
		tp_track_end();

	g_strstrip(err->str);
	g_string_set_size(err, strlen(err->str));

	if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		if (err->len)
			return g_string_free(err, FALSE);
		g_string_free(err, TRUE);
		if (status != -1 && !WIFEXITED(status))
			return g_strdup("(Subprocess crashed?)");
		return g_strdup(_("ERROR"));
	}
	if (warnings && err->len)
		*warnings = g_string_free(err, FALSE);
	else
		g_string_free(err, TRUE);
	return NULL;
}

char *tp_copy_file(const char *from, const char *to, char **warnings)
{
	/* Same options as support.c:copy_file(). */
	/* Room for: options, --no-preserve=ownership, "--", from, to, NULL. */
#if defined(HAVE_GETXATTR) || defined(HAVE_ATTROPEN)
	const char *argv[8] = {"cp", "-af", "--no-preserve=context,links"};
	int i = 3;
#else
	const char *argv[7] = {"cp", "-pRf"};
	int i = 2;
#endif
	gchar *dir = g_path_get_dirname(to);

	/* GNU cp run as root reports "failed to preserve ownership" and exits
	 * 1 on these filesystems although the data was copied; as a normal
	 * user the same failure is silent.  The directories and symlinks
	 * do_copy2() makes itself already ignore a failed lchown(). */
	if (tp_dest_lacks_ownership(dir))
		argv[i++] = "--no-preserve=ownership";
	g_free(dir);
	argv[i++] = "--";
	argv[i++] = from;
	argv[i++] = to;
	argv[i] = NULL;
	g_assert((gsize) i < G_N_ELEMENTS(argv));

	return tp_run(argv, to, warnings);
}

/* ------------------------------------------------------------ finishing */

void tp_finish(void)
{
	guint i;

	if (!tp.active)
		return;

	tp.flushing = TRUE;
	tp_send(TRUE);

	for (i = 0; i < tp.pending->len; i++)
		((Pending *) g_ptr_array_index(tp.pending, i))->writer_done = TRUE;
	while (tp.pending->len)
	{
		confirm_pending(TP_BUDGET_US);
		service_fuse();
		tp_send(FALSE);
	}

	/* Directory entries, FAT tables, journal and, on fuseblk, the block
	 * device below the daemon: only the destination filesystem.  A FUSE
	 * push may still be running; its successor sees anything newer. */
	wait_syncer();
	start_syncer(tp.dest_dir, TRUE);
	wait_syncer();

	tp.done = MAX(tp.total, 0);
	tp.fuse_logical = 0;
	g_free(tp.fuse_stat_path);
	tp.fuse_stat_path = NULL;
	tp_send(TRUE);

	tp.active = FALSE;
	if (tp.tracks)
		g_ptr_array_free(tp.tracks, TRUE);
	tp.tracks = NULL;
	g_hash_table_destroy(tp.seen);
	tp.seen = NULL;
	g_hash_table_destroy(tp.before);
	tp.before = NULL;
	g_ptr_array_free(tp.pending, TRUE);
	tp.pending = NULL;
}
