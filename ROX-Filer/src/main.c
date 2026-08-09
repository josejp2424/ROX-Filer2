/*
 * Rox-Filer2, continued from the original ROX-Filer project
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

/* main.c - parses command-line options and parameters, plus some global
 * 	    housekeeping.
 *
 * New to the code and feeling lost? Read global.h now.
 */

/*
 * Modificado por josejp2424 (2026):
 * port a GTK3 y adaptaciones específicas de esta versión.
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <grp.h>
#include <libxml/parser.h>

#ifdef HAVE_GETOPT_LONG
#  include <getopt.h>
#endif

#include <gtk/gtk.h>
#include <gdk/gdkx.h>		/* For rox_x_error */

#include "global.h"

#include "main.h"
#include "log.h"
#include "debug_log.h"
#include "support.h"
#include "gui_support.h"
#include "filer.h"
#include "display.h"
#include "mount.h"
#include "menu.h"
#include "dnd.h"
#include "options.h"
#include "choices.h"
#include "type.h"
#include "pixmaps.h"
#include "dir.h"
#include "diritem.h"
#include "action.h"
#include "i18n.h"
#include "remote.h"
#include "run.h"
#include "toolbar.h"
#include "bind.h"
#include "bookmarks.h"
#include "panel.h"
#include "session.h"
#include "minibuffer.h"
#include "xtypes.h"
#include "bulk_rename.h"
#include "gtksavebox.h"
#include "desktop.h"
#include "rox_config.h"
#include "filer_pair.h"
#include "search_integration.h"
#include "xdg_apps.h"
#include "custom_actions.h"

int number_of_windows = 0;	/* Quit when this reaches 0 again... */
int to_wakeup_pipe = -1;	/* Write here to get noticed */

/* Information about the Rox-Filer2 process */
uid_t euid;
gid_t egid;
int ngroups;			/* Number of supplemental groups */
gid_t *supplemental_groups = NULL;

/* Message to display at the top of each filer window */
const gchar *show_user_message = NULL;

int home_dir_len;
const char *home_dir, *app_dir;


#define COPYING                                                        \
        N_("Original ROX-Filer copyright (C) 2005 Thomas Leonard and contributors.\n" \
           "Rox-Filer2 continuation and development (C) 2026 josejp2424.\n" \
           "Rox-Filer2 comes with ABSOLUTELY NO WARRANTY,\n" \
           "to the extent permitted by law.\n" \
           "You may redistribute copies under the terms of the GNU General Public License.\n" \
           "For more information, see the file named COPYING.\n")

#ifdef HAVE_GETOPT_LONG
#  define USAGE   N_("Try `rox --help' for more information.\n")
#  define SHORT_ONLY_WARNING ""
#else
#  define USAGE   N_("Try `rox -h' for more information.\n")
#  define SHORT_ONLY_WARNING	\
		_("NOTE: Your system does not support long options - \n" \
		"you must use the short versions instead.\n\n")
#endif

#define BUGS_TO "<puppylinuxjosejp2424@gmail.com>"

#define HELP N_("Usage: rox [OPTION]... [FILE]...\n" \
       "Rox-Filer2 opens each directory or file listed, or the current working\n" \
       "directory if no arguments are given.\n\n" \
       "Desktop:\n" \
       "      --desktop\tstart the single Rox-Filer2 Desktop instance\n\n" \
       "File manager:\n" \
       "  -c, --client-id=ID\tused for session management\n" \
       "  -d, --dir=DIR\t\topen DIR as directory (not application)\n" \
       "  -D, --close=DIR\tclose DIR and its subdirectories\n" \
       "  -h, --help\t\tdisplay this help and exit\n" \
       "  -m, --mime-type=FILE\tprint MIME type of FILE and exit\n" \
       "  -n, --new\t\tstart a separate filer instance\n" \
       "  -R, --RPC\t\tinvoke method call read from stdin\n" \
       "  -s, --show=FILE\topen a directory showing FILE\n" \
       "  -u, --user\t\tshow user name in each window\n" \
       "  -U, --url=URL\t\topen file or directory in URI form\n" \
       "  -v, --version\t\tdisplay version information and exit\n" \
       "  -x, --examine=FILE\tFILE has changed - re-examine it\n\n" \
       "Legacy ROX panel compatibility:\n" \
       "  -b, --border=PANEL\topen PANEL as a border panel\n" \
       "  -B, --bottom=PANEL\topen PANEL as a bottom-edge panel\n" \
       "  -l, --left=PANEL\topen PANEL as a left-edge panel\n" \
       "  -r, --right=PANEL\topen PANEL as a right-edge panel\n" \
       "  -t, --top=PANEL\topen PANEL as a top-edge panel\n" \
       "\nReport bugs to %s.\n" \
       "Rox-Filer2 continuation: josejp2424\n" \
       "Original ROX-Filer author: Thomas Leonard\n" \
       "Project: https://github.com/josejp2424/ROX-Filer-gtk3\n")
#define SHORT_OPS "c:d:t:b:l:r:B:os:hvnux:m:D:RU:"

