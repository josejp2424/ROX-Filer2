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

/* run.c */

#include "config.h"

#include <errno.h>
#include <string.h>
#include <sys/param.h>

#include "global.h"

#include "run.h"
#include "support.h"
#include "gui_support.h"
#include "options.h"
#include "filer.h"
#include "display.h"
#include "main.h"
#include "type.h"
#include "dir.h"
#include "diritem.h"
#include "action.h"
#include "icon.h"
#include "choices.h"
#include "xdg_apps.h"

/* Static prototypes */
static void write_data(gpointer data, gint fd, RoxInputCondition cond);
static gboolean follow_symlink(const char *full_path,
			       FilerWindow *filer_window,
			       FilerWindow *src_window);
static gboolean open_file(const guchar *path, MIME_type *type);
static void open_mountpoint(const guchar *full_path, DirItem *item,
			    FilerWindow *filer_window, FilerWindow *src_window,
			    gboolean edit);
static gboolean run_desktop(const char *full_path,
			    const char **args, const char *dir);
static gboolean type_open(const char *path, MIME_type *type);

typedef struct _PipedData PipedData;

struct _PipedData
{
	guchar		*data;
	gint		tag;
	gulong		sent;
	gulong		length;
};

static Option o_run_action_helper;
static Option o_browser_command;

typedef struct {
	gchar *url;
	gchar *custom_command;
	gboolean custom_pending;
	guint fallback_index;
} BrowserLaunch;

typedef struct {
	BrowserLaunch *launch;
	guint timeout_id;
	gchar *description;
} BrowserAttempt;

typedef struct {
	const gchar *program;
	gboolean new_tab;
} BrowserFallback;

static const BrowserFallback browser_fallbacks[] = {
	{ "defaultbrowser", TRUE },
	{ "defaultbrowser", FALSE },
	{ "x-www-browser", TRUE },
	{ "x-www-browser", FALSE },
	{ "gnome-www-browser", TRUE },
	{ "gnome-www-browser", FALSE },
	{ "xdg-open", FALSE },
	{ NULL, FALSE }
};

static void browser_launch_free(BrowserLaunch *launch)
{
	if (!launch)
		return;
	g_free(launch->url);
	g_free(launch->custom_command);
	g_free(launch);
}

static gchar *browser_replace_uri_token(const gchar *arg, const gchar *url,
					gboolean *replaced)
{
	const gchar *cursor = arg;
	const gchar *match;
	GString *out;

	if (!arg)
		return g_strdup("");

	out = g_string_new(NULL);
	while ((match = strstr(cursor, "%u")) != NULL)
	{
		g_string_append_len(out, cursor, match - cursor);
		g_string_append(out, url ? url : "");
		cursor = match + 2;
		if (replaced)
			*replaced = TRUE;
	}
	g_string_append(out, cursor);
	return g_string_free(out, FALSE);
}

static GPtrArray *browser_build_custom_argv(const gchar *command,
						 const gchar *url)
{
	gchar **parsed = NULL;
	gint argc = 0;
	GError *error = NULL;
	GPtrArray *argv;
	gboolean replaced = FALSE;
	gint i;

	if (!command || !*command)
		return NULL;

	if (!g_shell_parse_argv(command, &argc, &parsed, &error))
	{
		rox_debug_log("BROWSER", "invalid custom command=%s error=%s",
			command, error ? error->message : "");
		g_clear_error(&error);
		return NULL;
	}

	if (argc < 1 || !parsed || !parsed[0] || !*parsed[0])
	{
		g_strfreev(parsed);
		return NULL;
	}

	argv = g_ptr_array_new_with_free_func(g_free);
	for (i = 0; i < argc; i++)
		g_ptr_array_add(argv, browser_replace_uri_token(parsed[i], url, &replaced));
	g_strfreev(parsed);

	if (!replaced && url && *url)
		g_ptr_array_add(argv, g_strdup(url));
	g_ptr_array_add(argv, NULL);
	return argv;
}

