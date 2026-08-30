/*
 * ROX-Filer GTK3 partition toolbar integration.
 *
 * Agregado por josejp2424 (2026): detección de particiones inspirada en la
 * integración de unidades de EssoraWM, con montaje directo para Puppy/root,
 * alternativa mediante udisksctl para usuarios normales y apertura de la
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
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gtk/gtk.h>

#include "global.h"
#include "support.h"
#include "drives.h"
#include "filer.h"
#include "gui_support.h"
#include "mount.h"
#include "rox_config.h"

#define DRIVE_ICON_INTERNAL  "drive-harddisk"

typedef RoxDriveInfo DriveInfo;

typedef struct
{
	FilerWindow *filer_window;
	DriveInfo *drive;
	GtkWidget *popover;
} DriveMenuAction;

/* Agregado por josejp2424 (2026): metadatos del dispositivo físico padre.
 * lsblk suele dejar TRAN/RM/MODEL vacíos en las líneas de particiones, aunque
 * estén presentes en la línea del disco. Se conservan aquí para heredarlos. */
typedef struct
{
	gchar *transport;
	gchar *model;
	gboolean removable;
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
	g_free(info);
}

static gchar *parse_lsblk_value(const gchar *line, const gchar *key);
static GPtrArray *read_drive_list(GError **error);
static gboolean drive_is_useful(const DriveInfo *drive, const gchar *type,
		const gchar *partlabel, const gchar *parttype);
static gboolean drive_array_has_device(GPtrArray *drives, const gchar *device);
static gboolean drive_is_hidden_by_essorawm(const gchar *name);
static gchar *command_first_line(gchar **argv);
static DriveInfo *drive_info_from_device(const gchar *device);
static void append_puppy_runtime_drives(GPtrArray *drives);
static void append_sysfs_partitions(GPtrArray *drives);
static gboolean technical_text_match(const gchar *value);
static gboolean name_looks_removable(const gchar *value);
static void drive_enrich_from_sysfs(DriveInfo *drive);
void rox_drive_info_free(gpointer data);
static gchar *find_mountpoint(const gchar *device);
static gboolean spawn_wait(gchar **argv, gchar **error_text);
static gchar *mount_drive(const DriveInfo *drive, gchar **error_text);
static gboolean unmount_drive(const DriveInfo *drive, gchar **error_text);
static gboolean eject_drive(const DriveInfo *drive, gchar **error_text);
static void drive_menu_action_free(gpointer data);
static void drive_grid_activate(GtkButton *button, gpointer data);
static gboolean drive_grid_button_press(GtkWidget *button,
		GdkEventButton *event, gpointer data);
static void drives_button_clicked(GtkToolButton *button, gpointer data);
static GtkWidget *drive_grid_button_new(const DriveInfo *drive);
static GtkWidget *drive_icon_widget(const DriveInfo *drive, gint size);

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
	start = strstr(line, pattern);
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

	path = g_build_filename("/sys/class/block", parent, "removable", NULL);
	value = read_sysfs_text(path);
	if (value)
		drive->removable = drive->removable || atoi(value) != 0;
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
		strstr(lower, "/boot/efi") || strstr(lower, "efi system") ||
		!strcmp(compact, "efi") || !strcmp(compact, "esp") ||
		!strcmp(compact, "efisystempartition") ||
		!strcmp(compact, "swap");

	g_free(compact);
	g_free(lower);
	return result;
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

	if (parttype &&
	    (!g_ascii_strcasecmp(parttype,
		"c12a7328-f81f-11d2-ba4b-00a0c93ec93b") ||
	     !g_ascii_strcasecmp(parttype, "ef00")))
		return FALSE;

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

/* Agregado por josejp2424 (2026): ejecutar una consulta pequeña y devolver
 * solamente la primera línea sin espacios finales. */