#ifdef HAVE_GETOPT_LONG
static struct option long_opts[] =
{
	{"dir", 1, NULL, 'd'},
	{"top", 1, NULL, 't'},
	{"bottom", 1, NULL, 'B'},
	{"border", 1, NULL, 'b'},
	{"left", 1, NULL, 'l'},
	{"override", 0, NULL, 'o'},
	{"right", 1, NULL, 'r'},
	{"help", 0, NULL, 'h'},
	{"version", 0, NULL, 'v'},
	{"user", 0, NULL, 'u'},
	{"new", 0, NULL, 'n'},
	{"RPC", 0, NULL, 'R'},
	{"show", 1, NULL, 's'},
	{"examine", 1, NULL, 'x'},
	{"close", 1, NULL, 'D'},
	{"mime-type", 1, NULL, 'm'},
	{"client-id", 1, NULL, 'c'},
	{"url", 1, NULL, 'U'},
	{"desktop", 0, NULL, 1000},
	{"desktop-wallpaper", 0, NULL, 1001},
	{"desktop-apps", 0, NULL, 1002},
	{"desktop-refresh", 0, NULL, 1003},
	{"pair", 0, NULL, 1004},
	{"pair-realign", 0, NULL, 1005},
	{"debug", 0, NULL, 1200},
	{"log-file", 1, NULL, 1201},
	{"log-level", 1, NULL, 1202},
	{"clear-logs", 0, NULL, 1203},
	/* Hidden options used only by tools/rox-filer-diagnostico.sh. */
	{"diagnose-open-with", 1, NULL, 1100},
	{"diagnose-terminal", 1, NULL, 1101},
	{"diagnose-rename", 1, NULL, 1102},
	{NULL, 0, NULL, 0},
};
#endif

/* Take control of panels away from WM? */
Option o_override_redirect;

/* Always start a new filer, even if one seems to be already running */
gboolean new_copy = FALSE;
static gboolean desktop_mode = FALSE;

typedef enum {
	DESKTOP_TOOL_NONE = 0,
	DESKTOP_TOOL_WALLPAPER,
	DESKTOP_TOOL_APPS,
	DESKTOP_TOOL_REFRESH
} DesktopTool;

static DesktopTool desktop_tool = DESKTOP_TOOL_NONE;
static gboolean pair_mode = FALSE;
static gboolean pair_realign_mode = FALSE;
static gchar *pair_left_arg = NULL;
static gchar *pair_right_arg = NULL;
static gchar *diagnose_open_with_desktop = NULL;
static gchar *diagnose_open_with_path = NULL;
static gchar *diagnose_terminal_path = NULL;
static gchar *diagnose_rename_path = NULL;

/* Maps child PIDs to Callback pointers */
static GHashTable *death_callbacks = NULL;
/* Rox-Filer2: serialize synchronous child waits against the legacy SIGCHLD
 * reaper. Without this, a worker-thread g_spawn_sync() can lose its child to
 * child_died_callback(), producing GLib ECHILD warnings. */
static GMutex child_reap_mutex;
static gboolean child_died_flag = FALSE;

Option o_dnd_no_hostnames;

/* Static prototypes */
static void show_features(void);
static void print_help_text(void);
static void soap_add(xmlNodePtr body,
			   xmlChar *function,
			   const xmlChar *arg1_name, const xmlChar *arg1_value,
			   const xmlChar *arg2_name, const xmlChar *arg2_value);
static void soap_reply(xmlDocPtr reply, gboolean rpc_mode);
static void child_died(int signum);
static void child_died_callback(void);
static void wake_up_cb(gpointer data, gint source, RoxInputCondition condition);
static void xrandr_size_change(GdkScreen *screen, gpointer user_data);
static GList *build_launch(Option *option, xmlNode *node, guchar *label);
static GList *build_make_script(Option *option, xmlNode *node, guchar *label);

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

/* The value that goes with an option */
#define VALUE (*optarg == '=' ? optarg + 1 : optarg)

static int rox_x_error(Display *display, XErrorEvent *error)
{
	gchar buf[64];

	XGetErrorText(display, error->error_code, buf, 63);

	g_warning ("The program '%s' received an X Window System error.\n"
			"This probably reflects a bug in the program.\n"
			"The error was '%s'.\n"
			"  (Details: serial %ld error_code %d request_code %d minor_code %d)\n"
			"  (Note to programmers: normally, X errors are reported asynchronously;\n"
			"   that is, you will receive the error a while after causing it.\n"
			"   To debug your program, run it with the --sync command line\n"
			"   option to change this behavior. You can then get a meaningful\n"
			"   backtrace from your debugger.)",
			g_get_prgname (),
			buf,
			error->serial,
			error->error_code,
			error->request_code,
			error->minor_code);

	/* Try to cope with BadWindow errors */
	if (error->error_code == BadWindow || error->error_code == BadDrawable)
	{
		g_warning(_("We got a BadWindow error from the X server. "
			    "This might be due to this GTK bug (during drag-and-drop?):\n"
			    "http://bugzilla.gnome.org/show_bug.cgi?id=152151\n"
			    "Trying to continue..."));
		return 0;
	}

	abort();
}

/* Modificado por josejp2424 (2026): permitir ejecutar el binario
 * directamente, sin depender de que AppRun defina APP_DIR. */