static GPtrArray *browser_build_fallback_argv(const BrowserFallback *fallback,
						   const gchar *url)
{
	GPtrArray *argv;

	if (!fallback || !fallback->program)
		return NULL;

	argv = g_ptr_array_new_with_free_func(g_free);
	g_ptr_array_add(argv, g_strdup(fallback->program));
	if (fallback->new_tab)
		g_ptr_array_add(argv, g_strdup("--new-tab"));
	if (url && *url)
		g_ptr_array_add(argv, g_strdup(url));
	g_ptr_array_add(argv, NULL);
	return argv;
}

static gboolean browser_launch_try_next(BrowserLaunch *launch);

static gboolean browser_attempt_timeout(gpointer data)
{
	BrowserAttempt *attempt = data;

	attempt->timeout_id = 0;
	if (attempt->launch)
	{
		rox_debug_log("BROWSER", "accepted=%s (process remained alive)",
			attempt->description ? attempt->description : "");
		browser_launch_free(attempt->launch);
		attempt->launch = NULL;
	}
	return G_SOURCE_REMOVE;
}

static void browser_attempt_child_status(gpointer data, gint status)
{
	BrowserAttempt *attempt = data;
	BrowserLaunch *launch = attempt->launch;
	GError *status_error = NULL;
	gboolean ok;

	if (attempt->timeout_id)
	{
		g_source_remove(attempt->timeout_id);
		attempt->timeout_id = 0;
	}

	/* If launch is NULL, the 2-second grace period already expired and this
	 * was accepted as a real browser process. ROX's SIGCHLD reaper has already
	 * collected it; there is nothing else to do. */
	if (!launch)
	{
		g_free(attempt->description);
		g_free(attempt);
		return;
	}

	attempt->launch = NULL;
	ok = g_spawn_check_wait_status(status, &status_error);
	if (ok)
	{
		rox_debug_log("BROWSER", "success=%s",
			attempt->description ? attempt->description : "");
		browser_launch_free(launch);
	}
	else
	{
		rox_debug_log("BROWSER", "failed=%s error=%s; trying next fallback",
			attempt->description ? attempt->description : "",
			status_error ? status_error->message : "");
		g_clear_error(&status_error);
		browser_launch_try_next(launch);
	}

	g_free(attempt->description);
	g_free(attempt);
}

static gboolean browser_spawn_attempt(BrowserLaunch *launch, GPtrArray *argv,
						 const gchar *description)
{
	BrowserAttempt *attempt;
	GError *error = NULL;
	GPid pid = 0;
	gboolean spawned;

	if (!argv || argv->len < 2 || !g_ptr_array_index(argv, 0))
		return FALSE;

	rox_debug_log("BROWSER", "trying=%s url=%s",
		description ? description : (const gchar *) g_ptr_array_index(argv, 0),
		launch->url ? launch->url : "");

	spawned = g_spawn_async(g_get_home_dir(), (gchar **) argv->pdata, NULL,
		G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
		NULL, NULL, &pid, &error);
	if (!spawned)
	{
		rox_debug_log("BROWSER", "spawn failed=%s error=%s",
			description ? description : "", error ? error->message : "");
		g_clear_error(&error);
		return FALSE;
	}

	attempt = g_new0(BrowserAttempt, 1);
	attempt->launch = launch;
	attempt->description = g_strdup(description ? description : "browser");
	on_child_death_status(pid, browser_attempt_child_status, attempt);
	/* Unsupported --new-tab options normally fail immediately. Keep a small
	 * grace period so we can retry without the option, but never reopen a URL
	 * later merely because a long-running browser eventually exits non-zero. */
	attempt->timeout_id = g_timeout_add(2000, browser_attempt_timeout, attempt);
	return TRUE;
}

