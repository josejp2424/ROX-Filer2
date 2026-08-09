/*
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2006, Thomas Leonard and others (see changelog for details).
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* type.c - code for dealing with filetypes */

/*
 * Modificado por josejp2424 (2026):
 * port a GTK3 y adaptaciones específicas de esta versión.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <sys/param.h>
#include <fnmatch.h>
#include <sys/types.h>
#include <fcntl.h>
#include <gio/gio.h>

#ifdef WITH_GNOMEVFS
# include <libgnomevfs/gnome-vfs.h>
# include <libgnomevfs/gnome-vfs-mime.h>
# include <libgnomevfs/gnome-vfs-mime-handlers.h>
# include <libgnomevfs/gnome-vfs-application-registry.h>
#endif

#include "global.h"

#include "string.h"
#include "fscache.h"
#include "main.h"
#include "pixmaps.h"
#include "run.h"
#include "gui_support.h"
#include "choices.h"
#include "type.h"
#include "support.h"
#include "diritem.h"
#include "dnd.h"
#include "options.h"
#include "filer.h"
#include "action.h"		/* (for action_chmod) */
#include "xml.h"
#include "dropbox.h"
#include "xdgmime.h"
#include "xtypes.h"
#include "run.h"
#include "debug_log.h"

#define TYPE_NS "http://www.freedesktop.org/standards/shared-mime-info"

/* File and directory text colours are provided by the active GTK theme.
 * ROX no longer keeps private per-file-type colour options. */

/* Static prototypes */
static void options_changed(void);
static MIME_type *get_mime_type(const gchar *type_name, gboolean can_create);
static void set_icon_theme(void);
static GList *build_icon_theme(Option *option, xmlNode *node, guchar *label);
/* Agregado por josejp2424 (2026): mantener los iconos de carpetas normales
 * sincronizados con el tema de iconos GTK3 activo. */
static GdkPixbuf *load_system_folder_icon(void);
static GtkIconInfo *mime_type_lookup_system_icon_info(MIME_type *type,
		gchar **resolved_name);
static void log_system_icon_theme(void);
static void system_icon_theme_changed(GtkIconTheme *theme, gpointer data);

/* Hash of all allocated MIME types, indexed by "media/subtype".
 * MIME_type structs are never freed; this table prevents memory leaks
 * when rereading the config files.
 */
static GHashTable *type_hash = NULL;

/* Most things on Unix are text files, so this is the default type */
MIME_type *text_plain;
MIME_type *inode_directory;
MIME_type *inode_mountpoint;
MIME_type *inode_pipe;
MIME_type *inode_socket;
MIME_type *inode_block_dev;
MIME_type *inode_char_dev;
MIME_type *application_executable;
MIME_type *application_octet_stream;
MIME_type *application_x_shellscript;
MIME_type *application_x_desktop;
MIME_type *inode_unknown;
MIME_type *inode_door;

static Option o_icon_theme;

static GtkIconTheme *icon_theme = NULL;
static GtkIconTheme *rox_theme = NULL;
static GtkIconTheme *gnome_theme = NULL;

void type_init(void)
{

	icon_theme = gtk_icon_theme_new();

	type_hash = g_hash_table_new(g_str_hash, g_str_equal);

	/* Agregado por josejp2424 (2026): observar el tema GTK3 del sistema.
	 * Al cambiar el tema se invalidan los iconos MIME almacenados para que
	 * las carpetas adopten inmediatamente el nuevo icono `folder`. */
	if (gtk_icon_theme_get_default())
		g_signal_connect(gtk_icon_theme_get_default(), "changed",
			G_CALLBACK(system_icon_theme_changed), NULL);
	log_system_icon_theme();

	text_plain = get_mime_type("text/plain", TRUE);
	inode_directory = get_mime_type("inode/directory", TRUE);
	inode_mountpoint = get_mime_type("inode/mount-point", TRUE);
	inode_pipe = get_mime_type("inode/fifo", TRUE);
	inode_socket = get_mime_type("inode/socket", TRUE);
	inode_block_dev = get_mime_type("inode/blockdevice", TRUE);
	inode_char_dev = get_mime_type("inode/chardevice", TRUE);
	application_executable = get_mime_type("application/x-executable", TRUE);
	application_octet_stream = get_mime_type("application/octet-stream", TRUE);
	application_x_shellscript = get_mime_type("application/x-shellscript", TRUE);
	application_x_desktop = get_mime_type("application/x-desktop", TRUE);
	application_x_desktop->executable = TRUE;
	inode_unknown = get_mime_type("inode/unknown", TRUE);
	inode_door = get_mime_type("inode/door", TRUE);

	option_add_string(&o_icon_theme, "icon_theme", "ROX");
	option_register_widget("icon-theme-chooser", build_icon_theme);


	set_icon_theme();

	option_add_notify(options_changed);
}