static gchar *find_application_directory(const gchar *argv0)
{
	const gchar *configured = g_getenv("APP_DIR");
	gchar *executable = NULL;
	gchar *directory;

	if (configured && *configured)
		return g_strdup(configured);

#ifdef __linux__
	/* /proc/self/exe resuelve también lanzadores y enlaces simbólicos. */
	executable = g_file_read_link("/proc/self/exe", NULL);
#endif

	if (!executable && argv0 && *argv0)
	{
		if (g_path_is_absolute(argv0))
			executable = g_strdup(argv0);
		else if (strchr(argv0, G_DIR_SEPARATOR))
		{
			gchar *current = g_get_current_dir();
			executable = g_build_filename(current, argv0, NULL);
			g_free(current);
		}
		else
			executable = g_find_program_in_path(argv0);
	}

	if (!executable)
		return g_get_current_dir();

	directory = g_path_get_dirname(executable);
	g_free(executable);
	return directory;
}


/* r73: un único binario puede forzar el backend según el enlace usado.
 * Debe hacerse antes de gtk_init(), porque GTK selecciona el backend al
 * abrir el display. */
static void configure_backend_from_program_name(const gchar *argv0)
{
    gchar *base;

    if (!argv0 || !*argv0)
        return;
    base = g_path_get_basename(argv0);
    if (g_strcmp0(base, "rox-x11") == 0) {
        g_setenv("GDK_BACKEND", "x11", TRUE);
        g_setenv("ROX_DESKTOP_BACKEND", "x11", TRUE);
    } else if (g_strcmp0(base, "rox-wayland") == 0) {
        g_setenv("GDK_BACKEND", "wayland", TRUE);
        g_setenv("ROX_DESKTOP_BACKEND", "wayland", TRUE);
    }
    g_free(base);
}

/* Parses the command-line to work out what the user wants to do.
 * Tries to send the request to an already-running copy of the filer.
 * If that fails, it initialises all the other modules and executes the
 * request itself.
 */