static gboolean browser_launch_try_next(BrowserLaunch *launch)
{
	GPtrArray *argv = NULL;
	gboolean spawned;

	if (launch->custom_pending)
	{
		launch->custom_pending = FALSE;
		argv = browser_build_custom_argv(launch->custom_command, launch->url);
		if (argv)
		{
			spawned = browser_spawn_attempt(launch, argv, "configured browser");
			g_ptr_array_free(argv, TRUE);
			if (spawned)
				return TRUE;
		}
	}

	while (browser_fallbacks[launch->fallback_index].program)
	{
		const BrowserFallback *fallback = &browser_fallbacks[launch->fallback_index++];
		gchar *description;

		argv = browser_build_fallback_argv(fallback, launch->url);
		description = g_strdup_printf("%s%s", fallback->program,
			fallback->new_tab ? " --new-tab" : "");
		spawned = browser_spawn_attempt(launch, argv, description);
		g_free(description);
		g_ptr_array_free(argv, TRUE);
		if (spawned)
			return TRUE;
	}

	delayed_error(_("No suitable web browser was found. Configure one in Options > Desktop, or install defaultbrowser, x-www-browser, gnome-www-browser or xdg-open."));
	browser_launch_free(launch);
	return FALSE;
}

void run_init(void)
{
	option_add_string(&o_run_action_helper, "run_action_helper", "");
	option_add_string(&o_browser_command, "browser_command", "");
}

gboolean rox_open_browser(const gchar *url)
{
	BrowserLaunch *launch;
	gchar *saved_command;

	/* Rox-Filer2 may have a long-running --desktop process while Options is
	 * edited in another filer process. Read the last saved browser command
	 * at launch time so pressing OK takes effect immediately everywhere,
	 * without restarting the desktop or the system. */
	saved_command = option_get_saved("browser_command");

	launch = g_new0(BrowserLaunch, 1);
	launch->url = g_strdup((url && *url) ? url : "about:blank");
	launch->custom_command = saved_command ? saved_command :
		g_strdup((const gchar *) o_browser_command.value);
	launch->custom_pending = launch->custom_command && *launch->custom_command;
	launch->fallback_index = 0;
	return browser_launch_try_next(launch);
}

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/


/* An application has been double-clicked (or run in some other way) */
void run_app(const char *path)
{
	GString	*apprun;
	const char *argv[] = {NULL, NULL};

	apprun = g_string_new(path);
	argv[0] = g_string_append(apprun, "/AppRun")->str;

	rox_spawn(home_dir, argv);

	g_string_free(apprun, TRUE);
}

/* Execute this program, passing all the URIs in the list as arguments.
 * URIs that are files on the local machine will be passed as simple
 * pathnames. The uri_list should be freed after this function returns.
 */
void run_with_files(const char *path, GList *uri_list)
{
	const char	**argv;
	int		argc = 0, i;
	struct stat 	info;
	MIME_type	*type;

	if (stat(path, &info))
	{
		delayed_error(_("Program %s not found - deleted?"), path);
		return;
	}

	argv = g_malloc(sizeof(char *) * (g_list_length(uri_list) + 2));

	if (S_ISDIR(info.st_mode))
		argv[argc++] = make_path(path, "AppRun");
	else
		argv[argc++] = path;

	while (uri_list)
	{
		const EscapedPath *uri = uri_list->data;
		char *local;

		local = get_local_path(uri);
		if (local)
			argv[argc++] = local;
		else
			argv[argc++] = unescape_uri(uri);
		uri_list = uri_list->next;
	}

	argv[argc++] = NULL;

	type = type_from_path(argv[0]);
	if (type && type == application_x_desktop)
	{
		run_desktop(argv[0], argv + 1, home_dir);
	}
	else
	{
		rox_spawn(home_dir, argv);
	}

	for (i = 1; i < argc; i++)
		g_free((gchar *) argv[i]);
	g_free(argv);
}

/* Run the program as '<path> -', piping the data to it via stdin.
 * You can g_free() the data as soon as this returns.
 */
