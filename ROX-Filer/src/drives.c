/*
 * ROX-Filer GTK3 partition toolbar integration.
 *
 * Agregado por josejp2424 (2026): detección de particiones inspirada en la
 * integración de unidades de EssoraWM, con montaje directo para Puppy/root,
 * autorización mediante pkexec para usuarios normales y apertura de la
 * partición dentro de la ventana actual de ROX-Filer.
 *
 * Copyright (C) 2026 josejp2424
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 */

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <mntent.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include "global.h"
#include "support.h"
#include "drives.h"
#include "drives_monitor.h"
#include "filer.h"
#include "gui_support.h"
#include "main.h"
#include "mount.h"
#include "options.h"
#include "rox_config.h"
#include "smb.h"
#include "image_mounter.h"

#define DRIVE_ICON_INTERNAL  "drive-harddisk"
#define DRIVES_CONFIG "drives.ini"
#define DRIVE_COMMAND_TIMEOUT_SECONDS 5
/* mount/umount/eject legitimamente tardan: vaciar paginas sucias de un
 * pendrive puede llevar decenas de segundos. El techo existe solo para que
 * el mutex de reap no quede tomado indefinidamente. */
#define DRIVE_ACTION_TIMEOUT_SECONDS 90
#define DRIVE_AUTH_TIMEOUT_SECONDS 300
#define ROX_MOUNT_HELPER_PATH "/usr/lib/rox-filer2/rox-mount-helper"
/* Espera maxima por el mutex de reap antes de abandonar el intento. */
#define DRIVE_REAP_LOCK_WAIT_SECONDS 2
#define PORTABLE_DISCOVERY_TIMEOUT_SECONDS 4

static gsize drive_visibility_once = 0;
static gint show_system_partitions = FALSE;

/* 2.13.0-8: startup automount is a normal ROX preference, persisted in the
 * same XDG Options file as the rest of the filer settings. */
static Option o_drives_mount_all_startup;
static Option o_drives_mount_removable_startup;
static gboolean startup_automount_scheduled = FALSE;
static gint startup_automount_lock_fd = -1;

typedef RoxDriveInfo DriveInfo;

typedef struct
{
	FilerWindow *filer_window;
	DriveInfo *drive;
	GtkWidget *popover;
} DriveMenuAction;

typedef struct
{
	FilerWindow *filer_window;
	GtkWidget *popover;
	GtkWidget *grid;
	GtkWidget *scrolled;
} DrivePopoverLive;

/* Agregado por josejp2424 (2026): metadatos del dispositivo físico padre.
 * lsblk suele dejar TRAN/RM/MODEL vacíos en las líneas de particiones, aunque
 * estén presentes en la línea del disco. Se conservan aquí para heredarlos. */
typedef struct
{
	gchar *transport;
	gchar *model;
	gchar *device;
	gboolean removable;
	gboolean hardware_removable;
	gboolean solid_state;
	gboolean optical;
} DriveParentInfo;

static void drive_parent_info_free(gpointer data)
{
	DriveParentInfo *info = data;
	if (!info)
		return;
	g_free(info->transport);
	g_free(info->model);
	g_free(info->device);
	g_free(info);
}

static gboolean spawn_capture_timeout(gchar **argv, guint timeout_seconds,
        gchar **stdout_text, gchar **stderr_text, gint *wait_status,
        GError **error);
static gchar *parse_lsblk_value(const gchar *line, const gchar *key);
static GPtrArray *read_drive_list(GError **error);
static gboolean drive_is_useful(const DriveInfo *drive, const gchar *type,
		const gchar *partlabel, const gchar *parttype);
static gboolean drive_array_has_device(GPtrArray *drives, const gchar *device);
static gboolean drive_array_has_mountpoint(GPtrArray *drives, const gchar *mountpoint)
{
	guint i;

	if (!drives || !mountpoint)
		return FALSE;
	for (i = 0; i < drives->len; i++)
	{
		DriveInfo *drive = g_ptr_array_index(drives, i);
		if (g_strcmp0(drive->mountpoint, mountpoint) == 0)
			return TRUE;
	}
	return FALSE;
}

static gboolean drive_is_hidden_by_essorawm(const gchar *name);
static gchar *command_first_line(gchar **argv);
static DriveInfo *drive_info_from_device(const gchar *device);
static void append_puppy_runtime_drives(GPtrArray *drives);
static void append_sysfs_partitions(GPtrArray *drives);
static void append_portable_devices(GPtrArray *drives);
static gboolean technical_text_match(const gchar *value);
static gboolean name_looks_removable(const gchar *value);
static void drive_enrich_from_sysfs(DriveInfo *drive);
void rox_drive_info_free(gpointer data);
static gchar *find_mountpoint(const gchar *device);
static gchar *drive_current_mountpoint(const DriveInfo *drive);
static gboolean spawn_wait(gchar **argv, gchar **error_text);
static gchar *mount_drive(const DriveInfo *drive, gchar **error_text);
static gboolean unmount_drive(const DriveInfo *drive, gchar **error_text);
static gboolean eject_drive(const DriveInfo *drive, gchar **error_text);
static void drive_menu_action_free(gpointer data);
static void drive_grid_quick_action(GtkButton *button, gpointer data);
static void drive_grid_activate(GtkButton *button, gpointer data);
static gboolean drive_grid_button_press(GtkWidget *button,
		GdkEventButton *event, gpointer data);
static void drives_button_clicked(GtkToolButton *button, gpointer data);
static GtkWidget *drive_grid_button_new(const DriveInfo *drive);
static GtkWidget *drive_icon_widget(const DriveInfo *drive, gint size);
static void drives_popover_fill(DrivePopoverLive *live, GPtrArray *drives,
		const GError *error);
static void drives_popover_monitor_changed(GPtrArray *drives, const GError *error,
		gpointer user_data);
static gboolean drive_auto_mount_candidate(const DriveInfo *drive,
		gboolean include_removable);

static gint hex_value(gchar value)
{
	if (value >= '0' && value <= '9')
		return value - '0';
	if (value >= 'a' && value <= 'f')
		return value - 'a' + 10;
	if (value >= 'A' && value <= 'F')
		return value - 'A' + 10;
	return -1;
}

/* Agregado por josejp2424: decodificar de forma segura el formato -P de
 * lsblk, incluidos espacios expresados como secuencias \xNN. */
static gchar *parse_lsblk_value(const gchar *line, const gchar *key)
{
	gchar *pattern;
	const gchar *start;
	const gchar *end;
	GString *output;

	pattern = g_strdup_printf("%s=\"", key);
	start = line;
	while ((start = strstr(start, pattern)) != NULL)
	{
		/* lsblk -P emits space-separated KEY="VALUE" fields.  Match a
		 * complete field name, not a suffix such as TYPE inside FSTYPE. */
		if (start == line || g_ascii_isspace((guchar) start[-1]))
			break;
		start += strlen(pattern);
	}
	g_free(pattern);
	if (!start)
		return NULL;

	start = strchr(start, '"');
	if (!start)
		return NULL;
	start++;
	end = start;
	while (*end)
	{
		if (*end == '"' && (end == start || end[-1] != '\\'))
			break;
		end++;
	}

	output = g_string_new(NULL);
	while (start < end)
	{
		if (*start == '\\' && start + 3 < end && start[1] == 'x')
		{
			gint high = hex_value(start[2]);
			gint low = hex_value(start[3]);
			if (high >= 0 && low >= 0)
			{
				g_string_append_c(output, (gchar) ((high << 4) | low));
				start += 4;
				continue;
			}
		}
		if (*start == '\\' && start + 1 < end)
			start++;
		g_string_append_c(output, *start++);
	}

	return g_string_free(output, FALSE);
}

static gboolean name_looks_removable(const gchar *value)
{
	gchar *lower;
	gboolean result;

	if (!value || !*value)
		return FALSE;

	lower = g_utf8_strdown(value, -1);
	result = strstr(lower, "usb") != NULL ||
		strstr(lower, "ventoy") != NULL ||
		strstr(lower, "pendrive") != NULL ||
		strstr(lower, "flash") != NULL ||
		strstr(lower, "removable") != NULL ||
		strstr(lower, "sd card") != NULL ||
		strstr(lower, "memory card") != NULL;
	g_free(lower);
	return result;
}

/* Agregado por josejp2424 (2026): heredar del dispositivo físico los
 * metadatos que lsblk suele dejar vacíos en las particiones. Esto permite que
 * sdb1 conserve TRAN=usb, que mmcblk0p1 se reconozca como tarjeta y que la GUI
 * de Particiones use exactamente el mismo icono que ROX Desktop. */
static gchar *read_sysfs_text(const gchar *path)
{
	gchar *text = NULL;

	if (!path || !g_file_get_contents(path, &text, NULL, NULL))
		return NULL;
	g_strstrip(text);
	return text;
}

static gchar *drive_parent_block_name(const gchar *name)
{
	gchar *partition_path;
	gchar *class_path;
	gchar *directory;
	gchar *parent;
	char *resolved;

	if (!name || !*name)
		return NULL;

	partition_path = g_build_filename("/sys/class/block", name,
		"partition", NULL);
	if (!g_file_test(partition_path, G_FILE_TEST_EXISTS))
	{
		g_free(partition_path);
		return g_strdup(name);
	}
	g_free(partition_path);

	class_path = g_build_filename("/sys/class/block", name, NULL);
	resolved = realpath(class_path, NULL);
	g_free(class_path);
	if (!resolved)
		return g_strdup(name);

	directory = g_path_get_dirname(resolved);
	parent = g_path_get_basename(directory);
	g_free(directory);
	free(resolved);
	return parent;
}

static void drive_enrich_from_sysfs(DriveInfo *drive)
{
	gchar *parent;
	gchar *path;
	gchar *value;
	gchar *class_path;
	char *resolved;

	if (!drive || !drive->name || !*drive->name)
		return;

	parent = drive_parent_block_name(drive->name);
	if (!parent)
		return;
	if (!drive->parent_device && drive->device)
		drive->parent_device = g_build_filename("/dev", parent, NULL);

	path = g_build_filename("/sys/class/block", parent, "removable", NULL);
	value = read_sysfs_text(path);
	if (value)
	{
		gboolean removable = atoi(value) != 0;
		drive->removable = drive->removable || removable;
		drive->hardware_removable = drive->hardware_removable || removable;
	}
	g_free(value);
	g_free(path);

	path = g_build_filename("/sys/class/block", parent, "queue",
		"rotational", NULL);
	value = read_sysfs_text(path);
	if (value)
		drive->solid_state = atoi(value) == 0;
	g_free(value);
	g_free(path);

	if (!drive->model || !*drive->model)
	{
		path = g_build_filename("/sys/class/block", parent, "device",
			"model", NULL);
		value = read_sysfs_text(path);
		if (value && *value)
		{
			g_free(drive->model);
			drive->model = value;
		}
		else
			g_free(value);
		g_free(path);
	}

	class_path = g_build_filename("/sys/class/block", parent, NULL);
	resolved = realpath(class_path, NULL);
	g_free(class_path);
	if (resolved)
	{
		if ((!drive->transport || !*drive->transport) &&
		    strstr(resolved, "/usb"))
		{
			g_free(drive->transport);
			drive->transport = g_strdup("usb");
			drive->removable = TRUE;
			drive->hardware_removable = TRUE;
		}
		else if ((!drive->transport || !*drive->transport) &&
		         (g_str_has_prefix(parent, "mmc") || strstr(resolved, "/mmc")))
		{
			g_free(drive->transport);
			drive->transport = g_strdup("mmc");
			drive->removable = TRUE;
		}
		else if ((!drive->transport || !*drive->transport) &&
		         g_str_has_prefix(parent, "nvme"))
		{
			g_free(drive->transport);
			drive->transport = g_strdup("nvme");
			drive->solid_state = TRUE;
		}
		free(resolved);
	}

	if (g_str_has_prefix(parent, "sr"))
		drive->optical = TRUE;
	g_free(parent);
}

static void drive_visibility_load(void)
{
	if (g_once_init_enter(&drive_visibility_once))
	{
		GKeyFile *key_file;
		GError *error = NULL;
		gboolean enabled = FALSE;

		key_file = rox_config_load(DRIVES_CONFIG);
		if (g_key_file_has_key(key_file, "Drives", "ShowSystemPartitions", NULL))
		{
			enabled = g_key_file_get_boolean(key_file, "Drives",
				"ShowSystemPartitions", &error);
			if (error)
			{
				g_warning("Unable to read ShowSystemPartitions: %s", error->message);
				g_clear_error(&error);
				enabled = FALSE;
			}
		}
		g_key_file_free(key_file);
		g_atomic_int_set(&show_system_partitions, enabled);
		g_once_init_leave(&drive_visibility_once, 1);
	}
}

static gboolean drive_visibility_save(gboolean enabled)
{
	GKeyFile *key_file = rox_config_load(DRIVES_CONFIG);
	GError *error = NULL;
	gboolean ok;

	g_key_file_set_boolean(key_file, "Drives", "ShowSystemPartitions", enabled);
	ok = rox_config_save(key_file, DRIVES_CONFIG, &error);
	if (!ok)
	{
		g_warning("Unable to save ShowSystemPartitions: %s",
			error ? error->message : "unknown error");
		g_clear_error(&error);
	}
	g_key_file_free(key_file);
	if (ok)
		g_atomic_int_set(&show_system_partitions, enabled);
	return ok;
}

static gboolean system_partition_text_match(const gchar *value)
{
	gchar *lower;
	gchar *compact;
	const gchar *read;
	gchar *write;
	gboolean result;

	if (!value || !*value)
		return FALSE;
	lower = g_utf8_strdown(value, -1);
	compact = g_malloc(strlen(lower) + 1);
	write = compact;
	for (read = lower; *read; read++)
		if (g_ascii_isalnum((guchar) *read))
			*write++ = *read;
	*write = '\0';

	result = strstr(lower, "/boot/efi") || !strcmp(lower, "/boot") ||
		strstr(lower, "efi system") ||
		!strcmp(compact, "efi") || !strcmp(compact, "esp") ||
		!strcmp(compact, "efisystempartition") ||
		!strcmp(compact, "boot") || !strcmp(compact, "bootpartition") ||
		!strcmp(compact, "biosboot");
	g_free(compact);
	g_free(lower);
	return result;
}

static gboolean technical_text_match(const gchar *value)
{
	gchar *lower;
	gchar *compact;
	const gchar *read;
	gchar *write;
	gboolean result;

	if (!value || !*value)
		return FALSE;

	lower = g_utf8_strdown(value, -1);
	compact = g_malloc(strlen(lower) + 1);
	write = compact;
	for (read = lower; *read; read++)
	{
		if (g_ascii_isalnum((guchar) *read))
			*write++ = *read;
	}
	*write = '\0';

	result = strstr(compact, "pupro") || strstr(compact, "puprw") ||
		!strcmp(compact, "pupa") || !strcmp(compact, "pupb") ||
		!strcmp(compact, "pupf") || !strcmp(compact, "pupk") ||
		!strcmp(compact, "pupz") || strstr(compact, "vtoyefi") ||
		!strcmp(compact, "swap");

	g_free(compact);
	g_free(lower);
	return result;
}

static gboolean partition_type_is_system(const gchar *parttype)
{
	return parttype &&
		(!g_ascii_strcasecmp(parttype, "c12a7328-f81f-11d2-ba4b-00a0c93ec93b") ||
		 !g_ascii_strcasecmp(parttype, "ef00") ||
		 !g_ascii_strcasecmp(parttype, "21686148-6449-6e6f-744e-656564454649") ||
		 !g_ascii_strcasecmp(parttype, "ef02"));
}