int main(int argc, char **argv)
{
	int		 wakeup_pipe[2];
	int		 i;
	struct sigaction act;
	guchar		*tmp, *dir;
	gchar *client_id = NULL, *base;
	gboolean	show_user = FALSE;
	gboolean	rpc_mode = FALSE;
	xmlDocPtr	rpc, soap_rpc = NULL, reply;
	xmlNodePtr	body;
	int		fd, ofd0=-1;
	gboolean clear_logs_requested = FALSE;
	GError *debug_log_error = NULL;

	g_mutex_init(&child_reap_mutex);

	/* Relocate stdin. We do need it (-R), but it can cause problems if
	 * a child process wants a password, etc...
	 * Do this BEFORE opening anything (e.g., the X connection), in
	 * case fd 0 isn't open at this point.
	 */
	fd = open("/dev/null", O_RDONLY);
	if (fd > 0)
	{
		ofd0=dup(0);
		close(0);
		dup2(fd, 0);
		close(fd);
	}

	configure_backend_from_program_name(argv[0]);
	g_set_application_name("Rox-Filer2");
	if (!rox_debug_log_preconfigure(argc, argv, &clear_logs_requested,
	                                &debug_log_error))
	{
		g_printerr("Rox-Filer2: %s\n", debug_log_error
		           ? debug_log_error->message : "unable to configure log");
		g_clear_error(&debug_log_error);
		return EXIT_FAILURE;
	}
	if (clear_logs_requested && argc == 2 &&
	    g_strcmp0(argv[1], "--clear-logs") == 0)
	{
		g_print("Rox-Filer2: diagnostic logs removed.\n");
		return EXIT_SUCCESS;
	}
	ROX_LOG_INFO("startup", "program=%s version=%s GDK_BACKEND=%s forced_backend=%s",
	             argv[0] ? argv[0] : "", VERSION,
	             g_getenv("GDK_BACKEND") ? g_getenv("GDK_BACKEND") : "auto",
	             g_getenv("ROX_DESKTOP_BACKEND")
	                 ? g_getenv("ROX_DESKTOP_BACKEND") : "auto");
	home_dir = g_get_home_dir();
	home_dir_len = strlen(home_dir);
	app_dir = find_application_directory(argv[0]);
	ROX_LOG_INFO("startup", "app_dir=%s home_dir=%s",
	             app_dir ? app_dir : "", home_dir ? home_dir : "");

	/* Rox-Filer2 conserva APP_DIR en el entorno. No se llama unsetenv() aquí:
	 * GLib/GTK puede haber inicializado soporte interno con hilos incluso antes
	 * de gtk_init(), y modificar el entorno global en ese punto no es seguro. */

	/* Get internationalisation up and running. This requires the
	 * choices system, to discover the user's preferred language.
	 */
	choices_init();
	options_init();
	i18n_init();
	xattr_init();

	/* Sometimes we want to take special action when a child
	 * process exits. This hash table is used to convert the
	 * child's PID to the callback function.
	 */
	death_callbacks = g_hash_table_new(NULL, NULL);

	/* Find out some information about ourself */
	euid = geteuid();
	egid = getegid();
	ngroups = getgroups(0, NULL);
	if (ngroups < 0)
		ngroups = 0;
	else if (ngroups > 0)
	{
		supplemental_groups = g_malloc(sizeof(gid_t) * ngroups);
		getgroups(ngroups, supplemental_groups);
	}

	if (argc == 2 &&
	    (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0))
	{
		/* Version output must also work without X11/Wayland. */
		g_print("Rox-Filer2 %s\n", VERSION);
		g_print("%s", _(COPYING));
		show_features();
		return EXIT_SUCCESS;
	}

	if (argc == 2 &&
	    (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0))
	{
		/* `rox --help` is a console operation and must not require a display. */
		print_help_text();
		return EXIT_SUCCESS;
	}

	option_add_int(&o_override_redirect, "override_redirect", FALSE);

	option_register_widget("launch", build_launch);
	option_register_widget("make-script", build_make_script);

#ifdef UNIT_TESTS
	bulk_rename_tests();
#endif

	/* The idea here is to convert the command-line arguments
	 * into a SOAP RPC.
	 * We attempt to invoke the call on an already-running copy of
	 * the filer if possible, or execute it ourselves if not.
	 */
	rpc = soap_new(&body);

	/* Note: must do this before checking our options,
	 * otherwise we report an error for Gtk's options.
	 */
	ROX_LOG_DEBUG("gtk", "calling gtk_init");
	gtk_init(&argc, &argv);
	if (gdk_display_get_default())
		ROX_LOG_INFO("gtk", "display type=%s name=%s monitors=%d",
		             G_OBJECT_TYPE_NAME(gdk_display_get_default()),
		             gdk_display_get_name(gdk_display_get_default()),
		             gdk_display_get_n_monitors(gdk_display_get_default()));
	else
		ROX_LOG_ERROR("gtk", "gtk_init completed without a default display");
	/* GTK3 loads the active GtkSettings, including
	 * ~/.config/gtk-3.0/settings.ini.  Do not install application-wide
	 * hard-coded colours here: they override the system theme. */

	g_signal_connect(gdk_screen_get_default(), "size-changed",
			 G_CALLBACK(xrandr_size_change), NULL);

	/* Process each option in turn */
	while (1)
	{
		int	c;
#ifdef HAVE_GETOPT_LONG
		int	long_index;
		c = getopt_long(argc, argv, SHORT_OPS,
				long_opts, &long_index);
#else
		c = getopt(argc, argv, SHORT_OPS);
#endif

		if (c == EOF)
			break;		/* No more options */

		switch (c)
		{
			case 'n':
				new_copy = TRUE;
				break;
			case 1000:
				desktop_mode = TRUE;
				new_copy = TRUE;
				break;
			case 1001:
				desktop_tool = DESKTOP_TOOL_WALLPAPER;
				new_copy = TRUE;
				break;
			case 1002:
				desktop_tool = DESKTOP_TOOL_APPS;
				new_copy = TRUE;
				break;
			case 1003:
				desktop_tool = DESKTOP_TOOL_REFRESH;
				new_copy = TRUE;
				break;
			case 1004:
				pair_mode = TRUE;
				break;
			case 1005:
				pair_realign_mode = TRUE;
				break;
			case 1200: /* --debug: configured before gtk_init */
			case 1201: /* --log-file */
			case 1202: /* --log-level */
			case 1203: /* --clear-logs */
				break;
			case 1100:
				diagnose_open_with_desktop = g_strdup(VALUE);
				new_copy = TRUE;
				break;
			case 1101:
				diagnose_terminal_path = pathdup(VALUE);
				new_copy = TRUE;
				break;
			case 1102:
				diagnose_rename_path = pathdup(VALUE);
				new_copy = TRUE;
				break;
			case 'o':
				info_message(_("The -o argument is no longer "
					"used. You can turn on override "
					"redirect from the Options box "
					"instead."));
				break;
			case 'v':
				g_print("Rox-Filer2 %s\n", VERSION);
				g_print("%s", _(COPYING));
				show_features();
				return EXIT_SUCCESS;
			case 'h':
				print_help_text();
#ifndef HAVE_GETOPT_LONG
				g_print("%s", _(SHORT_ONLY_WARNING));
#endif
				return EXIT_SUCCESS;
			case 'D':
			case 'd':
		        case 'x':
				/* Argument is a path */
				if (c == 'd' && VALUE[0] == '/')
					tmp = g_strdup(VALUE);
				else
					tmp = pathdup(VALUE);
				soap_add(body,
					c == 'D' ? "CloseDir" :
					c == 'd' ? "OpenDir" :
					c == 'x' ? "Examine" : "Unknown",
					"Filename", tmp,
					NULL, NULL);
				g_free(tmp);
				break;
			case 's':
				tmp = g_path_get_dirname(VALUE);

				if (tmp[0] == '/')
					dir = NULL;
				else
					dir = pathdup(tmp);

				base = g_path_get_basename(VALUE);
				soap_add(body, "Show",
					"Directory", dir ? dir : tmp,
					"Leafname", base);
				g_free(tmp);
				g_free(dir);
				g_free(base);
				break;
			case 'l':
			case 'r':
			case 't':
			case 'B':
				/* Argument is a leaf (or starts with /) */
				soap_add(body, "Panel", "Name", VALUE,
					 "Side", c == 'l' ? "Left" :
						 c == 'r' ? "Right" :
						 c == 't' ? "Top" :
						 c == 'B' ? "Bottom" :
						 "Unkown");
				break;
			case 'b':
				/* Argument is a leaf (or starts with /) */
				if (*VALUE)
					soap_add(body, "Panel", "Name", VALUE,
							NULL, NULL);
				else
					soap_add(body, "Panel",
							"Side", "Bottom",
							NULL, NULL);
				break;
			case 'u':
				show_user = TRUE;
				break;
		        case 'm':
			{
				MIME_type *type;
				type_init();
				diritem_init();
				pixmaps_init();
				type = type_get_type(VALUE);
				printf("%s/%s\n", type->media_type,
						type->subtype);
				return EXIT_SUCCESS;
			}
			case 'c':
				client_id = g_strdup(VALUE);
				break;
			case 'R':
				/* Reconnect stdin */
				if(ofd0>-1) {
					close(0);
					dup2(ofd0, 0);
				}
				soap_rpc = xmlParseFile("-");
				if (!soap_rpc)
					g_error("Invalid XML in RPC");
				/* Disconnect stdin again */
				fd = open("/dev/null", O_RDONLY);
				if (fd > 0)
				{
					close(0);
					dup2(fd, 0);
					close(fd);
				}
				/* Want to print return uninterpreted */
				rpc_mode=TRUE;

				break;

		        case 'U':
				soap_add(body, "RunURI",
						"URI", VALUE, NULL, NULL);
				break;

			default:
				printf(_(USAGE));
				return EXIT_FAILURE;
		}
	}


	ROX_LOG_DEBUG("command", "parsed desktop=%d tool=%d pair=%d new_copy=%d rpc=%d",
	              desktop_mode, desktop_tool, pair_mode, new_copy, rpc_mode);
	if (show_user)
		show_user_message = g_strdup_printf(_("Running as user '%s'"),
						    user_name(euid));

	/* Add each remaining (non-option) argument to the list of files
	 * to run.
	 */
	i = optind;
	if (diagnose_open_with_desktop)
	{
		if (i >= argc)
		{
			g_printerr("--diagnose-open-with requires one FILE argument\n");
			return EXIT_FAILURE;
		}
		diagnose_open_with_path = pathdup(argv[i++]);
		if (i < argc)
		{
			g_printerr("--diagnose-open-with accepts only one FILE argument\n");
			return EXIT_FAILURE;
		}
	}
	while (!diagnose_open_with_desktop && !diagnose_terminal_path &&
	       !diagnose_rename_path && i < argc)
	{
		tmp = pathdup(argv[i++]);
		if (pair_mode)
		{
			if (!pair_left_arg)
				pair_left_arg = g_strdup(tmp);
			else if (!pair_right_arg)
				pair_right_arg = g_strdup(tmp);
			else
			{
				g_printerr("%s\n", _("--pair accepts at most two folders."));
				g_free(tmp);
				return EXIT_FAILURE;
			}
		}
		else
			soap_add(body, "Run", "Filename", tmp, NULL, NULL);
		g_free(tmp);
	}

	if (pair_mode)
		soap_add(body, "PairWindows",
			"Left", pair_left_arg, "Right", pair_right_arg);
	if (pair_realign_mode)
		soap_add(body, "PairRealign", NULL, NULL, NULL, NULL);

	if (soap_rpc)
	{
		if (body->xmlChildrenNode)
			g_error("Can't use -R with other options - sorry!");
		xmlFreeDoc(rpc);
		body = NULL;
		rpc = soap_rpc;
	}
	else if (!body->xmlChildrenNode && !desktop_mode &&
	         desktop_tool == DESKTOP_TOOL_NONE &&
	         !diagnose_open_with_desktop && !diagnose_terminal_path &&
	         !diagnose_rename_path)
	{
		/* The user didn't request any action. Open the current
		 * directory.
		 */
		guchar	*dir;

		dir = g_get_current_dir();
		soap_add(body, "OpenDir", "Filename", dir, NULL, NULL);
		g_free(dir);
	}

	option_add_int(&o_dnd_no_hostnames, "dnd_no_hostnames", 1);

	/* ROX Desktop is a single desktop service. A second --desktop command
	 * refreshes the existing X11/XLibre instance and exits instead of
	 * creating another full-screen desktop window. */
	gui_support_init();
	if (desktop_mode && desktop_send_refresh_request()) {
		ROX_LOG_INFO("desktop", "refresh request delivered to existing desktop; exiting");
		xmlFreeDoc(rpc);
		return EXIT_SUCCESS;
	}

	/* Try to send the request to an already-running copy of the filer */
	if (!desktop_mode && desktop_tool == DESKTOP_TOOL_NONE &&
	    !diagnose_open_with_desktop && !diagnose_terminal_path &&
	    !diagnose_rename_path && remote_init(rpc, new_copy)) {
		ROX_LOG_INFO("remote", "request delivered to an existing Rox-Filer2 process");
		xmlFreeDoc(rpc);	/* avoid memleak */
		return EXIT_SUCCESS;	/* It worked - exit */
	}

	/* Put ourselves into the background (so 'rox' always works the
	 * same, whether we're already running or not).
	 * Not for -n, though (helps when debugging).
	 */
	if (!new_copy)
	{
		GdkDisplay *current_display = gdk_display_get_default();

		/* X11 historically backgrounds the filer after connecting to the
		 * display.  Doing that on Wayland is unsafe: the child inherits a
		 * libwayland connection created before fork(), which can leave a normal
		 * filer launch from the menu with no visible window.  Keep the Wayland
		 * process intact; graphical launchers do not require daemonisation. */
		if (current_display && GDK_IS_X11_DISPLAY(current_display))
		{
			pid_t child = fork();

			if (child > 0)
			{
				ROX_LOG_DEBUG("process", "X11 background child pid=%ld; parent exiting",
				              (long)child);
				_exit(0);
			}
			ROX_LOG_DEBUG("process", "X11 background process pid=%ld",
			              (long)getpid());
		}
		else
		{
			ROX_LOG_INFO("process",
			             "non-X11 display=%s: skipping post-GTK fork for safe native launch",
			             current_display ? G_OBJECT_TYPE_NAME(current_display) : "none");
		}
	}

	/* Initialize the rest of the filer... */

	ROX_LOG_DEBUG("startup", "initializing Rox-Filer2 modules");
	pixmaps_init();

	log_init();
	dnd_init();
	bind_init();
	bookmarks_init();
	dir_init();
	diritem_init();
	menu_init();
	xdg_apps_init();
	custom_actions_init();
	minibuffer_init();
	filer_init();
	filer_pair_init();
	search_integration_init();
	toolbar_init();
	display_init();
	mount_init();
	type_init();
	action_init();
	ROX_LOG_DEBUG("startup", "core modules initialized");

	panel_init();
	run_init();

	/* Let everyone update */
	options_notify();

	/* The diagnostic script invokes the exact launch paths without opening a
	 * filer window. These options are intentionally omitted from --help. */
	if (diagnose_open_with_desktop || diagnose_terminal_path ||
	    diagnose_rename_path)
	{
		gboolean diagnostic_ok;

		if (!g_getenv("ROX_DIAGNOSTIC"))
		{
			g_printerr("Diagnostic options require ROX_DIAGNOSTIC=1\n");
			xmlFreeDoc(rpc);
			return EXIT_FAILURE;
		}

		if (diagnose_open_with_desktop)
			diagnostic_ok = xdg_apps_diagnose_launch(
				diagnose_open_with_desktop, diagnose_open_with_path);
		else if (diagnose_terminal_path)
			diagnostic_ok = menu_diagnose_run_in_terminal(
				diagnose_terminal_path);
		else
			diagnostic_ok = menu_diagnose_rename_dialog(
				diagnose_rename_path);

		xmlFreeDoc(rpc);
		g_clear_pointer(&diagnose_open_with_desktop, g_free);
		g_clear_pointer(&diagnose_open_with_path, g_free);
		g_clear_pointer(&diagnose_terminal_path, g_free);
		g_clear_pointer(&diagnose_rename_path, g_free);
		return diagnostic_ok ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	/* When we get a signal, we can't do much right then. Instead,
	 * we send a char down this pipe, which causes the main loop to
	 * deal with the event next time we're idle.
	 */
	pipe(wakeup_pipe);
	close_on_exec(wakeup_pipe[0], TRUE);
	close_on_exec(wakeup_pipe[1], TRUE);
	rox_input_add_full(wakeup_pipe[0], ROX_INPUT_READ, wake_up_cb,
			NULL, NULL);
	to_wakeup_pipe = wakeup_pipe[1];

	/* If the pipe is full then we're going to get woken up anyway... */
	set_blocking(to_wakeup_pipe, FALSE);

	/* Let child processes die */
	act.sa_handler = child_died;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_NOCLDSTOP;
	sigaction(SIGCHLD, &act, NULL);

	/* Ignore SIGPIPE - check for EPIPE errors instead */
	act.sa_handler = SIG_IGN;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	sigaction(SIGPIPE, &act, NULL);

	/* Set up session managament if available */
	session_init(client_id);
	g_free(client_id);

	/* See if we need to migrate the Choices directories*/
	choices_migrate();

	/* Agregado por josejp2424 (2026): iniciar el escritorio nativo. */
	rox_config_init();
	desktop_init();
	if (desktop_mode)
	{
		ROX_LOG_INFO("desktop", "starting ROX Desktop");
		desktop_start();
	}
	else if (desktop_tool != DESKTOP_TOOL_NONE)
	{
		if (desktop_tool == DESKTOP_TOOL_WALLPAPER)
			desktop_open_wallpaper_manager();
		else if (desktop_tool == DESKTOP_TOOL_APPS)
			desktop_open_apps_manager();
		else if (desktop_tool == DESKTOP_TOOL_REFRESH)
		{
			if (!desktop_send_refresh_request())
			{
				g_printerr("%s\n", _("No running ROX Desktop instance was found."));
				xmlFreeDoc(rpc);
				return EXIT_FAILURE;
			}
		}
		xmlFreeDoc(rpc);
		return EXIT_SUCCESS;
	}

	/* Finally, execute the request */
	reply = run_soap(rpc);
	if (!pair_mode && !pair_realign_mode)
		filer_pair_startup_if_enabled();
	xmlFreeDoc(rpc);
	soap_reply(reply, rpc_mode);
	g_clear_pointer(&pair_left_arg, g_free);
	g_clear_pointer(&pair_right_arg, g_free);

	/* Convert X11 protocol failures into ROX diagnostics instead of aborting.
	 * Do not install X11-specific handling for a native Wayland display. */
	if (GDK_IS_X11_DISPLAY(gdk_display_get_default()))
		XSetErrorHandler(rox_x_error);

	/* Enter the main loop, processing events until all our windows
	 * are closed.
	 */
	if (number_of_windows > 0)
	{
		ROX_LOG_INFO("main-loop", "entering GTK main loop; windows=%d",
		             number_of_windows);
		gtk_main();
		ROX_LOG_INFO("main-loop", "GTK main loop finished");
	}

	return EXIT_SUCCESS;
}

void rox_child_reap_lock(void)
{
	g_mutex_lock(&child_reap_mutex);
}

void rox_child_reap_unlock(void)
{
	g_mutex_unlock(&child_reap_mutex);
}

/* Register a function to be called when process number 'child' dies. */
void on_child_death(gint child, CallbackFn callback, gpointer data)
{
	Callback	*cb;

	g_return_if_fail(callback != NULL);

	cb = g_new(Callback, 1);

	cb->callback = callback;
	cb->data = data;

	g_hash_table_insert(death_callbacks, GINT_TO_POINTER(child), cb);
}

void one_less_window(void)
{
	if (--number_of_windows < 1)
		gtk_main_quit();
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void print_help_text(void)
{
	gchar *formatted;
	gchar *desktop_option;
	gchar *line_end;
	gchar *prefix;

	formatted = g_strdup_printf(_(HELP), BUGS_TO);
	desktop_option = strstr(formatted, "--desktop");
	line_end = desktop_option ? strchr(desktop_option, '\n') : NULL;

	if (!line_end)
	{
		g_print("%s", formatted);
		g_free(formatted);
		return;
	}

	prefix = g_strndup(formatted, (line_end + 1) - formatted);
	g_print("%s", prefix);
	g_print("\nLaunchers:\n");
	g_print("      rox\t\t%s\n", _("automatically select X11 or Wayland"));
	g_print("      rox-x11\t\t%s\n", _("force the X11 backend"));
	g_print("      rox-wayland\t%s\n\n", _("force the native Wayland backend"));
	g_print("Desktop commands:\n");
	g_print("      --desktop-wallpaper\t%s\n",
		_("open the desktop wallpaper manager"));
	g_print("      --desktop-apps\t%s\n",
		_("open the desktop application manager"));
	g_print("      --desktop-refresh\t%s\n",
		_("refresh the running Rox-Filer2 Desktop"));
	g_print("      rox-x11 --desktop\t%s\n",
		_("force the X11 desktop backend"));
	g_print("      rox-wayland --desktop\t%s\n",
		_("force Wayland Layer Shell"));
	g_print("      --pair [LEFT RIGHT]\t%s\n",
		_("open two Rox-Filer2 windows side by side"));
	g_print("      --pair-realign\t%s\n",
		_("realign the current paired windows"));
	g_print("\nDiagnostics (disabled by default):\n");
	g_print("      --debug\t\twrite a technical diagnostic log\n");
	g_print("      --log-file=FILE\twrite the diagnostic log to FILE\n");
	g_print("      --log-level=LEVEL\terror, warning, info, debug or trace\n");
	g_print("      --clear-logs\tremove automatically managed Rox-Filer2 logs\n");
	g_print("%s", line_end + 1);

	g_free(prefix);
	g_free(formatted);
}

static void show_features(void)
{
	g_print("\n");
	g_print(_("Compiled with GTK version %s\n"), GTK_VERSION);
	g_print(_("Running with GTK version %d.%d.%d\n"),
				gtk_major_version,
				gtk_minor_version,
				gtk_micro_version);
	g_print("\n-- %s --\n\n", _("features set at compile time"));
	g_print("%s... %s\n", _("Large File Support"),
#ifdef LARGE_FILE_SUPPORT
		_("Yes")
#else
		_("No")
#endif
		);
	g_print("%s... %s\n", _("Binary compatibility"),
#if defined(HAVE_APSYMBOLS_H) || defined(HAVE_APBUILD_APSYMBOLS_H)
		_("Yes (can run with older glibc versions)")
#else
		_("No (apsymbols.h not found)")
#endif
	       );

	g_print("%s... %s\n", _("Extended attribute support"),
		xattr_supported(NULL)? _("Yes"): _("No"));
}

static void soap_add(xmlNodePtr body,
			   xmlChar *function,
			   const xmlChar *arg1_name, const xmlChar *arg1_value,
			   const xmlChar *arg2_name, const xmlChar *arg2_value)
{
	xmlNodePtr node;
	xmlNs *rox;

	rox = xmlSearchNsByHref(body->doc, body, ROX_NS);

	node = xmlNewChild(body, rox, function, NULL);

	if (arg1_name)
	{
		xmlNewTextChild(node, rox, arg1_name, arg1_value);
		if (arg2_name)
			xmlNewTextChild(node, rox, arg2_name, arg2_value);
	}
}

static void soap_reply(xmlDocPtr reply, gboolean rpc_mode)
{
	gboolean print=TRUE;

	if(!reply)
		return;

	if(!rpc_mode) {
		gchar **errs=extract_soap_errors(reply);

		if(errs) {
			int i;

			print=FALSE;

			for(i=0; errs[i]; i++)
				fprintf(stderr, "%s\n", errs[i]);

			g_strfreev(errs);
		}
	}

	/* Write the result, if any, to stdout */
	if(print)
		save_xml_file(reply, "-");
	xmlFreeDoc(reply);
}

/* This is called as a signal handler; simply ensures that
 * child_died_callback() will get called later.
 */
static void child_died(int signum)
{
	child_died_flag = TRUE;
	write(to_wakeup_pipe, "\0", 1);	/* Wake up! */
}

static void child_died_callback(void)
{
	int status;
	gint child;

	child_died_flag = FALSE;

	/* Find out which children exited and allow them to die. Serialize the
	 * waitpid(-1) reaper with synchronous waits running in worker threads. */
	for (;;)
	{
		Callback *cb = NULL;
		CallbackFn callback = NULL;
		gpointer callback_data = NULL;

		rox_child_reap_lock();
		child = waitpid(-1, &status, WNOHANG);
		if (child > 0)
		{
			cb = g_hash_table_lookup(death_callbacks, GINT_TO_POINTER(child));
			if (cb)
			{
				callback = cb->callback;
				callback_data = cb->data;
				g_hash_table_remove(death_callbacks, GINT_TO_POINTER(child));
				g_free(cb);
			}
		}
		rox_child_reap_unlock();

		if (child == 0 || child == -1)
			return;
		if (callback)
			callback(callback_data);
	}
}

#define BUFLEN 40
/* When data is written to_wakeup_pipe, this gets called from the event
 * loop some time later. Useful for getting out of signal handlers, etc.
 */
static void wake_up_cb(gpointer data, gint source, RoxInputCondition condition)
{
	char buf[BUFLEN];

	read(source, buf, BUFLEN);

	if (child_died_flag)
		child_died_callback();
}

static void xrandr_size_change(GdkScreen *screen, gpointer user_data)
{
	gui_store_screen_geometry(screen);

	panel_update_size();
	desktop_refresh_after_environment_change();
}

static GtkWidget *launch_button_new(const char *label, const char *uri,
				    const char *appname)
{
	GtkWidget *button;
	GClosure *closure;
	const gchar *slash;
	gchar *tip;

	button = button_new_mixed(ROX_ICON_PREFERENCES, label);
	closure = g_cclosure_new(G_CALLBACK(launch_uri),
					g_strdup(uri),
					(GClosureNotify) g_free);
	g_signal_connect_closure(button, "clicked", closure, FALSE);
	if(appname) {
		g_object_set_data_full(G_OBJECT(button), "appname",
				       g_strdup(appname),
				       (GDestroyNotify) g_free);
	}

	allow_right_click(button);

	slash = strrchr(uri, '/');
	if (!slash)
		slash = uri - 1;
	tip = g_strdup_printf(
			_("Left-click to run %s.\n"
			  "Right-click for a list of versions."),
			slash + 1);

	gtk_widget_set_tooltip_text(button, tip);

	g_free(tip);

	return button;
}

static GList *build_launch(Option *option, xmlNode *node, guchar *label)
{
	GtkWidget *button;
	char *uri;
	char *appname;

	g_return_val_if_fail(option == NULL, NULL);
	g_return_val_if_fail(label != NULL, NULL);

	uri = xmlGetProp(node, "uri");
	appname = xmlGetProp(node, "appname");

	g_return_val_if_fail(uri != NULL, NULL);

	button = launch_button_new(_(label), uri, appname);
	gtk_widget_set_halign(button, GTK_ALIGN_START);
	gtk_widget_set_valign(button, GTK_ALIGN_CENTER);

	g_free(uri);
	if(appname)
	  g_free(appname);

	return g_list_append(NULL, button);
}

/* Call back from save box to create a rox script */
static gint new_script_cb(GObject *savebox,
			 const gchar *path, gpointer data)
{
       FILE *fp;

       fp = fopen(path, "w");

       if (fp == NULL)
       {
               report_error(_("Error creating '%s': %s"),
                               path, g_strerror(errno));
	       return GTK_XDS_SAVE_ERROR;
        }

       fprintf(fp, "#!/bin/sh\n");
       fprintf(fp, "exec %s/AppRun \"$@\"\n", app_dir);

       fclose(fp);
       chmod(path, 0755);

       dir_check_this(path);

       return GTK_XDS_SAVED;
}

/* Option button to create the rox script clicked */
static void make_script_clicked(GtkWidget *button, gpointer udata)
{
	const gchar *filename;
	GtkWidget   *savebox;
	MaskedPixmap *image;

	/* Default to saving in current filer window */
	if(window_with_focus)
		filename=make_path(window_with_focus->sym_path, "rox");
	else
		filename="rox";
	image = type_to_icon(application_x_shellscript);

	/* Create a save box to save the script */
	savebox = gtk_savebox_new(_("Save"));
	gtk_savebox_set_action(GTK_SAVEBOX(savebox), GDK_ACTION_COPY);
	g_signal_connect(savebox, "save_to_file",
				G_CALLBACK(new_script_cb), NULL);

	gtk_window_set_title(GTK_WINDOW(savebox), _("Start script"));

	gtk_savebox_set_pathname(GTK_SAVEBOX(savebox), filename);
	gtk_savebox_set_icon(GTK_SAVEBOX(savebox), image->pixbuf);
	g_object_unref(image);

	gtk_widget_show(savebox);
}

/* Build option button to create rox script */
static GList *build_make_script(Option *option, xmlNode *node, guchar *label)
{
	GtkWidget *button;
	gchar     *tip;

	g_return_val_if_fail(option == NULL, NULL);
	g_return_val_if_fail(label != NULL, NULL);


	button = gtk_button_new_with_label(_(label));
	g_signal_connect(button, "clicked", G_CALLBACK(make_script_clicked),
			 NULL);

	tip = _("Click to save a script to run Rox-Filer2.\n"
		"If you are using Zero Install you should use 0alias "
		"instead.");
	gtk_widget_set_tooltip_text(button, tip);

	gtk_widget_set_halign(button, GTK_ALIGN_START);
	gtk_widget_set_valign(button, GTK_ALIGN_CENTER);

	return g_list_append(NULL, button);
}