/* Read-load all the glob patterns.
 * Note: calls filer_update_all.
 */
void reread_mime_files(void)
{
	gtk_icon_theme_rescan_if_needed(icon_theme);

	xdg_mime_shutdown();

	filer_update_all();
}

/* Returns the MIME_type structure for the given type name. It is looked
 * up in type_hash and returned if found. If not found (and can_create is
 * TRUE) then a new MIME_type is made, added to type_hash and returned.
 * NULL is returned if type_name is not in type_hash and can_create is
 * FALSE, or if type_name does not contain a '/' character.
 */
static MIME_type *get_mime_type(const gchar *type_name, gboolean can_create)
{
        MIME_type *mtype;
	gchar *slash;

	mtype = g_hash_table_lookup(type_hash, type_name);
	if (mtype || !can_create)
		return mtype;

	slash = strchr(type_name, '/');
	if (slash == NULL)
	{
		g_warning("MIME type '%s' does not contain a '/' character!",
			  type_name);
		return NULL;
	}

	mtype = g_new(MIME_type, 1);
	mtype->media_type = g_strndup(type_name, slash - type_name);
	mtype->subtype = g_strdup(slash + 1);
	mtype->image = NULL;
	mtype->comment = NULL;

	mtype->executable = xdg_mime_mime_type_subclass(type_name,
						"application/x-executable");

	g_hash_table_insert(type_hash, g_strdup(type_name), mtype);

	return mtype;
}

const char *basetype_name(DirItem *item)
{
	if (item->flags & ITEM_FLAG_SYMLINK)
		return _("Sym link");
	else if (item->flags & ITEM_FLAG_MOUNT_POINT)
		return _("Mount point");
	else if (item->flags & ITEM_FLAG_APPDIR)
		return _("App dir");

	switch (item->base_type)
	{
		case TYPE_FILE:
			return _("File");
		case TYPE_DIRECTORY:
			return _("Dir");
		case TYPE_CHAR_DEVICE:
			return _("Char dev");
		case TYPE_BLOCK_DEVICE:
			return _("Block dev");
		case TYPE_PIPE:
			return _("Pipe");
		case TYPE_SOCKET:
			return _("Socket");
		case TYPE_DOOR:
			return _("Door");
	}

	return _("Unknown");
}

struct mime_list {
	GList *list;
	gboolean only_regular;
};

static void append_names(gpointer key, gpointer value, gpointer udata)
{
	struct mime_list *mlist = (struct mime_list*) udata;

	if(!mlist->only_regular || strncmp((char *)key, "inode/", 6)!=0)
		mlist->list = g_list_prepend(mlist->list, key);
}

/* Return list of all mime type names. Caller must free the list
 * but NOT the strings it contains (which are never freed).
 If only_regular is true then inode types are excluded.
 */
GList *mime_type_name_list(gboolean only_regular)
{
	struct mime_list list;

	list.list=NULL;
	list.only_regular=only_regular;

	g_hash_table_foreach(type_hash, append_names, &list);
	list.list = g_list_sort(list.list, (GCompareFunc) strcmp);

	return list.list;
}

/*			MIME-type guessing 			*/

/* Get the type of this file - stats the file and uses that if
 * possible. For regular or missing files, uses the pathname.
 */
MIME_type *type_get_type(const guchar *path)
{
	DirItem		*item;
	MIME_type	*type = NULL;

	item = diritem_new("");
	diritem_restat(path, item, NULL);
	if (item->base_type != TYPE_ERROR)
		type = item->mime_type;
	diritem_free(item);

	if (type)
		return type;

	type = type_from_path(path);

	if (!type)
		return text_plain;

	return type;
}

/* Returns a pointer to the MIME-type.
 *
 * Tries all enabled methods:
 * - Look for extended attribute
 * - If no attribute, check file name
 * - If no name rule, check contents
 *
 * NULL if we can't think of anything.
 */
MIME_type *type_from_path(const char *path)
{
	MIME_type *mime_type = NULL;
	const char *type_name;

	/* Check for extended attribute first */
	mime_type = xtype_get(path);
	if (mime_type)
		return mime_type;

	/* Try name and contents next */
	type_name = xdg_mime_get_mime_type_for_file(path, NULL);
	if (type_name)
		return get_mime_type(type_name, TRUE);

	return NULL;
}