static gchar *command_first_line(gchar **argv)
{
	gchar *output = NULL;
	gchar *error_output = NULL;
	gint status = 0;
	GError *error = NULL;
	gchar *line;

	if (!rox_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
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
			drive->removable = drive->removable || atoi(value) != 0;
		g_free(value);
		g_free(removable_path);
	}

	if (!drive->label || !*drive->label)
	{
		g_free(drive->label);
		drive->label = drive->size && *drive->size
			? g_strdup_printf(_("Volume %s"), drive->size)
			: g_strdup(base);
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
	if (!rox_spawn_sync(NULL, argv_full, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
		&stdout_text, &stderr_text, &status, &spawn_error) ||
	    !WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		g_clear_error(&spawn_error);
		g_clear_pointer(&stdout_text, g_free);
		g_clear_pointer(&stderr_text, g_free);
		status = 0;
		if (!rox_spawn_sync(NULL, argv_compat, NULL, G_SPAWN_SEARCH_PATH,
			NULL, NULL, &stdout_text, &stderr_text, &status, &spawn_error))
		{
			/* Modificado por josejp2424 (2026): no abandonar la detección
			 * cuando lsblk no está disponible. Puppy y sysfs siguen siendo
			 * fuentes válidas para listar las unidades del escritorio. */
			g_clear_error(&spawn_error);
			g_free(stderr_text);
			append_puppy_runtime_drives(drives);
			append_sysfs_partitions(drives);
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
		rm = parse_lsblk_value(lines[i], "RM");
		hotplug = parse_lsblk_value(lines[i], "HOTPLUG");
		rota = parse_lsblk_value(lines[i], "ROTA");
		parent->removable = (rm && atoi(rm) != 0) ||
			(hotplug && atoi(hotplug) != 0) ||
			(parent->transport &&
			 (!g_ascii_strcasecmp(parent->transport, "usb") ||
			  !g_ascii_strcasecmp(parent->transport, "mmc") ||
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
			drive->solid_state = parent->solid_state;
			drive->optical = parent->optical;
		}
		if (rota && *rota)
			drive->solid_state = atoi(rota) == 0;

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

		if (drive_is_useful(drive, drive->type, partlabel, parttype))
		{
			if ((!drive->label || !*drive->label) && partlabel && *partlabel)
			{
				g_free(drive->label);
				drive->label = g_strdup(partlabel);
			}
			else if ((!drive->label || !*drive->label) &&
			         drive->size && *drive->size)
			{
				g_free(drive->label);
				drive->label = g_strdup_printf(_("Volume %s"), drive->size);
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

	/* Modificado por josejp2424 (2026): cuando lsblk funciona, conservar
	 * exclusivamente su lista filtrada con las mismas reglas de EssoraWM.
	 * Los respaldos de Puppy/sysfs sólo se usan si lsblk falla por completo;
	 * mezclarlos aquí agregaba particiones técnicas o sin uso visible. */
	return drives;
}

static gchar *find_mountpoint(const gchar *device)
{
	FILE *mounts;
	struct mntent *entry;
	gchar *device_real;
	gchar *result = NULL;

	if (!device || !*device)
		return NULL;

	device_real = realpath(device, NULL);
	mounts = setmntent("/proc/self/mounts", "r");
	if (!mounts)
	{
		free(device_real);
		return NULL;
	}

	while ((entry = getmntent(mounts)) != NULL)
	{
		gchar *entry_real = realpath(entry->mnt_fsname, NULL);
		gboolean same = !strcmp(entry->mnt_fsname, device) ||
			(device_real && entry_real && !strcmp(device_real, entry_real));
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

static gboolean spawn_wait(gchar **argv, gchar **error_text)
{
	gchar *stderr_text = NULL;
	gint status = 0;
	GError *error = NULL;
	gboolean ok;

	if (error_text)
		*error_text = NULL;

	ok = rox_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL,
		NULL, &stderr_text, &status, &error);
	if (!ok)
	{
		if (error_text)
			*error_text = g_strdup(error->message);
		g_error_free(error);
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

/* Agregado por josejp2424: Puppy/root monta directamente en /mnt/<dispositivo>;
 * otros usuarios utilizan udisksctl cuando está disponible. */
static gchar *mount_drive(const DriveInfo *drive, gchar **error_text)
{
	gchar *mountpoint;
	gchar *udisksctl;
	gchar *local_error = NULL;

	if (error_text)
		*error_text = NULL;
	if (!drive || !drive->device)
		return NULL;

	mountpoint = find_mountpoint(drive->device);
	if (mountpoint)
		return mountpoint;

	if (geteuid() == 0)
	{
		gchar *base = g_path_get_basename(drive->device);
		gchar *target = g_build_filename("/mnt", base, NULL);
		gboolean existed = g_file_test(target, G_FILE_TEST_IS_DIR);
		gchar *argv[] = {(gchar *) "/bin/mount", drive->device, target, NULL};

		g_free(base);
		if (g_mkdir_with_parents(target, 0755) == 0 &&
		    spawn_wait(argv, &local_error))
		{
			g_free(local_error);
			return target;
		}
		if (!existed)
			rmdir(target);
		g_free(target);
	}

	udisksctl = g_find_program_in_path("udisksctl");
	if (udisksctl)
	{
		gchar *argv[] = {udisksctl, (gchar *) "mount", (gchar *) "-b",
			drive->device, NULL};
		g_free(local_error);
		local_error = NULL;
		if (spawn_wait(argv, &local_error))
		{
			g_free(udisksctl);
			g_free(local_error);
			return find_mountpoint(drive->device);
		}
		g_free(udisksctl);
	}

	if (error_text)
		*error_text = local_error ? local_error :
			g_strdup_printf(_("Could not mount '%s'."), drive->device);
	else
		g_free(local_error);
	return NULL;
}

/* Agregado por josejp2424 (2026): desmontaje integrado. Puppy ejecuta
 * umount directamente como root; los usuarios normales utilizan udisksctl
 * cuando está disponible. */
static gboolean unmount_drive(const DriveInfo *drive, gchar **error_text)
{
	gchar *mountpoint;
	gchar *udisksctl;
	gchar *local_error = NULL;
	gboolean ok = FALSE;

	if (error_text)
		*error_text = NULL;
	if (!drive || !drive->device)
		return FALSE;

	mountpoint = find_mountpoint(drive->device);
	if (!mountpoint)
		return TRUE;

	if (geteuid() == 0)
	{
		gchar *argv[] = {(gchar *) "/bin/umount", mountpoint, NULL};
		ok = spawn_wait(argv, &local_error);
	}

	if (!ok)
	{
		udisksctl = g_find_program_in_path("udisksctl");
		if (udisksctl)
		{
			gchar *argv[] = {udisksctl, (gchar *) "unmount",
				(gchar *) "-b", drive->device, NULL};
			g_free(local_error);
			local_error = NULL;
			ok = spawn_wait(argv, &local_error);
			g_free(udisksctl);
		}
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

/* Agregado por josejp2424 (2026): obtener el dispositivo físico padre de
 * una partición para poder expulsar o apagar de forma segura el medio. */
static gchar *drive_parent_device(const DriveInfo *drive)
{
	gchar *lsblk;
	gchar *parent = NULL;

	if (!drive || !drive->device)
		return NULL;
	lsblk = g_find_program_in_path("lsblk");
	if (lsblk)
	{
		gchar *argv[] = {lsblk, (gchar *) "-ndo", (gchar *) "PKNAME",
			drive->device, NULL};
		gchar *name = command_first_line(argv);
		if (name && *name)
			parent = g_build_filename("/dev", name, NULL);
		g_free(name);
		g_free(lsblk);
	}
	return parent ? parent : g_strdup(drive->device);
}

/* Agregado por josejp2424 (2026): expulsión segura para medios extraíbles.
 * Primero desmonta el volumen y luego usa udisksctl o eject como respaldo. */
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

	device = drive_parent_device(drive);
	program = g_find_program_in_path("udisksctl");
	if (program)
	{
		gchar *argv[] = {program, (gchar *) "power-off", (gchar *) "-b",
			device, NULL};
		ok = spawn_wait(argv, &local_error);
		g_free(program);
	}

	if (!ok)
	{
		program = g_find_program_in_path("eject");
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
	copy->removable = source->removable;
	copy->optical = source->optical;
	copy->network = source->network;
	copy->solid_state = source->solid_state;
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

static void drive_menu_mount(GtkMenuItem *item, gpointer data)
{
	DriveMenuAction *action = data;
	GtkWidget *popover;
	gchar *mountpoint;
	gchar *error_text = NULL;

	(void) item;
	if (!action || !action->drive)
		return;
	popover = drive_action_ref_popover(action);
	mountpoint = mount_drive(action->drive, &error_text);
	if (!mountpoint)
	{
		report_error("%s", error_text ? error_text :
			_("The partition could not be mounted."));
		g_free(error_text);
		drive_action_finish_popover(popover, FALSE);
		return;
	}
	g_free(mountpoint);
	g_free(error_text);
	/* Modificado por josejp2424 (2026): actualizar una sola vez al finalizar. */
	mount_update(TRUE);
	filer_update_all();
	drive_action_finish_popover(popover, TRUE);
}

static void drive_menu_unmount(GtkMenuItem *item, gpointer data)
{
	DriveMenuAction *action = data;
	GtkWidget *popover;
	gchar *error_text = NULL;

	(void) item;
	if (!action || !action->drive)
		return;
	popover = drive_action_ref_popover(action);
	if (!unmount_drive(action->drive, &error_text))
	{
		report_error("%s", error_text ? error_text :
			_("The partition could not be unmounted."));
		g_free(error_text);
		drive_action_finish_popover(popover, FALSE);
		return;
	}
	g_free(error_text);
	/* Modificado por josejp2424 (2026): actualizar vistas y montajes sólo
	 * después de que el comando haya terminado correctamente. */
	mount_update(TRUE);
	filer_update_all();
	drive_action_finish_popover(popover, TRUE);
}

static void drive_menu_eject(GtkMenuItem *item, gpointer data)
{
	DriveMenuAction *action = data;
	GtkWidget *popover;
	gchar *error_text = NULL;

	(void) item;
	if (!action || !action->drive)
		return;
	popover = drive_action_ref_popover(action);
	if (!eject_drive(action->drive, &error_text))
	{
		report_error("%s", error_text ? error_text :
			_("The device could not be ejected."));
		g_free(error_text);
		drive_action_finish_popover(popover, FALSE);
		return;
	}
	g_free(error_text);
	mount_update(TRUE);
	filer_update_all();
	drive_action_finish_popover(popover, TRUE);
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

	mountpoint = find_mountpoint(action->drive->device);
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

	gtk_widget_set_sensitive(mount_item, !mounted);
	gtk_widget_set_sensitive(unmount_item, mounted);

	gtk_menu_shell_append(GTK_MENU_SHELL(menu), open_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), mount_item);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), unmount_item);

	/* Modificado por josejp2424 (2026): ocultar Eject por completo para
	 * discos y memorias USB. La acción sólo tiene sentido en medios ópticos. */
	if (action->drive->optical)
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
	GtkWidget *popover;
	gchar *mountpoint;
	gchar *error_text = NULL;

	(void) button;
	if (!action || !action->drive || !action->filer_window ||
	    !filer_exists(action->filer_window))
		return;

	popover = action->popover;
	mountpoint = find_mountpoint(action->drive->device);
	if (!mountpoint && action->drive->mountpoint &&
	    *action->drive->mountpoint &&
	    g_file_test(action->drive->mountpoint, G_FILE_TEST_IS_DIR))
		mountpoint = g_strdup(action->drive->mountpoint);

	if (!mountpoint)
		mountpoint = mount_drive(action->drive, &error_text);

	if (!mountpoint)
	{
		report_error("%s", error_text ? error_text :
			_("The partition could not be mounted."));
		g_free(error_text);
		return;
	}

	if (!g_file_test(mountpoint, G_FILE_TEST_IS_DIR))
	{
		report_error(_("The partition '%s' was mounted, but its mount point could not be found."),
			action->drive->device);
		g_free(mountpoint);
		g_free(error_text);
		return;
	}

	{
		FilerWindow *target_window = action->filer_window;

		/* Modificado por josejp2424 (2026): cerrar primero la cuadrícula.
		 * Al destruirla se libera la acción del botón, por eso conservamos
		 * previamente el puntero de la ventana que debe abrir la unidad. */
		if (GTK_IS_WIDGET(popover))
			gtk_widget_destroy(popover);
		filer_change_to(target_window, mountpoint, NULL);
	}
	g_free(mountpoint);
	g_free(error_text);
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
	ROX_DRIVE_ICON_FLOPPY,
	ROX_DRIVE_ICON_NETWORK
} RoxDriveIconKind;

static RoxDriveIconKind drive_icon_kind(const RoxDriveInfo *drive)
{
	gchar *lower_model = NULL;
	gboolean solid_state = FALSE;

	if (!drive)
		return ROX_DRIVE_ICON_INTERNAL;
	if (drive->network)
		return ROX_DRIVE_ICON_NETWORK;
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
	static const gchar *floppy[] = {
		"media-floppy", "drive-floppy", NULL
	};
	static const gchar *network[] = {
		"drive-network", "network-server", "folder-remote", NULL
	};
	static const gchar *internal[] = {
		"drive-harddisk", "drive-harddisk-system", NULL
	};

	switch (kind)
	{
		case ROX_DRIVE_ICON_OPTICAL: return optical;
		case ROX_DRIVE_ICON_USB: return usb;
		case ROX_DRIVE_ICON_SD: return sd;
		case ROX_DRIVE_ICON_SSD: return ssd;
		case ROX_DRIVE_ICON_FLOPPY: return floppy;
		case ROX_DRIVE_ICON_NETWORK: return network;
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

	mountpoint = find_mountpoint(drive->device);
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
	gtk_widget_set_tooltip_text(button, drive->device);
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

static void drives_button_clicked(GtkToolButton *button, gpointer data)
{
	FilerWindow *filer_window = data;
	GtkWidget *popover;
	GtkWidget *outer;
	GtkWidget *title;
	GtkWidget *scrolled;
	GtkWidget *grid;
	GPtrArray *drives;
	GError *error = NULL;
	guint i;
	guint rows;
	gint content_height;

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

	scrolled = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(outer), scrolled, TRUE, TRUE, 0);

	grid = gtk_grid_new();
	gtk_grid_set_column_spacing(GTK_GRID(grid), 6);
	gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
	gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
	gtk_container_add(GTK_CONTAINER(scrolled), grid);

	drives = read_drive_list(&error);
	if (error)
	{
		GtkWidget *message = gtk_label_new(error->message);
		gtk_label_set_line_wrap(GTK_LABEL(message), TRUE);
		gtk_grid_attach(GTK_GRID(grid), message, 0, 0, 4, 1);
		g_error_free(error);
	}
	else if (drives->len == 0)
	{
		GtkWidget *message = gtk_label_new(_("No usable partitions found"));
		gtk_grid_attach(GTK_GRID(grid), message, 0, 0, 4, 1);
	}
	else
	{
		for (i = 0; i < drives->len; i++)
		{
			DriveInfo *drive = g_ptr_array_index(drives, i);
			DriveMenuAction *action = g_new0(DriveMenuAction, 1);
			GtkWidget *drive_button = drive_grid_button_new(drive);

			action->filer_window = filer_window;
			action->drive = drive_info_copy(drive);
			action->popover = popover;
			g_signal_connect(drive_button, "clicked",
				G_CALLBACK(drive_grid_activate), action);
			/* Agregado por josejp2424 (2026): clic derecho para el menú
			 * tradicional Abrir/Montar/Desmontar/Expulsar. */
			gtk_widget_add_events(drive_button, GDK_BUTTON_PRESS_MASK);
			g_signal_connect(drive_button, "button-press-event",
				G_CALLBACK(drive_grid_button_press), action);
			gtk_widget_set_tooltip_text(drive_button,
				_("Left click: open or mount\nRight click: drive actions"));
			g_object_set_data_full(G_OBJECT(drive_button), "rox-drive-action",
				action, drive_menu_action_free);
			gtk_grid_attach(GTK_GRID(grid), drive_button,
				(gint) (i % 4), (gint) (i / 4), 1, 1);
		}
	}

	rows = drives->len > 0 ? (drives->len + 3) / 4 : 1;
	content_height = MIN(360, MAX(120, (gint) rows * 104 + 8));
	gtk_widget_set_size_request(scrolled, 584, content_height);
	g_ptr_array_free(drives, TRUE);

	gtk_widget_show_all(popover);
	gtk_popover_popup(GTK_POPOVER(popover));
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

RoxDriveInfo *rox_drive_find_by_device(const gchar *device, GError **error)
{
	GPtrArray *drives;
	RoxDriveInfo *result = NULL;
	guint i;

	if (!device)
		return NULL;
	drives = read_drive_list(error);
	for (i = 0; drives && i < drives->len; i++)
	{
		RoxDriveInfo *drive = g_ptr_array_index(drives, i);
		if (g_strcmp0(drive->device, device) == 0)
		{
			result = drive_info_copy(drive);
			break;
		}
	}
	if (drives)
		g_ptr_array_free(drives, TRUE);
	return result;
}

gchar *rox_drive_find_mountpoint(const gchar *device)
{
	return find_mountpoint(device);
}

gchar *rox_drive_mount(const RoxDriveInfo *drive, gchar **error_text)
{
	return mount_drive(drive, error_text);
}

gboolean rox_drive_unmount(const RoxDriveInfo *drive, gchar **error_text)
{
	return unmount_drive(drive, error_text);
}

gboolean rox_drive_eject(const RoxDriveInfo *drive, gchar **error_text)
{
	return eject_drive(drive, error_text);
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
