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

/* pixmaps.c - code for handling pixbufs (despite the name!) */

/*
 * Modificado por josejp2424 (2026):
 * port a GTK3 y adaptaciones específicas de esta versión.
 */

#include "config.h"
#define PIXMAPS_C

/* Remove pixmaps from the cache when they haven't been accessed for
 * this period of time (seconds).
 */

#define PIXMAP_PURGE_TIME 1200
#define PIXMAP_THUMB_SIZE  128
#define PIXMAP_THUMB_TOO_OLD_TIME  5

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <string.h>

#include <gtk/gtk.h>

#include "global.h"

#include "fscache.h"
#include "support.h"
#include "gui_support.h"
#include "pixmaps.h"
#include "main.h"
#include "filer.h"
#include "dir.h"
#include "diritem.h"
#include "choices.h"
#include "options.h"
#include "type.h"

GFSCache *pixmap_cache = NULL;
GFSCache *desktop_icon_cache = NULL;

static const char * bad_xpm[] = {
"12 12 3 1",
" 	c #000000000000",
".	c #FFFF00000000",
"x	c #FFFFFFFFFFFF",
"            ",
" ..xxxxxx.. ",
" ...xxxx... ",
" x...xx...x ",
" xx......xx ",
" xxx....xxx ",
" xxx....xxx ",
" xx......xx ",
" x...xx...x ",
" ...xxxx... ",
" ..xxxxxx.. ",
"            "};

MaskedPixmap *im_error;
MaskedPixmap *im_unknown;

MaskedPixmap *im_appdir;

MaskedPixmap *im_dirs;

GtkIconSize mount_icon_size = -1;

/* Rox-Filer2 2.12.2-8: optional ffmpeg video thumbnails. */
static Option o_video_thumbnails;

typedef struct _ChildThumbnail ChildThumbnail;

/* There is one of these for each active child process */
struct _ChildThumbnail {
	gchar	 *path;
	GFunc	 callback;
	gpointer data;
};

/* Static prototypes */

static void load_default_pixmaps(void);
static gint purge(gpointer data);
static MaskedPixmap *image_from_file(const char *path);
static MaskedPixmap *image_from_desktop_file(const char *path);
static MaskedPixmap *get_bad_image(void);
static GdkPixbuf *scale_pixbuf_up(GdkPixbuf *src, int max_w, int max_h);
static GdkPixbuf *get_thumbnail_for(const char *path);
static void thumbnail_child_done(ChildThumbnail *info);
static void child_create_thumbnail(const gchar *path, MIME_type *type);
static GList *thumbs_purge_cache(Option *option, xmlNode *node, guchar *label);
static gchar *thumbnail_path(const gchar *path);
static gchar *thumbnail_cache_dir(void);
static gchar *thumbnail_program(MIME_type *type);
static GdkPixbuf *extract_tiff_thumbnail(const gchar *path);
static gboolean create_video_thumbnail_ffmpeg(const gchar *path);
static void pixmaps_options_changed(void);

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

/* Modificado por josejp2424: los iconos de la interfaz se obtienen del
 * tema GTK3 activo, conservando sólo los fallbacks necesarios. */
void pixmaps_init(void)
{
	option_add_int(&o_video_thumbnails, "video_thumbnails", FALSE);
	/* Modificado por josejp2424: GTK3 obtiene los iconos del GtkIconTheme.
	 * Los iconos propios de ROX conservan fallback en AppDir/images mediante
	 * image_new_icon()/mp_from_icon(), sin registrar un GtkIconFactory.
	 */
	pixmap_cache = g_fscache_new((GFSLoadFunc) image_from_file, NULL, NULL);
	desktop_icon_cache = g_fscache_new((GFSLoadFunc) image_from_desktop_file, NULL, NULL);

	g_timeout_add(10000, purge, NULL);

	mount_icon_size = gtk_icon_size_register("rox-mount-size", 14, 14);

	load_default_pixmaps();

	option_register_widget("thumbs-purge-cache", thumbs_purge_cache);
	option_add_notify(pixmaps_options_changed);
}

/* Map historical AppDir image names to the active icon theme. */
/* Agregado por josejp2424: equivalencias entre nombres históricos de ROX
 * y nombres estándar del tema de iconos del sistema. */
static const char *legacy_pixmap_icon_name(const char *name)
{
	static const struct { const char *legacy; const char *themed; } names[] = {
		{"application", "application-x-executable"},
		{"dirs", ROX_ICON_DIRECTORY},
		{"iconified", "application-x-executable"},
		{"rox-mount", ROX_ICON_MOUNT},
		{"rox-mounted", ROX_ICON_MOUNTED},
		{"rox-select", ROX_ICON_SELECT},
		{"rox-show-details", ROX_ICON_SHOW_DETAILS},
		{"rox-show-hidden", ROX_ICON_SHOW_HIDDEN},
		{"rox-xattr", ROX_ICON_XATTR},
		{"symlink", ROX_ICON_SYMLINK},
	};
	guint i;

	for (i = 0; i < G_N_ELEMENTS(names); i++)
		if (g_str_equal(name, names[i].legacy))
			return names[i].themed;
	return NULL;
}

static gint icon_size_pixels(int size)
{
	gint width = 0, height = 0;

	if (size >= GTK_ICON_SIZE_MENU && size <= GTK_ICON_SIZE_DIALOG &&
	    gtk_icon_size_lookup((GtkIconSize) size, &width, &height))
		return MAX(width, height);
	return MAX(size, 1);
}