/* Return the default application selected by the standard XDG/GIO stack.
 * The caller owns the returned reference. Parent MIME types are considered
 * only when the exact type has no configured application. */
GAppInfo *type_get_default_application(MIME_type *type)
{
	GAppInfo *app;
	gchar *type_name;
	gchar **parents;
	gchar **parent;

	g_return_val_if_fail(type != NULL, NULL);

	/* GIO reads $XDG_CONFIG_HOME/mimeapps.list (normally
	 * ~/.config/mimeapps.list), the system XDG mimeapps.list files and the
	 * associations declared by installed .desktop files. */
	type_name = g_strconcat(type->media_type, "/", type->subtype, NULL);
	app = g_app_info_get_default_for_type(type_name, FALSE);
	if (app) {
		g_free(type_name);
		return app;
	}

	parents = xdg_mime_list_mime_parents(type_name);
	g_free(type_name);
	if (!parents)
		return NULL;

	for (parent = parents; *parent; parent++) {
		app = g_app_info_get_default_for_type(*parent, FALSE);
		if (app)
			break;
	}
	free(parents);
	return app;
}


MIME_type *mime_type_lookup(const char *type)
{
	return get_mime_type(type, TRUE);
}

static void init_aux_theme(GtkIconTheme **ptheme, const char *name)
{
	if (*ptheme)
		return;
	*ptheme = gtk_icon_theme_new();
	gtk_icon_theme_set_custom_theme(*ptheme, name);
}

inline static void init_rox_theme(void)
{
	init_aux_theme(&rox_theme, "ROX");
}

inline static void init_gnome_theme(void)
{
	init_aux_theme(&gnome_theme, "gnome");
}

/* r75: registrar el tema real que GTK está usando. El antiguo selector de
 * temas MIME de ROX puede seguir existiendo como compatibilidad, pero los
 * iconos Freedesktop se deben resolver contra el tema activo del sistema. */
static void log_system_icon_theme(void)
{
	GtkSettings *settings = gtk_settings_get_default();
	GtkIconTheme *theme = gtk_icon_theme_get_default();
	gchar *name = NULL;
	gchar **paths = NULL;
	gint n_paths = 0;
	gint i;

	if (settings)
		g_object_get(settings, "gtk-icon-theme-name", &name, NULL);
	ROX_LOG_INFO("icon-theme", "gtk-icon-theme-name=%s",
			name ? name : "(unset)");
	g_free(name);

	if (!theme)
	{
		ROX_LOG_WARNING("icon-theme", "GTK did not provide a default icon theme");
		return;
	}

	gtk_icon_theme_get_search_path(theme, &paths, &n_paths);
	for (i = 0; i < n_paths; i++)
		ROX_LOG_DEBUG("icon-theme", "search-path[%d]=%s", i, paths[i]);
	g_strfreev(paths);
}

/* Resolve a MIME icon using the active GTK/Freedesktop icon theme. GIO
 * supplies an ordered GThemedIcon list (for example text-plain followed by
 * text-x-generic), which is much more complete than the historical ROX MIME
 * theme. */
static GtkIconInfo *mime_type_lookup_system_icon_info(MIME_type *type,
		gchar **resolved_name)
{
	GtkIconTheme *theme = gtk_icon_theme_get_default();
	GtkIconInfo *info = NULL;
	GIcon *gicon = NULL;
	gchar *content_type;
	gchar *generic = NULL;

	if (resolved_name)
		*resolved_name = NULL;
	if (!theme || !type)
		return NULL;

	content_type = g_strconcat(type->media_type, "/", type->subtype, NULL);
	gicon = g_content_type_get_icon(content_type);
	if (gicon)
	{
		if (resolved_name && G_IS_THEMED_ICON(gicon))
		{
			const gchar * const *names = g_themed_icon_get_names(G_THEMED_ICON(gicon));
			if (names && names[0])
				*resolved_name = g_strdup(names[0]);
		}
		info = gtk_icon_theme_lookup_by_gicon(theme, gicon, HUGE_HEIGHT,
				GTK_ICON_LOOKUP_FORCE_SIZE);
		g_object_unref(gicon);
	}

	if (!info)
	{
		generic = g_content_type_get_generic_icon_name(content_type);
		if (generic)
		{
			info = gtk_icon_theme_lookup_icon(theme, generic, HUGE_HEIGHT,
					GTK_ICON_LOOKUP_FORCE_SIZE);
			if (info && resolved_name && !*resolved_name)
				*resolved_name = g_strdup(generic);
		}
	}

	g_free(generic);
	g_free(content_type);
	return info;
}