static gboolean drive_is_useful(const DriveInfo *drive, const gchar *type,
		const gchar *partlabel, const gchar *parttype)
{
	const gchar *base;
	gboolean useful_type;

	if (!drive || !drive->device || strncmp(drive->device, "/dev/", 5))
		return FALSE;

	base = strrchr(drive->device, '/');
	base = base ? base + 1 : drive->device;
	if (!g_ascii_strncasecmp(base, "loop", 4) ||
	    !g_ascii_strncasecmp(base, "zram", 4) ||
	    !g_ascii_strncasecmp(base, "ram", 3) ||
	    !g_ascii_strncasecmp(base, "dm-", 3))
		return FALSE;

	if (drive->mountpoint &&
	    (!strncmp(drive->mountpoint, "/initrd/pup_", 12) ||
	     !strncmp(drive->mountpoint, "/pup_", 5)))
		return FALSE;

	if (drive->fstype &&
	    (!g_ascii_strcasecmp(drive->fstype, "swap") ||
	     !g_ascii_strcasecmp(drive->fstype, "squashfs") ||
	     !g_ascii_strcasecmp(drive->fstype, "overlay") ||
	     !g_ascii_strcasecmp(drive->fstype, "aufs")))
		return FALSE;

	drive_visibility_load();
	if (!g_atomic_int_get(&show_system_partitions))
	{
		if (partition_type_is_system(parttype))
			return FALSE;
		if (system_partition_text_match(drive->label) ||
		    system_partition_text_match(drive->mountpoint) ||
		    system_partition_text_match(partlabel))
			return FALSE;
	}

	if (technical_text_match(drive->label) ||
	    technical_text_match(drive->mountpoint) ||
	    technical_text_match(partlabel))
		return FALSE;

	useful_type = type && (!strcmp(type, "part") || !strcmp(type, "crypt") ||
		!strcmp(type, "lvm") || !strcmp(type, "rom"));

	/* Modificado por josejp2424 (2026): usar exactamente la condición de
	 * EssoraWM. Una entrada debe ser una partición/volumen útil y, además,
	 * tener sistema de archivos, estar montada o ser removible. Esto evita
	 * mostrar discos físicos, particiones técnicas y entradas vacías de sysfs. */
	return drive->name && *drive->name &&
		(useful_type || (drive->fstype && *drive->fstype) ||
		 (drive->mountpoint && *drive->mountpoint)) &&
		((drive->fstype && *drive->fstype) ||
		 (drive->mountpoint && *drive->mountpoint) || drive->removable) &&
		!drive_is_hidden_by_essorawm(drive->name);
}

/* Agregado por josejp2424 (2026): evitar duplicados al combinar lsblk,
 * los iconos runtime de Puppy y /sys/class/block. */
static gboolean drive_array_has_device(GPtrArray *drives, const gchar *device)
{
	guint i;

	if (!drives || !device)
		return FALSE;
	for (i = 0; i < drives->len; i++)
	{
		DriveInfo *drive = g_ptr_array_index(drives, i);
		if (drive->device && !strcmp(drive->device, device))
			return TRUE;
	}
	return FALSE;
}

/* Agregado por josejp2424 (2026): respetar la misma lista de unidades
 * ocultas que EssoraWM. Cada línea contiene el nombre corto del dispositivo
 * (por ejemplo sda1 o nvme0n1p2). */
static gboolean drive_is_hidden_by_essorawm(const gchar *name)
{
	gchar *path;
	gchar *contents = NULL;
	gchar **lines;
	gboolean hidden = FALSE;
	gint i;

	if (!name || !*name)
		return FALSE;

	/* Modificado por josejp2424 (2026): guardar la lista de unidades ocultas
	 * junto con la configuración tradicional de ROX-Filer. */
	path = g_build_filename(rox_config_dir(), "hidden-drives", NULL);
	if (!g_file_get_contents(path, &contents, NULL, NULL))
	{
		g_free(path);
		return FALSE;
	}
	g_free(path);

	lines = g_strsplit(contents, "\n", -1);
	for (i = 0; lines[i]; i++)
	{
		gchar *line = g_strstrip(lines[i]);
		if (*line && !strcmp(line, name))
		{
			hidden = TRUE;
			break;
		}
	}
	g_strfreev(lines);
	g_free(contents);
	return hidden;
}

/* Execute a short-lived helper without allowing a broken block device to
 * pin the shared drive-monitor worker forever.  ROX has a legacy SIGCHLD
 * reaper, so keep the same child-reap mutex used by rox_spawn_sync() while
 * this child is alive. */
/* Tomar el mutex de reap sin bloquear para siempre.  rox_child_reap_lock()
 * no tiene techo y un mount lento lo sostiene mientras dure el comando. */
static gboolean reap_lock_wait(guint seconds)
{
	gint64 deadline = g_get_monotonic_time() + (gint64) seconds * G_USEC_PER_SEC;
	for (;;)
	{
		if (rox_child_reap_trylock())
			return TRUE;
		if (g_get_monotonic_time() >= deadline)
			return FALSE;
		g_usleep(20000);
	}
}

/* Fallos de infraestructura: el helper no llego a correr o no termino.  No
 * son "lsblk no esta instalado", asi que no deben activar el fallback a
 * sysfs/Puppy: hacerlo publicaria una lista vacia como si fuera cierta. */
static gboolean spawn_error_is_infrastructural(const GError *e)
{
	return e && (g_error_matches(e, G_IO_ERROR, G_IO_ERROR_TIMED_OUT) ||
	             g_error_matches(e, G_IO_ERROR, G_IO_ERROR_BUSY));
}

static void drain_child_pipe(gint fd, GString *buffer)
{
	char chunk[4096];
	ssize_t n;

	if (fd < 0 || !buffer)
		return;
	for (;;)
	{
		n = read(fd, chunk, sizeof(chunk));
		if (n > 0)
		{
			g_string_append_len(buffer, chunk, (gssize) n);
			continue;
		}
		if (n < 0 && errno == EINTR)
			continue;
		break;
	}
}

static gboolean spawn_capture_timeout(gchar **argv, guint timeout_seconds,
        gchar **stdout_text, gchar **stderr_text, gint *wait_status,
        GError **error)
{
	GPid pid = 0;
	gint out_fd = -1;
	gint err_fd = -1;
	gint status = 0;
	gboolean spawned;
	gboolean timed_out = FALSE;
	gboolean child_done = FALSE;
	GString *out = g_string_new(NULL);
	GString *err = g_string_new(NULL);
	gint64 deadline;

	if (stdout_text)
		*stdout_text = NULL;
	if (stderr_text)
		*stderr_text = NULL;
	if (wait_status)
		*wait_status = 0;

	if (!reap_lock_wait(DRIVE_REAP_LOCK_WAIT_SECONDS))
	{
		/* Otro helper (tipicamente un mount o umount lento) tiene el mutex.
		 * Abandonar en vez de dejar clavado un hilo del pool de GTask: el
		 * monitor conserva su ultimo snapshot y reintenta en el proximo poll. */
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_BUSY,
			"%s: child reaper busy", argv[0]);
		g_free(g_string_free(out, FALSE));
		g_free(g_string_free(err, FALSE));
		return FALSE;
	}
	spawned = g_spawn_async_with_pipes(NULL, argv, NULL,
		G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
		NULL, NULL, &pid, NULL, &out_fd, &err_fd, error);
	if (!spawned)
	{
		rox_child_reap_unlock();
		g_free(g_string_free(out, FALSE));
		g_free(g_string_free(err, FALSE));
		return FALSE;
	}

	(void) fcntl(out_fd, F_SETFL, fcntl(out_fd, F_GETFL, 0) | O_NONBLOCK);
	(void) fcntl(err_fd, F_SETFL, fcntl(err_fd, F_GETFL, 0) | O_NONBLOCK);
	deadline = g_get_monotonic_time() +
		(gint64) timeout_seconds * G_USEC_PER_SEC;

	while (!child_done)
	{
		pid_t waited;
		struct pollfd fds[2];
		gint timeout_ms;
		gint64 remaining;

		drain_child_pipe(out_fd, out);
		drain_child_pipe(err_fd, err);
		waited = waitpid((pid_t) pid, &status, WNOHANG);
		if (waited == (pid_t) pid)
		{
			child_done = TRUE;
			break;
		}
		if (waited < 0 && errno != EINTR)
		{
			g_set_error(error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
				"waitpid(%s): %s", argv[0], g_strerror(errno));
			break;
		}

		remaining = deadline - g_get_monotonic_time();
		if (remaining <= 0)
		{
			timed_out = TRUE;
			(void) kill((pid_t) pid, SIGTERM);
			g_usleep(100000);
			waited = waitpid((pid_t) pid, &status, WNOHANG);
			if (waited == 0)
			{
				/* SIGKILL cannot wake a task already stuck in kernel D-state.
				 * Never turn the timeout into another unbounded waitpid(). The
				 * normal ROX SIGCHLD reaper will collect it if/when it exits. */
				(void) kill((pid_t) pid, SIGKILL);
			}
			else if (waited == (pid_t) pid)
				child_done = TRUE;
			break;
		}

		fds[0].fd = out_fd;
		fds[0].events = POLLIN;
		fds[0].revents = 0;
		fds[1].fd = err_fd;
		fds[1].events = POLLIN;
		fds[1].revents = 0;
		timeout_ms = (gint) MIN((gint64) 100,
			(remaining + 999) / 1000);
		if (poll(fds, 2, timeout_ms) < 0 && errno != EINTR)
		{
			g_set_error(error, G_IO_ERROR, g_io_error_from_errno(errno),
				"poll(%s): %s", argv[0], g_strerror(errno));
			break;
		}
	}

	/* Drain anything written immediately before exit. */
	drain_child_pipe(out_fd, out);
	drain_child_pipe(err_fd, err);
	if (!child_done && !timed_out)
	{
		(void) kill((pid_t) pid, SIGKILL);
		/* Best effort only: never block the shared monitor on a kernel-stuck
		 * helper. The process-wide SIGCHLD reaper owns eventual cleanup. */
		(void) waitpid((pid_t) pid, &status, WNOHANG);
	}
	close(out_fd);
	close(err_fd);
	g_spawn_close_pid(pid);
	rox_child_reap_unlock();

	if (stdout_text)
		*stdout_text = g_string_free(out, FALSE);
	else
		g_free(g_string_free(out, FALSE));
	if (stderr_text)
		*stderr_text = g_string_free(err, FALSE);
	else
		g_free(g_string_free(err, FALSE));
	if (wait_status)
		*wait_status = status;

	if (timed_out)
	{
		g_set_error(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT,
			"%s timed out after %u seconds", argv[0], timeout_seconds);
		return FALSE;
	}
	return error == NULL || *error == NULL;
}

/* Agregado por josejp2424 (2026): ejecutar una consulta pequeña y devolver
 * solamente la primera línea sin espacios finales. */
static gchar *command_first_line(gchar **argv)
{
	gchar *output = NULL;
	gchar *error_output = NULL;
	gint status = 0;
	GError *error = NULL;
	gchar *line;

	if (!spawn_capture_timeout(argv, DRIVE_COMMAND_TIMEOUT_SECONDS,
		&output, &error_output, &status, &error))
	{
		g_clear_error(&error);
		g_free(output);
		g_free(error_output);
		return NULL;
	}
	g_free(error_output);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 || !output)
	{
		g_free(output);
		return NULL;
	}

	line = g_strstrip(output);
	if (!*line)
	{
		g_free(output);
		return NULL;
	}
	return output;
}

/* Agregado por josejp2424 (2026): construir metadatos para una partición
 * detectada fuera de lsblk. Se consultan blkid y lsblk de forma opcional. */
static DriveInfo *drive_info_from_device(const gchar *device)
{
	DriveInfo *drive;
	gchar *base;
	gchar *argv_type[] = {(gchar *) "blkid", (gchar *) "-o",
		(gchar *) "value", (gchar *) "-s", (gchar *) "TYPE",
		(gchar *) device, NULL};
	gchar *argv_label[] = {(gchar *) "blkid", (gchar *) "-o",
		(gchar *) "value", (gchar *) "-s", (gchar *) "LABEL",
		(gchar *) device, NULL};
	gchar *argv_parttype[] = {(gchar *) "blkid", (gchar *) "-o",
		(gchar *) "value", (gchar *) "-s", (gchar *) "PART_ENTRY_TYPE",
		(gchar *) device, NULL};
	gchar *argv_size[] = {(gchar *) "lsblk", (gchar *) "-dn",
		(gchar *) "-o", (gchar *) "SIZE", (gchar *) device, NULL};

	if (!device || strncmp(device, "/dev/", 5))
		return NULL;

	drive = g_new0(DriveInfo, 1);
	base = g_path_get_basename(device);
	drive->name = g_strdup(base);
	drive->device = g_strdup(device);
	drive->mountpoint = find_mountpoint(device);
	drive->fstype = command_first_line(argv_type);
	drive->label = command_first_line(argv_label);
	{
		gchar *parttype = command_first_line(argv_parttype);
		drive->system_partition = partition_type_is_system(parttype) ||
			system_partition_text_match(drive->label);
		g_free(parttype);
	}
	drive->size = command_first_line(argv_size);
	drive->type = g_str_has_prefix(base, "sr") ? g_strdup("rom") : g_strdup("part");
	drive->optical = g_str_has_prefix(base, "sr") ||
		(drive->fstype && (!g_ascii_strcasecmp(drive->fstype, "iso9660") ||
		 !g_ascii_strcasecmp(drive->fstype, "udf")));
	drive_enrich_from_sysfs(drive);

	{
		gchar *removable_path = g_build_filename("/sys/class/block", base,
			"removable", NULL);
		gchar *value = NULL;
		if (g_file_get_contents(removable_path, &value, NULL, NULL))
		{
			gboolean removable = atoi(value) != 0;
			drive->removable = drive->removable || removable;
			drive->hardware_removable = drive->hardware_removable || removable;
		}
		g_free(value);
		g_free(removable_path);
	}

	if (!drive->label || !*drive->label)
	{
		g_free(drive->label);
		if (drive->size && *drive->size)
		{
			drive->label = g_strdup_printf(_("Volume %s"), drive->size);
			drive->label_is_synthetic = TRUE;
		}
		else
			drive->label = g_strdup(base);
	}
	g_free(base);
	return drive;
}

/* Agregado por josejp2424 (2026): Puppy crea archivos drive_* para las
 * unidades que muestra en el escritorio. Usarlos como fuente adicional hace
 * que el botón de ROX vea exactamente las mismas particiones disponibles. */
static void append_puppy_runtime_drives(GPtrArray *drives)
{
	GDir *dir;
	const gchar *name;

	dir = g_dir_open("/tmp/pup_event_frontend", 0, NULL);
	if (!dir)
		return;

	while ((name = g_dir_read_name(dir)) != NULL)
	{
		gchar *device;
		DriveInfo *drive;

		if (!g_str_has_prefix(name, "drive_") || !name[6])
			continue;
		device = g_build_filename("/dev", name + 6, NULL);
		if (drive_array_has_device(drives, device))
		{
			g_free(device);
			continue;
		}
		drive = drive_info_from_device(device);
		if (drive && drive_is_useful(drive, "part", NULL, NULL))
			g_ptr_array_add(drives, drive);
		else
			rox_drive_info_free(drive);
		g_free(device);
	}
	g_dir_close(dir);
}

/* Agregado por josejp2424 (2026): último respaldo sin depender de columnas
 * particulares de lsblk. /sys/class/block/<nombre>/partition identifica
 * particiones reales en discos SATA, NVMe, MMC y USB. */
static void append_sysfs_partitions(GPtrArray *drives)
{
	GDir *dir;
	const gchar *name;

	dir = g_dir_open("/sys/class/block", 0, NULL);
	if (!dir)
		return;

	while ((name = g_dir_read_name(dir)) != NULL)
	{
		gchar *partition_flag = g_build_filename("/sys/class/block", name,
			"partition", NULL);
		gchar *device;
		DriveInfo *drive;

		if (!g_file_test(partition_flag, G_FILE_TEST_EXISTS))
		{
			g_free(partition_flag);
			continue;
		}
		g_free(partition_flag);

		device = g_build_filename("/dev", name, NULL);
		if (drive_array_has_device(drives, device))
		{
			g_free(device);
			continue;
		}
		drive = drive_info_from_device(device);
		if (drive && drive_is_useful(drive, "part", NULL, NULL))
			g_ptr_array_add(drives, drive);
		else
			rox_drive_info_free(drive);
		g_free(device);
	}
	g_dir_close(dir);
}

static void append_foreign_network_path(GPtrArray *drives,
		const gchar *path, const gchar *label, const gchar *fstype)
{
	DriveInfo *drive;

	if (!path || !*path || drive_array_has_mountpoint(drives, path))
		return;
	drive = g_new0(DriveInfo, 1);
	drive->name = g_strdup(label && *label ? label : path);
	drive->device = g_strdup(path);
	drive->label = g_strdup(label && *label ? label : path);
	drive->fstype = g_strdup(fstype ? fstype : "fuse");
	drive->mountpoint = g_strdup(path);
	drive->type = g_strdup("network");
	drive->transport = g_strdup("smb");
	drive->network = TRUE;
	drive->foreign = TRUE;
	/* Never probe a foreign FUSE path during the global drive scan.
	 * access/stat on a dead network mount can block in D-state indefinitely. */
	g_ptr_array_add(drives, drive);
}