void run_with_data(const char *path, gpointer data, gulong length)
{
	const char	*argv[] = {NULL, "-", NULL};
	struct stat 	info;
	int		fds[2];
	PipedData	*pd;

	if (stat(path, &info))
	{
		delayed_error(_("Program %s not found - deleted?"), path);
		return;
	}

	if (S_ISDIR(info.st_mode))
		argv[0] = make_path(path, "AppRun");
	else
		argv[0] = path;

	if (pipe(fds))
	{
		delayed_error("pipe: %s", g_strerror(errno));
		return;
	}
	close_on_exec(fds[1], TRUE);
	close_on_exec(fds[0], TRUE);

	switch (fork())
	{
		case -1:
			delayed_error("fork: %s", g_strerror(errno));
			close(fds[1]);
			break;
		case 0:
			/* We are the child */
			chdir(home_dir);
			if (dup2(fds[0], 0) == -1)
				g_warning("dup2() failed: %s\n",
						g_strerror(errno));
			else
			{
				close_on_exec(0, FALSE);
				if (execv(argv[0], (char **) argv))
					g_warning("execv(%s) failed: %s\n",
						argv[0], g_strerror(errno));
			}
			_exit(1);
		default:
			/* We are the parent */
			set_blocking(fds[1], FALSE);
			pd = g_new(PipedData, 1);
			pd->data = g_malloc(length);
			memcpy(pd->data, data, length);
			pd->length = length;
			pd->sent = 0;
			pd->tag = rox_input_add_full(fds[1], ROX_INPUT_WRITE,
						write_data, pd, NULL);
			break;
	}

	close(fds[0]);
}

/* Splits args into an argument vector, and runs the program. Must be
 * executable.
 */
void run_with_args(const char *path, DirItem *item, const char *args)
{
	GError *error = NULL;
	gchar **argv = NULL;
	int n_args = 0;

	if (item->base_type != TYPE_DIRECTORY && item->base_type != TYPE_FILE)
	{
		delayed_error("Arguments (%s) given for non-executable item %s",
				args, path);
		return;
	}

	if (!g_shell_parse_argv(args, &n_args, &argv, &error))
	{
		delayed_error("Failed to parse argument string '%s':\n%s",
				args, error->message);
		g_error_free(error);
		return;
	}

	g_return_if_fail(argv != NULL);
	g_return_if_fail(error == NULL);

	argv = g_realloc(argv, (n_args + 2) * sizeof(gchar *));
	memmove(argv + 1, argv, (n_args + 1) * sizeof(gchar *));

	if (item->base_type == TYPE_DIRECTORY)
		argv[0] = g_strconcat(path, "/AppRun", NULL);
	else
		argv[0] = g_strdup(path);

	rox_spawn(home_dir, (const gchar **) argv);

	g_strfreev(argv);
}

/* Load a file, open a directory or run an application. Or, if 'edit' is set:
 * edit a file, open an application, follow a symlink or mount a device.
 *
 * filer_window is the window to use for displaying a directory.
 * NULL will always use a new directory when needed.
 * src_window is the window to copy options from, or NULL.
 *
 * Returns TRUE on success.
 */
gboolean run_diritem(const gchar *full_path,
		     DirItem *item,
		     FilerWindow *filer_window,
		     FilerWindow *src_window,
		     gboolean edit)
{
	if (item->flags & ITEM_FLAG_SYMLINK && edit)
		return follow_symlink(full_path, filer_window, src_window);

	switch (item->base_type)
	{
		case TYPE_DIRECTORY:
			if (item->flags & ITEM_FLAG_APPDIR && !edit)
			{
				run_app(full_path);
				return TRUE;
			}

			if (item->flags & ITEM_FLAG_MOUNT_POINT)
			{
				open_mountpoint(full_path, item,
						filer_window, src_window, edit);
			}
			else if (filer_window)
				filer_change_to(filer_window, full_path, NULL);
			else
				filer_opendir(full_path, src_window, NULL);
			return TRUE;
		case TYPE_FILE:
			if (EXECUTABLE_FILE(item) && !edit)
			{
				const char *argv[] = {NULL, NULL};
				guchar	*dir = filer_window
						? filer_window->sym_path
						: NULL;

				if (item->mime_type == application_x_desktop)
					return run_desktop(full_path,
							   NULL, dir);
				else
					argv[0] = full_path;

				return rox_spawn(dir, argv) != 0;
			}

			return open_file(full_path, edit ? text_plain
						  : item->mime_type);
		case TYPE_ERROR:
			delayed_error(_("File doesn't exist, or I can't "
					  "access it: %s"), full_path);
			return FALSE;
		default:
		        delayed_error(
				_("I don't know how to open '%s'"), full_path);
			return FALSE;
	}
}