/* Agregado por josejp2424 (2026): cargar las carpetas normales desde el
 * tema GTK3 activo, usando los nombres estándar de Freedesktop. Esto evita
 * que un antiguo MIME-icons/inode_directory.png imponga otro color. */
static GdkPixbuf *load_system_folder_icon(void)
{
	GtkIconTheme *theme = gtk_icon_theme_get_default();
	static const gchar *names[] = {
		ROX_ICON_DIRECTORY,
		"folder-symbolic",
		NULL
	};
	guint i;

	if (!theme)
		return NULL;

	for (i = 0; names[i] != NULL; i++)
	{
		GdkPixbuf *pixbuf;

		if (!gtk_icon_theme_has_icon(theme, names[i]))
			continue;

		pixbuf = gtk_icon_theme_load_icon(theme, names[i], HUGE_HEIGHT,
				GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
		if (pixbuf)
			return pixbuf;
	}

	return NULL;
}

/* Legacy ROX/GNOME MIME icon-name lookup, used only after the active GTK
 * theme has been tried. */
static GtkIconInfo *mime_type_lookup_icon_info(GtkIconTheme *theme,
		MIME_type *type)
{
	char *type_name = g_strconcat(type->media_type, "-", type->subtype, NULL);
	GtkIconInfo *full = gtk_icon_theme_lookup_icon(theme, type_name, HUGE_HEIGHT, 0);

	g_free(type_name);
	if (!full)
	{
		/* Ugly hack... try for a GNOME icon */
		if (type == inode_directory)
			type_name = g_strdup("gnome-fs-directory");
		else
			type_name = g_strconcat("gnome-mime-", type->media_type,
					"-", type->subtype, NULL);
		full = gtk_icon_theme_lookup_icon(theme, type_name, HUGE_HEIGHT, 0);
		g_free(type_name);
	}
	if (!full)
	{
		/* Try for a media type */
		type_name = g_strconcat(type->media_type, "-x-generic", NULL);
		full = gtk_icon_theme_lookup_icon(theme, type_name, HUGE_HEIGHT, 0);
		g_free(type_name);
	}
	if (!full)
	{
		/* Ugly hack... try for a GNOME default media icon */
		type_name = g_strconcat("gnome-mime-", type->media_type, NULL);

		full = gtk_icon_theme_lookup_icon(theme, type_name, HUGE_HEIGHT, 0);
		g_free(type_name);
	}
	return full;
}

/*			Actions for types 			*/

/* Return the image for this type, loading it if needed.
 * Places to check are: (eg type="text_plain", base="text")
 * 1. <Choices>/MIME-icons/base_subtype
 * 2. Active GTK/Freedesktop icon theme via GIO
 * 3. Selected legacy ROX/GNOME MIME theme
 * 4. Unknown type icon.
 *
 * Special case: If an icon cannot be found for inode/mount-point, the icon for
 * inode/directory will be returned (if possible).
 *
 * Note: You must g_object_unref() the image afterwards.
 */
MaskedPixmap *type_to_icon(MIME_type *type)
{
	GtkIconInfo *full;
	GdkPixbuf *folder_pixbuf;
	GdkPixbuf *loaded_pixbuf = NULL;
	gchar *resolved_icon_name = NULL;
	char	*type_name, *path;
	time_t	now;

	if (type == NULL)
	{
		g_object_ref(im_unknown);
		return im_unknown;
	}

	now = time(NULL);
	/* Already got an image? */
	if (type->image)
	{
		/* Yes - don't recheck too often */
		if (abs(now - type->image_time) < 2)
		{
			g_object_ref(type->image);
			return type->image;
		}
		g_object_unref(type->image);
		type->image = NULL;
	}

again:
	/* Modificado por josejp2424 (2026): una carpeta normal usa primero el
	 * icono `folder` del tema GTK3 activo. Los iconos personalizados .DirIcon
	 * se resuelven antes en diritem.c y continúan teniendo prioridad. */
	if (type == inode_directory)
	{
		folder_pixbuf = load_system_folder_icon();
		if (folder_pixbuf)
		{
			type->image = masked_pixmap_new(folder_pixbuf);
			g_object_unref(folder_pixbuf);
			ROX_LOG_DEBUG("mime-icon", "mime=inode/directory source=gtk-system name=folder");
			goto out;
		}
	}

	/* Los MIME-icons históricos siguen disponibles para otros tipos, pero ya
	 * no pueden reemplazar el color de las carpetas del tema GTK3. */
	type_name = g_strconcat(type->media_type, "_", type->subtype,
				".png", NULL);
	path = choices_find_xdg_path_load(type_name, "MIME-icons", SITE);
	g_free(type_name);
	if (path)
	{
		type->image = g_fscache_lookup(pixmap_cache, path);
		if (type->image)
			ROX_LOG_DEBUG("mime-icon", "mime=%s/%s source=choices path=%s",
				type->media_type, type->subtype, path);
		g_free(path);
	}

	if (type->image)
		goto out;

	/* r75: use the active GTK icon theme before legacy ROX/GNOME themes. */
	full = mime_type_lookup_system_icon_info(type, &resolved_icon_name);
	if (full)
	{
		GError *load_error = NULL;
		const gchar *icon_path = gtk_icon_info_get_filename(full);

		loaded_pixbuf = gtk_icon_info_load_icon(full, &load_error);
		if (loaded_pixbuf)
		{
			type->image = masked_pixmap_new(loaded_pixbuf);
			g_object_unref(loaded_pixbuf);
			ROX_LOG_DEBUG("mime-icon",
				"mime=%s/%s source=gtk-system name=%s path=%s",
				type->media_type, type->subtype,
				resolved_icon_name ? resolved_icon_name : "(gicon)",
				icon_path ? icon_path : "(builtin)");
		}
		else
		{
			ROX_LOG_WARNING("mime-icon",
				"mime=%s/%s system icon load failed name=%s error=%s",
				type->media_type, type->subtype,
				resolved_icon_name ? resolved_icon_name : "(gicon)",
				load_error ? load_error->message : "unknown");
		}
		g_clear_error(&load_error);
		gtk_icon_info_free(full);
		g_clear_pointer(&resolved_icon_name, g_free);
		if (type->image)
			goto out;
	}
	g_clear_pointer(&resolved_icon_name, g_free);

	full = mime_type_lookup_icon_info(icon_theme, type);
	if (!full && icon_theme != rox_theme)
	{
		init_rox_theme();
		full = mime_type_lookup_icon_info(rox_theme, type);
	}
	if (!full && icon_theme != gnome_theme)
	{
		init_gnome_theme();
		full = mime_type_lookup_icon_info(gnome_theme, type);
	}
	if (!full && type == inode_mountpoint)
	{
		/* Try to use the inode/directory icon for inode/mount-point */
		type = inode_directory;
		goto again;
	}
	if (full)
	{
		const char *icon_path;
		GError *load_error = NULL;
		/* Keep the historical cache when the theme exposes a file. For
		 * builtin icons, ask GTK to render the icon directly. */
		icon_path = gtk_icon_info_get_filename(full);
		if (icon_path != NULL)
			type->image = g_fscache_lookup(pixmap_cache, icon_path);
		if (!type->image)
		{
			loaded_pixbuf = gtk_icon_info_load_icon(full, &load_error);
			if (loaded_pixbuf)
			{
				type->image = masked_pixmap_new(loaded_pixbuf);
				g_object_unref(loaded_pixbuf);
			}
		}
		if (type->image)
			ROX_LOG_DEBUG("mime-icon",
				"mime=%s/%s source=legacy-theme path=%s",
				type->media_type, type->subtype,
				icon_path ? icon_path : "(builtin)");
		else
			ROX_LOG_WARNING("mime-icon",
				"mime=%s/%s legacy icon load failed error=%s",
				type->media_type, type->subtype,
				load_error ? load_error->message : "unknown");
		g_clear_error(&load_error);
		gtk_icon_info_free(full);
	}

out:
	if (!type->image)
	{
		/* One ref from the type structure, one returned */
		type->image = im_unknown;
		g_object_ref(im_unknown);
		ROX_LOG_WARNING("mime-icon", "mime=%s/%s source=unknown-fallback",
			type->media_type, type->subtype);
	}

	type->image_time = now;

	g_object_ref(type->image);
	return type->image;
}

GdkAtom type_to_atom(MIME_type *type)
{
	char	*str;
	GdkAtom	retval;

	g_return_val_if_fail(type != NULL, GDK_NONE);

	str = g_strconcat(type->media_type, "/", type->subtype, NULL);
	retval = gdk_atom_intern(str, FALSE);
	g_free(str);

	return retval;
}

/* Return a human-readable description of the XDG default application. */
gchar *describe_current_command(MIME_type *type)
{
	GAppInfo *app;
	const gchar *name;
	const gchar *command;
	gchar *description;

	g_return_val_if_fail(type != NULL, NULL);

	app = type_get_default_application(type);
	if (!app)
		return g_strdup(_("No default application defined"));

	name = g_app_info_get_display_name(app);
	command = g_app_info_get_commandline(app);
	if (name && *name && command && *command)
		description = g_strdup_printf("%s — %s", name, command);
	else if (name && *name)
		description = g_strdup(name);
	else if (command && *command)
		description = g_strdup(command);
	else
		description = g_strdup(_("Default XDG application"));

	g_object_unref(app);
	return description;
}


/* Display the standard GTK/XDG application chooser for this MIME type. */
void type_set_handler_dialog(MIME_type *type)
{
	GtkWidget *dialog;
	GtkWindow *parent = NULL;
	GList *windows;
	GList *node;
	gchar *mime_type;
	gint response;

	g_return_if_fail(type != NULL);

	/* Elegir como padre la ventana activa de ROX para que el selector quede
	 * centrado y no pueda ocultarse detrás del gestor. */
	windows = gtk_window_list_toplevels();
	for (node = windows; node; node = node->next) {
		if (GTK_IS_WINDOW(node->data) && gtk_window_is_active(GTK_WINDOW(node->data))) {
			parent = GTK_WINDOW(node->data);
			break;
		}
	}
	g_list_free(windows);

	mime_type = g_strconcat(type->media_type, "/", type->subtype, NULL);
	dialog = gtk_app_chooser_dialog_new_for_content_type(parent,
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, mime_type);
	gtk_window_set_title(GTK_WINDOW(dialog), _("Choose Default Application"));
	gtk_window_set_position(GTK_WINDOW(dialog), parent
		? GTK_WIN_POS_CENTER_ON_PARENT : GTK_WIN_POS_CENTER);
	gtk_app_chooser_dialog_set_heading(GTK_APP_CHOOSER_DIALOG(dialog),
		_("Select the default application for this file type"));

	response = gtk_dialog_run(GTK_DIALOG(dialog));
	if (response == GTK_RESPONSE_OK) {
		GAppInfo *app = gtk_app_chooser_get_app_info(GTK_APP_CHOOSER(dialog));
		if (app) {
			GError *error = NULL;
			if (!g_app_info_set_as_default_for_type(app, mime_type, &error)) {
				report_error(_("Unable to set the default application for %s: %s"),
					mime_type, error ? error->message : _("Unknown error"));
				g_clear_error(&error);
			} else {
				/* También la registra como última usada para que los menús
				 * Open With de aplicaciones XDG mantengan el mismo orden. */
				g_app_info_set_as_last_used_for_type(app, mime_type, NULL);
			}
			g_object_unref(app);
		}
	}

	gtk_widget_destroy(dialog);
	g_free(mime_type);
}



MIME_type *mime_type_from_base_type(int base_type)
{
	switch (base_type)
	{
		case TYPE_FILE:
			return text_plain;
		case TYPE_DIRECTORY:
			return inode_directory;
		case TYPE_PIPE:
			return inode_pipe;
		case TYPE_SOCKET:
			return inode_socket;
		case TYPE_BLOCK_DEVICE:
			return inode_block_dev;
		case TYPE_CHAR_DEVICE:
			return inode_char_dev;
	        case TYPE_DOOR:
	                return inode_door;
	}
	return inode_unknown;
}

/* Takes the st_mode field from stat() and returns the base type.
 * Should not be a symlink.
 */
int mode_to_base_type(int st_mode)
{
	if (S_ISREG(st_mode))
		return TYPE_FILE;
	else if (S_ISDIR(st_mode))
		return TYPE_DIRECTORY;
	else if (S_ISBLK(st_mode))
		return TYPE_BLOCK_DEVICE;
	else if (S_ISCHR(st_mode))
		return TYPE_CHAR_DEVICE;
	else if (S_ISFIFO(st_mode))
		return TYPE_PIPE;
	else if (S_ISSOCK(st_mode))
		return TYPE_SOCKET;
	else if (S_ISDOOR(st_mode))
		return TYPE_DOOR;

	return TYPE_ERROR;
}

/* Returns TRUE if this regular non-executable file can have its default
 * application selected through the standard XDG application chooser.
 */
gboolean can_set_run_action(DirItem *item)
{
	g_return_val_if_fail(item != NULL, FALSE);

	return item->base_type == TYPE_FILE && !EXECUTABLE_FILE(item);
}

static void expire_timer(gpointer key, gpointer value, gpointer data)
{
	MIME_type *type = value;

	type->image_time = 0;
}

/* Agregado por josejp2424 (2026): refrescar los iconos cuando cambia el tema
 * GTK3 del sistema, sin exigir reiniciar ROX-Filer. */
static void system_icon_theme_changed(GtkIconTheme *theme, gpointer data)
{
	(void) theme;
	(void) data;

	if (!type_hash)
		return;

	ROX_LOG_INFO("icon-theme", "GTK icon theme changed; invalidating MIME icon cache");
	log_system_icon_theme();
	g_hash_table_foreach(type_hash, expire_timer, NULL);
	full_refresh();
}

static void options_changed(void)
{
	if (o_icon_theme.has_changed)
	{
		set_icon_theme();
		g_hash_table_foreach(type_hash, expire_timer, NULL);
		full_refresh();
	}
}

/* Return the text colour supplied by the active GTK theme.
 * Legacy ROX per-file-type colours are intentionally ignored. */
GdkRGBA *type_get_colour(DirItem *item, GdkRGBA *normal)
{
	(void) item;
	return normal;
}

static char **get_xdg_data_dirs(int *n_dirs)
{
	const char *env;
	char **dirs;
	int i, n;

	env = getenv("XDG_DATA_DIRS");
	if (!env)
		env = "/usr/local/share/:/usr/share/";
	dirs = g_strsplit(env, ":", 0);
	g_return_val_if_fail(dirs != NULL, NULL);
	for (n = 0; dirs[n]; n++)
		;
	for (i = n; i > 0; i--)
		dirs[i] = dirs[i - 1];
	env = getenv("XDG_DATA_HOME");
	if (env)
		dirs[0] = g_strdup(env);
	else
		dirs[0] = g_build_filename(g_get_home_dir(), ".local",
					   "share", NULL);
	*n_dirs = n + 1;
	return dirs;
}

/* Try to fill in 'type->comment' from this document */
static void get_comment(MIME_type *type, const guchar *path)
{
	xmlNode *node;
	XMLwrapper *doc;

	doc = xml_cache_load(path);
	if (!doc)
		return;

	node = xml_get_section(doc, TYPE_NS, "comment");

	if (node)
	{
		char *val;
		g_return_if_fail(type->comment == NULL);
		val= xmlNodeListGetString(node->doc, node->xmlChildrenNode, 1);
		type->comment = g_strdup(val);
		xmlFree(val);
	}

	g_object_unref(doc);
}

/* Fill in the comment field for this MIME type */
static void find_comment(MIME_type *type)
{
	char **dirs;
	int i, n_dirs = 0;

	if (type->comment)
	{
		g_free(type->comment);
		type->comment = NULL;
	}

	dirs = get_xdg_data_dirs(&n_dirs);
	g_return_if_fail(dirs != NULL);

	for (i = 0; i < n_dirs; i++)
	{
		guchar *path;

		path = g_strdup_printf("%s/mime/%s/%s.xml", dirs[i],
				type->media_type, type->subtype);
		get_comment(type, path);
		g_free(path);
		if (type->comment)
			break;
	}

	if (!type->comment)
		type->comment = g_strdup_printf("%s/%s", type->media_type,
						type->subtype);

	for (i = 0; i < n_dirs; i++)
		g_free(dirs[i]);
	g_free(dirs);
}

const char *mime_type_comment(MIME_type *type)
{
	if (!type->comment)
		find_comment(type);

	return type->comment;
}

static void unref_icon_theme(void)
{
	if (icon_theme && icon_theme != rox_theme && icon_theme != gnome_theme)
		g_object_unref(icon_theme);
}

static void set_icon_theme(void)
{
	struct stat info;
	char *icon_home;
	const char *theme_dir;
	const char *theme_name = o_icon_theme.value;

	if (!theme_name || !*theme_name)
		theme_name = "ROX";

	if (!strcmp(theme_name, "ROX"))
	{
		unref_icon_theme();
		init_rox_theme();
		icon_theme = rox_theme;
	}
	else if (!strcmp(theme_name, "gnome"))
	{
		unref_icon_theme();
		init_gnome_theme();
		icon_theme = gnome_theme;
	}
	else
	{
		if (icon_theme == rox_theme || icon_theme == gnome_theme)
			icon_theme = gtk_icon_theme_new();
		gtk_icon_theme_set_custom_theme(icon_theme, theme_name);
	}

	/* Ensure the ROX theme exists. */

	icon_home = g_build_filename(home_dir, ".icons", "ROX", NULL);
	if (stat(icon_home, &info) == 0)
		return;	/* Already exists */

	/* First, create the .icons directory */
	theme_dir = make_path(home_dir, ".icons");
	if (!file_exists(theme_dir))
		mkdir(theme_dir, 0755);

	if (lstat(icon_home, &info) == 0)
	{
		/* Probably a broken symlink, then. Remove it. */
		if (unlink(icon_home))
			g_warning("Error removing broken symlink %s: %s", icon_home, g_strerror(errno));
		else
			g_warning("Removed broken symlink %s", icon_home);
	}

	if (symlink(make_path(app_dir, "ROX"), icon_home))
	{
		delayed_error(_("Failed to create symlink '%s':\n%s"), icon_home, g_strerror(errno));
		open_to_show(icon_home);
	}
	g_free(icon_home);

	gtk_icon_theme_rescan_if_needed(icon_theme);
}

static guchar *read_theme(Option *option)
{
	const gchar *theme;

	theme = gtk_combo_box_get_active_id(GTK_COMBO_BOX(option->widget));
	return g_strdup(theme ? theme : "ROX");
}

static void update_theme(Option *option)
{
	/* Modificado por josejp2424: selección por ID con GtkComboBox GTK3. */
	if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(option->widget),
					 option->value))
	{
		g_warning("Theme '%s' not found", option->value);
		gtk_combo_box_set_active(GTK_COMBO_BOX(option->widget), 0);
	}
}