static void append_network_mounts(GPtrArray *drives)
{
	FILE *mounts;
	struct mntent entry_buf;
	struct mntent *entry;
	char mntbuf[4096];

	mounts = setmntent("/proc/self/mounts", "r");
	if (!mounts)
		return;

	while ((entry = getmntent_r(mounts, &entry_buf, mntbuf, sizeof(mntbuf))) != NULL)
	{
		if (g_strcmp0(entry->mnt_type, "cifs") == 0 ||
		    g_strcmp0(entry->mnt_type, "smb3") == 0)
		{
			DriveInfo *drive;
			const gchar *remote = entry->mnt_fsname;
			gchar *label;

			if (!remote || !*remote || drive_array_has_device(drives, remote))
				continue;
			label = g_strdup(g_str_has_prefix(remote, "//") ? remote + 2 : remote);
			drive = g_new0(DriveInfo, 1);
			drive->name = g_strdup(label);
			drive->device = g_strdup(remote);
			drive->label = g_strdup(label);
			drive->fstype = g_strdup(entry->mnt_type);
			drive->mountpoint = g_strdup(entry->mnt_dir);
			drive->type = g_strdup("network");
			drive->transport = g_strdup("smb");
			drive->network = TRUE;
			drive->foreign = !rox_smb_mountpoint_is_managed(entry->mnt_dir);
			/* /proc/self/mounts already tells us the mount exists.  Do not touch
			 * the remote filesystem merely to decorate the device list. */
			g_ptr_array_add(drives, drive);
			g_free(label);
			continue;
		}

		/* Compatibility only: never enumerate children of a foreign FUSE
		 * network mount from the periodic global scan.  g_dir_open()/stat() on
		 * gvfs, kio-fuse or smbnetfs may block indefinitely when the server is
		 * gone.  Expose the already-mounted local bridge root as foreign; users
		 * can browse its children explicitly without risking the drive monitor. */
		if (g_strcmp0(entry->mnt_type, "fuse.gvfsd-fuse") == 0)
			append_foreign_network_path(drives, entry->mnt_dir,
				"GVfs network", entry->mnt_type);
		else if (g_strcmp0(entry->mnt_type, "fuse.kio-fuse") == 0)
			append_foreign_network_path(drives, entry->mnt_dir,
				"KIO network", entry->mnt_type);
		else if (g_strcmp0(entry->mnt_type, "fuse.smbnetfs") == 0)
			append_foreign_network_path(drives, entry->mnt_dir,
				"SMBNetFS network", entry->mnt_type);
	}
	endmntent(mounts);
}


/* 2.12.2-95: portable devices are intentionally runtime-optional.  The drive
 * model sees both already-mounted non-block filesystems under /media and, when
 * helper programs are available, unmounted MTP/PTP/iOS devices.  No GVfs is
 * required and none of these helpers is needed for Rox-Filer2 to start. */
static gboolean portable_mountpoint_present(const gchar *mountpoint)
{
	FILE *mounts;
	struct mntent entry_buf;
	struct mntent *entry;
	char mntbuf[4096];
	gboolean present = FALSE;

	if (!mountpoint || !*mountpoint)
		return FALSE;
	mounts = setmntent("/proc/self/mounts", "r");
	if (!mounts)
		return FALSE;
	while ((entry = getmntent_r(mounts, &entry_buf, mntbuf, sizeof(mntbuf))) != NULL)
	{
		if (g_strcmp0(entry->mnt_dir, mountpoint) == 0)
		{
			present = TRUE;
			break;
		}
	}
	endmntent(mounts);
	return present;
}

static gboolean portable_transport_present(GPtrArray *drives, const gchar *transport)
{
	guint i;
	if (!drives || !transport)
		return FALSE;
	for (i = 0; i < drives->len; i++)
	{
		DriveInfo *drive = g_ptr_array_index(drives, i);
		if (drive->portable && g_strcmp0(drive->transport, transport) == 0)
			return TRUE;
	}
	return FALSE;
}

static void portable_add(GPtrArray *drives, const gchar *device,
		const gchar *label, const gchar *fstype, const gchar *transport,
		const gchar *mountpoint)
{
	DriveInfo *drive;

	if (!drives || !device || !*device)
		return;
	if (mountpoint && drive_array_has_mountpoint(drives, mountpoint))
		return;
	if (!mountpoint && drive_array_has_device(drives, device))
		return;

	drive = g_new0(DriveInfo, 1);
	drive->name = g_strdup(label && *label ? label : device);
	drive->device = g_strdup(device);
	drive->label = g_strdup(label && *label ? label : device);
	drive->fstype = g_strdup(fstype && *fstype ? fstype : "fuse");
	drive->mountpoint = mountpoint ? g_strdup(mountpoint) : NULL;
	drive->type = g_strdup("portable");
	drive->transport = g_strdup(transport && *transport ? transport : "media");
	drive->portable = TRUE;
	drive->removable = TRUE;
	g_ptr_array_add(drives, drive);
}

static const gchar *portable_transport_for_mount(const gchar *fstype,
		const gchar *fsname)
{
	if ((fstype && (strstr(fstype, "mtp") || strstr(fstype, "simple-mtpfs") ||
	               strstr(fstype, "jmtpfs"))) ||
	    (fsname && (strstr(fsname, "mtp") || strstr(fsname, "jmtpfs"))))
		return "mtp";
	if ((fstype && strstr(fstype, "ifuse")) || (fsname && strstr(fsname, "ifuse")))
		return "ios";
	if ((fstype && strstr(fstype, "gphotofs")) || (fsname && strstr(fsname, "gphotofs")))
		return "ptp";
	return "media";
}

static gboolean portable_path_under_media(const gchar *path)
{
	return path && g_str_has_prefix(path, "/media/") && path[7] != '\0';
}

static gboolean portable_known_mount_type(const gchar *fstype, const gchar *fsname)
{
	return (fstype && (strstr(fstype, "mtp") || strstr(fstype, "jmtpfs") ||
		strstr(fstype, "simple-mtpfs") || strstr(fstype, "ifuse") ||
		strstr(fstype, "gphotofs"))) ||
		(fsname && (strstr(fsname, "mtp") || strstr(fsname, "jmtpfs") ||
		strstr(fsname, "simple-mtpfs") || strstr(fsname, "ifuse") ||
		strstr(fsname, "gphotofs")));
}

static void append_portable_mounted_paths(GPtrArray *drives)
{
	FILE *mounts;
	struct mntent entry_buf;
	struct mntent *entry;
	char mntbuf[4096];

	mounts = setmntent("/proc/self/mounts", "r");
	if (!mounts)
		return;
	while ((entry = getmntent_r(mounts, &entry_buf, mntbuf, sizeof(mntbuf))) != NULL)
	{
		gchar *base;
		const gchar *transport;

		/* Block devices are already represented by lsblk/sysfs.  Network and
		 * managed image mounts have their own richer model.  Everything else
		 * mounted below /media is still useful and must be easy to unmount. */
		if ((!portable_path_under_media(entry->mnt_dir) &&
		     !portable_known_mount_type(entry->mnt_type, entry->mnt_fsname)) ||
		    g_str_has_prefix(entry->mnt_fsname, "/dev/") ||
		    g_strcmp0(entry->mnt_type, "cifs") == 0 ||
		    g_strcmp0(entry->mnt_type, "smb3") == 0 ||
		    drive_array_has_mountpoint(drives, entry->mnt_dir))
			continue;

		base = g_path_get_basename(entry->mnt_dir);
		transport = portable_transport_for_mount(entry->mnt_type, entry->mnt_fsname);
		portable_add(drives,
			entry->mnt_fsname && *entry->mnt_fsname ? entry->mnt_fsname : entry->mnt_dir,
			base, entry->mnt_type, transport, entry->mnt_dir);
		g_free(base);
	}
	endmntent(mounts);
}


static gboolean usb_sysfs_has_value(const gchar *filename, const gchar *wanted)
{
	GDir *dir;
	const gchar *name;
	gboolean found = FALSE;

	dir = g_dir_open("/sys/bus/usb/devices", 0, NULL);
	if (!dir)
		return FALSE;
	while ((name = g_dir_read_name(dir)) != NULL)
	{
		gchar *path = g_build_filename("/sys/bus/usb/devices", name, filename, NULL);
		gchar *text = NULL;
		if (g_file_get_contents(path, &text, NULL, NULL))
		{
			g_strstrip(text);
			if (g_ascii_strcasecmp(text, wanted) == 0)
				found = TRUE;
		}
		g_free(text);
		g_free(path);
		if (found)
			break;
	}
	g_dir_close(dir);
	return found;
}

static gboolean usb_has_mtp_or_ptp_candidate(void)
{
	/* USB Still Image class 06 is used by PTP and by the common MTP USB
	 * transport.  Avoid waking every helper during the 45-second safety poll
	 * when no such device is connected. */
	return usb_sysfs_has_value("bInterfaceClass", "06");
}

static gboolean usb_has_apple_candidate(void)
{
	return usb_sysfs_has_value("idVendor", "05ac");
}

static void append_simple_mtp_devices(GPtrArray *drives)
{
	gchar *program = g_find_program_in_path("simple-mtpfs");
	gchar *out = NULL;
	gchar *err = NULL;
	gint status = 0;
	gchar *argv[3];
	gchar **lines;
	gint i;

	if (!usb_has_mtp_or_ptp_candidate() || !program ||
	    portable_transport_present(drives, "mtp"))
	{
		g_free(program);
		return;
	}
	argv[0] = program;
	argv[1] = (gchar *) "--list-devices";
	argv[2] = NULL;
	if (!spawn_capture_timeout(argv, PORTABLE_DISCOVERY_TIMEOUT_SECONDS,
		&out, &err, &status, NULL) || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		g_free(program); g_free(out); g_free(err);
		return;
	}
	lines = g_strsplit(out ? out : "", "\n", -1);
	for (i = 0; lines[i]; i++)
	{
		gchar *colon = strchr(lines[i], ':');
		gchar *end = NULL;
		long index;
		gchar *device;
		gchar *label;
		if (!colon)
			continue;
		*colon = '\0';
		index = strtol(g_strstrip(lines[i]), &end, 10);
		if (!end || *end != '\0' || index < 0)
			continue;
		label = g_strstrip(colon + 1);
		if (!*label)
			label = (gchar *) _("MTP device");
		device = g_strdup_printf("mtp:simple:%ld", index);
		portable_add(drives, device, label, "mtp", "mtp", NULL);
		g_free(device);
	}
	g_strfreev(lines);
	g_free(program); g_free(out); g_free(err);
}

static void append_jmtpfs_device(GPtrArray *drives)
{
	gchar *program;
	gchar *out = NULL;
	gchar *err = NULL;
	gint status = 0;
	gchar *argv[3];
	gchar **lines;
	gint i;

	if (!usb_has_mtp_or_ptp_candidate() || portable_transport_present(drives, "mtp"))
		return;
	program = g_find_program_in_path("jmtpfs");
	if (!program)
		return;
	argv[0] = program;
	argv[1] = (gchar *) "-l";
	argv[2] = NULL;
	if (!spawn_capture_timeout(argv, PORTABLE_DISCOVERY_TIMEOUT_SECONDS,
		&out, &err, &status, NULL) || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		g_free(program); g_free(out); g_free(err);
		return;
	}
	lines = g_strsplit(out ? out : "", "\n", -1);
	for (i = 0; lines[i]; i++)
	{
		gchar *line = g_strstrip(lines[i]);
		gchar *is_a;
		gchar *label;
		if (!g_str_has_prefix(line, "Device "))
			continue;
		is_a = strstr(line, " is a ");
		label = is_a ? g_strstrip(is_a + 6) : line;
		if (*label && label[strlen(label) - 1] == '.')
			label[strlen(label) - 1] = '\0';
		portable_add(drives, "mtp:jmtpfs:first",
			*label ? label : _("MTP device"), "mtp", "mtp", NULL);
		/* jmtpfs can select by USB bus/device rather than list index.  Until
		 * Rox has that mapping, expose the first device exactly as jmtpfs does. */
		break;
	}
	g_strfreev(lines);
	g_free(program); g_free(out); g_free(err);
}

static void append_ios_devices(GPtrArray *drives)
{
	gchar *id_program;
	gchar *ifuse_program;
	gchar *out = NULL;
	gchar *err = NULL;
	gint status = 0;
	gchar *argv[3];
	gchar **lines;
	gint i;

	if (!usb_has_apple_candidate() || portable_transport_present(drives, "ios"))
		return;
	id_program = g_find_program_in_path("idevice_id");
	ifuse_program = g_find_program_in_path("ifuse");
	if (!id_program || !ifuse_program)
	{
		g_free(id_program); g_free(ifuse_program);
		return;
	}
	argv[0] = id_program;
	argv[1] = (gchar *) "-l";
	argv[2] = NULL;
	if (!spawn_capture_timeout(argv, PORTABLE_DISCOVERY_TIMEOUT_SECONDS,
		&out, &err, &status, NULL) || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		g_free(id_program); g_free(ifuse_program); g_free(out); g_free(err);
		return;
	}
	lines = g_strsplit(out ? out : "", "\n", -1);
	for (i = 0; lines[i]; i++)
	{
		gchar *udid = g_strstrip(lines[i]);
		gchar *device;
		gchar *label = NULL;
		gchar *info_program;
		if (!*udid)
			continue;
		info_program = g_find_program_in_path("ideviceinfo");
		if (info_program)
		{
			gchar *name_out = NULL;
			gchar *name_err = NULL;
			gint name_status = 0;
			gchar *name_argv[] = {info_program, (gchar *) "-u", udid,
				(gchar *) "-k", (gchar *) "DeviceName", NULL};
			if (spawn_capture_timeout(name_argv, 2, &name_out, &name_err,
				&name_status, NULL) && WIFEXITED(name_status) && WEXITSTATUS(name_status) == 0)
			{
				g_strstrip(name_out);
				if (*name_out)
					label = g_strdup(name_out);
			}
			g_free(name_out); g_free(name_err); g_free(info_program);
		}
		if (!label)
			label = g_strdup(_("iPhone / iPad"));
		device = g_strdup_printf("ios:%s", udid);
		portable_add(drives, device, label, "ifuse", "ios", NULL);
		g_free(device); g_free(label);
	}
	g_strfreev(lines);
	g_free(id_program); g_free(ifuse_program); g_free(out); g_free(err);
}

static void append_ptp_device(GPtrArray *drives)
{
	gchar *gphoto;
	gchar *gphotofs;
	gchar *out = NULL;
	gchar *err = NULL;
	gint status = 0;
	gchar *argv[3];
	gchar **lines;
	gint i;

	if (!usb_has_mtp_or_ptp_candidate() || portable_transport_present(drives, "ptp"))
		return;
	gphoto = g_find_program_in_path("gphoto2");
	gphotofs = g_find_program_in_path("gphotofs");
	if (!gphoto || !gphotofs)
	{
		g_free(gphoto); g_free(gphotofs);
		return;
	}
	argv[0] = gphoto;
	argv[1] = (gchar *) "--auto-detect";
	argv[2] = NULL;
	if (!spawn_capture_timeout(argv, PORTABLE_DISCOVERY_TIMEOUT_SECONDS,
		&out, &err, &status, NULL) || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		g_free(gphoto); g_free(gphotofs); g_free(out); g_free(err);
		return;
	}
	lines = g_strsplit(out ? out : "", "\n", -1);
	for (i = 0; lines[i]; i++)
	{
		gchar *line = g_strstrip(lines[i]);
		gchar *usb = strstr(line, "usb:");
		gchar *label;
		if (!usb || line == usb)
			continue;
		label = g_strndup(line, (gsize) (usb - line));
		g_strstrip(label);
		if (*label)
		{
			portable_add(drives, "ptp:gphoto:first", label,
				"gphotofs", "ptp", NULL);
			g_free(label);
			break;
		}
		g_free(label);
	}
	g_strfreev(lines);
	g_free(gphoto); g_free(gphotofs); g_free(out); g_free(err);
}

static void append_portable_devices(GPtrArray *drives)
{
#ifdef ROX_PORTABLE_DEVICES
	if (!drives)
		return;
	append_portable_mounted_paths(drives);
	append_simple_mtp_devices(drives);
	append_jmtpfs_device(drives);
	append_ios_devices(drives);
	append_ptp_device(drives);
#else
	(void) drives;
#endif
}