/* Attempt to open this item */
gboolean run_by_path(const gchar *full_path)
{
	gboolean retval;
	DirItem	*item;

	/* XXX: Loads an image - wasteful */
	item = diritem_new("");
	diritem_restat(full_path, item, NULL);
	retval = run_diritem(full_path, item, NULL, NULL, FALSE);
	diritem_free(item);

	return retval;
}

/* Convert uri to path and call run_by_path() */
gboolean run_by_uri(const gchar *uri, gchar **errmsg)
{
	gboolean retval;
	gchar *tmp, *tmp2;
	gchar *scheme;
	gchar *cmd;

	scheme=get_uri_scheme((EscapedPath *) uri);
	if(!scheme)
	{
		*errmsg=g_strdup_printf(_("'%s' is not a valid URI"),
						uri);
		return FALSE;
	}

	if(strcmp(scheme, "file")==0) {
		tmp=get_local_path((EscapedPath *) uri);
		if(tmp) {
			tmp2=pathdup(tmp);
			retval=run_by_path(tmp2);
			if(!retval)
				*errmsg=g_strdup_printf(_("%s not accessable"),
							tmp);

			g_free(tmp2);
			g_free(tmp);

		} else {
			retval=FALSE;
			*errmsg=g_strdup_printf(_("Non-local URL %s"), uri);
		}

	} else if((cmd=choices_find_xdg_path_load(scheme, "URI", SITE))) {
		DirItem *item;

		item=diritem_new(scheme);
		diritem_restat(cmd, item, NULL);

		run_with_args(cmd, item, uri);
		retval=TRUE; /* we hope... */

		diritem_free(item);
		g_free(cmd);

	} else {
		retval=FALSE;
		*errmsg=g_strdup_printf(_("%s: no handler for %s"),
					uri, scheme);
	}

	g_free(scheme);

	return retval;
}

/* Open dir/Help, or show a message if missing */
void show_help_files(const char *dir)
{
	const char	*help_dir;

	help_dir = make_path(dir, "Help");

	if (file_exists(help_dir))
		filer_opendir(help_dir, NULL, NULL);
	else
		info_message(
			_("Application:\n"
			"This is an application directory - you can "
			"run it as a program, or open it (hold down "
			"Shift while you open it). Most applications provide "
			"their own help here, but this one doesn't."));
}

/* Open a directory viewer showing this file, and wink it */
void open_to_show(const gchar *path)
{
	FilerWindow	*new;
	guchar		*dir, *slash;

	g_return_if_fail(path != NULL);

	dir = g_strdup(path);
	slash = strrchr(dir, '/');
	if (slash == dir || !slash)
	{
		/* Item in the root (or root itself!) */
		new = filer_opendir("/", NULL, NULL);
		if (new && dir[1])
			display_set_autoselect(new, dir + 1);
	}
	else
	{
		*slash = '\0';
		new = filer_opendir(dir, NULL, NULL);
		if (new)
		{
			if (slash[1] == '.')
				display_set_hidden(new, TRUE);
			display_set_autoselect(new, slash + 1);
		}
	}

	g_free(dir);
}

/* Invoked using -x, this indicates that the filesystem has been modified
 * and we should look at this item again.
 */