/* Create a MaskedPixmap from the active icon theme, with an optional bundled
 * fallback basename. Always returns a valid image. */
static MaskedPixmap *mp_from_icon_file(const char *icon_name,
		const char *fallback_name, int size)
{
	GdkPixbuf *pixbuf = NULL;
	MaskedPixmap *retval;
	GtkIconTheme *theme;
	GError *err = NULL;
	const gchar *resolved;
	const gchar *fallback;
	gint pixels;

	if (!icon_name || !*icon_name)
		return get_bad_image();

	pixels = icon_size_pixels(size);
	resolved = rox_icon_name(icon_name);
	theme = gtk_icon_theme_get_default();
	if (theme && gtk_icon_theme_has_icon(theme, resolved))
	{
		pixbuf = gtk_icon_theme_load_icon(theme, resolved, pixels,
				GTK_ICON_LOOKUP_FORCE_SIZE, &err);
		if (!pixbuf && err)
			g_clear_error(&err);
	}

	fallback = fallback_name ? fallback_name :
		rox_icon_fallback_name(icon_name);
	if (!pixbuf && fallback)
	{
		gchar *path = g_strconcat(app_dir, "/images/", fallback,
				".png", NULL);
		pixbuf = gdk_pixbuf_new_from_file_at_size(path, pixels, pixels, &err);
		g_free(path);
		if (!pixbuf && err)
			g_clear_error(&err);
	}

	if (!pixbuf && theme && gtk_icon_theme_has_icon(theme, "image-missing"))
	{
		pixbuf = gtk_icon_theme_load_icon(theme, "image-missing", pixels,
				GTK_ICON_LOOKUP_FORCE_SIZE, &err);
		if (!pixbuf && err)
			g_clear_error(&err);
	}

	if (!pixbuf)
		return get_bad_image();

	retval = masked_pixmap_new(pixbuf);
	g_object_unref(pixbuf);
	return retval;
}

/* Historical callers still pass AppDir image basenames. Prefer an equivalent
 * icon from the user's current theme and use the old file only as fallback. */
MaskedPixmap *load_pixmap(const char *name)
{
	const char *themed = legacy_pixmap_icon_name(name);

	if (themed)
		return mp_from_icon_file(themed,
			g_str_equal(name, "rox-show-hidden") ? name : NULL,
			ICON_WIDTH);

	/* Unknown third-party resource: retain the original AppDir behaviour. */
	{
		guchar *path = g_strconcat(app_dir, "/images/", name, ".png", NULL);
		MaskedPixmap *retval = image_from_file(path);
		g_free(path);
		return retval ? retval : get_bad_image();
	}
}

static MaskedPixmap *mp_from_icon(const char *icon_name, int size)
{
	return mp_from_icon_file(icon_name, NULL, size);
}

/* Agregado por josejp2424 (2026): devolver el icono estándar de la carpeta
 * personal desde el tema GTK3 activo. user-home tiene prioridad absoluta;
 * folder-home y folder sólo se usan si el tema no ofrece el nombre estándar. */
MaskedPixmap *pixmap_home_icon(void)
{
	GtkIconTheme *theme = gtk_icon_theme_get_default();
	static const gchar *names[] = {
		ROX_ICON_HOME,
		"folder-home",
		ROX_ICON_DIRECTORY,
		NULL
	};
	guint i;

	if (theme)
	{
		for (i = 0; names[i] != NULL; i++)
		{
			GdkPixbuf *pixbuf;
			MaskedPixmap *image;

			if (!gtk_icon_theme_has_icon(theme, names[i]))
				continue;
			pixbuf = gtk_icon_theme_load_icon(theme, names[i], HUGE_HEIGHT,
				GTK_ICON_LOOKUP_FORCE_SIZE, NULL);
			if (!pixbuf)
				continue;
			image = masked_pixmap_new(pixbuf);
			g_object_unref(pixbuf);
			return image;
		}
	}

	return mp_from_icon(ROX_ICON_DIRECTORY, HUGE_HEIGHT);
}


void pixmap_make_huge(MaskedPixmap *mp)
{
	if (mp->huge_pixbuf)
		return;

	g_return_if_fail(mp->src_pixbuf != NULL);

	/* Limit to small size now, otherwise they get scaled up in mixed mode.
	 * Also looked ugly.
	 */
	mp->huge_pixbuf = scale_pixbuf_up(mp->src_pixbuf,
					  SMALL_WIDTH, SMALL_HEIGHT);

	if (!mp->huge_pixbuf)
	{
		mp->huge_pixbuf = mp->src_pixbuf;
		g_object_ref(mp->huge_pixbuf);
	}

	mp->huge_width = gdk_pixbuf_get_width(mp->huge_pixbuf);
	mp->huge_height = gdk_pixbuf_get_height(mp->huge_pixbuf);
}

void pixmap_make_small(MaskedPixmap *mp)
{
	if (mp->sm_pixbuf)
		return;

	g_return_if_fail(mp->src_pixbuf != NULL);

	mp->sm_pixbuf = scale_pixbuf(mp->src_pixbuf, SMALL_WIDTH, SMALL_HEIGHT);

	if (!mp->sm_pixbuf)
	{
		mp->sm_pixbuf = mp->src_pixbuf;
		g_object_ref(mp->sm_pixbuf);
	}

	mp->sm_width = gdk_pixbuf_get_width(mp->sm_pixbuf);
	mp->sm_height = gdk_pixbuf_get_height(mp->sm_pixbuf);
}