static void append_managed_image_mounts(GPtrArray *drives)
{
	GPtrArray *mounts;
	guint i;

	if (!drives)
		return;

	mounts = image_mounter_list_managed_mounts();
	if (!mounts)
		return;

	for (i = 0; i < mounts->len; i++)
	{
		ImageMounterManagedMount *managed = g_ptr_array_index(mounts, i);
		DriveInfo *drive;
		gchar *base;
		GStatBuf st;

		if (!managed || !managed->image || !managed->blockdev ||
		    !managed->mountpoint || drive_array_has_device(drives, managed->blockdev))
			continue;

		drive = g_new0(DriveInfo, 1);
		base = g_path_get_basename(managed->image);
		drive->name = g_strdup(base);
		drive->label = g_strdup(base);
		drive->device = g_strdup(managed->blockdev);
		drive->mountpoint = g_strdup(managed->mountpoint);
		drive->type = g_strdup("image");
		drive->transport = g_strdup("loop");
		drive->parent_device = g_strdup(managed->loopdev);
		drive->backing_file = g_strdup(managed->image);
		drive->managed_image = TRUE;
		drive->label_is_synthetic = FALSE;

		if (g_stat(managed->image, &st) == 0 && S_ISREG(st.st_mode))
			drive->size = g_format_size((guint64) st.st_size);

		/* The entry exists only because Image Mounter owns a live state file.
		 * Never scan /dev/loop* globally: Puppy uses loop devices internally
		 * for SFS files and layered filesystems. */
		g_ptr_array_add(drives, drive);
		g_free(base);
	}

	g_ptr_array_unref(mounts);
}

void rox_drive_info_free(gpointer data)
{
	DriveInfo *drive = data;
	if (!drive)
		return;
	g_free(drive->name);
	g_free(drive->device);
	g_free(drive->label);
	g_free(drive->fstype);
	g_free(drive->mountpoint);
	g_free(drive->size);
	g_free(drive->type);
	g_free(drive->transport);
	g_free(drive->model);
	g_free(drive->parent_device);
	g_free(drive->backing_file);
	g_free(drive);
}

/* Agregado por josejp2424: usar la misma idea de EssoraWM para mostrar sólo
 * particiones reales y evitar capas técnicas de Puppy, swap, loop y EFI. */
static GPtrArray *read_drive_list(GError **error)
{
	gchar *stdout_text = NULL;
	gchar *stderr_text = NULL;
	gint status = 0;
	gchar *argv_full[] = {
		(gchar *) "lsblk", (gchar *) "-P", (gchar *) "-p",
		(gchar *) "-o",
		(gchar *) "NAME,PATH,PKNAME,LABEL,FSTYPE,MOUNTPOINT,RM,TYPE,HOTPLUG,TRAN,MODEL,ROTA,PARTLABEL,PARTTYPE,SIZE",
		NULL
	};
	gchar *argv_compat[] = {
		(gchar *) "lsblk", (gchar *) "-P", (gchar *) "-p",
		(gchar *) "-o",
		(gchar *) "NAME,LABEL,FSTYPE,MOUNTPOINT,RM,TYPE,TRAN,MODEL,SIZE",
		NULL
	};
	gchar **lines;
	gint i;
	GPtrArray *drives;
	GHashTable *parents;
	GError *spawn_error = NULL;

	drives = g_ptr_array_new_with_free_func(rox_drive_info_free);

	/* Modificado por josejp2424: algunos Puppy incluyen una versión antigua
	 * de lsblk sin PATH, HOTPLUG, PARTLABEL o PARTTYPE. Intentar primero la
	 * consulta completa de EssoraWM y repetir con columnas compatibles. */
	if (!spawn_capture_timeout(argv_full, DRIVE_COMMAND_TIMEOUT_SECONDS,
		&stdout_text, &stderr_text, &status, &spawn_error) ||
	    !WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		/* A timeout means the block stack itself is unhealthy. Do not launch a
		 * second lsblk (or the per-device fallbacks), because that would only
		 * extend the stall. The monitor keeps its last known-good snapshot. */
		if (spawn_error_is_infrastructural(spawn_error))
		{
			g_clear_pointer(&stdout_text, g_free);
			g_clear_pointer(&stderr_text, g_free);
			g_propagate_error(error, spawn_error);
			g_ptr_array_unref(drives);
			return NULL;
		}
		g_clear_error(&spawn_error);
		g_clear_pointer(&stdout_text, g_free);
		g_clear_pointer(&stderr_text, g_free);
		status = 0;
		if (!spawn_capture_timeout(argv_compat, DRIVE_COMMAND_TIMEOUT_SECONDS,
			&stdout_text, &stderr_text, &status, &spawn_error))
		{
			if (spawn_error_is_infrastructural(spawn_error))
			{
				g_clear_pointer(&stdout_text, g_free);
				g_clear_pointer(&stderr_text, g_free);
				g_propagate_error(error, spawn_error);
				g_ptr_array_unref(drives);
				return NULL;
			}
			/* Modificado por josejp2424 (2026): no abandonar la detección
			 * cuando lsblk no está disponible. Puppy y sysfs siguen siendo
			 * fuentes válidas para listar las unidades del escritorio. */
			g_clear_error(&spawn_error);
			g_free(stderr_text);
			append_puppy_runtime_drives(drives);
			append_sysfs_partitions(drives);
			append_managed_image_mounts(drives);
			append_network_mounts(drives);
			append_portable_devices(drives);
			return drives;
		}
	}

	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		/* Modificado por josejp2424 (2026): usar los respaldos locales
		 * antes de informar un error. */
		g_free(stdout_text);
		g_free(stderr_text);
		append_puppy_runtime_drives(drives);
		append_sysfs_partitions(drives);
		append_managed_image_mounts(drives);
		append_network_mounts(drives);
		append_portable_devices(drives);
		if (drives->len == 0)
			g_set_error(error, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED,
				"%s", _("No usable partitions found"));
		return drives;
	}
	g_free(stderr_text);

	lines = g_strsplit(stdout_text ? stdout_text : "", "\n", -1);
	parents = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
		drive_parent_info_free);

	/* Agregado por josejp2424 (2026): primera pasada para recordar el bus y
	 * las propiedades del disco físico. Después cada partición hereda esos
	 * datos por PKNAME, evitando que un USB termine con icono de disco SATA. */
	for (i = 0; lines[i]; i++)
	{
		gchar *type;
		gchar *name;
		gchar *base;
		gchar *rm;
		gchar *hotplug;
		gchar *rota;
		DriveParentInfo *parent;

		if (!*lines[i])
			continue;
		type = parse_lsblk_value(lines[i], "TYPE");
		if (!type || (g_ascii_strcasecmp(type, "disk") &&
		              g_ascii_strcasecmp(type, "rom")))
		{
			g_free(type);
			continue;
		}
		name = parse_lsblk_value(lines[i], "NAME");
		if (!name || !*name)
		{
			g_free(type);
			g_free(name);
			continue;
		}
		base = g_path_get_basename(name);
		parent = g_new0(DriveParentInfo, 1);
		parent->transport = parse_lsblk_value(lines[i], "TRAN");
		parent->model = parse_lsblk_value(lines[i], "MODEL");
		parent->device = name[0] == '/' ? g_strdup(name) :
			g_build_filename("/dev", base, NULL);
		rm = parse_lsblk_value(lines[i], "RM");
		hotplug = parse_lsblk_value(lines[i], "HOTPLUG");
		rota = parse_lsblk_value(lines[i], "ROTA");
		parent->hardware_removable = (rm && atoi(rm) != 0) ||
			(parent->transport && !g_ascii_strcasecmp(parent->transport, "usb"));
		parent->removable = parent->hardware_removable ||
			(hotplug && atoi(hotplug) != 0) ||
			(parent->transport &&
			 (!g_ascii_strcasecmp(parent->transport, "mmc") ||
			  !g_ascii_strcasecmp(parent->transport, "sd")));
		parent->solid_state = rota && atoi(rota) == 0;
		parent->optical = !g_ascii_strcasecmp(type, "rom") ||
			g_str_has_prefix(base, "sr");
		g_hash_table_replace(parents, base, parent);
		g_free(rm);
		g_free(hotplug);
		g_free(rota);
		g_free(name);
		g_free(type);
	}

	for (i = 0; lines[i]; i++)
	{
		DriveInfo *drive;
		gchar *rm;
		gchar *hotplug;
		gchar *pkname;
		gchar *rota;
		gchar *partlabel;
		gchar *parttype;
		DriveParentInfo *parent = NULL;

		if (!*lines[i])
			continue;

		drive = g_new0(DriveInfo, 1);
		drive->name = parse_lsblk_value(lines[i], "NAME");
		drive->device = parse_lsblk_value(lines[i], "PATH");
		if (!drive->device && drive->name && drive->name[0] == '/')
			drive->device = g_strdup(drive->name);
		else if (!drive->device && drive->name && *drive->name)
			drive->device = g_build_filename("/dev", drive->name, NULL);
		if (drive->name)
		{
			gchar *base = g_path_get_basename(drive->name);
			g_free(drive->name);
			drive->name = base;
		}
		drive->label = parse_lsblk_value(lines[i], "LABEL");
		drive->fstype = parse_lsblk_value(lines[i], "FSTYPE");
		drive->mountpoint = parse_lsblk_value(lines[i], "MOUNTPOINT");
		drive->size = parse_lsblk_value(lines[i], "SIZE");
		rm = parse_lsblk_value(lines[i], "RM");
		hotplug = parse_lsblk_value(lines[i], "HOTPLUG");
		drive->type = parse_lsblk_value(lines[i], "TYPE");
		drive->transport = parse_lsblk_value(lines[i], "TRAN");
		drive->model = parse_lsblk_value(lines[i], "MODEL");
		pkname = parse_lsblk_value(lines[i], "PKNAME");
		rota = parse_lsblk_value(lines[i], "ROTA");
		partlabel = parse_lsblk_value(lines[i], "PARTLABEL");
		parttype = parse_lsblk_value(lines[i], "PARTTYPE");
		drive->system_partition = partition_type_is_system(parttype) ||
			system_partition_text_match(drive->label) ||
			system_partition_text_match(partlabel);

		if (pkname && *pkname)
		{
			gchar *parent_name = g_path_get_basename(pkname);
			parent = g_hash_table_lookup(parents, parent_name);
			g_free(parent_name);
		}
		if (parent)
		{
			if ((!drive->transport || !*drive->transport) &&
			    parent->transport && *parent->transport)
			{
				g_free(drive->transport);
				drive->transport = g_strdup(parent->transport);
			}
			if ((!drive->model || !*drive->model) &&
			    parent->model && *parent->model)
			{
				g_free(drive->model);
				drive->model = g_strdup(parent->model);
			}
			drive->removable = parent->removable;
			drive->hardware_removable = parent->hardware_removable;
			drive->parent_device = g_strdup(parent->device);
			drive->solid_state = parent->solid_state;
			drive->optical = parent->optical;
		}
		if (rota && *rota)
			drive->solid_state = atoi(rota) == 0;

		/* Eject/power-off must use hardware signals only.  Keep the broader
		 * removable heuristic below for visibility, but never for safety. */
		drive->hardware_removable = drive->hardware_removable ||
			(rm && atoi(rm) != 0) ||
			(drive->transport && !g_ascii_strcasecmp(drive->transport, "usb"));

		drive->removable = drive->removable ||
			(rm && atoi(rm) != 0) ||
			(hotplug && atoi(hotplug) != 0) ||
			(drive->transport && (!g_ascii_strcasecmp(drive->transport, "usb") ||
			          !g_ascii_strcasecmp(drive->transport, "mmc") ||
			          !g_ascii_strcasecmp(drive->transport, "sd"))) ||
			name_looks_removable(drive->label) ||
			name_looks_removable(partlabel) ||
			name_looks_removable(drive->model);

		drive->optical = drive->optical ||
			(drive->type && !g_ascii_strcasecmp(drive->type, "rom")) ||
			(drive->name && g_str_has_prefix(drive->name, "sr")) ||
			(drive->fstype && (!g_ascii_strcasecmp(drive->fstype, "iso9660") ||
			 !g_ascii_strcasecmp(drive->fstype, "udf")));
		drive_enrich_from_sysfs(drive);
		if (!drive->parent_device && drive->device && drive->type &&
		    (!g_ascii_strcasecmp(drive->type, "disk") ||
		     !g_ascii_strcasecmp(drive->type, "rom")))
			drive->parent_device = g_strdup(drive->device);

		if (drive_is_useful(drive, drive->type, partlabel, parttype))
		{
			if ((!drive->label || !*drive->label) && partlabel && *partlabel)
			{
				g_free(drive->label);
				drive->label = g_strdup(partlabel);
				drive->label_is_synthetic = FALSE;
			}
			else if ((!drive->label || !*drive->label) &&
			         drive->size && *drive->size)
			{
				g_free(drive->label);
				drive->label = g_strdup_printf(_("Volume %s"), drive->size);
				drive->label_is_synthetic = TRUE;
			}
			g_ptr_array_add(drives, drive);
		}
		else
			rox_drive_info_free(drive);

		g_free(rm);
		g_free(hotplug);
		g_free(pkname);
		g_free(rota);
		g_free(partlabel);
		g_free(parttype);
	}

	g_hash_table_destroy(parents);
	g_strfreev(lines);
	g_free(stdout_text);

	/* Los montajes CIFS ya existentes sí se agregan: son recursos de red
	 * explícitamente conectados por el usuario y deben poder abrirse y
	 * desmontarse desde la misma barra de unidades. */
	append_managed_image_mounts(drives);
	append_network_mounts(drives);
	append_portable_devices(drives);
	return drives;
}

static gchar *find_mountpoint(const gchar *device)
{
	FILE *mounts;
	struct mntent entry_buf;
	struct mntent *entry;
	char mntbuf[4096];
	gchar *device_real;
	gchar *result = NULL;

	if (!device || !*device)
		return NULL;

	device_real = g_str_has_prefix(device, "/dev/") ? realpath(device, NULL) : NULL;
	mounts = setmntent("/proc/self/mounts", "r");
	if (!mounts)
	{
		free(device_real);
		return NULL;
	}

	while ((entry = getmntent_r(mounts, &entry_buf, mntbuf, sizeof(mntbuf))) != NULL)
	{
		gchar *entry_real = NULL;
		gboolean same = !strcmp(entry->mnt_fsname, device);

		/* Only resolve local device nodes. Network/FUSE paths are handled by
		 * drive_current_mountpoint() and must never be realpath()'d here. */
		if (!same && device_real && g_str_has_prefix(entry->mnt_fsname, "/dev/"))
			entry_real = realpath(entry->mnt_fsname, NULL);
		if (!same && device_real && entry_real)
			same = !strcmp(device_real, entry_real);
		free(entry_real);
		if (same)
		{
			result = g_strdup(entry->mnt_dir);
			break;
		}
	}

	endmntent(mounts);
	free(device_real);
	return result;
}

static gboolean network_mountpoint_present(const DriveInfo *drive)
{
	FILE *mounts;
	struct mntent entry_buf;
	struct mntent *entry;
	char mntbuf[4096];
	gboolean present = FALSE;

	if (!drive || !drive->mountpoint || !*drive->mountpoint)
		return FALSE;

	/* CIFS/SMB3 has one real mount-table entry per share, so refresh its
	 * state without stat()/realpath() on the remote filesystem.  FUSE bridges
	 * expose shares as subdirectories of one parent mount; for those, the
	 * last asynchronous scan remains the authoritative snapshot. */
	if (g_strcmp0(drive->fstype, "cifs") != 0 &&
	    g_strcmp0(drive->fstype, "smb3") != 0)
		return TRUE;

	mounts = setmntent("/proc/self/mounts", "r");
	if (!mounts)
		return TRUE;
	while ((entry = getmntent_r(mounts, &entry_buf, mntbuf, sizeof(mntbuf))) != NULL)
	{
		if ((!strcmp(entry->mnt_type, "cifs") || !strcmp(entry->mnt_type, "smb3")) &&
		    !strcmp(entry->mnt_dir, drive->mountpoint))
		{
			present = TRUE;
			break;
		}
	}
	endmntent(mounts);
	return present;
}

static gchar *drive_current_mountpoint(const DriveInfo *drive)
{
	if (!drive)
		return NULL;
	if (drive->network)
		return network_mountpoint_present(drive) ? g_strdup(drive->mountpoint) : NULL;
	if (drive->portable)
		return drive->mountpoint && portable_mountpoint_present(drive->mountpoint) ?
			g_strdup(drive->mountpoint) : NULL;
	return find_mountpoint(drive->device);
}

static gboolean spawn_wait_timeout(gchar **argv, guint timeout_seconds,
		gchar **error_text)
{
	gchar *stderr_text = NULL;
	gint status = 0;
	GError *error = NULL;
	gboolean ok;

	if (error_text)
		*error_text = NULL;

	ok = spawn_capture_timeout(argv, timeout_seconds,
		NULL, &stderr_text, &status, &error);
	if (!ok)
	{
		if (error_text)
			*error_text = g_strdup(error ? error->message :
				_("The command failed."));
		g_clear_error(&error);
		g_free(stderr_text);
		return FALSE;
	}

	ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
	if (!ok && error_text)
		*error_text = g_strdup(stderr_text && *stderr_text ? stderr_text :
			_("The command failed."));
	g_free(stderr_text);
	return ok;
}