void examine(const gchar *path)
{
	struct stat info;

	if (mc_stat(path, &info) != 0)
	{
		/* Deleted? Do a paranoid update of everything... */
		filer_check_mounted(path);
	}
	else
	{
		/* Update directory containing this item... */
		dir_check_this(path);

		/* If this is itself a directory then rescan its contents... */
		if (S_ISDIR(info.st_mode))
			refresh_dirs(path);

		/* If it is referenced by a legacy panel, update the icon... */
		icons_may_update(path);
	}
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/


static void write_data(gpointer data, gint fd, RoxInputCondition cond)
{
	PipedData *pd = (PipedData *) data;

	while (pd->sent < pd->length)
	{
		int	sent;

		sent = write(fd, pd->data + pd->sent, pd->length - pd->sent);

		if (sent < 0)
		{
			if (errno == EAGAIN)
				return;
			delayed_error(_("Could not send data to program: %s"),
					g_strerror(errno));
			goto finish;
		}

		pd->sent += sent;
	}

finish:
	g_source_remove(pd->tag);
	g_free(pd->data);
	g_free(pd);
	close(fd);
}

/* Follow the link 'full_path' and display it in filer_window, or a
 * new window if that is NULL.
 */
static gboolean follow_symlink(const char *full_path,
			       FilerWindow *filer_window,
			       FilerWindow *src_window)
{
	char	*real, *slash;
	char	*new_dir;
	char	path[MAXPATHLEN + 1];
	int	got;

	got = readlink(full_path, path, MAXPATHLEN);
	if (got < 0)
	{
		delayed_error(_("Could not read link: %s"),
				  g_strerror(errno));
		return FALSE;
	}

	g_return_val_if_fail(got <= MAXPATHLEN, FALSE);
	path[got] = '\0';

	/* Make a relative path absolute */
	if (path[0] != '/')
	{
		guchar	*tmp;
		slash = strrchr(full_path, '/');
		g_return_val_if_fail(slash != NULL, FALSE);

		tmp = g_strndup(full_path, slash - full_path);
		real = pathdup(make_path(tmp, path));
		/* NB: full_path may be invalid here... */
		g_free(tmp);
	}
	else
		real = pathdup(path);

	slash = strrchr(real, '/');
	if (!slash)
	{
		g_free(real);
		delayed_error(
			_("Broken symlink (or you don't have permission "
			  "to follow it): %s"), full_path);
		return FALSE;
	}

	*slash = '\0';

	if (*real)
		new_dir = real;
	else
		new_dir = "/";

	if (filer_window)
		filer_change_to(filer_window, new_dir, slash + 1);
	else
	{
		FilerWindow *new;

		new = filer_opendir(new_dir, src_window, NULL);
		if (new)
			display_set_autoselect(new, slash + 1);
	}

	g_free(real);

	return TRUE;
}

/* Load this file into an appropriate editor */
static gboolean open_file(const guchar *path, MIME_type *type)
{
	g_return_val_if_fail(type != NULL, FALSE);

	if (type_open(path, type))
		return TRUE;

	if (o_run_action_helper.value && strcmp(o_run_action_helper.value, ""))
	{
		GError *error = NULL;
		gint argc = 0;
		gchar **argv = NULL;
		int i, j;
		gboolean success = FALSE;
		GPtrArray *expanded = NULL;

		gchar *mimetype_string;

		mimetype_string = g_strconcat(
			type->media_type, "/", type->subtype, NULL);

		if (!g_shell_parse_argv(
			o_run_action_helper.value, &argc, &argv, &error))
		{
			delayed_error("Failed to parse '%s':\n%s",
				o_run_action_helper.value, error->message);
			goto run_action_helper_err;
		}

		expanded = g_ptr_array_new();

		for (i = 0; i < argc; i++)
		{
			const char *src = argv[i];
			gchar **str_array;

			str_array = g_strsplit(src, "%", -1);

			/* If src contained at least 1 "%" character, */
			if (str_array[1])
			{
				GString *new_arg = g_string_new(str_array[0]);

				for (j = 1; str_array[j]; j++)

					switch (str_array[j][0])
					{
						case 'f':
						case 'F':
							g_string_append(new_arg, path);
							break;
						case 'm':
						case 'M':
							g_string_append(new_arg, mimetype_string);
							break;
						default:
							g_string_append(new_arg, "%");
							g_string_append(new_arg, str_array[j]);
					}

				g_ptr_array_add(expanded, g_strdup(new_arg->str));
				g_string_free(new_arg, TRUE);
			}
			else
			{
				g_ptr_array_add(expanded, g_strdup(src));
			}

			g_strfreev(str_array);
		}

		g_ptr_array_add(expanded, NULL);

		success = rox_spawn(home_dir, (const gchar **) expanded->pdata);

run_action_helper_err:
		if (error != NULL)
			g_error_free(error);
		if (argv != NULL)
			g_strfreev(argv);
		if (expanded != NULL)
		{
			g_ptr_array_foreach(expanded, (GFunc) g_free, NULL);
			g_ptr_array_free(expanded, TRUE);
		}

		g_free(mimetype_string);

		if (success)
			return TRUE;
	}

	/* No default exists. Open the standard GTK application chooser instead of
	 * falling back to the historical ROX OpenWith directories. The chooser also
	 * offers Add Custom Application, which creates a user .desktop file and can
	 * register it through GIO/mimeapps.list. */
	{
		GList *paths = g_list_prepend(NULL, (gpointer) path);
		GtkWindow *parent = window_with_focus
			? GTK_WINDOW(window_with_focus->window) : NULL;

		xdg_apps_choose_for_paths(paths, parent, TRUE);
		g_list_free(paths);
	}

	return TRUE;
}

/* Called like run_diritem, when a mount-point is opened */
static void open_mountpoint(const guchar *full_path, DirItem *item,
			    FilerWindow *filer_window, FilerWindow *src_window,
			    gboolean edit)
{
	gboolean mounted = (item->flags & ITEM_FLAG_MOUNTED) != 0;

	if (mounted == edit)
	{
		GList	*paths;

		paths = g_list_prepend(NULL, (gpointer) full_path);
		action_mount(paths, filer_window == NULL, !mounted, -1);
		g_list_free(paths);
		if (filer_window && !mounted)
			filer_change_to(filer_window, full_path, NULL);
	}
	else
	{
		if (filer_window)
			filer_change_to(filer_window, full_path, NULL);
		else
			filer_opendir(full_path, src_window, NULL);
	}
}

/* Agregado por josejp2424 (2026): convertir una ruta local en URI para los
 * códigos %u y %U del estándar Desktop Entry. */
static gchar *desktop_argument_uri(const gchar *argument)
{
	gchar *uri;

	if (!argument || !*argument)
		return NULL;
	if (strstr(argument, "://"))
		return g_strdup(argument);
	uri = g_filename_to_uri(argument, NULL, NULL);
	return uri ? uri : g_strdup(argument);
}

/* Agregado por josejp2424 (2026): expandir códigos Desktop Entry dentro de
 * un argumento. Esto cubre lanzadores que usan --option=%f además de los
 * códigos independientes habituales. */
static gchar *expand_desktop_token(const gchar *src, const char **args,
		const gchar *desktop_file, const gchar *name, const gchar *icon,
		gboolean *inserted_args)
{
	GString *out = g_string_new(NULL);
	const gchar *p;

	for (p = src; *p; p++)
	{
		const gchar *value = NULL;
		gchar *uri = NULL;

		if (*p != '%' || p[1] == '\0')
		{
			g_string_append_c(out, *p);
			continue;
		}

		p++;
		switch (*p)
		{
			case '%':
				g_string_append_c(out, '%');
				break;
			case 'f':
			case 'F':
				if (args && args[0])
					value = args[0];
				*inserted_args = TRUE;
				break;
			case 'u':
			case 'U':
				if (args && args[0])
				{
					uri = desktop_argument_uri(args[0]);
					value = uri;
				}
				*inserted_args = TRUE;
				break;
			case 'i':
				value = icon;
				break;
			case 'c':
				value = name;
				break;
			case 'k':
				value = desktop_file;
				break;
			case 'd': case 'D': case 'n': case 'N':
			case 'v': case 'm':
				break;
			default:
				g_string_append_c(out, '%');
				g_string_append_c(out, *p);
				break;
		}

		if (value)
			g_string_append(out, value);
		g_free(uri);
	}

	return g_string_free(out, FALSE);
}

/* full_path is a .desktop file. Execute the application, using the Exec line
 * from the file.
 * Returns TRUE on success.
 */
static gboolean run_desktop(const char *full_path,
			    const char **args,
			    const char *dir)
{
	GError *error = NULL;
	char *exec = NULL;
	char *terminal = NULL;
	char *req_dir = NULL;
	char *name = NULL;
	char *icon = NULL;
	gint argc = 0;
	gchar **argv = NULL;
	GPtrArray *expanded = NULL;
	gboolean inserted_args = FALSE;
	int i;
	gboolean success = FALSE;

	get_values_from_desktop_file(full_path,
					&error,
					"Desktop Entry", "Exec", &exec,
					"Desktop Entry", "Terminal", &terminal,
					"Desktop Entry", "Path", &req_dir,
					"Desktop Entry", "Name", &name,
					"Desktop Entry", "Icon", &icon,
					NULL);
	if (error)
	{
		delayed_error("Failed to parse .desktop file '%s':\n%s",
				full_path, error->message);
		goto err;
	}

	if (!exec)
	{
		delayed_error("Can't find Exec command in .desktop file '%s'",
				full_path);
		goto err;
	}

	if (!g_shell_parse_argv(exec, &argc, &argv, &error))
	{
		delayed_error("Failed to parse '%s' from '%s':\n%s",
				exec, full_path, error->message);
		goto err;
	}

	expanded = g_ptr_array_new();

	if (terminal && g_ascii_strcasecmp(terminal, "true") == 0) {
		g_ptr_array_add(expanded, g_strdup("xterm"));
		g_ptr_array_add(expanded, g_strdup("-e"));
	}

	for (i = 0; i < argc; i++)
	{
		const char *src = argv[i];

		if (g_strcmp0(src, "%F") == 0 || g_strcmp0(src, "%U") == 0)
		{
			int j;
			for (j = 0; args && args[j]; j++)
			{
				if (src[1] == 'U')
					g_ptr_array_add(expanded, desktop_argument_uri(args[j]));
				else
					g_ptr_array_add(expanded, g_strdup(args[j]));
			}
			inserted_args = TRUE;
			continue;
		}
		if (g_strcmp0(src, "%f") == 0 || g_strcmp0(src, "%u") == 0)
		{
			if (args && args[0])
			{
				if (src[1] == 'u')
					g_ptr_array_add(expanded, desktop_argument_uri(args[0]));
				else
					g_ptr_array_add(expanded, g_strdup(args[0]));
			}
			inserted_args = TRUE;
			continue;
		}
		if (g_strcmp0(src, "%i") == 0)
		{
			if (icon && *icon)
			{
				g_ptr_array_add(expanded, g_strdup("--icon"));
				g_ptr_array_add(expanded, g_strdup(icon));
			}
			continue;
		}

		{
			gchar *token = expand_desktop_token(src, args, full_path,
				name, icon, &inserted_args);
			if (token && *token)
				g_ptr_array_add(expanded, token);
			else
				g_free(token);
		}
	}

	if (!inserted_args)
	{
		int j;
		for (j = 0; args && args[j]; j++)
			g_ptr_array_add(expanded, g_strdup(args[j]));
	}
	g_ptr_array_add(expanded, NULL);

	if (req_dir && req_dir[0])
		dir = req_dir;

	success = rox_spawn(dir, (const gchar **) expanded->pdata);
err:
	if (error != NULL)
		g_error_free(error);
	g_free(exec);
	g_free(terminal);
	g_free(req_dir);
	g_free(name);
	g_free(icon);
	if (argv != NULL)
		g_strfreev(argv);
	if (expanded != NULL)
	{
		g_ptr_array_foreach(expanded, (GFunc) g_free, NULL);
		g_ptr_array_free(expanded, TRUE);
	}

	return success;
}

gboolean run_desktop_entry(const char *desktop_file,
                           const char **args, const char *working_dir)
{
	return run_desktop(desktop_file, args,
		working_dir && *working_dir ? working_dir : g_get_home_dir());
}

/* Open a regular file with the application selected by the standard
 * XDG/GIO association system. Returns FALSE only when no association exists,
 * allowing the optional compatibility helper to run afterwards. */
static gboolean type_open(const char *path, MIME_type *type)
{
	GAppInfo *app;
	GList paths = {0};
	gboolean launched;
	GtkWindow *parent = window_with_focus
		? GTK_WINDOW(window_with_focus->window) : NULL;

	g_return_val_if_fail(path != NULL, FALSE);
	g_return_val_if_fail(type != NULL, FALSE);

	app = type_get_default_application(type);
	if (!app)
		return FALSE;

	paths.data = (gpointer) path;
	launched = xdg_apps_launch_app_info(app, &paths, parent);
	g_object_unref(app);

	/* Si tanto GIO como el Exec= directo fallan, devolver FALSE permite que
	 * open_file() muestre el selector de aplicaciones en vez de tragarse el
	 * primer intento sin abrir nada. */
	return launched;
}