/* Load image 'path' in the background and insert into pixmap_cache.
 * Call callback(data, path) when done (path is NULL => error).
 * If the image is already uptodate, or being created already, calls the
 * callback right away.
 */
void pixmap_background_thumb(const gchar *path, GFunc callback, gpointer data)
{
	gboolean	found;
	MaskedPixmap	*image;
	pid_t		child;
	ChildThumbnail	*info;
	MIME_type       *type;
	gchar		*thumb_prog, *base;

	image = pixmap_try_thumb(path, TRUE);

	if (image)
	{
		/* Thumbnail loaded */
		callback(data, (gpointer)path);
		return;
	}

	/* Is it currently being created? */
	image = g_fscache_lookup_full(pixmap_cache, path,
					FSCACHE_LOOKUP_ONLY_NEW, &found);

	if (found)
	{
		/* Thumbnail is known, or being created */
		if (image)
			g_object_unref(image);
		callback(data, image? (gpointer)path: NULL);
		return;
	}

	/* Not in memory, nor in the thumbnails directory.  We need to
	 * generate it */

	type = type_from_path(path);
	if (!type)
		type = text_plain;

	/* Video previews are an explicit user choice.  Existing MIME-thumb
	 * helpers are still honoured when enabled; otherwise Rox-Filer2 falls
	 * back to its built-in ffmpeg launcher. */
	if (strcmp(type->media_type, "video") == 0 && !o_video_thumbnails.int_value)
	{
		callback(data, NULL);
		return;
	}

	thumb_prog = thumbnail_program(type);

	/* Only attempt to load image types ourselves.  Video is also accepted
	 * when the option is enabled and ffmpeg is available. */
	if (thumb_prog == NULL && strcmp(type->media_type, "image") != 0)
	{
		gchar *ffmpeg = NULL;

		if (strcmp(type->media_type, "video") == 0 &&
		    o_video_thumbnails.int_value)
			ffmpeg = g_find_program_in_path("ffmpeg");

		if (!ffmpeg)
		{
			callback(data, NULL);
			return;		/* Don't know how to handle this type */
		}
		g_free(ffmpeg);
	}

	/* Mark this path as being generated only after we know it is eligible.
	 * This lets enabling video thumbnails later work immediately. */
	g_fscache_insert(pixmap_cache, path, NULL, TRUE);

	child = fork();
	if (child == -1)
	{
		g_free(thumb_prog);
		delayed_error("fork(): %s", g_strerror(errno));
		callback(data, NULL);
		return;
	}

	if (child == 0)
	{
		/* We are the child process.  (We are sloppy with freeing
		   memory, but since we go away very quickly, that's ok.) */
		if (thumb_prog)
		{
			DirItem *item;

			base = g_path_get_basename(thumb_prog);
			item = diritem_new(base);
			g_free(base);
			diritem_restat(thumb_prog, item, NULL);
			if (item->flags & ITEM_FLAG_APPDIR)
				thumb_prog = g_strconcat(thumb_prog, "/AppRun",
						NULL);

			execl(thumb_prog, thumb_prog, path,
					thumbnail_path(path),
					g_strdup_printf("%d", PIXMAP_THUMB_SIZE),
					NULL);

			_exit(1);
		}

		if (strcmp(type->media_type, "video") == 0 &&
		    o_video_thumbnails.int_value)
			_exit(create_video_thumbnail_ffmpeg(path) ? 0 : 1);

		child_create_thumbnail(path, type);
		_exit(0);
	}

	g_free(thumb_prog);

	info = g_new(ChildThumbnail, 1);
	info->path = g_strdup(path);
	info->callback = callback;
	info->data = data;
	on_child_death(child, (CallbackFn) thumbnail_child_done, info);
}

/*
 * Return the thumbnail for a file, only if available.  If the
 * can_load flags is set this includes loading from the cache, otherwise
 * only if already in memory
 */