static gboolean spawn_wait(gchar **argv, gchar **error_text)
{
	/* Con techo: el mutex de reap se sostiene mientras el hijo vive, asi que un
	 * mount colgado no puede congelar el monitor de unidades para siempre. */
	return spawn_wait_timeout(argv, DRIVE_ACTION_TIMEOUT_SECONDS, error_text);
}

static const gchar *const drive_system_dirs[] = {
	"/usr/bin", "/bin", "/usr/sbin", "/sbin", "/usr/local/bin",
	"/usr/local/sbin", NULL
};

static gchar *drive_system_program(const gchar *name)
{
	guint i;

	if (!name || !*name || strchr(name, '/'))
		return NULL;
	for (i = 0; drive_system_dirs[i]; i++)
	{
		gchar *candidate = g_build_filename(drive_system_dirs[i], name, NULL);
		if (g_file_test(candidate, G_FILE_TEST_IS_EXECUTABLE))
			return candidate;
		g_free(candidate);
	}
	return NULL;
}


/* 2.12.2-95: create FUSE mountpoints without assuming Puppy/root.  Normal
 * users prefer /media/$USER when it is writable; otherwise use XDG_RUNTIME_DIR
 * so the portable-device feature remains usable on distributions that reserve
 * /media for privileged automounters. */
static gchar *portable_mount_root(void)
{
	gchar *root;

	if (geteuid() == 0)
	{
		(void) g_mkdir_with_parents("/media", 0755);
		return g_strdup("/media");
	}

	root = g_build_filename("/media", g_get_user_name(), NULL);
	if ((g_file_test(root, G_FILE_TEST_IS_DIR) ||
	     g_mkdir_with_parents(root, 0755) == 0) && access(root, W_OK | X_OK) == 0)
		return root;
	g_free(root);

	if (g_get_user_runtime_dir() && *g_get_user_runtime_dir())
		root = g_build_filename(g_get_user_runtime_dir(), "rox-filer2-media", NULL);
	else
		root = g_strdup_printf("/tmp/rox-filer2-%u-media", (guint) geteuid());
	if (g_mkdir_with_parents(root, 0700) != 0)
	{
		g_free(root);
		return NULL;
	}
	return root;
}

static gchar *portable_safe_component(const gchar *text)
{
	const gchar *src = text && *text ? text : "portable-device";
	GString *safe = g_string_sized_new(strlen(src));
	gboolean underscore = FALSE;

	for (; *src; src++)
	{
		gchar c = *src;
		if (!(g_ascii_isalnum((guchar) c) || c == '-' || c == '_' || c == '.'))
			c = '_';
		if (c == '_' && underscore)
			continue;
		underscore = c == '_';
		g_string_append_c(safe, c);
	}
	if (safe->len == 0)
		g_string_assign(safe, "portable-device");
	return g_string_free(safe, FALSE);
}

static gchar *portable_mount_target(const DriveInfo *drive)
{
	gchar *root = portable_mount_root();
	gchar *safe;
	gchar *leaf;
	gchar *target;
	guint hash;

	if (!root)
		return NULL;
	safe = portable_safe_component(rox_drive_display_name(drive));
	hash = g_str_hash(drive && drive->device ? drive->device : safe) & 0xffff;
	leaf = g_strdup_printf("%s-%04x", safe, hash);
	target = g_build_filename(root, leaf, NULL);
	g_free(root); g_free(safe); g_free(leaf);
	return target;
}

static gchar *portable_mount_drive(const DriveInfo *drive, gchar **error_text)
{
	gchar *target;
	gchar *program = NULL;
	gchar *local_error = NULL;
	gboolean ok = FALSE;
	gint i;

	if (error_text)
		*error_text = NULL;
	if (!drive || !drive->portable || !drive->device)
		return NULL;
	if (drive->mountpoint && portable_mountpoint_present(drive->mountpoint))
		return g_strdup(drive->mountpoint);

	target = portable_mount_target(drive);
	if (!target || g_mkdir_with_parents(target, 0755) != 0)
	{
		if (error_text)
			*error_text = g_strdup_printf(_("Could not mount '%s'."), drive->device);
		g_free(target);
		return NULL;
	}

	if (g_str_has_prefix(drive->device, "mtp:simple:"))
	{
		const gchar *index = drive->device + strlen("mtp:simple:");
		program = g_find_program_in_path("simple-mtpfs");
		if (program)
		{
			gchar *argv[] = {program, (gchar *) "--device", (gchar *) index, target, NULL};
			ok = spawn_wait(argv, &local_error);
		}
	}
	else if (g_str_has_prefix(drive->device, "mtp:jmtpfs:"))
	{
		program = g_find_program_in_path("jmtpfs");
		if (program)
		{
			gchar *argv[] = {program, target, NULL};
			ok = spawn_wait(argv, &local_error);
		}
	}
	else if (g_str_has_prefix(drive->device, "ios:"))
	{
		const gchar *udid = drive->device + strlen("ios:");
		program = g_find_program_in_path("ifuse");
		if (program)
		{
			gchar *argv[] = {program, target, (gchar *) "--udid", (gchar *) udid, NULL};
			ok = spawn_wait(argv, &local_error);
		}
	}
	else if (g_str_has_prefix(drive->device, "ptp:gphoto:"))
	{
		program = g_find_program_in_path("gphotofs");
		if (program)
		{
			gchar *argv[] = {program, target, NULL};
			ok = spawn_wait(argv, &local_error);
		}
	}

	if (!program && !local_error)
	{
		const gchar *needed = g_str_has_prefix(drive->device, "ios:") ? "ifuse" :
			g_str_has_prefix(drive->device, "ptp:") ? "gphotofs" :
			g_str_has_prefix(drive->device, "mtp:simple:") ? "simple-mtpfs" : "jmtpfs";
		local_error = g_strdup_printf(_("Required program '%s' was not found."), needed);
	}
	g_free(program);

	if (ok)
	{
		for (i = 0; i < 30 && !portable_mountpoint_present(target); i++)
			g_usleep(100000);
		if (portable_mountpoint_present(target))
		{
			g_free(local_error);
			return target;
		}
		g_free(local_error);
		local_error = g_strdup_printf(_("Could not mount '%s'."), drive->device);
	}

	(void) g_rmdir(target);
	if (error_text)
		*error_text = local_error ? local_error :
			g_strdup_printf(_("Could not mount '%s'."), drive->device);
	else
		g_free(local_error);
	g_free(target);
	return NULL;
}

static gboolean portable_unmount_drive(const DriveInfo *drive, gchar **error_text)
{
	gchar *mountpoint;
	gchar *program;
	gchar *local_error = NULL;
	gboolean ok = FALSE;
	gboolean ours;

	if (error_text)
		*error_text = NULL;
	if (!drive || !drive->portable)
		return FALSE;
	mountpoint = drive_current_mountpoint(drive);
	if (!mountpoint)
		return TRUE;
	ours = drive->device && (g_str_has_prefix(drive->device, "mtp:") ||
		g_str_has_prefix(drive->device, "ios:") ||
		g_str_has_prefix(drive->device, "ptp:"));

	program = g_find_program_in_path("fusermount3");
	if (program)
	{
		gchar *argv[] = {program, (gchar *) "-u", mountpoint, NULL};
		ok = spawn_wait(argv, &local_error);
		g_free(program);
	}
	if (!ok)
	{
		g_free(local_error);
		local_error = NULL;
		program = g_find_program_in_path("fusermount");
		if (program)
		{
			gchar *argv[] = {program, (gchar *) "-u", mountpoint, NULL};
			ok = spawn_wait(argv, &local_error);
			g_free(program);
		}
	}
	if (!ok)
	{
		program = drive_system_program("umount");
		if (program)
		{
			gchar *argv[] = {program, mountpoint, NULL};
			g_free(local_error);
			local_error = NULL;
			ok = spawn_wait(argv, &local_error);
			g_free(program);
		}
	}

	if (ok)
	{
		if (ours)
			(void) g_rmdir(mountpoint);
		g_free(mountpoint);
		g_free(local_error);
		return TRUE;
	}
	if (error_text)
		*error_text = local_error ? local_error :
			g_strdup_printf(_("Could not unmount '%s'."), drive->device);
	else
		g_free(local_error);
	g_free(mountpoint);
	return FALSE;
}

/* Normal-user drive actions use one fixed, package-owned helper through
 * pkexec.  No shell command and no arbitrary executable path is accepted. */
static gboolean drive_pkexec_action(const gchar *operation, const gchar *device,
		gchar **error_text)
{
	gchar *pkexec;
	gchar *argv[5];
	gboolean ok;

	if (error_text)
		*error_text = NULL;
	pkexec = drive_system_program("pkexec");
	if (!pkexec)
	{
		if (error_text)
			*error_text = g_strdup_printf(_("Required program '%s' was not found."),
				"pkexec");
		return FALSE;
	}
	if (!g_file_test(ROX_MOUNT_HELPER_PATH, G_FILE_TEST_IS_EXECUTABLE))
	{
		if (error_text)
			*error_text = g_strdup_printf(_("Required program '%s' was not found."),
				ROX_MOUNT_HELPER_PATH);
		g_free(pkexec);
		return FALSE;
	}

	argv[0] = pkexec;
	argv[1] = (gchar *) ROX_MOUNT_HELPER_PATH;
	argv[2] = (gchar *) operation;
	argv[3] = (gchar *) device;
	argv[4] = NULL;
	/* Give the user enough time to answer the graphical polkit prompt. */
	ok = spawn_wait_timeout(argv, DRIVE_AUTH_TIMEOUT_SECONDS, error_text);
	g_free(pkexec);
	return ok;
}

/* Puppy/root mounts directly.  Normal-user sessions use pkexec and the
 * package-owned rox-mount-helper; udisksctl is no longer the authorization
 * path for mounting partitions. */
static gchar *mount_drive(const DriveInfo *drive, gchar **error_text)
{
	gchar *mountpoint;
	gchar *local_error = NULL;

	if (error_text)
		*error_text = NULL;
	if (!drive || !drive->device)
		return NULL;

	mountpoint = drive_current_mountpoint(drive);
	if (mountpoint)
		return mountpoint;

	if (drive->portable)
		return portable_mount_drive(drive, error_text);

	if (drive->network)
	{
		if (error_text)
			*error_text = g_strdup(_("This network resource is no longer mounted."));
		return NULL;
	}

	if (drive->managed_image)
	{
		if (error_text)
			*error_text = g_strdup(_("The mounted image is no longer available."));
		return NULL;
	}

	if (geteuid() == 0)
	{
		gchar *base = g_path_get_basename(drive->device);
		gchar *target = g_build_filename("/mnt", base, NULL);
		gboolean existed = g_file_test(target, G_FILE_TEST_IS_DIR);
		gchar *mount_prog = drive_system_program("mount");
		gchar *argv[] = {mount_prog, drive->device, target, NULL};

		g_free(base);
		if (mount_prog && g_mkdir_with_parents(target, 0755) == 0 &&
		    spawn_wait(argv, &local_error))
		{
			g_free(local_error);
			g_free(mount_prog);
			return target;
		}
		if (!mount_prog && !local_error)
			local_error = g_strdup_printf(_("Could not mount '%s'."), drive->device);
		if (!existed)
			rmdir(target);
		g_free(mount_prog);
		g_free(target);
	}
	else if (drive_pkexec_action("mount-device", drive->device, &local_error))
	{
		gchar *detected = find_mountpoint(drive->device);
		g_free(local_error);
		if (detected)
			return detected;
		local_error = g_strdup_printf(_("Could not mount '%s'."), drive->device);
	}

	if (error_text)
		*error_text = local_error ? local_error :
			g_strdup_printf(_("Could not mount '%s'."), drive->device);
	else
		g_free(local_error);
	return NULL;
}


typedef struct
{
	RoxDriveInfo *drive;
	gchar *error_text;
} DriveMountJob;

typedef struct
{
	gchar *mountpoint;
	gchar *error_text;
} DriveMountResult;

static void drive_mount_job_free(gpointer data)
{
	DriveMountJob *job = data;
	if (!job)
		return;
	rox_drive_info_free(job->drive);
	g_free(job->error_text);
	g_free(job);
}

static void drive_mount_result_free(gpointer data)
{
	DriveMountResult *result = data;
	if (!result)
		return;
	g_free(result->mountpoint);
	g_free(result->error_text);
	g_free(result);
}

static void drive_mount_task(GTask *task, gpointer source_object,
		gpointer task_data, GCancellable *cancellable)
{
	DriveMountJob *job = task_data;
	DriveMountResult *result = g_new0(DriveMountResult, 1);
	(void) source_object;
	(void) cancellable;

	result->mountpoint = mount_drive(job->drive, &result->error_text);
	g_task_return_pointer(task, result, drive_mount_result_free);
}

void rox_drive_mount_async(const RoxDriveInfo *drive,
		GAsyncReadyCallback callback, gpointer user_data)
{
	GTask *task;
	DriveMountJob *job;

	g_return_if_fail(drive != NULL);
	job = g_new0(DriveMountJob, 1);
	job->drive = rox_drive_info_copy(drive);
	task = g_task_new(NULL, NULL, callback, user_data);
	g_task_set_task_data(task, job, drive_mount_job_free);
	g_task_run_in_thread(task, drive_mount_task);
	g_object_unref(task);
}

gchar *rox_drive_mount_finish(GAsyncResult *result, gchar **error_text)
{
	DriveMountResult *mount_result;
	gchar *mountpoint;

	if (error_text)
		*error_text = NULL;
	g_return_val_if_fail(G_IS_TASK(result), NULL);
	mount_result = g_task_propagate_pointer(G_TASK(result), NULL);
	if (!mount_result)
		return NULL;
	mountpoint = g_steal_pointer(&mount_result->mountpoint);
	if (error_text)
		*error_text = g_steal_pointer(&mount_result->error_text);
	drive_mount_result_free(mount_result);
	if (mountpoint)
		rox_drives_monitor_request_scan();
	return mountpoint;
}

/* Integrated unmounting.  Root/Puppy executes umount directly; a normal
 * user authenticates with pkexec and the fixed Rox-Filer2 helper. */
static gboolean unmount_drive(const DriveInfo *drive, gchar **error_text)
{
	gchar *mountpoint;
	gchar *local_error = NULL;
	gboolean ok = FALSE;

	if (error_text)
		*error_text = NULL;
	if (!drive || !drive->device)
		return FALSE;
	if (drive->managed_image)
	{
		if (!drive->backing_file || !*drive->backing_file)
		{
			if (error_text)
				*error_text = g_strdup(_("The mounted image is no longer available."));
			return FALSE;
		}
		return image_mounter_unmount_managed_sync(drive->backing_file, error_text);
	}
	if (drive->portable)
		return portable_unmount_drive(drive, error_text);
	if (drive->foreign)
	{
		if (error_text)
			*error_text = g_strdup(_("This network resource is managed by another application; unmount it there."));
		return FALSE;
	}

	mountpoint = drive_current_mountpoint(drive);
	if (!mountpoint)
		return TRUE;

	if (drive->network)
	{
		ok = rox_smb_unmount_path(mountpoint, &local_error);
		g_free(mountpoint);
		if (ok)
		{
			g_free(local_error);
			return TRUE;
		}
		if (error_text)
			*error_text = local_error ? local_error :
				g_strdup_printf(_("Could not unmount '%s'."), drive->device);
		else
			g_free(local_error);
		return FALSE;
	}

	if (geteuid() == 0)
	{
		gchar *umount_prog = drive_system_program("umount");
		gchar *argv[] = {umount_prog, mountpoint, NULL};
		if (umount_prog)
			ok = spawn_wait(argv, &local_error);
		else
			local_error = g_strdup_printf(_("Could not unmount '%s'."), drive->device);
		g_free(umount_prog);
	}
	else
	{
		ok = drive_pkexec_action("unmount-device", drive->device, &local_error);
	}

	g_free(mountpoint);
	if (ok)
	{
		g_free(local_error);
		return TRUE;
	}

	if (error_text)
		*error_text = local_error ? local_error :
			g_strdup_printf(_("Could not unmount '%s'."), drive->device);
	else
		g_free(local_error);
	return FALSE;
}


/* Safe eject.  Normal-user sessions authorize the hardware operation through
 * pkexec as well; root sessions retain direct system-tool behaviour. */