static void add_themes_from_dir(GPtrArray *names, const char *dir)
{
	GPtrArray *list;
	int i;

	if (access(dir, F_OK) != 0)
		return;

	list = list_dir(dir);
	g_return_if_fail(list != NULL);

	for (i = 0; i < list->len; i++)
	{
		char *index_path;

		index_path = g_build_filename(dir, list->pdata[i],
						"index.theme", NULL);

		if (access(index_path, F_OK) == 0)
			g_ptr_array_add(names, list->pdata[i]);
		else
			g_free(list->pdata[i]);

		g_free(index_path);
	}

	g_ptr_array_free(list, TRUE);
}

static GList *build_icon_theme(Option *option, xmlNode *node, guchar *label)
{
	GtkWidget *button, *hbox;
	GPtrArray *names;
	gchar **theme_dirs = NULL;
	gint n_dirs = 0;
	int i;

	g_return_val_if_fail(option != NULL, NULL);
	g_return_val_if_fail(label != NULL, NULL);

	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);

	gtk_box_pack_start(GTK_BOX(hbox), gtk_label_new(_(label)),
				FALSE, TRUE, 0);

	/* Modificado por josejp2424: temas mediante GtkIconTheme + GtkComboBoxText. */
	button = gtk_combo_box_text_new();
	gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);

	gtk_icon_theme_get_search_path(icon_theme, &theme_dirs, &n_dirs);
	names = g_ptr_array_new();
	for (i = 0; i < n_dirs; i++)
		add_themes_from_dir(names, theme_dirs[i]);
	g_strfreev(theme_dirs);

	g_ptr_array_sort(names, strcmp2);

	for (i = 0; i < names->len; i++)
	{
		char *name = names->pdata[i];

		gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(button), name, name);
		g_free(name);
	}

	g_ptr_array_free(names, TRUE);

	option->update_widget = update_theme;
	option->read_widget = read_theme;
	option->widget = button;
	g_signal_connect_swapped(button, "changed",
			G_CALLBACK(option_check_widget),
			option);

	return g_list_append(NULL, hbox);
}