MaskedPixmap *pixmap_try_thumb(const gchar *path, gboolean can_load)
{
	gboolean  found;
	MaskedPixmap *image;
	GdkPixbuf *pixbuf;
	MIME_type *type;

	/* The option controls both creation and display of cached video previews. */
	type = type_from_path(path);
	if (type && strcmp(type->media_type, "video") == 0 &&
	    !o_video_thumbnails.int_value)
		return NULL;

	image = g_fscache_lookup_full(pixmap_cache, path,
					FSCACHE_LOOKUP_ONLY_NEW, &found);

	if (found)
	{
		/* Thumbnail is known, or being created */
		if (image)
			return image;
	}

	if(!can_load)
		return NULL;

	pixbuf = get_thumbnail_for(path);

	if (!pixbuf)
	{
		struct stat info1, info2;
		char *dir;

		/* Skip zero-byte files. They're either empty, or
		 * special (may cause us to hang, e.g. /proc/kmsg). */
		if (mc_stat(path, &info1) == 0 && info1.st_size == 0) {
			return NULL;
		}

		dir = g_path_get_dirname(path);

		/* If the image itself is in the standard thumbnail cache, load
		 * it now (do not create thumbnails for thumbnails).
		 */
		if (mc_stat(dir, &info1) != 0)
		{
			g_free(dir);
			return NULL;
		}
		g_free(dir);

		{
			gchar *cache_dir = thumbnail_cache_dir();
			gboolean in_cache = mc_stat(cache_dir, &info2) == 0 &&
				info1.st_dev == info2.st_dev &&
				info1.st_ino == info2.st_ino;
			g_free(cache_dir);
			if (in_cache)
			{
			pixbuf = rox_pixbuf_new_from_file_at_scale(path,
					PIXMAP_THUMB_SIZE, PIXMAP_THUMB_SIZE,
								   TRUE, NULL);
				if (!pixbuf)
				{
					return NULL;
				}
			}
		}
	}

	if (pixbuf)
	{
		MaskedPixmap *image;

		image = masked_pixmap_new(pixbuf);
		g_object_unref(pixbuf);
		g_fscache_insert(pixmap_cache, path, image, TRUE);
		return image;
	}

	return NULL;
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

/* Return the standard Freedesktop thumbnail cache directory. */
static gchar *thumbnail_cache_dir(void)
{
	gchar *path = g_build_filename(g_get_user_cache_dir(),
				       "thumbnails", "normal", NULL);
	g_mkdir_with_parents(path, 0700);
	return path;
}

/* Create a thumbnail file for this image */
static void save_thumbnail(const char *pathname, GdkPixbuf *full)
{
	struct stat info;
	gchar *path;
	int original_width, original_height;
	GString *to;
	gchar *cache_dir;
	char *md5, *swidth, *sheight, *ssize, *smtime, *uri;
	mode_t old_mask;
	int name_len;
	GdkPixbuf *thumb;

	thumb = scale_pixbuf(full, PIXMAP_THUMB_SIZE, PIXMAP_THUMB_SIZE);

	original_width = gdk_pixbuf_get_width(full);
	original_height = gdk_pixbuf_get_height(full);

	if (mc_stat(pathname, &info) != 0)
		return;

	swidth = g_strdup_printf("%d", original_width);
	sheight = g_strdup_printf("%d", original_height);
	ssize = g_strdup_printf("%" SIZE_FMT, info.st_size);
	smtime = g_strdup_printf("%ld", (long) info.st_mtime);

	path = pathdup(pathname);
	uri = g_filename_to_uri(path, NULL, NULL);
	if (!uri)
	        uri = g_strconcat("file://", path, NULL);
	md5 = md5_hash(uri);
	g_free(path);

	cache_dir = thumbnail_cache_dir();
	to = g_string_new(cache_dir);
	g_string_append_c(to, G_DIR_SEPARATOR);
	g_string_append(to, md5);
	g_free(cache_dir);
	name_len = to->len + 4; /* Truncate to this length when renaming */
	g_string_append_printf(to, ".png.ROX-Filer-%ld", (long) getpid());

	g_free(md5);

	old_mask = umask(0077);
	gdk_pixbuf_save(thumb, to->str, "png", NULL,
			"tEXt::Thumb::Image::Width", swidth,
			"tEXt::Thumb::Image::Height", sheight,
			"tEXt::Thumb::Size", ssize,
			"tEXt::Thumb::MTime", smtime,
			"tEXt::Thumb::URI", uri,
			"tEXt::Software", PROJECT,
			NULL);
	umask(old_mask);

	/* We create the file ###.png.ROX-Filer-PID and rename it to avoid
	 * a race condition if two programs create the same thumb at
	 * once.
	 */
	{
		gchar *final;

		final = g_strndup(to->str, name_len);
		if (rename(to->str, final))
			g_warning("Failed to rename '%s' to '%s': %s",
				  to->str, final, g_strerror(errno));
		g_free(final);
	}

	g_string_free(to, TRUE);
	g_free(swidth);
	g_free(sheight);
	g_free(ssize);
	g_free(smtime);
	g_free(uri);
}

static gchar *thumbnail_path(const char *path)
{
	gchar *uri, *md5;
	GString *to;
	gchar *ans;
	gchar *cache_dir;

	uri = g_filename_to_uri(path, NULL, NULL);
	if(!uri)
	       uri = g_strconcat("file://", path, NULL);
	md5 = md5_hash(uri);

	cache_dir = thumbnail_cache_dir();
	to = g_string_new(cache_dir);
	g_string_append_c(to, G_DIR_SEPARATOR);
	g_string_append(to, md5);
	g_free(cache_dir);
	g_string_append(to, ".png");

	g_free(md5);
	g_free(uri);

	ans=to->str;
	g_string_free(to, FALSE);

	return ans;
}

/* When video thumbnail support changes, forget in-memory thumbnail state and
 * redraw filer windows.  Disk thumbnails are left intact and are ignored while
 * the option is disabled. */
static void pixmaps_options_changed(void)
{
	if (!o_video_thumbnails.has_changed || !pixmap_cache)
		return;

	g_fscache_purge(pixmap_cache, 0);
	full_refresh();
}

/* Return a program to create thumbnails for files of this type.
 * NULL to try to make it ourself (using gdk).
 * g_free the result.
 */
static gchar *thumbnail_program(MIME_type *type)
{
	gchar *leaf;
	gchar *path;

	if (!type)
		return NULL;

	leaf = g_strconcat(type->media_type, "_", type->subtype, NULL);
	path = choices_find_xdg_path_load(leaf, "MIME-thumb", SITE);
	g_free(leaf);
	if (path)
	{
		return path;
	}

	path = choices_find_xdg_path_load(type->media_type, "MIME-thumb",
					  SITE);

	return path;
}

/* Run one ffmpeg attempt from the already-forked thumbnail worker.
 * Use only POSIX process functions here: no shell is involved and paths with
 * spaces or metacharacters are passed as normal argv elements. */
static gboolean run_ffmpeg_thumbnail_attempt(const gchar *path,
					     const gchar *output,
					     const gchar *seek,
					     const gchar *filter)
{
	pid_t pid;
	pid_t waited;
	int status = 0;
	struct stat st;

	pid = fork();
	if (pid == -1)
		return FALSE;

	if (pid == 0)
	{
		execlp("ffmpeg", "ffmpeg",
			"-loglevel", "error", "-y",
			"-ss", seek, "-i", path,
			"-vf", filter, "-frames:v", "1", output,
			(char *) NULL);
		_exit(127);
	}

	do
	{
		waited = waitpid(pid, &status, 0);
	} while (waited == -1 && errno == EINTR);

	if (waited != pid)
		return FALSE;

	return WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
	       stat(output, &st) == 0 && st.st_size > 0;
}

/* Rox-Filer2 video thumbnailer.  First try around 10 seconds to avoid blank
 * title frames; short clips fall back to one second. */
static gboolean create_video_thumbnail_ffmpeg(const gchar *path)
{
	gchar *output;
	gchar *temporary;
	gchar *filter;
	gboolean ok;
	GdkPixbuf *frame = NULL;
	GdkPixbuf *verified = NULL;

	/* ffmpeg can create the picture, but a raw ffmpeg PNG does not contain
	 * the Freedesktop Thumb::MTime/Size metadata that get_thumbnail_for()
	 * uses to validate cached thumbnails.  Generate to a temporary PNG, load
	 * it, then save it through ROX's normal save_thumbnail() path so video and
	 * image thumbnails have exactly the same cache format. */
	output = thumbnail_path(path);
	temporary = g_strdup_printf("%s.ffmpeg-%ld.png", output, (long) getpid());
	filter = g_strdup_printf(
		"scale=%d:%d:force_original_aspect_ratio=decrease",
		PIXMAP_THUMB_SIZE, PIXMAP_THUMB_SIZE);

	/* Remove a thumbnail produced by 2.12.2-6/7 without metadata. */
	unlink(output);
	unlink(temporary);

	ok = run_ffmpeg_thumbnail_attempt(path, temporary, "00:00:10", filter);
	if (!ok)
	{
		unlink(temporary);
		ok = run_ffmpeg_thumbnail_attempt(path, temporary, "00:00:01", filter);
	}

	if (ok)
	{
		frame = gdk_pixbuf_new_from_file(temporary, NULL);
		if (frame)
		{
			save_thumbnail(path, frame);
			g_object_unref(frame);

			/* Verify using the same routine the parent will use. */
			verified = get_thumbnail_for(path);
			ok = verified != NULL;
			if (verified)
				g_object_unref(verified);
		}
		else
			ok = FALSE;
	}

	unlink(temporary);
	if (!ok)
		unlink(output);
	g_free(filter);
	g_free(temporary);
	g_free(output);
	return ok;
}

/* Called in a subprocess. Load path and create the thumbnail
 * file. Parent will notice when we die.
 */
static void child_create_thumbnail(const gchar *path, MIME_type *type)
{
	GdkPixbuf *image=NULL;

        if(strcmp(type->subtype, "jpeg")==0)
            image=extract_tiff_thumbnail(path);

	if(!image)
            image = rox_pixbuf_new_from_file_at_scale(path,
			PIXMAP_THUMB_SIZE, PIXMAP_THUMB_SIZE, TRUE, NULL);

	if (image)
		save_thumbnail(path, image);

	/* (no need to unref, as we're about to exit) */
}

/* Called when the child process exits */
static void thumbnail_child_done(ChildThumbnail *info)
{
	GdkPixbuf *thumb;

	thumb = get_thumbnail_for(info->path);

	if (thumb)
	{
		MaskedPixmap *image;

		image = masked_pixmap_new(thumb);
		g_object_unref(thumb);

		g_fscache_insert(pixmap_cache, info->path, image, FALSE);
		g_object_unref(image);

		info->callback(info->data, info->path);
	}
	else
		info->callback(info->data, NULL);

	g_free(info->path);
	g_free(info);
}


/* Check if we have an up-to-date thumbnail for this image.
 * If so, return it. Otherwise, returns NULL.
 */
static GdkPixbuf *get_thumbnail_for(const char *pathname)
{
	GdkPixbuf *thumb = NULL;
	char *thumb_path, *md5, *uri, *path;
	const char *ssize, *smtime;
	struct stat info;
	time_t ttime, now;

	path = pathdup(pathname);
	uri = g_filename_to_uri(path, NULL, NULL);
	if(!uri)
	        uri = g_strconcat("file://", path, NULL);
	md5 = md5_hash(uri);
	g_free(uri);

	{
		gchar *cache_dir = thumbnail_cache_dir();
		thumb_path = g_strdup_printf("%s%c%s.png", cache_dir,
					      G_DIR_SEPARATOR, md5);
		g_free(cache_dir);
	}
	g_free(md5);

	thumb = gdk_pixbuf_new_from_file(thumb_path, NULL);
	if (!thumb)
		goto err;

	/* Note that these don't need freeing... */
	ssize = gdk_pixbuf_get_option(thumb, "tEXt::Thumb::Size");
	/* This is optional, so don't flag an error if it is missing */

	smtime = gdk_pixbuf_get_option(thumb, "tEXt::Thumb::MTime");
	if (!smtime)
		goto err;

	if (mc_stat(path, &info) != 0)
		goto err;

	ttime=(time_t) atol(smtime);
	time(&now);
	if (info.st_mtime != ttime && now>ttime+PIXMAP_THUMB_TOO_OLD_TIME)
		goto err;

	if (ssize && info.st_size < atol(ssize))
		goto err;

	goto out;
err:
	if (thumb)
		g_object_unref(thumb);
	thumb = NULL;
out:
	g_free(path);
	g_free(thumb_path);
	return thumb;
}

/* Load the image 'path' and return a pointer to the resulting
 * MaskedPixmap. NULL on failure.
 * Doesn't check for thumbnails (this is for small icons).
 */
static MaskedPixmap *image_from_file(const char *path)
{
	GdkPixbuf	*pixbuf;
	MaskedPixmap	*image;
	GError		*error = NULL;

	pixbuf = gdk_pixbuf_new_from_file(path, &error);
	if (!pixbuf)
	{
		g_warning("%s\n", error->message);
		g_error_free(error);
		return NULL;
	}

	image = masked_pixmap_new(pixbuf);

	g_object_unref(pixbuf);

	return image;
}

/* Load this icon named by this .desktop file from the current theme.
 * NULL on failure.
 */
static MaskedPixmap *image_from_desktop_file(const char *path)
{
	GError *error = NULL;
	MaskedPixmap *image = NULL;
	char *icon = NULL;

	icon = get_value_from_desktop_file(path,
					"Desktop Entry", "Icon", &error);
	if (error)
	{
		g_warning("Failed to parse .desktop file '%s':\n%s",
				path, error->message);
		goto err;
	}
	if (!icon)
		goto err;

	if (icon[0] == '/')
		image = image_from_file(icon);
	else
	{
		GdkPixbuf *pixbuf;
		int tmp_fd;
		char *extension;

		/* For some unknown reason, some icon names have extensions.
		 * Remove them.
		 */
		extension = strrchr(icon, '.');
		if (extension && (strcmp(extension, ".png") == 0
						|| strcmp(extension, ".xpm") == 0
						|| strcmp(extension, ".svg") == 0))
		{
			*extension = '\0';
		}

		/* SVG reader is very noisy, so redirect stderr to stdout */
		tmp_fd = dup(2);
		dup2(1, 2);
		pixbuf = theme_load_icon(icon, HUGE_WIDTH, 0, NULL);
		dup2(tmp_fd, 2);
		close(tmp_fd);

		if (pixbuf == NULL)
			goto err;	/* Might just not be in the theme */

		image = masked_pixmap_new(pixbuf);
		g_object_unref(pixbuf);
	}
err:
	if (error != NULL)
		g_error_free(error);
	if (icon != NULL)
		g_free(icon);
	return image;
}

/* Scale src down to fit in max_w, max_h and return the new pixbuf.
 * If src is small enough, then ref it and return that.
 */
GdkPixbuf *scale_pixbuf(GdkPixbuf *src, int max_w, int max_h)
{
	int	w, h;

	w = gdk_pixbuf_get_width(src);
	h = gdk_pixbuf_get_height(src);

	if (w <= max_w && h <= max_h)
	{
		g_object_ref(src);
		return src;
	}
	else
	{
		float scale_x = ((float) w) / max_w;
		float scale_y = ((float) h) / max_h;
		float scale = MAX(scale_x, scale_y);
		int dest_w = w / scale;
		int dest_h = h / scale;

		return gdk_pixbuf_scale_simple(src,
						MAX(dest_w, 1),
						MAX(dest_h, 1),
						GDK_INTERP_BILINEAR);
	}
}

/* Scale src up to fit in max_w, max_h and return the new pixbuf.
 * If src is that size or bigger, then ref it and return that.
 */
static GdkPixbuf *scale_pixbuf_up(GdkPixbuf *src, int max_w, int max_h)
{
	int	w, h;

	w = gdk_pixbuf_get_width(src);
	h = gdk_pixbuf_get_height(src);

	if (w == 0 || h == 0 || w >= max_w || h >= max_h)
	{
		g_object_ref(src);
		return src;
	}
	else
	{
		float scale_x = max_w / ((float) w);
		float scale_y = max_h / ((float) h);
		float scale = MIN(scale_x, scale_y);

		return gdk_pixbuf_scale_simple(src,
						w * scale,
						h * scale,
						GDK_INTERP_BILINEAR);
	}
}

/* Return a pointer to the (static) bad image. The ref counter will ensure
 * that the image is never freed.
 */
static MaskedPixmap *get_bad_image(void)
{
	GdkPixbuf *bad;
	MaskedPixmap *mp;

	bad = gdk_pixbuf_new_from_xpm_data(bad_xpm);
	mp = masked_pixmap_new(bad);
	g_object_unref(bad);

	return mp;
}

/* Called now and then to clear out old pixmaps */
static gint purge(gpointer data)
{
	g_fscache_purge(pixmap_cache, PIXMAP_PURGE_TIME);

	return TRUE;
}

static gpointer parent_class;

static void masked_pixmap_finialize(GObject *object)
{
	MaskedPixmap *mp = (MaskedPixmap *) object;

	if (mp->src_pixbuf)
	{
		g_object_unref(mp->src_pixbuf);
		mp->src_pixbuf = NULL;
	}

	if (mp->huge_pixbuf)
	{
		g_object_unref(mp->huge_pixbuf);
		mp->huge_pixbuf = NULL;
	}
	if (mp->pixbuf)
	{
		g_object_unref(mp->pixbuf);
		mp->pixbuf = NULL;
	}

	if (mp->sm_pixbuf)
	{
		g_object_unref(mp->sm_pixbuf);
		mp->sm_pixbuf = NULL;
	}

	G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void masked_pixmap_class_init(gpointer gclass, gpointer data)
{
	GObjectClass *object = (GObjectClass *) gclass;

	parent_class = g_type_class_peek_parent(gclass);

	object->finalize = masked_pixmap_finialize;
}

static void masked_pixmap_init(GTypeInstance *object, gpointer gclass)
{
	MaskedPixmap *mp = (MaskedPixmap *) object;

	mp->src_pixbuf = NULL;

	mp->huge_pixbuf = NULL;
	mp->huge_width = -1;
	mp->huge_height = -1;

	mp->pixbuf = NULL;
	mp->width = -1;
	mp->height = -1;

	mp->sm_pixbuf = NULL;
	mp->sm_width = -1;
	mp->sm_height = -1;
}

static GType masked_pixmap_get_type(void)
{
	static GType type = 0;

	if (!type)
	{
		static const GTypeInfo info =
		{
			sizeof (MaskedPixmapClass),
			NULL,			/* base_init */
			NULL,			/* base_finalise */
			masked_pixmap_class_init,
			NULL,			/* class_finalise */
			NULL,			/* class_data */
			sizeof(MaskedPixmap),
			0,			/* n_preallocs */
			masked_pixmap_init
		};

		type = g_type_register_static(G_TYPE_OBJECT, "MaskedPixmap",
					      &info, 0);
	}

	return type;
}

MaskedPixmap *masked_pixmap_new(GdkPixbuf *full_size)
{
	MaskedPixmap *mp;
	GdkPixbuf	*src_pixbuf, *normal_pixbuf;

	g_return_val_if_fail(full_size != NULL, NULL);

	src_pixbuf = scale_pixbuf(full_size, HUGE_WIDTH, HUGE_HEIGHT);
	g_return_val_if_fail(src_pixbuf != NULL, NULL);

	normal_pixbuf = scale_pixbuf(src_pixbuf, ICON_WIDTH, ICON_HEIGHT);
	g_return_val_if_fail(normal_pixbuf != NULL, NULL);

	mp = g_object_new(masked_pixmap_get_type(), NULL);

	mp->src_pixbuf = src_pixbuf;

	mp->pixbuf = normal_pixbuf;
	mp->width = gdk_pixbuf_get_width(normal_pixbuf);
	mp->height = gdk_pixbuf_get_height(normal_pixbuf);

	return mp;
}

/* Load all the standard pixmaps. Also sets the default window icon. */
static void load_default_pixmaps(void)
{
	GdkPixbuf *pixbuf;
	GError *error = NULL;

	im_error = mp_from_icon(ROX_ICON_DIALOG_WARNING,
				 GTK_ICON_SIZE_DIALOG);
	im_unknown = mp_from_icon(ROX_ICON_DIALOG_QUESTION,
				   GTK_ICON_SIZE_DIALOG);

	im_dirs = load_pixmap("dirs");
	im_appdir = load_pixmap("application");

	/* Rox-Filer2 2.12.2-26: prefer the installed application icon from
	 * hicolor.  Fall back to the historical .DirIcon when running directly
	 * from an unpacked source tree before installation. */
	pixbuf = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(),
			"rox-filer2", 64, GTK_ICON_LOOKUP_FORCE_SIZE, &error);
	if (!pixbuf)
	{
		g_clear_error(&error);
		pixbuf = gdk_pixbuf_new_from_file(
				make_path(app_dir, ".DirIcon"), &error);
	}
	if (pixbuf)
	{
		GList *icon_list;

		icon_list = g_list_append(NULL, pixbuf);
		gtk_window_set_default_icon_list(icon_list);
		g_list_free(icon_list);

		g_object_unref(G_OBJECT(pixbuf));
	}
	else if (error)
	{
		g_warning("%s\n", error->message);
		g_error_free(error);
	}
}

/* Delete cached thumbnails directly.  The old implementation passed cache
 * files to action_delete(), which opened ROX's normal (and confusing) file
 * deletion dialog and could fail while the Options window was open.  A cache
 * purge should be a simple maintenance operation, not a normal delete action. */
static guint purge_thumbnail_directory(const gchar *path, GString *errors)
{
	DIR *dir;
	struct dirent *ent;
	guint removed = 0;

	dir = opendir(path);
	if (!dir)
	{
		if (errno != ENOENT)
			g_string_append_printf(errors, "%s: %s\n", path, g_strerror(errno));
		return 0;
	}

	while ((ent = readdir(dir)))
	{
		gchar *file;
		struct stat st;

		if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
			continue;

		file = g_build_filename(path, ent->d_name, NULL);
		if (lstat(file, &st) == 0 && !S_ISDIR(st.st_mode))
		{
			if (unlink(file) == 0)
				removed++;
			else
				g_string_append_printf(errors, "%s: %s\n", file, g_strerror(errno));
		}
		g_free(file);
	}

	closedir(dir);
	return removed;
}

/* Also purges the in-memory thumbnail cache and redraws filer windows. */
static void purge_disk_cache(GtkWidget *button, gpointer data)
{
	gchar *path;
	gchar *legacy_path;
	GString *errors;
	guint removed = 0;

	(void) button;
	(void) data;

	errors = g_string_new(NULL);

	/* Do not call thumbnail_cache_dir() here: that helper creates the
	 * directory.  There is no reason to create a cache just to empty it. */
	path = g_build_filename(g_get_user_cache_dir(),
				"thumbnails", "normal", NULL);
	removed += purge_thumbnail_directory(path, errors);

	/* Clean the location used by older ROX releases too, if it still exists. */
	legacy_path = g_build_filename(g_get_home_dir(),
				       ".thumbnails", "normal", NULL);
	if (g_strcmp0(path, legacy_path) != 0)
		removed += purge_thumbnail_directory(legacy_path, errors);

	g_fscache_purge(pixmap_cache, 0);
	full_refresh();

	if (errors->len > 0)
		report_error(_("Can't delete thumbnails:\n%s"), errors->str);
	else if (removed == 0)
		info_message(_("There are no thumbnails to delete"));

	g_string_free(errors, TRUE);
	g_free(legacy_path);
	g_free(path);
}

static GList *thumbs_purge_cache(Option *option, xmlNode *node, guchar *label)
{
	GtkWidget *button;

	g_return_val_if_fail(option == NULL, NULL);

	button = button_new_mixed(ROX_ICON_CLEAR,
				  _("Clear thumbnail cache"));
	gtk_widget_set_halign(button, GTK_ALIGN_START);
	gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
	g_signal_connect(button, "clicked", G_CALLBACK(purge_disk_cache), NULL);

	return g_list_append(NULL, button);
}

/* Exif reading.
 * Based on Thierry Bousch's public domain exifdump.py.
 */

#define JPEG_FORMAT        0x201
#define JPEG_FORMAT_LENGTH 0x202

/*
 * Extract n-byte integer in Motorola (big-endian) format
 */
static inline long long s2n_motorola(const unsigned char *p, int len)
{
    long long a=0;
    int i;

    for(i=0; i<len; i++)
        a=(a<<8) | (int)(p[i]);

    return a;
}

/*
 * Extract n-byte integer in Intel (little-endian) format
 */
static inline long long s2n_intel(const unsigned char *p, int len)
{
    long long a=0;
    int i;

    for(i=0; i<len; i++)
        a=a | (((int) p[i]) << (i*8));

    return a;
}

/*
 * Extract n-byte integer from data
 */
static int s2n(const unsigned char *dat, int off, int len, char format)
{
    const unsigned char *p=dat+off;

    switch(format) {
    case 'I':
        return s2n_intel(p, len);

    case 'M':
        return s2n_motorola(p, len);
    }

    return 0;
}

/*
 * Load header of JPEG/Exif file and attempt to extract the embedded
 * thumbnail.  Return NULL on failure.
 */
static GdkPixbuf *extract_tiff_thumbnail(const gchar *path)
{
    FILE *in;
    unsigned char header[256];
    int i, n;
    int length;
    unsigned char *data;
    char format;
    int ifd, entries;
    int thumb=0, tlength=0;
    GdkPixbuf *buf=NULL;

    in=fopen(path, "rb");
    if(!in) {
        return NULL;
    }

    /* Check for Exif format */
    n=fread(header, 1, 12, in);
    if(n!=12 || strncmp((char *) header, "\377\330\377\341", 4)!=0 ||
       strncmp((char *)header+6, "Exif", 4)!=0) {
        fclose(in);
        return NULL;
    }

    /* Read header */
    length=header[4]*256+header[5];
    data=g_new(unsigned char, length);
    n=fread(data, 1, length, in);
    fclose(in);   /* File no longer needed */
    if(n!=length) {
        g_free(data);
        return NULL;
    }

    /* Big or little endian (as 'M' or 'I') */
    format=data[0];

    /* Skip over main section */
    ifd=s2n(data, 4, 4, format);
    entries=s2n(data, ifd, 2, format);

    /* Second section contains data on thumbnail */
    ifd=s2n(data, ifd+2+12*entries, 4, format);
    entries=s2n(data, ifd, 2, format);

    /* Loop over the entries */
    for(i=0; i<entries; i++) {
        int entry=ifd+2+12*i;
        int tag=s2n(data, entry, 2, format);
        int type=s2n(data, entry+2, 2, format);
        int offset=entry+8;

        if(type==4) {
            int val=(int) s2n(data, offset, 4, format);

            /* Only interested in two entries, the location of the thumbnail
               and its size */
            switch(tag) {
            case JPEG_FORMAT: thumb=val; break;
            case JPEG_FORMAT_LENGTH: tlength=val; break;
            }
        }
    }

    if(thumb && tlength) {
        GError *err=NULL;
        GdkPixbufLoader *loader;

        /* Don't read outside the header (some files have incorrect data) */
        if(thumb+tlength>length)
            tlength=length-thumb;

        loader=gdk_pixbuf_loader_new();
        gdk_pixbuf_loader_write(loader, data+thumb, tlength, &err);
        if(err) {
            g_error_free(err);
            return NULL;
        }

        gdk_pixbuf_loader_close(loader, &err);
        if(err) {
            g_error_free(err);
            return NULL;
        }

        buf=gdk_pixbuf_loader_get_pixbuf(loader);
        g_object_ref(buf);      /* Ref the image before we unref the loader */
        g_object_unref(loader);
    }

    g_free(data);

    return buf;
}