static gboolean eject_drive(const DriveInfo *drive, gchar **error_text)
{
	gchar *device;
	gchar *program;
	gchar *local_error = NULL;
	gboolean ok = FALSE;

	if (error_text)
		*error_text = NULL;
	if (!drive || !drive->device)
		return FALSE;
	if (!rox_drive_can_eject(drive))
	{
		if (error_text)
			*error_text = g_strdup_printf(_("Could not eject '%s'."), drive->device);
		return FALSE;
	}

	if (!unmount_drive(drive, &local_error))
	{
		if (error_text)
			*error_text = local_error;
		else
			g_free(local_error);
		return FALSE;
	}
	g_free(local_error);
	local_error = NULL;

	device = g_strdup(drive->parent_device ? drive->parent_device : drive->device);
	if (geteuid() != 0)
	{
		ok = drive_pkexec_action(drive->optical ? "eject-optical" : "power-off",
			device, &local_error);
	}
	else if (drive->optical)
	{
		program = drive_system_program("eject");
		if (program)
		{
			gchar *argv[] = {program, device, NULL};
			ok = spawn_wait(argv, &local_error);
			g_free(program);
		}
	}
	else if (drive->hardware_removable)
	{
		program = drive_system_program("udisksctl");
		if (program)
		{
			gchar *argv[] = {program, (gchar *) "power-off", (gchar *) "-b",
				device, NULL};
			ok = spawn_wait(argv, &local_error);
			g_free(program);
		}
	}

	if (geteuid() == 0 && !ok && drive->optical)
	{
		program = drive_system_program("udisksctl");
		if (program)
		{
			gchar *argv[] = {program, (gchar *) "power-off", (gchar *) "-b",
				device, NULL};
			g_free(local_error);
			local_error = NULL;
			ok = spawn_wait(argv, &local_error);
			g_free(program);
		}
	}
	else if (geteuid() == 0 && !ok && drive->hardware_removable)
	{
		program = drive_system_program("eject");
		if (program)
		{
			gchar *argv[] = {program, device, NULL};
			g_free(local_error);
			local_error = NULL;
			ok = spawn_wait(argv, &local_error);
			g_free(program);
		}
	}

	if (ok)
	{
		g_free(local_error);
		g_free(device);
		return TRUE;
	}

	if (error_text)
		*error_text = local_error ? local_error :
			g_strdup_printf(_("Could not eject '%s'."), drive->device);
	else
		g_free(local_error);
	g_free(device);
	return FALSE;
}


static void drive_menu_action_free(gpointer data)
{
	DriveMenuAction *action = data;
	if (!action)
		return;
	rox_drive_info_free(action->drive);
	g_free(action);
}

static DriveInfo *drive_info_copy(const DriveInfo *source)
{
	DriveInfo *copy;
	if (!source)
		return NULL;
	copy = g_new0(DriveInfo, 1);
	copy->name = g_strdup(source->name);
	copy->device = g_strdup(source->device);
	copy->label = g_strdup(source->label);
	copy->fstype = g_strdup(source->fstype);
	copy->mountpoint = g_strdup(source->mountpoint);
	copy->size = g_strdup(source->size);
	copy->type = g_strdup(source->type);
	copy->transport = g_strdup(source->transport);
	copy->model = g_strdup(source->model);
	copy->parent_device = g_strdup(source->parent_device);
	copy->backing_file = g_strdup(source->backing_file);
	copy->managed_image = source->managed_image;
	copy->removable = source->removable;
	copy->hardware_removable = source->hardware_removable;
	copy->optical = source->optical;
	copy->network = source->network;
	copy->portable = source->portable;
	copy->foreign = source->foreign;
	copy->solid_state = source->solid_state;
	copy->system_partition = source->system_partition;
	copy->label_is_synthetic = source->label_is_synthetic;
	return copy;
}

/* Agregado por josejp2424 (2026): el menú contextual conserva una copia
 * propia de la acción para que siga siendo válida aunque cierre el popover. */
static DriveMenuAction *drive_menu_action_copy(const DriveMenuAction *source)
{
	DriveMenuAction *copy;

	if (!source)
		return NULL;
	copy = g_new0(DriveMenuAction, 1);
	copy->filer_window = source->filer_window;
	copy->drive = drive_info_copy(source->drive);
	copy->popover = source->popover;
	return copy;
}

/* Agregado por josejp2424 (2026): conservar una referencia temporal al
 * popover. Una actualización de montajes puede cerrar la ventana y liberar la
 * acción mientras el callback todavía está terminando. */
static GtkWidget *drive_action_ref_popover(DriveMenuAction *action)
{
	if (!action || !GTK_IS_WIDGET(action->popover))
		return NULL;
	return g_object_ref(action->popover);
}

static void drive_action_finish_popover(GtkWidget *popover, gboolean close_it)
{
	if (!popover)
		return;
	if (close_it && GTK_IS_WIDGET(popover))
		gtk_widget_destroy(popover);
	g_object_unref(popover);
}

static void drive_menu_open(GtkMenuItem *item, gpointer data)
{
	(void) item;
	drive_grid_activate(NULL, data);
}

typedef struct
{
	GtkWidget *popover;
	FilerWindow *filer_window;
	gboolean open_after_mount;
} DriveMountUi;

static void drive_mount_ui_free(DriveMountUi *ui)
{
	if (!ui)
		return;
	if (ui->popover)
		g_object_unref(ui->popover);
	g_free(ui);
}

static void drive_mount_ui_done(GObject *source_object, GAsyncResult *result,
		gpointer user_data)
{
	DriveMountUi *ui = user_data;
	gchar *error_text = NULL;
	gchar *mountpoint;
	(void) source_object;

	mountpoint = rox_drive_mount_finish(result, &error_text);
	if (!mountpoint)
	{
		report_error("%s", error_text ? error_text :
			_("The partition could not be mounted."));
		g_free(error_text);
		drive_action_finish_popover(ui->popover, FALSE);
		ui->popover = NULL;
		drive_mount_ui_free(ui);
		return;
	}

	mount_update(TRUE);
	filer_update_all();
	if (ui->open_after_mount && ui->filer_window && filer_exists(ui->filer_window))
	{
		FilerWindow *target = ui->filer_window;
		drive_action_finish_popover(ui->popover, TRUE);
		ui->popover = NULL;
		filer_change_to(target, mountpoint, NULL);
	}
	else
	{
		drive_action_finish_popover(ui->popover, TRUE);
		ui->popover = NULL;
	}
	g_free(mountpoint);
	g_free(error_text);
	drive_mount_ui_free(ui);
}

static void drive_menu_mount(GtkMenuItem *item, gpointer data)
{
	DriveMenuAction *action = data;
	DriveMountUi *ui;

	(void) item;
	if (!action || !action->drive)
		return;
	ui = g_new0(DriveMountUi, 1);
	ui->popover = drive_action_ref_popover(action);
	ui->filer_window = action->filer_window;
	rox_drive_mount_async(action->drive, drive_mount_ui_done, ui);
}

typedef struct
{
	GtkWidget *popover;
	gboolean eject;
} DriveActionUi;

static void drive_action_ui_done(GObject *source_object, GAsyncResult *result,
		gpointer user_data)
{
	DriveActionUi *ui = user_data;
	gchar *error_text = NULL;
	gboolean ok;
	(void) source_object;

	ok = ui->eject ? rox_drive_eject_finish(result, &error_text)
	               : rox_drive_unmount_finish(result, &error_text);
	if (!ok)
		report_error("%s", error_text ? error_text :
			(ui->eject ? _("The device could not be ejected.")
			           : _("The partition could not be unmounted.")));
	else
	{
		mount_update(TRUE);
		filer_update_all();
	}
	drive_action_finish_popover(ui->popover, ok);
	g_free(error_text);
	g_free(ui);
}

static void drive_menu_unmount(GtkMenuItem *item, gpointer data)
{
	DriveMenuAction *action = data;
	DriveActionUi *ui;

	(void) item;
	if (!action || !action->drive)
		return;
	ui = g_new0(DriveActionUi, 1);
	ui->popover = drive_action_ref_popover(action);
	rox_drive_unmount_async(action->drive, drive_action_ui_done, ui);
}

static void drive_menu_eject(GtkMenuItem *item, gpointer data)
{
	DriveMenuAction *action = data;
	DriveActionUi *ui;

	(void) item;
	if (!action || !action->drive)
		return;
	ui = g_new0(DriveActionUi, 1);
	ui->popover = drive_action_ref_popover(action);
	ui->eject = TRUE;
	rox_drive_eject_async(action->drive, drive_action_ui_done, ui);
}

/* 2.13.0-7: quick mounted-state action for the Classic partitions
 * popover.  It mirrors the small eject/unmount arrow used by desktop drive
 * icons: mounted volumes show the arrow; unmounted volumes do not. */
static void drive_grid_quick_action(GtkButton *button, gpointer data)
{
	DriveMenuAction *action = data;
	DriveActionUi *ui;

	(void) button;
	if (!action || !action->drive)
		return;

	ui = g_new0(DriveActionUi, 1);
	ui->popover = drive_action_ref_popover(action);
	ui->eject = rox_drive_can_eject(action->drive);
	if (ui->eject)
		rox_drive_eject_async(action->drive, drive_action_ui_done, ui);
	else
		rox_drive_unmount_async(action->drive, drive_action_ui_done, ui);
}

/* Agregado por josejp2424 (2026): menú contextual pequeño, acorde a la
 * interfaz tradicional de ROX. El clic izquierdo conserva montar/abrir. */
static gboolean drive_grid_button_press(GtkWidget *button,
		GdkEventButton *event, gpointer data)
{
	DriveMenuAction *action = data;
	DriveMenuAction *menu_action;
	GtkWidget *menu;
	GtkWidget *open_item;
	GtkWidget *mount_item;
	GtkWidget *unmount_item;
	GtkWidget *eject_item;
	GtkWidget *separator;
	gchar *mountpoint;
	gboolean mounted;

	if (!event || event->type != GDK_BUTTON_PRESS || event->button != 3 ||
	    !action || !action->drive)
		return FALSE;

	mountpoint = drive_current_mountpoint(action->drive);
	mounted = mountpoint != NULL;
	g_free(mountpoint);

	menu = rox_menu_new();
	menu_action = drive_menu_action_copy(action);
	g_object_set_data_full(G_OBJECT(menu), "rox-drive-menu-action",
		menu_action, drive_menu_action_free);
	open_item = gtk_menu_item_new_with_label(_("Open"));
	mount_item = gtk_menu_item_new_with_label(_("Mount"));
	unmount_item = gtk_menu_item_new_with_label(_("Unmount"));
	separator = NULL;
	eject_item = NULL;

	gtk_widget_set_sensitive(mount_item, !mounted && !action->drive->foreign);
	gtk_widget_set_sensitive(unmount_item, mounted && !action->drive->foreign);

	gtk_menu_shell_append(GTK_MENU_SHELL(menu), open_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), mount_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), unmount_item);

	/* Medios ópticos y unidades extraíbles ofrecen expulsión segura. Para USB,
	 * eject_drive() desmonta primero y luego intenta apagar el dispositivo. */
	if (rox_drive_can_eject(action->drive))
	{
		separator = gtk_separator_menu_item_new();
		eject_item = gtk_menu_item_new_with_label(_("Eject"));
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), separator);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), eject_item);
	}

	g_signal_connect(open_item, "activate", G_CALLBACK(drive_menu_open), menu_action);
	g_signal_connect(mount_item, "activate", G_CALLBACK(drive_menu_mount), menu_action);
	g_signal_connect(unmount_item, "activate", G_CALLBACK(drive_menu_unmount), menu_action);
	if (eject_item)
		g_signal_connect(eject_item, "activate", G_CALLBACK(drive_menu_eject), menu_action);
	g_signal_connect_swapped(menu, "selection-done",
		G_CALLBACK(gtk_widget_destroy), menu);
	gtk_menu_attach_to_widget(GTK_MENU(menu), button, NULL);
	gtk_widget_show_all(menu);
	gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *) event);
	return TRUE;
}

static void drive_grid_activate(GtkButton *button, gpointer data)
{
	DriveMenuAction *action = data;
	gchar *mountpoint;

	(void) button;
	if (!action || !action->drive || !action->filer_window ||
	    !filer_exists(action->filer_window))
		return;

	mountpoint = drive_current_mountpoint(action->drive);
	if (!mountpoint && !action->drive->network && action->drive->mountpoint &&
	    *action->drive->mountpoint &&
	    g_file_test(action->drive->mountpoint, G_FILE_TEST_IS_DIR))
		mountpoint = g_strdup(action->drive->mountpoint);

	if (!mountpoint)
	{
		DriveMountUi *ui;
		if (action->drive->network)
		{
			report_error("%s", _("This network resource is no longer mounted."));
			return;
		}
		ui = g_new0(DriveMountUi, 1);
		ui->popover = drive_action_ref_popover(action);
		ui->filer_window = action->filer_window;
		ui->open_after_mount = TRUE;
		rox_drive_mount_async(action->drive, drive_mount_ui_done, ui);
		return;
	}

	if (!action->drive->network && !g_file_test(mountpoint, G_FILE_TEST_IS_DIR))
	{
		report_error(_("The partition '%s' was mounted, but its mount point could not be found."),
			action->drive->device);
		g_free(mountpoint);
		return;
	}

	{
		FilerWindow *target_window = action->filer_window;
		if (GTK_IS_WIDGET(action->popover))
			gtk_widget_destroy(action->popover);
		filer_change_to(target_window, mountpoint, NULL);
	}
	g_free(mountpoint);
}

/* Agregado por josejp2424 (2026): clasificación única de iconos.
 * ROX no debe convertir un USB, una tarjeta o sr0 en drive-harddisk sólo
 * porque el tema GTK activo no publique todos los nombres Freedesktop. */
typedef enum
{
	ROX_DRIVE_ICON_INTERNAL,
	ROX_DRIVE_ICON_SSD,
	ROX_DRIVE_ICON_USB,
	ROX_DRIVE_ICON_SD,
	ROX_DRIVE_ICON_OPTICAL,
	ROX_DRIVE_ICON_IMAGE,
	ROX_DRIVE_ICON_FLOPPY,
	ROX_DRIVE_ICON_NETWORK,
	ROX_DRIVE_ICON_PORTABLE,
	ROX_DRIVE_ICON_CAMERA
} RoxDriveIconKind;

static RoxDriveIconKind drive_icon_kind(const RoxDriveInfo *drive)
{
	gchar *lower_model = NULL;
	gboolean solid_state = FALSE;

	if (!drive)
		return ROX_DRIVE_ICON_INTERNAL;
	if (drive->network)
		return ROX_DRIVE_ICON_NETWORK;
	if (drive->managed_image)
		return ROX_DRIVE_ICON_IMAGE;
	if (drive->portable)
		return g_strcmp0(drive->transport, "ptp") == 0 ?
			ROX_DRIVE_ICON_CAMERA : ROX_DRIVE_ICON_PORTABLE;
	if (drive->optical ||
	    (drive->type && !g_ascii_strcasecmp(drive->type, "rom")) ||
	    (drive->name && g_str_has_prefix(drive->name, "sr")) ||
	    (drive->fstype && (!g_ascii_strcasecmp(drive->fstype, "iso9660") ||
	     !g_ascii_strcasecmp(drive->fstype, "udf"))))
		return ROX_DRIVE_ICON_OPTICAL;
	if (drive->name && g_str_has_prefix(drive->name, "fd"))
		return ROX_DRIVE_ICON_FLOPPY;
	if ((drive->transport && (!g_ascii_strcasecmp(drive->transport, "mmc") ||
	     !g_ascii_strcasecmp(drive->transport, "sd"))) ||
	    (drive->name && g_str_has_prefix(drive->name, "mmc")))
		return ROX_DRIVE_ICON_SD;
	if ((drive->transport && !g_ascii_strcasecmp(drive->transport, "usb")) ||
	    drive->removable || name_looks_removable(drive->label) ||
	    name_looks_removable(drive->model))
		return ROX_DRIVE_ICON_USB;

	solid_state = drive->solid_state;
	if (drive->name && g_str_has_prefix(drive->name, "nvme"))
		solid_state = TRUE;
	if (drive->model)
	{
		lower_model = g_utf8_strdown(drive->model, -1);
		if (strstr(lower_model, "ssd") || strstr(lower_model, "solid state"))
			solid_state = TRUE;
		g_free(lower_model);
	}
	return solid_state ? ROX_DRIVE_ICON_SSD : ROX_DRIVE_ICON_INTERNAL;
}

/* Los nombres son identificadores semánticos del tema GTK activo.
 * No se buscan archivos dentro de otros temas: GTK3 lee gtk-icon-theme-name
 * desde GtkSettings (settings.ini/XSettings) y resuelve cada nombre mediante
 * el tema seleccionado y su cadena Inherits. */