GtkIconInfo *theme_lookup_icon(const gchar *icon_name, gint size,
		GtkIconLookupFlags flags)
{
	GtkIconInfo *result = gtk_icon_theme_lookup_icon(icon_theme,
			icon_name, size, flags);

	if (!result && icon_theme != rox_theme)
	{
		init_rox_theme();
		result = gtk_icon_theme_lookup_icon(rox_theme,
			icon_name, size, flags);
	}
	if (!result && icon_theme != gnome_theme)
	{
		init_gnome_theme();
		result = gtk_icon_theme_lookup_icon(gnome_theme,
			icon_name, size, flags);
	}
	return result;
}

GdkPixbuf *theme_load_icon(const gchar *icon_name, gint size,
		GtkIconLookupFlags flags, GError **perror)
{
	GdkPixbuf *result = gtk_icon_theme_load_icon(icon_theme,
			icon_name, size, flags, NULL);

	if (!result && icon_theme != gnome_theme)
	{
		init_gnome_theme();
		result = gtk_icon_theme_load_icon(gnome_theme,
			icon_name, size, flags, NULL);
	}
	if (!result && icon_theme != rox_theme)
	{
		init_rox_theme();
		result = gtk_icon_theme_load_icon(rox_theme,
			icon_name, size, flags, perror);
	}
	return result;
}