static const gchar *const *drive_icon_names_for_kind(RoxDriveIconKind kind)
{
	static const gchar *optical[] = {
		"media-cdrw", "media-optical", "drive-optical", NULL
	};
	static const gchar *usb[] = {
		"drive-removable-media", "drive-removable-media-usb",
		"media-flash-usb", NULL
	};
	static const gchar *sd[] = {
		"media-flash", "media-flash-sd-mmc", "media-memory-sd", NULL
	};
	static const gchar *ssd[] = {
		"drive-harddisk-solidstate", "drive-harddisk-system",
		"drive-harddisk", NULL
	};
	static const gchar *image[] = {
		"media-optical", "drive-removable-media", "drive-harddisk", NULL
	};
	static const gchar *floppy[] = {
		"media-floppy", "drive-floppy", NULL
	};
	static const gchar *network[] = {
		"drive-network", "network-server", "folder-remote", NULL
	};
	static const gchar *portable[] = {
		"phone", "multimedia-player", "smartphone",
		"drive-removable-media", NULL
	};
	static const gchar *camera[] = {
		"camera-photo", "camera", "image-x-generic",
		"drive-removable-media", NULL
	};
	static const gchar *internal[] = {
		"drive-harddisk", "drive-harddisk-system", NULL
	};

	switch (kind)
	{
		case ROX_DRIVE_ICON_OPTICAL: return optical;
		case ROX_DRIVE_ICON_IMAGE: return image;
		case ROX_DRIVE_ICON_USB: return usb;
		case ROX_DRIVE_ICON_SD: return sd;
		case ROX_DRIVE_ICON_SSD: return ssd;
		case ROX_DRIVE_ICON_FLOPPY: return floppy;
		case ROX_DRIVE_ICON_NETWORK: return network;
		case ROX_DRIVE_ICON_PORTABLE: return portable;
		case ROX_DRIVE_ICON_CAMERA: return camera;
		case ROX_DRIVE_ICON_INTERNAL:
		default: return internal;
	}
}

static const gchar *const *drive_icon_names(const RoxDriveInfo *drive)
{
	return drive_icon_names_for_kind(drive_icon_kind(drive));
}

/* Devuelve el primer nombre disponible dentro del GtkIconTheme activo.
 * Si todavía no fue indexado, conserva el nombre canónico para que GTK lo
 * resuelva al dibujar. Nunca devuelve una ruta de otro tema instalado. */
const gchar *rox_drive_icon_name(const RoxDriveInfo *drive)
{
	const gchar *const *names = drive_icon_names(drive);
	GtkIconTheme *theme = gtk_icon_theme_get_default();
	gint i;

	if (theme)
		for (i = 0; names[i]; i++)
			if (gtk_icon_theme_has_icon(theme, names[i]))
				return names[i];

	return names[0] ? names[0] : DRIVE_ICON_INTERNAL;
}

/* Crear siempre un GThemedIcon. GtkImage/GtkIconTheme resolverán el archivo
 * usando gtk-icon-theme-name del GtkSettings activo. Un GFileIcon apuntando a
 * /usr/share/icons/<otro-tema>/... ignoraría settings.ini y fue la causa de
 * que aparecieran iconos de GNOME. */
GIcon *rox_drive_get_icon(const RoxDriveInfo *drive)
{
	const gchar *const *names = drive_icon_names(drive);

	return g_themed_icon_new_from_names((gchar **) names, -1);
}

GtkWidget *rox_drive_icon_widget_new(const RoxDriveInfo *drive, gint size)
{
	GIcon *icon;
	GtkWidget *image;

	icon = rox_drive_get_icon(drive);
	image = gtk_image_new_from_gicon(icon, GTK_ICON_SIZE_DIALOG);
	gtk_image_set_pixel_size(GTK_IMAGE(image), MAX(16, size));
	g_object_unref(icon);
	return image;
}

const gchar *rox_drive_display_name(const RoxDriveInfo *drive)
{
	if (!drive)
		return _("Partitions");
	if (drive->label && *drive->label)
		return drive->label;
	if (drive->name && *drive->name)
		return drive->name;
	return drive->device ? drive->device : _("Partitions");
}

static GtkWidget *drive_icon_widget(const DriveInfo *drive, gint size)
{
	return rox_drive_icon_widget_new(drive, size);
}

/* Agregado por josejp2424 (2026): representar cada partición como un botón
 * compacto para poder distribuir las unidades horizontalmente en grupos de
 * cuatro, en lugar de una única lista vertical o una hilera interminable. */
static GtkWidget *drive_grid_button_new(const DriveInfo *drive)
{
	GtkWidget *button;
	GtkWidget *box;
	GtkWidget *image;
	GtkWidget *label_widget;
	gchar *mountpoint;
	gchar *detail;
	gchar *markup;
	const gchar *title;
	const gchar *status;

	mountpoint = drive_current_mountpoint(drive);
	status = mountpoint ? _("Mounted") : _("Not mounted");
	title = drive->label && *drive->label ? drive->label :
		(drive->name && *drive->name ? drive->name : drive->device);

	if (drive->size && *drive->size)
		detail = g_strdup_printf("%s · %s",
			drive->name ? drive->name : drive->device, drive->size);
	else
		detail = g_strdup(drive->name ? drive->name : drive->device);

	markup = g_markup_printf_escaped("<b>%s</b>\n%s\n%s",
		title ? title : _("Partitions"), detail ? detail : "", status);

	button = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_widget_set_size_request(button, 138, 96);
	gtk_widget_set_hexpand(button, TRUE);

	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
	gtk_widget_set_margin_start(box, 5);
	gtk_widget_set_margin_end(box, 5);
	gtk_widget_set_margin_top(box, 5);
	gtk_widget_set_margin_bottom(box, 5);
	gtk_container_add(GTK_CONTAINER(button), box);

	image = drive_icon_widget(drive, 32);
	gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);

	label_widget = gtk_label_new(NULL);
	gtk_label_set_markup(GTK_LABEL(label_widget), markup);
	gtk_label_set_justify(GTK_LABEL(label_widget), GTK_JUSTIFY_CENTER);
	gtk_label_set_ellipsize(GTK_LABEL(label_widget), PANGO_ELLIPSIZE_END);
	gtk_label_set_max_width_chars(GTK_LABEL(label_widget), 20);
	gtk_widget_set_tooltip_text(button, drive->mountpoint && *drive->mountpoint ?
		drive->mountpoint : drive->device);
	gtk_box_pack_start(GTK_BOX(box), label_widget, TRUE, TRUE, 0);

	g_free(markup);
	g_free(detail);
	g_free(mountpoint);
	return button;
}

static void destroy_popover_when_closed(GtkPopover *popover, gpointer data)
{
	(void) data;
	gtk_widget_destroy(GTK_WIDGET(popover));
}

typedef struct
{
	GtkToolButton *button;
	GtkWidget *popover;
	FilerWindow *filer_window;
} DriveVisibilityToggle;

static void drive_visibility_toggle_free(gpointer data)
{
	DriveVisibilityToggle *toggle = data;
	if (!toggle)
		return;
	if (toggle->button)
		g_object_unref(toggle->button);
	g_free(toggle);
}

static void drives_button_clicked(GtkToolButton *button, gpointer data);

static void drive_visibility_toggled(GtkToggleButton *toggle_button, gpointer data)
{
	DriveVisibilityToggle *toggle = data;
	gboolean enabled = gtk_toggle_button_get_active(toggle_button);

	if (!toggle)
		return;
	drive_visibility_load();
	if (enabled == g_atomic_int_get(&show_system_partitions))
		return;

	if (enabled)
	{
		GtkWidget *dialog = gtk_message_dialog_new(
			toggle->filer_window && toggle->filer_window->window ?
			GTK_WINDOW(toggle->filer_window->window) : NULL,
			GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
			"%s", _("Show boot/system partitions"));
		gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
			"%s", _("Boot and EFI partitions can contain files required to start the system. Modifying or deleting them may make the system unbootable. Show these partitions anyway?"));
		gtk_dialog_add_buttons(GTK_DIALOG(dialog), _("Cancel"), GTK_RESPONSE_CANCEL,
			_("Show"), GTK_RESPONSE_ACCEPT, NULL);
		if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT)
		{
			gtk_widget_destroy(dialog);
			g_signal_handlers_block_by_func(toggle_button, drive_visibility_toggled, data);
			gtk_toggle_button_set_active(toggle_button, FALSE);
			g_signal_handlers_unblock_by_func(toggle_button, drive_visibility_toggled, data);
			return;
		}
		gtk_widget_destroy(dialog);
	}

	if (!drive_visibility_save(enabled))
	{
		g_signal_handlers_block_by_func(toggle_button, drive_visibility_toggled, data);
		gtk_toggle_button_set_active(toggle_button, g_atomic_int_get(&show_system_partitions));
		g_signal_handlers_unblock_by_func(toggle_button, drive_visibility_toggled, data);
		return;
	}

	/* Rebuild immediately so Classic Partitions reflects the choice.  Keep
	 * independent references because destroying the popover also destroys the
	 * check button and therefore frees DriveVisibilityToggle. */
	{
		GtkToolButton *button = g_object_ref(toggle->button);
		FilerWindow *filer_window = toggle->filer_window;
		GtkWidget *popover = toggle->popover;
		if (GTK_IS_WIDGET(popover))
			gtk_widget_destroy(popover);
		drives_button_clicked(button, filer_window);
		g_object_unref(button);
	}
}

static void drives_popover_attach_drive(DrivePopoverLive *live,
		DriveInfo *drive, guint position, guint row_offset)
{
	DriveMenuAction *action;
	GtkWidget *cell;
	GtkWidget *drive_button;
	GtkWidget *quick_button;
	GtkWidget *quick_image;
	gchar *mountpoint;
	gboolean mounted;

	if (!live || !drive)
		return;

	action = g_new0(DriveMenuAction, 1);
	cell = gtk_overlay_new();
	drive_button = drive_grid_button_new(drive);
	action->filer_window = live->filer_window;
	action->drive = drive_info_copy(drive);
	action->popover = live->popover;
	g_signal_connect(drive_button, "clicked",
		G_CALLBACK(drive_grid_activate), action);
	gtk_widget_add_events(drive_button, GDK_BUTTON_PRESS_MASK);
	g_signal_connect(drive_button, "button-press-event",
		G_CALLBACK(drive_grid_button_press), action);
	gtk_widget_set_tooltip_text(drive_button,
		_("Left click: open or mount\nRight click: drive actions"));
	gtk_container_add(GTK_CONTAINER(cell), drive_button);

	/* Match the desktop drive icons: the arrow is also the mounted-state
	 * indicator.  Foreign mounts stay read-only from ROX because another
	 * application owns their lifecycle. */
	mountpoint = drive_current_mountpoint(drive);
	mounted = mountpoint != NULL;
	g_free(mountpoint);
	if (mounted && !drive->foreign)
	{
		quick_button = gtk_button_new();
		gtk_button_set_relief(GTK_BUTTON(quick_button), GTK_RELIEF_NONE);
		gtk_widget_set_halign(quick_button, GTK_ALIGN_END);
		gtk_widget_set_valign(quick_button, GTK_ALIGN_START);
		gtk_widget_set_margin_top(quick_button, 2);
		gtk_widget_set_margin_end(quick_button, 2);
		gtk_widget_set_size_request(quick_button, 24, 24);
		gtk_style_context_add_class(gtk_widget_get_style_context(quick_button),
			"rox-drive-quick");
		quick_image = gtk_image_new_from_icon_name("media-eject-symbolic",
			GTK_ICON_SIZE_MENU);
		if (!gtk_icon_theme_has_icon(gtk_icon_theme_get_default(),
			"media-eject-symbolic"))
			gtk_image_set_from_icon_name(GTK_IMAGE(quick_image), "media-eject",
				GTK_ICON_SIZE_MENU);
		gtk_container_add(GTK_CONTAINER(quick_button), quick_image);
		gtk_widget_set_tooltip_text(quick_button,
			rox_drive_can_eject(drive) ? _("Eject") : _("Unmount"));
		g_signal_connect(quick_button, "clicked",
			G_CALLBACK(drive_grid_quick_action), action);
		gtk_overlay_add_overlay(GTK_OVERLAY(cell), quick_button);
		gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(cell), quick_button, FALSE);
	}

	g_object_set_data_full(G_OBJECT(cell), "rox-drive-action",
		action, drive_menu_action_free);
	gtk_grid_attach(GTK_GRID(live->grid), cell,
		(gint) (position % 4), (gint) (row_offset + position / 4), 1, 1);
}

static void drives_popover_fill(DrivePopoverLive *live, GPtrArray *drives,
		const GError *error)
{
	GList *children, *node;
	guint i;
	guint normal_count = 0;
	guint portable_count = 0;
	guint image_count = 0;
	guint normal_pos = 0;
	guint portable_pos = 0;
	guint image_pos = 0;
	guint normal_rows = 0;
	guint portable_rows = 0;
	guint rows = 1;
	gint content_height;

	if (!live || !GTK_IS_WIDGET(live->grid) || !GTK_IS_WIDGET(live->popover))
		return;

	children = gtk_container_get_children(GTK_CONTAINER(live->grid));
	for (node = children; node; node = node->next)
		gtk_widget_destroy(GTK_WIDGET(node->data));
	g_list_free(children);

	if (error && !drives)
	{
		GtkWidget *message = gtk_label_new(error->message);
		gtk_label_set_line_wrap(GTK_LABEL(message), TRUE);
		gtk_grid_attach(GTK_GRID(live->grid), message, 0, 0, 4, 1);
	}
	else if (!drives)
	{
		GtkWidget *message = gtk_label_new(_("Scanning"));
		gtk_grid_attach(GTK_GRID(live->grid), message, 0, 0, 4, 1);
	}
	else if (drives->len == 0)
	{
		GtkWidget *message = gtk_label_new(_("No usable partitions found"));
		gtk_grid_attach(GTK_GRID(live->grid), message, 0, 0, 4, 1);
	}
	else
	{
		for (i = 0; i < drives->len; i++)
		{
			DriveInfo *drive = g_ptr_array_index(drives, i);
			if (drive->managed_image)
				image_count++;
			else if (drive->portable)
				portable_count++;
			else
				normal_count++;
		}

		for (i = 0; i < drives->len; i++)
		{
			DriveInfo *drive = g_ptr_array_index(drives, i);
			if (drive->managed_image || drive->portable)
				continue;
			drives_popover_attach_drive(live, drive, normal_pos++, 0);
		}

		normal_rows = (normal_count + 3) / 4;
		rows = MAX(1, normal_rows);

		if (portable_count > 0)
		{
			GtkWidget *heading = gtk_label_new(NULL);
			gchar *markup = g_markup_printf_escaped("<b>%s</b>",
				_("Portable Devices"));
			gtk_label_set_markup(GTK_LABEL(heading), markup);
			gtk_label_set_xalign(GTK_LABEL(heading), 0.0);
			gtk_widget_set_margin_top(heading, normal_count ? 8 : 0);
			gtk_widget_set_margin_bottom(heading, 2);
			gtk_grid_attach(GTK_GRID(live->grid), heading,
				0, (gint) normal_rows, 4, 1);
			g_free(markup);

			for (i = 0; i < drives->len; i++)
			{
				DriveInfo *drive = g_ptr_array_index(drives, i);
				if (!drive->portable)
					continue;
				drives_popover_attach_drive(live, drive, portable_pos++,
					normal_rows + 1);
			}
			portable_rows = (portable_count + 3) / 4;
			rows = normal_rows + 1 + portable_rows;
		}

		if (image_count > 0)
		{
			GtkWidget *heading = gtk_label_new(NULL);
			gchar *markup = g_markup_printf_escaped("<b>%s</b>",
				_("Mounted Images"));
			gtk_label_set_markup(GTK_LABEL(heading), markup);
			gtk_label_set_xalign(GTK_LABEL(heading), 0.0);
			gtk_widget_set_margin_top(heading, (normal_count || portable_count) ? 8 : 0);
			gtk_widget_set_margin_bottom(heading, 2);
			gtk_grid_attach(GTK_GRID(live->grid), heading,
				0, (gint) (normal_rows + (portable_count ? 1 + portable_rows : 0)), 4, 1);
			g_free(markup);

			for (i = 0; i < drives->len; i++)
			{
				DriveInfo *drive = g_ptr_array_index(drives, i);
				if (!drive->managed_image)
					continue;
				drives_popover_attach_drive(live, drive, image_pos++,
					normal_rows + (portable_count ? 1 + portable_rows : 0) + 1);
			}
			rows = normal_rows + (portable_count ? 1 + portable_rows : 0) +
				1 + (image_count + 3) / 4;
		}
	}

	content_height = MIN(430, MAX(120, (gint) rows * 104 + 8));
	gtk_widget_set_size_request(live->scrolled, 584, content_height);
	gtk_widget_show_all(live->grid);
}

static void drives_popover_monitor_changed(GPtrArray *drives,
		const GError *error, gpointer user_data)
{
	DrivePopoverLive *live = user_data;

	if (!live || !GTK_IS_WIDGET(live->popover) ||
	    !gtk_widget_get_parent(live->popover))
		return;
	/* Preserve the current list on a transient scan failure. */
	if (!drives && error)
		return;
	drives_popover_fill(live, drives, error);
}

static void drives_button_clicked(GtkToolButton *button, gpointer data)
{
	FilerWindow *filer_window = data;
	GtkWidget *popover;
	GtkWidget *outer;
	GtkWidget *title;
	GtkWidget *show_system;
	GtkWidget *scrolled;
	GtkWidget *grid;
	GPtrArray *drives;
	DrivePopoverLive *live;

	/* Agregado por josejp2424 (2026): GtkPopover con GtkGrid de cuatro
	 * columnas. La quinta unidad comienza una nueva fila y así sucesivamente. */
	popover = gtk_popover_new(GTK_WIDGET(button));
	gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
	gtk_popover_set_modal(GTK_POPOVER(popover), TRUE);
	g_signal_connect(popover, "closed",
		G_CALLBACK(destroy_popover_when_closed), NULL);

	outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_widget_set_margin_start(outer, 10);
	gtk_widget_set_margin_end(outer, 10);
	gtk_widget_set_margin_top(outer, 10);
	gtk_widget_set_margin_bottom(outer, 10);
	gtk_container_add(GTK_CONTAINER(popover), outer);

	title = gtk_label_new(NULL);
	{
		gchar *title_markup = g_markup_printf_escaped("<b>%s</b>",
			_("Partitions"));
		gtk_label_set_markup(GTK_LABEL(title), title_markup);
		g_free(title_markup);
	}
	gtk_label_set_xalign(GTK_LABEL(title), 0.0);
	gtk_box_pack_start(GTK_BOX(outer), title, FALSE, FALSE, 0);

	drive_visibility_load();
	show_system = gtk_check_button_new_with_label(_("Show boot/system partitions"));
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(show_system), g_atomic_int_get(&show_system_partitions));
	{
		DriveVisibilityToggle *toggle = g_new0(DriveVisibilityToggle, 1);
		toggle->button = g_object_ref(button);
		toggle->popover = popover;
		toggle->filer_window = filer_window;
		g_object_set_data_full(G_OBJECT(show_system), "rox-drive-visibility-toggle",
			toggle, drive_visibility_toggle_free);
		g_signal_connect(show_system, "toggled",
			G_CALLBACK(drive_visibility_toggled), toggle);
	}
	gtk_widget_set_tooltip_text(show_system,
		_("Boot and EFI partitions are hidden by default for safety."));
	gtk_box_pack_start(GTK_BOX(outer), show_system, FALSE, FALSE, 0);

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(outer), scrolled, TRUE, TRUE, 0);

	grid = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
	gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
	gtk_container_add(GTK_CONTAINER(scrolled), grid);

	live = g_new0(DrivePopoverLive, 1);
	live->filer_window = filer_window;
	live->popover = popover;
	live->grid = grid;
	live->scrolled = scrolled;
	g_object_set_data_full(G_OBJECT(popover), "rox-drives-popover-live", live, g_free);

	drives = rox_drives_monitor_snapshot_copy();
	drives_popover_fill(live, drives, NULL);
	if (drives)
		g_ptr_array_free(drives, TRUE);

	/* Classic is a live subscriber while its popover exists, exactly like
	 * Desktop and Modern.  The weak owner removes it automatically on close. */
	rox_drives_monitor_subscribe(G_OBJECT(popover),
		drives_popover_monitor_changed, live);

	gtk_widget_show_all(popover);
	gtk_popover_popup(GTK_POPOVER(popover));
}

typedef enum
{
	DRIVE_ASYNC_UNMOUNT,
	DRIVE_ASYNC_EJECT
} DriveAsyncAction;

typedef struct
{
	RoxDriveInfo *drive;
	DriveAsyncAction action;
} DriveAsyncJob;

typedef struct
{
	gboolean ok;
	gchar *error_text;
} DriveAsyncResult;

static void drive_async_job_free(gpointer data)
{
	DriveAsyncJob *job = data;
	if (!job)
		return;
	rox_drive_info_free(job->drive);
	g_free(job);
}

static void drive_async_result_free(gpointer data)
{
	DriveAsyncResult *result = data;
	if (!result)
		return;
	g_free(result->error_text);
	g_free(result);
}

static void drive_async_task(GTask *task, gpointer source_object,
		gpointer task_data, GCancellable *cancellable)
{
	DriveAsyncJob *job = task_data;
	DriveAsyncResult *result = g_new0(DriveAsyncResult, 1);
	(void) source_object;
	(void) cancellable;

	if (job->action == DRIVE_ASYNC_EJECT)
		result->ok = eject_drive(job->drive, &result->error_text);
	else
		result->ok = unmount_drive(job->drive, &result->error_text);
	g_task_return_pointer(task, result, drive_async_result_free);
}

static void drive_async_start(const RoxDriveInfo *drive, DriveAsyncAction action,
		GAsyncReadyCallback callback, gpointer user_data)
{
	DriveAsyncJob *job;
	GTask *task;

	g_return_if_fail(drive != NULL);
	job = g_new0(DriveAsyncJob, 1);
	job->drive = rox_drive_info_copy(drive);
	job->action = action;
	task = g_task_new(NULL, NULL, callback, user_data);
	g_task_set_task_data(task, job, drive_async_job_free);
	g_task_run_in_thread(task, drive_async_task);
	g_object_unref(task);
}

static gboolean drive_async_finish(GAsyncResult *result, gchar **error_text)
{
	DriveAsyncResult *action_result;
	gboolean ok;

	if (error_text)
		*error_text = NULL;
	g_return_val_if_fail(G_IS_TASK(result), FALSE);
	action_result = g_task_propagate_pointer(G_TASK(result), NULL);
	if (!action_result)
		return FALSE;
	ok = action_result->ok;
	if (error_text)
		*error_text = g_steal_pointer(&action_result->error_text);
	drive_async_result_free(action_result);
	return ok;
}

/* 2.13.0-8: PMADAS inspired the user-facing behaviour, but all probing and
 * mounting use ROX-Filer2's existing drive backend.  No Startup script,
 * probepart database or second configuration file is introduced. */
static gboolean startup_auto_mount_lock_acquire(void)
{
	const gchar *runtime = g_getenv("XDG_RUNTIME_DIR");
	gchar *path;

	if (startup_automount_lock_fd >= 0)
		return TRUE;
	if (!runtime || !*runtime || !g_file_test(runtime, G_FILE_TEST_IS_DIR))
		runtime = g_get_tmp_dir();
	path = g_strdup_printf("%s/rox-filer2-automount-%lu.lock", runtime,
		(unsigned long) geteuid());
	startup_automount_lock_fd = g_open(path, O_CREAT | O_RDWR, 0600);
	g_free(path);
	if (startup_automount_lock_fd < 0)
		return FALSE;
	if (flock(startup_automount_lock_fd, LOCK_EX | LOCK_NB) != 0)
	{
		close(startup_automount_lock_fd);
		startup_automount_lock_fd = -1;
		return FALSE;
	}
	return TRUE;
}

static void startup_auto_mount_lock_release(void)
{
	if (startup_automount_lock_fd < 0)
		return;
	(void) flock(startup_automount_lock_fd, LOCK_UN);
	close(startup_automount_lock_fd);
	startup_automount_lock_fd = -1;
}

typedef struct
{
	GPtrArray *queue;
	guint index;
} StartupAutoMount;

static void startup_auto_mount_free(StartupAutoMount *state)
{
	if (!state)
		return;
	if (state->queue)
		g_ptr_array_unref(state->queue);
	g_free(state);
	startup_auto_mount_lock_release();
}

static gboolean drive_auto_mount_candidate(const DriveInfo *drive,
		gboolean include_removable)
{
	gchar *mounted;
	gboolean ok;

	if (!drive || !drive->device || !g_str_has_prefix(drive->device, "/dev/"))
		return FALSE;
	/* MTP/PTP/iOS, network resources and mounted images keep their own
	 * explicit lifecycle and are never part of disk automount. */
	if (drive->portable || drive->network || drive->managed_image || drive->foreign)
		return FALSE;
	if (drive->system_partition)
		return FALSE;
	/* Avoid waking optical drives merely because ROX started. */
	if (drive->optical)
		return FALSE;
	if (!include_removable && (drive->removable || drive->hardware_removable))
		return FALSE;
	if (!drive->fstype || !*drive->fstype)
		return FALSE;
	/* Never automount boot/technical/encrypted-container filesystems.  The
	 * normal list already hides EFI by default; the textual guard remains so
	 * making system partitions visible cannot turn them into automount targets. */
	if (!g_ascii_strcasecmp(drive->fstype, "swap") ||
	    !g_ascii_strcasecmp(drive->fstype, "squashfs") ||
	    !g_ascii_strcasecmp(drive->fstype, "overlay") ||
	    !g_ascii_strcasecmp(drive->fstype, "aufs") ||
	    !g_ascii_strcasecmp(drive->fstype, "crypto_LUKS") ||
	    !g_ascii_strcasecmp(drive->fstype, "iso9660") ||
	    !g_ascii_strcasecmp(drive->fstype, "udf"))
		return FALSE;
	if (system_partition_text_match(drive->label) ||
	    technical_text_match(drive->label))
		return FALSE;

	mounted = drive_current_mountpoint(drive);
	ok = mounted == NULL;
	g_free(mounted);
	return ok;
}

static void startup_auto_mount_next(StartupAutoMount *state);

static void startup_auto_mount_done(GObject *source_object, GAsyncResult *result,
		gpointer user_data)
{
	StartupAutoMount *state = user_data;
	gchar *error_text = NULL;
	gchar *mountpoint;
	RoxDriveInfo *drive;
	(void) source_object;

	if (!state || !state->queue || state->index == 0 ||
	    state->index > state->queue->len)
	{
		startup_auto_mount_free(state);
		return;
	}
	drive = g_ptr_array_index(state->queue, state->index - 1);
	mountpoint = rox_drive_mount_finish(result, &error_text);
	if (!mountpoint)
		g_warning("Automatic mount of %s failed: %s",
			drive && drive->device ? drive->device : "drive",
			error_text ? error_text : "unknown error");
	g_free(mountpoint);
	g_free(error_text);
	startup_auto_mount_next(state);
}

static void startup_auto_mount_next(StartupAutoMount *state)
{
	RoxDriveInfo *drive;

	if (!state || !state->queue || state->index >= state->queue->len)
	{
		startup_auto_mount_free(state);
		rox_drives_monitor_request_scan();
		return;
	}
	drive = g_ptr_array_index(state->queue, state->index++);
	rox_drive_mount_async(drive, startup_auto_mount_done, state);
}

static void startup_auto_mount_scan_thread(GTask *task, gpointer source_object,
		gpointer task_data, GCancellable *cancellable)
{
	GError *error = NULL;
	GPtrArray *drives;
	(void) source_object;
	(void) task_data;
	(void) cancellable;

	drives = rox_drives_read(&error);
	if (!drives)
	{
		if (error)
			g_task_return_error(task, error);
		else
			g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED,
				"Unable to scan drives for startup automount");
		return;
	}
	g_task_return_pointer(task, drives, (GDestroyNotify) g_ptr_array_unref);
}

static void startup_auto_mount_scan_done(GObject *source_object,
		GAsyncResult *result, gpointer user_data)
{
	GPtrArray *drives;
	StartupAutoMount *state;
	GError *error = NULL;
	gboolean include_removable;
	guint i;
	(void) source_object;
	(void) user_data;

	drives = g_task_propagate_pointer(G_TASK(result), &error);
	if (!drives)
	{
		if (error)
		{
			g_warning("Unable to scan drives for startup automount: %s", error->message);
			g_clear_error(&error);
		}
		startup_auto_mount_lock_release();
		return;
	}
	if (!o_drives_mount_all_startup.int_value)
	{
		g_ptr_array_unref(drives);
		startup_auto_mount_lock_release();
		return;
	}

	include_removable = o_drives_mount_removable_startup.int_value != 0;
	state = g_new0(StartupAutoMount, 1);
	state->queue = g_ptr_array_new_with_free_func(rox_drive_info_free);
	for (i = 0; i < drives->len; i++)
	{
		RoxDriveInfo *drive = g_ptr_array_index(drives, i);
		if (drive_auto_mount_candidate(drive, include_removable))
			g_ptr_array_add(state->queue, rox_drive_info_copy(drive));
	}
	g_ptr_array_unref(drives);

	if (state->queue->len == 0)
	{
		startup_auto_mount_free(state);
		return;
	}
	/* Serial mounting avoids simultaneous pkexec dialogs in normal-user
	 * sessions and keeps startup behaviour predictable. */
	startup_auto_mount_next(state);
}

static gboolean startup_auto_mount_begin(gpointer data)
{
	GTask *task;
	(void) data;

	if (!o_drives_mount_all_startup.int_value)
		return G_SOURCE_REMOVE;
	/* Desktop and a normal filer may be started together.  Only one process
	 * performs startup automount; the lock is runtime-only and not a setting. */
	if (!startup_auto_mount_lock_acquire())
		return G_SOURCE_REMOVE;
	task = g_task_new(NULL, NULL, startup_auto_mount_scan_done, NULL);
	g_task_run_in_thread(task, startup_auto_mount_scan_thread);
	g_object_unref(task);
	return G_SOURCE_REMOVE;
}

void rox_drives_init(void)
{
	option_add_int(&o_drives_mount_all_startup,
		"drives_mount_all_startup", FALSE);
	option_add_int(&o_drives_mount_removable_startup,
		"drives_mount_removable_startup", FALSE);
}

void rox_drives_startup_auto_mount(void)
{
	if (startup_automount_scheduled || !o_drives_mount_all_startup.int_value)
		return;
	startup_automount_scheduled = TRUE;
	/* Let the first filer/desktop window become responsive before probing
	 * disks or presenting an authentication request. */
	g_timeout_add(750, startup_auto_mount_begin, NULL);
}

/* Agregado por josejp2424 (2026): API pública compartida con ROX Desktop. */
GPtrArray *rox_drives_read(GError **error)
{
	return read_drive_list(error);
}

RoxDriveInfo *rox_drive_info_copy(const RoxDriveInfo *source)
{
	return drive_info_copy(source);
}

gchar *rox_drive_current_mountpoint(const RoxDriveInfo *drive)
{
	return drive_current_mountpoint(drive);
}

gboolean rox_drive_can_eject(const RoxDriveInfo *drive)
{
	return drive && (drive->optical || drive->hardware_removable) &&
		drive->parent_device && g_str_has_prefix(drive->parent_device, "/dev/");
}

void rox_drive_unmount_async(const RoxDriveInfo *drive,
		GAsyncReadyCallback callback, gpointer user_data)
{
	drive_async_start(drive, DRIVE_ASYNC_UNMOUNT, callback, user_data);
}

gboolean rox_drive_unmount_finish(GAsyncResult *result, gchar **error_text)
{
	gboolean ok = drive_async_finish(result, error_text);
	if (ok)
		rox_drives_monitor_request_scan();
	return ok;
}

void rox_drive_eject_async(const RoxDriveInfo *drive,
		GAsyncReadyCallback callback, gpointer user_data)
{
	drive_async_start(drive, DRIVE_ASYNC_EJECT, callback, user_data);
}

gboolean rox_drive_eject_finish(GAsyncResult *result, gchar **error_text)
{
	gboolean ok = drive_async_finish(result, error_text);
	if (ok)
		rox_drives_monitor_request_scan();
	return ok;
}

/* Agregado por josejp2424: este botón no pertenece a la lista configurable,
 * por lo que siempre permanece visible mientras exista una barra de herramientas. */
GtkToolItem *drives_toolbar_button_new(FilerWindow *filer_window)
{
	GtkWidget *image;
	GtkToolItem *item;

	g_return_val_if_fail(filer_window != NULL, NULL);
	image = image_new_icon(DRIVE_ICON_INTERNAL, GTK_ICON_SIZE_LARGE_TOOLBAR);
	item = gtk_tool_button_new(image, _("Partitions"));
	/* Modificado por josejp2424 (2026): marcarlo como elemento importante
	 * y no homogéneo para que permanezca visible junto a Subir. */
	gtk_tool_item_set_is_important(item, TRUE);
	gtk_tool_item_set_homogeneous(item, FALSE);
	/* Modificado por josejp2424 (2026): reflejar las acciones completas
	 * disponibles desde el menú contextual de cada unidad. */
	gtk_tool_item_set_tooltip_text(item,
		_("Show partitions, mount, unmount and open them"));
	g_signal_connect(item, "clicked", G_CALLBACK(drives_button_clicked),
		filer_window);
	return item;
}
