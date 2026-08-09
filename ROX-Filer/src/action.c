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

/* action.c - code for handling the filer action windows.
 * These routines generally fork() and talk to us via pipes.
 */

/*
 * Modificado por josejp2424 (2026):
 * port a GTK3 y adaptaciones específicas de esta versión.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/param.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <utime.h>
#include <stdarg.h>
#include <gio/gio.h>

#include "global.h"

#include "action.h"
#include "trash.h"
#include "abox.h"
#include "string.h"
#include "support.h"
#include "gui_support.h"
#include "filer.h"
#include "display.h"
#include "main.h"
#include "options.h"
#include "modechange.h"
#include "find.h"
#include "dir.h"
#include "icon.h"
#include "mount.h"
#include "type.h"
#include "xtypes.h"
#include "log.h"

#if defined(HAVE_GETXATTR)
# define ATTR_MAN_PAGE N_("See the attr(5) man page for full details.")
#elif defined(HAVE_ATTROPEN)
# define ATTR_MAN_PAGE N_("See the fsattr(5) man page for full details.")
#else
# define ATTR_MAN_PAGE N_("You do not appear to have OS support.")
#endif

/* Parent->Child messages are one character each:
 *
 * Y/N 		Yes/No button clicked
 * F		Force deletion of non-writeable items
 * Q		Quiet toggled
 * E		Entry text changed
 * W		neWer toggled
 */

typedef struct _GUIside GUIside;
typedef void ActionChild(gpointer data);
typedef void ForDirCB(const char *path, const char *dest_path);

/* Agregado por josejp2424 (2026): política de conflictos compartida por
 * el motor clásico y el nuevo motor rápido basado en rsync. */
typedef enum
{
	CONFLICT_ASK = 0,
	CONFLICT_REPLACE_ALL,
	CONFLICT_SKIP_EXISTING,
	CONFLICT_UPDATE_NEWER,
	CONFLICT_CANCELLED
} ConflictPolicy;

/* Agregado por josejp2424 (2026): respuestas directas para los botones
 * visibles de política de conflictos. Se mantienen fuera del rango de las
 * respuestas estándar de GtkDialog. */
enum
{
	RESPONSE_CONFLICT_ASK = 1201,
	RESPONSE_CONFLICT_REPLACE,
	RESPONSE_CONFLICT_SKIP,
	RESPONSE_CONFLICT_NEWER
};

typedef enum
{
	RSYNC_FAILED = -1,
	RSYNC_SKIPPED = 0,
	RSYNC_DONE = 1
} RsyncResult;

struct _GUIside
{
	ABox		*abox;		/* The action window widget */

	int 		from_child;	/* File descriptor */
	FILE		*to_child;
	int 		input_tag;	/* rox_input_add() */
	pid_t		child;		/* Process ID */
	int		errors;		/* Number of errors so far */
	gboolean	show_info;	/* For Disk Usage */

	guchar		**default_string; /* Changed when the entry changes */
	void		(*entry_string_func)(GtkWidget *widget,
					     const guchar *string);

	int		abort_attempts;
	gboolean	finishing;	/* Completion/pipe cleanup already handled */
};

/* These don't need to be in a structure because we fork() before
 * using them again.
 */
static gboolean mount_open_dir = FALSE;
static gboolean mount_mount = FALSE;	/* (FALSE => unmount) */
static int 	from_parent = 0;
static FILE	*to_parent = NULL;
static gboolean	quiet = FALSE;
static GString  *message = NULL;
static const char *action_dest = NULL;
static const char *action_leaf = NULL;
static void (*action_do_func)(const char *source, const char *dest);
/* Agregado por josejp2424 (2026): selección por operación. El valor se
 * copia al proceso hijo en fork(), por lo que nunca queda activo para la
 * siguiente copia o movimiento. */
static ConflictPolicy conflict_policy = CONFLICT_ASK;
static gboolean use_rsync_engine = FALSE;
static gboolean rsync_available = FALSE;
/* Agregado por josejp2424 (2026): el borrado permanente confirmado se
 * procesa por lotes, sin preguntar ni refrescar por cada archivo interno. */
static gboolean delete_batch_mode = FALSE;
static double	size_tally;		/* For Disk Usage */
static unsigned long dir_counter;	/* For Disk Usage */
static unsigned long file_counter;	/* For Disk Usage */

static struct mode_change *mode_change = NULL;	/* For Permissions */
static FindCondition *find_condition = NULL;	/* For Find */
static MIME_type *type_change = NULL;

/* Only used by child */
static gboolean o_force = FALSE;
static gboolean o_brief = FALSE;
static gboolean o_recurse = FALSE;
static gboolean o_merge = FALSE;
static gboolean o_newer = FALSE;
static gboolean o_ignore = FALSE;

static Option o_action_copy, o_action_move, o_action_link;
static Option o_action_delete, o_action_mount;
static Option o_action_force, o_action_brief, o_action_recurse;
static Option o_action_merge, o_action_newer, o_action_ignore;

static Option o_action_mount_command;
static Option o_action_umount_command;
static Option o_action_eject_command;

/* Whenever the text in these boxes is changed we store a copy of the new
 * string to be used as the default next time.
 */
static guchar	*last_chmod_string = NULL;
static guchar	*last_find_string = NULL;
static guchar	*last_settype_string = NULL;

/* Set to one of the above before forking. This may change over a call to
 * reply(). It is reset to NULL once the text is parsed.
 */
static guchar	*new_entry_string = NULL;

/* Static prototypes */
static void send_done(void);
static void send_check_path(const gchar *path);
static void send_mount_path(const gchar *path);
static gboolean printf_send(const char *msg, ...);
static gboolean send_msg(void);
static gboolean send_error(void);
static gboolean send_dir(const char *dir);
static gboolean read_exact(int source, char *buffer, ssize_t len);
static void do_mount(const guchar *path, gboolean mount);
static gboolean printf_reply(int fd, gboolean ignore_quiet,
			     const char *msg, ...);
static gboolean printf_conflict_reply(int fd, const char *msg, ...);
static gboolean remove_pinned_ok(GList *paths);
static const char *make_dest_path(const char *object, const char *dir);
static void do_copy2(const char *path, const char *dest);
static void do_move2(const char *path, const char *dest);
static gboolean destination_has_conflicts(GList *paths, const char *dest,
					 const char *leaf);
static ConflictPolicy choose_conflict_policy(const gchar *operation);
static RsyncResult run_rsync_operation(const char *source, const char *dest_path,
				      gboolean remove_source);
static void remove_empty_source_dirs(const char *path);
static void do_copy_fast(const char *path, const char *dest);
static void do_move_fast(const char *path, const char *dest);
static void list_cb(gpointer data);
static void rsync_copy_list_cb(gpointer data);
/* Agregado por josejp2424: cierre fiable de los diálogos de copia,
 * movimiento y borrado mediante un mensaje explícito de finalización. */
static void finish_action(GUIside *gui_side);
/* Agregado por josejp2424 (2026): papelera estándar Freedesktop mediante GIO. */
static void trash_cb(gpointer data);
static gboolean confirm_trash_paths(GList *paths);
static gboolean confirm_permanent_delete_paths(GList *paths);

/*			SUPPORT				*/


/* This is called whenever the user edits the entry box (if any) - send the
 * new string.
 */
static void entry_changed(GtkEditable *entry, GUIside *gui_side)
{
	guchar	*text;

	g_return_if_fail(gui_side->default_string != NULL);

	text = gtk_editable_get_chars(entry, 0, -1);

	if (gui_side->entry_string_func)
		gui_side->entry_string_func(GTK_WIDGET(entry), text);

	g_free(*(gui_side->default_string));
	*(gui_side->default_string) = text;	/* Gets text's ref */

	if (!gui_side->to_child)
		return;

	fputc('E', gui_side->to_child);
	fputs(text, gui_side->to_child);
	fputc('\n', gui_side->to_child);
	fflush(gui_side->to_child);
}

void show_condition_help(gpointer data)
{
	GtkWidget *help;
	GtkWidget *text;

	help = gtk_dialog_new();
	gtk_window_set_position(GTK_WINDOW(help), GTK_WIN_POS_CENTER);
	gtk_window_set_title(GTK_WINDOW(help), _("Find expression reference"));
	dialog_add_icon_button(GTK_DIALOG(help), ROX_ICON_CLOSE,
			_("_Close"), GTK_RESPONSE_CANCEL);
	gtk_dialog_set_default_response(GTK_DIALOG(help), GTK_RESPONSE_CANCEL);

	text = gtk_label_new(NULL);
	rox_widget_set_padding(GTK_WIDGET(text), 2, 2);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(help))), text, TRUE, TRUE, 0);
	gtk_label_set_selectable(GTK_LABEL(text), TRUE);
	gtk_label_set_markup(GTK_LABEL(text), _(
"<u>Quick Start</u>\n"
"Just put the name of the file you're looking for in single quotes:\n"
"<b>'index.html'</b> (to find a file called 'index.html')\n"
"\n"
"<u>Examples</u>\n"
"<b>'*.htm', '*.html'</b> (finds HTML files)\n"
"<b>IsDir 'lib'</b> (finds directories called 'lib')\n"
"<b>IsReg 'core'</b> (finds a regular file called 'core')\n"
"<b>! (IsDir, IsReg)</b> (is neither a directory nor a regular file)\n"
"<b>mtime after 1 day ago and size > 1Mb</b> (big, and recently modified)\n"
"<b>'CVS' prune, isreg</b> (a regular file not in CVS)\n"
"<b>IsReg system(grep -q fred \"%\")</b> (contains the word 'fred')\n"
"\n"
"<u>Simple Tests</u>\n"
"<b>IsReg, IsLink, IsDir, IsChar, IsBlock, IsDev, IsPipe, IsSocket, IsDoor</b> "
"(types)\n"
"<b>IsSUID, IsSGID, IsSticky, IsReadable, IsWriteable, IsExecutable</b> "
"(permissions)\n"
"<b>IsEmpty, IsMine</b>\n"
#if defined(HAVE_GETXATTR) || defined(HAVE_ATTROPEN)
"<b>HasXattr</b> "
"(extended attributes)\n"
#endif
"A pattern in single quotes is a shell-style wildcard pattern to match. If it\n"
"contains a slash then the match is against the full path; otherwise it is\n"
"against the leafname only.\n"
"\n"
"<u>Comparisons</u>\n"
"<b>&lt;, &lt;=, =, !=, &gt;, &gt;=, After, Before</b> (compare two values)\n"
"<b>5 bytes, 1Kb, 2Mb, 3Gb</b> (file sizes)\n"
"<b>2 secs|mins|hours|days|weeks|years  ago|hence</b> (times)\n"
"<b>atime, ctime, mtime, now, size, inode, nlinks, uid, gid, blocks</b> "
"(values)\n"
"\n"
"<u>Specials</u>\n"
"<b>system(command)</b> (true if 'command' returns with a zero exit status;\n"
"a % in 'command' is replaced with the path of the current file)\n"
#if defined(HAVE_GETXATTR) || defined(HAVE_ATTROPEN)
"<b>prune</b> (false, and prevents searching the contents of a directory)\n"
"<b>label '<i>color</i>'</b> (true if user.label matches <i>color</i> as a color)\n"
"<b>xattr '<i>attr</i>'</b> (true if file has non-empty extended attribute <i>attr</i>)."));
#else
"<b>prune</b> (false, and prevents searching the contents of a directory)."));
#endif

	g_signal_connect(help, "response",
			G_CALLBACK(gtk_widget_destroy), NULL);

	gtk_widget_show_all(help);
}

static void show_chmod_help(gpointer data)
{
	GtkWidget *help;
	GtkWidget *text;

	help = gtk_dialog_new();
	gtk_window_set_position(GTK_WINDOW(help), GTK_WIN_POS_CENTER);
	gtk_window_set_title(GTK_WINDOW(help), _("Change permissions reference"));
	dialog_add_icon_button(GTK_DIALOG(help), ROX_ICON_CLOSE,
			_("_Close"), GTK_RESPONSE_CANCEL);
	gtk_dialog_set_default_response(GTK_DIALOG(help), GTK_RESPONSE_CANCEL);

	text = gtk_label_new(NULL);
	rox_widget_set_padding(GTK_WIDGET(text), 2, 2);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(help))), text, TRUE, TRUE, 0);
	gtk_label_set_selectable(GTK_LABEL(text), TRUE);
	gtk_label_set_markup(GTK_LABEL(text), _(
"Normally, you can just select a command from the menu (click \n"
"on the arrow beside the command box). Sometimes, you need more...\n"
"\n"
"The format of a command is: <b>CHANGE, CHANGE, ...</b>\n"
"Each <b>CHANGE</b> is: <b>WHO HOW PERMISSIONS</b>\n"
"<b>WHO</b> is some combination of <b>u</b>, <b>g</b> and <b>o</b> which "
"determines whether to\n"
"change the permissions for the User (owner), Group or Others.\n"
"<b>HOW</b> is <b>+</b>, <b>-</b> or <b>=</b> to add, remove or set "
"exactly the permissions.\n"
"<b>PERMISSIONS</b> is some combination of the letters <b>rwxXstugo</b>\n"
"\n"
"Bracketed text and spaces are ignored.\n"
"\n"
"<u>Examples</u>\n"
"<b>u+rw</b>: the file owner gains read and write permission\n"
"<b>g=u</b>: the group permissions are set to be the same as the user's\n"
"<b>o=u-w</b>: others get the same permissions as the owner, but without "
"write permission\n"
"<b>a+x</b>: <b>a</b>ll get execute/access permission - same as <b>ugo+x</b>\n"
"<b>a+X</b>: directories become accessable by everyone; files which were\n"
"executable by anyone become executable by everyone\n"
"<b>u+rw, go+r</b>: two commands at once!\n"
"<b>u+s</b>: set the SetUID bit - often has no effect on script files\n"
"<b>755</b>: set the permissions directly\n"
"\n"
"See the chmod(1) man page for full details."));

	g_signal_connect(help, "response",
			G_CALLBACK(gtk_widget_destroy), NULL);

	gtk_widget_show_all(help);
}


static void show_settype_help(gpointer data)
{
	GtkWidget *help;
	GtkWidget *text;

	help = gtk_dialog_new();
	gtk_window_set_position(GTK_WINDOW(help), GTK_WIN_POS_CENTER);
	gtk_window_set_title(GTK_WINDOW(help), _("Set type reference"));
	dialog_add_icon_button(GTK_DIALOG(help), ROX_ICON_CLOSE,
			_("_Close"), GTK_RESPONSE_CANCEL);
	gtk_dialog_set_default_response(GTK_DIALOG(help), GTK_RESPONSE_CANCEL);

	text = gtk_label_new(NULL);
	rox_widget_set_padding(GTK_WIDGET(text), 2, 2);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(help))), text, TRUE, TRUE, 0);
	gtk_label_set_selectable(GTK_LABEL(text), TRUE);
	gtk_label_set_markup(GTK_LABEL(text), _(
"Normally Rox-Filer2 determines the type of a regular file\n"
"by matching it's name against a pattern. To change the\n"
"type of the file you must rename it.\n"
"\n"
"Newer file systems can support something called 'Extended\n"
"Attributes' which can be used to store additional data with\n"
"each file as named parameters. Rox-Filer2 uses the\n"
"'user.mime_type' attribute to store file types.\n"
"\n"
"File types are only supported for regular files, not\n"
"directories, devices, pipes or sockets, and then only\n"
"on certain file systems and where the OS implements them.\n"));

	text = gtk_label_new(_(ATTR_MAN_PAGE));
	rox_widget_set_padding(GTK_WIDGET(text), 2, 2);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(help))), text, TRUE, TRUE, 0);

	g_signal_connect(help, "response",
			G_CALLBACK(gtk_widget_destroy), NULL);

	gtk_widget_show_all(help);
}

/* Agregado por josejp2424: implementación del cierre fiable de los
 * diálogos de operaciones cuando el proceso hijo confirma que terminó. */
static void finish_action(GUIside *gui_side)
{
	ABox *abox;
	GtkTextBuffer *text_buffer;

	if (!gui_side || gui_side->finishing)
		return;

	gui_side->finishing = TRUE;
	abox = gui_side->abox;
	text_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(abox->log));

	/* The explicit completion message is sent after every previous log,
	 * refresh and error message.  Closing here avoids depending solely on
	 * a G_IO_HUP notification, which was unreliable in the GTK3 port. */
	gui_side->child = 0;

	if (gui_side->to_child)
	{
		fclose(gui_side->to_child);
		gui_side->to_child = NULL;
	}
	if (gui_side->from_child >= 0)
	{
		close(gui_side->from_child);
		gui_side->from_child = -1;
	}
	if (gui_side->input_tag)
	{
		guint tag = gui_side->input_tag;
		gui_side->input_tag = 0;
		g_source_remove(tag);
	}

	abox_cancel_ask(abox);
	/* Modificado por josejp2424 (2026): el GIF sólo debe animarse mientras
	 * la operación está activa. Detenerlo antes de dejar abierto un informe
	 * de errores o información evita consumo continuo de CPU. */
	abox_stop_operation_animation(abox);

	if (gui_side->errors)
	{
		guchar *report;

		if (gui_side->errors == 1)
			report = g_strdup(_("There was one error.\n"));
		else
			report = g_strdup_printf(_("There were %d errors.\n"),
					 gui_side->errors);

		gtk_text_buffer_insert_at_cursor(text_buffer, report, -1);
		g_free(report);
	}
	else if (gui_side->show_info == FALSE)
		gtk_widget_destroy(GTK_WIDGET(abox));
}

static void process_message(GUIside *gui_side, const gchar *buffer)
{
	ABox *abox = gui_side->abox;

	if (*buffer == 'D')
	{
		finish_action(gui_side);
		return;
	}
	else if (*buffer == '?')
		abox_ask(abox, buffer + 1);
	else if (*buffer == 'C')
		abox_ask_conflict(abox, buffer + 1);
	else if (*buffer == 's')
		dir_check_this(buffer + 1);	/* Update this item */
	else if (*buffer == '=')
		abox_add_filename(abox, buffer + 1);
	else if (*buffer == '#')
		abox_clear_results(abox);
	else if (*buffer == 'X')
	{
		filer_close_recursive(g_strdup(buffer + 1));
		/* Let child know it's safe to continue... */
		fputc('X', gui_side->to_child);
		fflush(gui_side->to_child);
	}
	else if (*buffer == 'm' || *buffer == 'M')
	{
		/* Mount / major changes to this path */
		if (*buffer == 'M')
		{
			mount_update(TRUE);
			mount_user_mount(buffer + 1);
		}
		filer_check_mounted(buffer + 1);
	}
	else if (*buffer == '/')
		abox_set_current_object(abox, buffer + 1);
	else if (*buffer == 'o')
		filer_opendir(buffer + 1, NULL, NULL);
	else if (*buffer == '!')
	{
		gui_side->errors++;
		abox_log(abox, buffer + 1, "error");
	}
	else if (*buffer == '<')
		abox_set_file(abox, 0, buffer+1);
	else if (*buffer == '>')
	{
		abox_set_file(abox, 1, buffer+1);
		abox_show_compare(abox, TRUE);
	}
	else if (*buffer == '%')
	{
		abox_set_percentage(abox, atoi(buffer+1));
	}
	else
		abox_log(abox, buffer + 1, NULL);
}

/* Called when the child sends us a message */
static void message_from_child(gpointer 	  data,
			        gint     	  source,
			        RoxInputCondition condition)
{
	char buf[5];
	GUIside	*gui_side = (GUIside *) data;
	ABox	*abox = gui_side->abox;
	if (read_exact(source, buf, 4))
	{
		ssize_t message_len;
		char	*buffer;

		buf[4] = '\0';
		message_len = strtol(buf, NULL, 16);
		buffer = g_malloc(message_len + 1);
		if (message_len > 0 && read_exact(source, buffer, message_len))
		{
			buffer[message_len] = '\0';
			process_message(gui_side, buffer);
			g_free(buffer);
			return;
		}
		g_printerr("Child died in the middle of a message.\n");
	}

	if (gui_side->abort_attempts)
		abox_log(abox, _("\nProcess terminated.\n"), "error");

	/* EOF/HUP fallback. Normal operations finish through the explicit D
	 * protocol message, but this also handles an unexpected child exit. */
	finish_action(gui_side);
}

/* Scans src_dir, calling cb(item, dest_path) for each item */
static void for_dir_contents(ForDirCB *cb,
			     const char *src_dir,
			     const char *dest_path)
{
	DIR	*d;
	struct dirent *ent;
	GList  *list = NULL, *next;

	d = mc_opendir(src_dir);
	if (!d)
	{
		/* Message displayed is "ERROR reading 'path': message" */
		printf_send("!%s '%s': %s\n", _("ERROR reading"),
			    src_dir, g_strerror(errno));
		return;
	}

	send_dir(src_dir);

	while ((ent = mc_readdir(d)))
	{
		if (ent->d_name[0] == '.' && (ent->d_name[1] == '\0'
			|| (ent->d_name[1] == '.' && ent->d_name[2] == '\0')))
			continue;
		list = g_list_prepend(list, g_strdup(make_path(src_dir,
							       ent->d_name)));
	}
	mc_closedir(d);

	for (next = list; next; next = next->next)
	{
		cb((char *) next->data, dest_path);

		g_free(next->data);
	}
	g_list_free(list);
}

/* Read this many bytes into the buffer. TRUE on success. */
static gboolean read_exact(int source, char *buffer, ssize_t len)
{
	while (len > 0)
	{
		ssize_t got;
		got = read(source, buffer, len);
		if (got < 1)
			return FALSE;
		len -= got;
		buffer += got;
	}
	return TRUE;
}

static void send_done(void)
{
	printf_send(_("'\nDone\n"));
}

/* Notify the filer that this item has been updated */
static void send_check_path(const gchar *path)
{
	printf_send("s%s", path);
}

/* Notify the filer that this whole subtree has changed (eg, been unmounted) */
static void send_mount_path(const gchar *path)
{
	printf_send("m%s", path);
}

/* Send a message to the filer process. The first character indicates the
 * type of the message.
 */
static gboolean printf_send(const char *msg, ...)
{
        va_list args;
	gchar *tmp;

	va_start(args, msg);
	tmp = g_strdup_vprintf(msg, args);
	va_end(args);

	g_string_assign(message, tmp);
	g_free(tmp);

	return send_msg();
}

/* Send 'message' to our parent process. TRUE on success. */
static gboolean send_msg(void)
{
	char len_buffer[5];
	ssize_t len;

	g_return_val_if_fail(message->len < 0xffff, FALSE);

	sprintf(len_buffer, "%04" G_GSIZE_MODIFIER "x", message->len);
	fwrite(len_buffer, 1, 4, to_parent);
	len = fwrite(message->str, 1, message->len, to_parent);
	fflush(to_parent);
	return len == (ssize_t) message->len;
}

/* Set the directory indicator at the top of the window */
static gboolean send_dir(const char *dir)
{
	return printf_send("/%s", dir);
}

static gboolean send_error(void)
{
	return printf_send("!%s: %s\n", _("ERROR"), g_strerror(errno));
}

static void response(GtkDialog *dialog, gint response, GUIside *gui_side)
{
	gchar code;
	if (!gui_side->to_child)
		return;

	if (response == GTK_RESPONSE_YES)
		code = abox_apply_to_all(gui_side->abox) ? 'A' : 'Y';
	else if (response == GTK_RESPONSE_NO)
		code = abox_apply_to_all(gui_side->abox) ? 'S' : 'N';
	else
		return;

	fputc(code, gui_side->to_child);
	fflush(gui_side->to_child);
	abox_show_compare(gui_side->abox, FALSE);
}

static void flag_toggled(ABox *abox, gint flag, GUIside *gui_side)
{
	if (!gui_side->to_child)
		return;

	fputc(flag, gui_side->to_child);
	fflush(gui_side->to_child);
}

static void read_new_entry_text(void)
{
	int	len;
	char	c;
	GString	*new;

	new = g_string_new(NULL);

	for (;;)
	{
		len = read(from_parent, &c, 1);
		if (len != 1)
		{
			fprintf(stderr, "read() error: %s\n",
					g_strerror(errno));
			_exit(1);	/* Parent died? */
		}

		if (c == '\n')
			break;
		g_string_append_c(new, c);
	}

	g_free(new_entry_string);
	new_entry_string = new->str;
	g_string_free(new, FALSE);
}

static void process_flag(char flag)
{
	switch (flag)
	{
		case 'Q':
			quiet = !quiet;
			break;
		case 'F':
			o_force = !o_force;
			break;
		case 'R':
			o_recurse = !o_recurse;
			break;
		case 'B':
			o_brief = !o_brief;
			break;
		case 'M':
			o_merge = !o_merge;
			break;
		case 'W':
			o_newer = !o_newer;
			break;
		case 'I':
			o_ignore = !o_ignore;
			break;
		case 'E':
			read_new_entry_text();
			break;
		default:
			printf_send("!ERROR: Bad message '%c'\n", flag);
			break;
	}
}

/* If the parent has sent any flag toggles, read them */
static void check_flags(void)
{
	fd_set 	set;
	int	got;
	char	retval;
	struct timeval tv;

	FD_ZERO(&set);

	while (1)
	{
		FD_SET(from_parent, &set);
		tv.tv_sec = 0;
		tv.tv_usec = 0;
		got = select(from_parent + 1, &set, NULL, NULL, &tv);

		if (got == -1)
			g_error("select() failed: %s\n", g_strerror(errno));
		else if (!got)
			return;

		got = read(from_parent, &retval, 1);
		if (got != 1)
			g_error("read() error: %s\n", g_strerror(errno));

		process_flag(retval);
	}
}

/* Read until the user sends a reply. If ignore_quiet is TRUE then
 * the user MUST click Yes or No, else treat quiet on as Yes.
 * If the user needs prompting then does send_msg().
 */
static gboolean printf_reply(int fd, gboolean ignore_quiet,
			     const char *msg, ...)
{
	ssize_t len;
	char retval;
	va_list args;
	gchar *tmp;

	if (quiet && !ignore_quiet)
		return TRUE;

	va_start(args, msg);
	tmp = g_strdup_vprintf(msg, args);
	va_end(args);

	g_string_assign(message, tmp);
	g_free(tmp);

	send_msg();

	while (1)
	{
		len = read(fd, &retval, 1);
		if (len != 1)
		{
			fprintf(stderr, "read() error: %s\n",
					g_strerror(errno));
			_exit(1);	/* Parent died? */
		}

		switch (retval)
		{
			case 'Y':
				printf_send("' %s\n", _("Yes"));
				return TRUE;
			case 'N':
				printf_send("' %s\n", _("No"));
				return FALSE;
			default:
				process_flag(retval);
				break;
		}
	}
}

/* Agregado por josejp2424 (2026): pregunta específica de conflicto.
 * A = reemplazar todos y S = omitir todos durante esta operación. */
static gboolean printf_conflict_reply(int fd, const char *msg, ...)
{
	ssize_t len;
	char retval;
	va_list args;
	gchar *tmp;

	if (conflict_policy == CONFLICT_REPLACE_ALL)
		return TRUE;
	if (conflict_policy == CONFLICT_SKIP_EXISTING)
		return FALSE;

	va_start(args, msg);
	tmp = g_strdup_vprintf(msg, args);
	va_end(args);
	g_string_assign(message, tmp);
	g_free(tmp);

	/* La C permite a la GUI mostrar "aplicar a todos" sólo en conflictos. */
	g_string_prepend_c(message, 'C');
	send_msg();

	while (1)
	{
		len = read(fd, &retval, 1);
		if (len != 1)
		{
			fprintf(stderr, "read() error: %s\n", g_strerror(errno));
			_exit(1);
		}

		switch (retval)
		{
			case 'A':
				conflict_policy = CONFLICT_REPLACE_ALL;
				printf_send("' %s\n", _("Replace all"));
				return TRUE;
			case 'S':
				conflict_policy = CONFLICT_SKIP_EXISTING;
				printf_send("' %s\n", _("Skip all"));
				return FALSE;
			case 'Y':
				printf_send("' %s\n", _("Yes"));
				return TRUE;
			case 'N':
				printf_send("' %s\n", _("No"));
				return FALSE;
			default:
				process_flag(retval);
				break;
		}
	}
}

static void abort_operation(GtkWidget *widget, gpointer data)
{
	GUIside	*gui_side = (GUIside *) data;

	if (gui_side->child)
	{
		if (gui_side->abort_attempts == 0)
		{
			abox_log(ABOX(widget),
				 _("\nAsking child process to terminate...\n"),
				 "error");
			kill(-gui_side->child, SIGTERM);
		}
		else
		{
			abox_log(ABOX(widget),
				 _("\nTrying to KILL run-away process...\n"),
				 "error");
			kill(-gui_side->child, SIGKILL);
			kill(-gui_side->child, SIGCONT);
		}
		gui_side->abort_attempts++;
	}
	else
		gtk_widget_destroy(widget);
}

static void destroy_action_window(GtkWidget *widget, gpointer data)
{
	GUIside	*gui_side = (GUIside *) data;

	if (gui_side->child)
		kill(-gui_side->child, SIGTERM);

	if (gui_side->to_child)
		fclose(gui_side->to_child);
	if (gui_side->from_child >= 0)
		close(gui_side->from_child);
	if (gui_side->input_tag)
		g_source_remove(gui_side->input_tag);

	g_free(gui_side);

	one_less_window();
}

/* Create two pipes, fork() a child and return a pointer to a GUIside struct
 * (NULL on failure). The child calls func().
 */
static GUIside *start_action(GtkWidget *abox, ActionChild *func, gpointer data,
		int force, int brief, int recurse, int merge, int newer, int ignore)
{
	gboolean	autoq;
	int		filedes[4];	/* 0 and 2 are for reading */
	GUIside		*gui_side;
	pid_t		child;
	struct sigaction act;

	if (pipe(filedes))
	{
		report_error("pipe: %s", g_strerror(errno));
		gtk_widget_destroy(abox);
		return NULL;
	}

	if (pipe(filedes + 2))
	{
		close(filedes[0]);
		close(filedes[1]);
		report_error("pipe: %s", g_strerror(errno));
		gtk_widget_destroy(abox);
		return NULL;
	}

	autoq = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(ABOX(abox)->quiet));

	o_force = force;
	o_brief = brief;
	o_recurse = recurse;
	o_merge = merge;
	o_newer = newer;
	o_ignore = ignore;

	child = fork();
	switch (child)
	{
		case -1:
			report_error("fork: %s", g_strerror(errno));
			gtk_widget_destroy(abox);
			return NULL;
		case 0:
			/* We are the child */

			/* Create a new process group */
			setpgid(0, 0);

			quiet = autoq;

			dir_drop_all_notifies();

			/* Reset the SIGCHLD handler */
			act.sa_handler = SIG_DFL;
			sigemptyset(&act.sa_mask);
			act.sa_flags = 0;
			sigaction(SIGCHLD, &act, NULL);

			message = g_string_new(NULL);
			close(filedes[0]);
			close(filedes[3]);
			to_parent = fdopen(filedes[1], "wb");
			from_parent = filedes[2];
			func(data);
			send_dir("");
			/* Agregado por josejp2424: notificar al proceso GTK que la operación terminó. */
			printf_send("D");
			fclose(to_parent);
			to_parent = NULL;
			_exit(0);
	}

	/* We are the parent */
	close(filedes[1]);
	close(filedes[2]);
	gui_side = g_new(GUIside, 1);
	gui_side->from_child = filedes[0];
	gui_side->to_child = fdopen(filedes[3], "wb");
	gui_side->child = child;
	gui_side->errors = 0;
	gui_side->show_info = FALSE;
	gui_side->default_string = NULL;
	gui_side->entry_string_func = NULL;
	gui_side->abort_attempts = 0;
	gui_side->finishing = FALSE;

	gui_side->abox = ABOX(abox);
	g_signal_connect(abox, "destroy",
			G_CALLBACK(destroy_action_window), gui_side);

	g_signal_connect(abox, "response", G_CALLBACK(response), gui_side);
	g_signal_connect(abox, "flag_toggled",
			 G_CALLBACK(flag_toggled), gui_side);
	g_signal_connect(abox, "abort_operation",
			 G_CALLBACK(abort_operation), gui_side);

	gui_side->input_tag = rox_input_add_full(gui_side->from_child,
						ROX_INPUT_READ,
						message_from_child,
						gui_side, NULL);

	return gui_side;
}

/* 			ACTIONS ON ONE ITEM 			*/

/* These may call themselves recursively, or ask questions, etc */

/* Updates the global size_tally, file_counter and dir_counter */
static void do_usage(const char *src_path, const char *unused)
{
	struct 		stat info;

	check_flags();

	if (mc_lstat(src_path, &info))
	{
		printf_send("'%s:\n", src_path);
		send_error();
	}
	else if (S_ISREG(info.st_mode) || S_ISLNK(info.st_mode))
	{
	        file_counter++;
		size_tally += info.st_size;
	}
	else if (S_ISDIR(info.st_mode))
	{
	        dir_counter++;
		if (printf_reply(from_parent, FALSE,
				 _("?Count contents of %s?"), src_path))
		{
			char *safe_path;
			safe_path = g_strdup(src_path);
			for_dir_contents(do_usage, safe_path, safe_path);
			g_free(safe_path);
		}
	}
	else
		file_counter++;
}

/* dest_path is the dir containing src_path */
static void do_delete(const char *src_path, const char *unused)
{
	struct stat 	info;
	gboolean	write_prot;
	char		*safe_path;
	gchar		*base = g_path_get_basename(src_path);

	check_flags();

	if (mc_lstat(src_path, &info))
	{
		send_error();
		return;
	}

	write_prot = S_ISLNK(info.st_mode) ? FALSE
					   : access(src_path, W_OK) != 0;
	/* Modificado por josejp2424 (2026): en un borrado permanente por
	 * lotes, Force evita de verdad la pregunta por cada archivo protegido. */
	if ((write_prot && !o_force) || !quiet)
	{
		int res;

		printf_send("<%s", src_path);
		printf_send(">");
		res=printf_reply(from_parent, write_prot && !o_force,
				  _("?Delete %s'%s'?"),
				  write_prot ? _("WRITE-PROTECTED ") : "",
				 src_path);
		printf_send("<");
		if (!res)
			return;
	}
	else if (!o_brief)
		printf_send(_("'Deleting '%s'\n"), src_path);

	safe_path = g_strdup(src_path);

	if (S_ISDIR(info.st_mode))
	{
		for_dir_contents(do_delete, safe_path, safe_path);
		if (rmdir(safe_path))
		{
			g_free(safe_path);
			send_error();
			return;
		}
		if (!delete_batch_mode)
		{
			printf_send(_("'Directory '%s' deleted\n"), safe_path);
			send_mount_path(safe_path);
		}
	}
	else if (unlink(src_path))
		send_error();
	else
	{
		if (!delete_batch_mode)
			send_check_path(safe_path);
		if (!delete_batch_mode && strcmp(base, ".DirIcon") == 0)
		{
			gchar *dir;
			dir = g_path_get_dirname(safe_path);
			send_check_path(dir);
			g_free(dir);
		}
	}

	g_free(safe_path);
	g_free(base);
}

static void do_eject(const char *path)
{
	const char *argv[]={"sh", "-c", NULL, NULL};
	char *err;

	check_flags();

	if (!quiet)
	{
		int res;
		printf_send("<%s", path);
		printf_send(">");
		res=printf_reply(from_parent, !o_force,
				  _("?Eject '%s'?"),
				 path);
		printf_send("<");
		if (!res)
			return;
	}
	else if (!o_brief)
		printf_send(_("'Eject '%s'\n"), path);

	/* Need to close all sub-directories now, or we
	 * can't unmount if dnotify is used.
	 */
	{
		char c = '?';
		printf_send("X%s", path);
		/* Wait until it's safe... */
		read(from_parent, &c, 1);
		g_return_if_fail(c == 'X');
	}

	argv[2] = build_command_with_path(o_action_eject_command.value,
					  path);
	err = fork_exec_wait((const char**)argv);
	g_free((gchar *) argv[2]);
	if (err)
	{
		printf_send(_("!%s\neject failed\n"), err);
		g_free(err);
	}

	printf_send("M%s", path);

}

/* path is the item to check. If is is a directory then we may recurse
 * (unless prune is used).
 */
static void do_find(const char *path, const char *unused)
{
	FindInfo	info;
	gchar *base = g_path_get_basename(path);

	check_flags();

	if (!quiet)
	{
		if (!printf_reply(from_parent, FALSE, _("?Check '%s'?"), path))
			return;
	}

	for (;;)
	{
		if (new_entry_string)
		{
			find_condition_free(find_condition);
			find_condition = find_compile(new_entry_string);
			null_g_free(&new_entry_string);
		}

		if (find_condition)
			break;

		printf_send(_("!Invalid find condition - "
			      "change it and try again\n"));
		if (!printf_reply(from_parent, TRUE,
				  _("?Check '%s'?"), path))
			return;
	}

	if (mc_lstat(path, &info.stats))
	{
		send_error();
		printf_send(_("'(while checking '%s')\n"), path);
		return;
	}

	info.fullpath = path;
	time(&info.now);	/* XXX: Not for each check! */

	info.leaf = base;
	info.prune = FALSE;
	if (find_test_condition(find_condition, &info))
		printf_send("=%s", path);

	if (S_ISDIR(info.stats.st_mode) && !info.prune)
	{
		char *safe_path;
		safe_path = g_strdup(path);
		for_dir_contents(do_find, safe_path, safe_path);
		g_free(safe_path);
	}
	g_free(base);
}

/* Like mode_compile(), but ignores spaces and bracketed bits */
static struct mode_change *nice_mode_compile(const char *mode_string,
				      unsigned int masked_ops)
{
	GString			*new;
	int			brackets = 0;
	struct mode_change	*retval = NULL;

	new = g_string_new(NULL);

	for (; *mode_string; mode_string++)
	{
		if (*mode_string == '(')
			brackets++;
		if (*mode_string == ')')
		{
			brackets--;
			if (brackets < 0)
				break;
			continue;
		}

		if (brackets == 0 && *mode_string != ' ')
			g_string_append_c(new, *mode_string);
	}

	if (brackets == 0)
		retval = mode_compile(new->str, masked_ops);
	g_string_free(new, TRUE);
	return retval;
}

static void do_chmod(const char *path, const char *unused)
{
	struct stat 	info;
	mode_t		new_mode;

	check_flags();

	if (mc_lstat(path, &info))
	{
		send_error();
		return;
	}
	if (S_ISLNK(info.st_mode))
		return;

	if (!quiet)
	{
		int res;
		printf_send("<%s", path);
		printf_send(">");
		res=printf_reply(from_parent, FALSE,
				 _("?Change permissions of '%s'?"), path);
		printf_send("<");
		if (!res)
			return;
	}
	else if (!o_brief)
		printf_send(_("'Changing permissions of '%s'\n"), path);

	for (;;)
	{
		if (new_entry_string)
		{
			if (mode_change)
				mode_free(mode_change);
			mode_change = nice_mode_compile(new_entry_string,
							MODE_MASK_ALL);
			null_g_free(&new_entry_string);
		}

		if (mode_change)
			break;

		printf_send(
			_("!Invalid mode command - change it and try again\n"));
		if (!printf_reply(from_parent, TRUE,
				  _("?Change permissions of '%s'?"), path))
			return;
	}

	if (mc_lstat(path, &info))
	{
		send_error();
		return;
	}
	if (S_ISLNK(info.st_mode))
		return;

	new_mode = mode_adjust(info.st_mode, mode_change);
	if (chmod(path, new_mode))
	{
		send_error();
		return;
	}

	send_check_path(path);

	if (S_ISDIR(info.st_mode))
	{
		send_mount_path(path);

		if (o_recurse)
		{
			guchar *safe_path;
			safe_path = g_strdup(path);
			for_dir_contents(do_chmod, safe_path, safe_path);
			g_free(safe_path);
		}
	}
}

static void do_settype(const char *path, const char *unused)
{
	struct stat 	info;

	check_flags();

	if (mc_lstat(path, &info))
	{
		send_error();
		return;
	}
	if (S_ISLNK(info.st_mode))
		return;

	if (!quiet)
	{
		int res;
		printf_send("<%s", path);
		printf_send(">");
		if (S_ISDIR(info.st_mode))
			res=printf_reply(from_parent, FALSE,
					 _("?Change contents of '%s'?"), path);
		else
			res=printf_reply(from_parent, FALSE,
					 _("?Change type of '%s'?"), path);
		printf_send("<");
		if (!res)
			return;
	}

	for (;;)
	{
		if (new_entry_string)
		{
			type_change = mime_type_lookup(new_entry_string);
			null_g_free(&new_entry_string);
		}

		if (type_change)
			break;

		printf_send(_("!Invalid type - "
			      "change it and try again\n"));
		if (!printf_reply(from_parent, TRUE,
				  _("?Change type of '%s'?"), path))
			return;
	}

	if (mc_lstat(path, &info))
	{
		send_error();
		return;
	}
	if (S_ISLNK(info.st_mode))
		return;

	if (S_ISREG(info.st_mode))
	{
		if (!o_brief)
		{
			const char *comment;

			comment = mime_type_comment(type_change);
			printf_send(_("'Changing type of '%s' to '%s'\n"), path,
				    comment);
		}

		if (xtype_set(path, type_change))
		{
			send_error();
			return;
		}

		send_check_path(path);
	}
	else if (S_ISDIR(info.st_mode))
	{
		if (o_recurse)
		{
			guchar *safe_path;
			safe_path = g_strdup(path);
			for_dir_contents(do_settype, safe_path, unused);
			g_free(safe_path);
		}
		else if(!o_brief)
		{
			printf_send(_("'Not changing type of directory '%s'\n"),
				    path);
		}
	}
	else if(!o_brief)
	{
		printf_send(_("'Non-regular file '%s' not changed\n"),
			    path);
	}
}

/* Agregado por josejp2424 (2026): comprueba conflictos de primer nivel.
 * Si una carpeta de destino ya existe, cualquier conflicto interno quedará
 * cubierto por la política elegida para esa carpeta. */
static gboolean destination_has_conflicts(GList *paths, const char *dest,
					 const char *leaf)
{
	GList *iter;

	for (iter = paths; iter; iter = iter->next)
	{
		const char *source = (const char *) iter->data;
		gchar *base = leaf ? g_strdup(leaf) : g_path_get_basename(source);
		gchar *target = g_build_filename(dest, base, NULL);
		struct stat info;
		gboolean exists = (mc_lstat(target, &info) == 0);

		g_free(target);
		g_free(base);
		if (exists)
			return TRUE;
	}

	return FALSE;
}

/* Modificado por josejp2424 (2026): selector compacto de conflictos al
 * estilo clásico de ROX-Filer. Las políticas se presentan como botones
 * normales en el área inferior del diálogo, sin cuadrículas ni tamaños
 * forzados. */
static ConflictPolicy choose_conflict_policy(const gchar *operation)
{
	GtkWidget *dialog;
	GtkWidget *content;
	GtkWidget *label;
	GtkWidget *action_area;
	GtkWidget *button;
	gint response_id;
	ConflictPolicy policy = CONFLICT_CANCELLED;
	gchar *title;

	title = g_strdup_printf(_("%s conflict policy"), operation);
	dialog = gtk_dialog_new();
	gtk_window_set_title(GTK_WINDOW(dialog), title);
	g_free(title);

	gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
	gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
	gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
	gtk_window_set_type_hint(GTK_WINDOW(dialog), GDK_WINDOW_TYPE_HINT_DIALOG);

	/* Modificado por josejp2424 (2026): este selector debe conservar el
	 * tamaño compacto de los diálogos originales de ROX y no recibir el
	 * mínimo global de 640x400 reservado para ventanas de trabajo. */
	g_object_set_data(G_OBJECT(dialog), "rox-standard-size-exempt",
		GINT_TO_POINTER(1));

	content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	label = gtk_label_new(_("Some items already exist in the destination. "
		"Choose how Rox-Filer2 should handle all conflicts in this operation:"));
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(label), 72);
	gtk_label_set_xalign(GTK_LABEL(label), 0.0);
	gtk_widget_set_margin_start(label, 12);
	gtk_widget_set_margin_end(label, 12);
	gtk_widget_set_margin_top(label, 12);
	gtk_widget_set_margin_bottom(label, 8);
	gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 0);

	/* Modificado por josejp2424 (2026): botones pequeños, alineados como los
	 * botones Cancelar/No/Sí del diálogo clásico de operaciones de ROX. */
	action_area = gtk_dialog_get_action_area(GTK_DIALOG(dialog));
	gtk_box_set_spacing(GTK_BOX(action_area), 6);
	gtk_button_box_set_layout(GTK_BUTTON_BOX(action_area), GTK_BUTTONBOX_END);

	button = dialog_add_icon_button(GTK_DIALOG(dialog), ROX_ICON_CANCEL,
		_("_Cancel"), GTK_RESPONSE_CANCEL);
	gtk_widget_set_tooltip_text(button, _("Cancel"));

	button = dialog_add_icon_button(GTK_DIALOG(dialog),
		ROX_ICON_DIALOG_QUESTION, _("Ask"), RESPONSE_CONFLICT_ASK);
	gtk_widget_set_tooltip_text(button, _("Ask for each conflict"));

	button = dialog_add_icon_button(GTK_DIALOG(dialog), "go-next",
		_("Skip all"), RESPONSE_CONFLICT_SKIP);
	gtk_widget_set_tooltip_text(button, _("Skip existing files"));

	button = dialog_add_icon_button(GTK_DIALOG(dialog), ROX_ICON_REFRESH,
		_("Newer"), RESPONSE_CONFLICT_NEWER);
	gtk_widget_set_tooltip_text(button,
		_("Replace only if the source is newer"));

	button = dialog_add_icon_button(GTK_DIALOG(dialog), ROX_ICON_COPY,
		_("Replace all"), RESPONSE_CONFLICT_REPLACE);
	gtk_widget_set_tooltip_text(button, _("Replace existing files"));

	gtk_dialog_set_default_response(GTK_DIALOG(dialog), RESPONSE_CONFLICT_ASK);
	gtk_widget_show_all(dialog);
	response_id = gtk_dialog_run(GTK_DIALOG(dialog));

	switch (response_id)
	{
		case RESPONSE_CONFLICT_ASK:
			policy = CONFLICT_ASK;
			break;
		case RESPONSE_CONFLICT_REPLACE:
			policy = CONFLICT_REPLACE_ALL;
			break;
		case RESPONSE_CONFLICT_SKIP:
			policy = CONFLICT_SKIP_EXISTING;
			break;
		case RESPONSE_CONFLICT_NEWER:
			policy = CONFLICT_UPDATE_NEWER;
			break;
		default:
			policy = CONFLICT_CANCELLED;
			break;
	}

	gtk_widget_destroy(dialog);
	return policy;
}

static gboolean rsync_is_available(void)
{
	gchar *program = g_find_program_in_path("rsync");
	gboolean found = program != NULL;
	g_free(program);
	return found;
}

/* Agregado por josejp2424 (2026): ejecuta una carpeta completa mediante un
 * único proceso rsync. Esto evita iniciar un cp separado por cada archivo. */
static RsyncResult run_rsync_operation(const char *source, const char *dest_path,
				      gboolean remove_source)
{
	const gchar *argv[16];
	gint argc = 0;
	gchar *source_arg = NULL;
	gchar *dest_arg = NULL;
	gchar *errors = NULL;
	GError *spawn_error = NULL;
	gint status = 0;
	struct stat source_info;
	struct stat dest_info;
	gboolean is_dir;
	gboolean dest_exists;
	gboolean ok;

	if (mc_lstat(source, &source_info) != 0)
	{
		send_error();
		return RSYNC_FAILED;
	}
	is_dir = S_ISDIR(source_info.st_mode);
	dest_exists = (mc_lstat(dest_path, &dest_info) == 0);

	/* Modificado por josejp2424 (2026): resolver los cambios de tipo antes
	 * de llamar a rsync. rsync no puede fusionar una carpeta con un archivo. */
	if (dest_exists && is_dir != S_ISDIR(dest_info.st_mode))
	{
		if (conflict_policy == CONFLICT_SKIP_EXISTING ||
		    (conflict_policy == CONFLICT_UPDATE_NEWER &&
		     source_info.st_mtime <= dest_info.st_mtime))
		{
			printf_send(_("'Skipped existing destination '%s'\n"), dest_path);
			return RSYNC_SKIPPED;
		}

		if (S_ISDIR(dest_info.st_mode))
		{
			if (rmdir(dest_path) != 0)
			{
				printf_send(_("!Cannot replace non-empty directory '%s': %s\n"),
					dest_path, g_strerror(errno));
				return RSYNC_FAILED;
			}
		}
		else if (unlink(dest_path) != 0)
		{
			printf_send(_("!Cannot replace '%s': %s\n"),
				dest_path, g_strerror(errno));
			return RSYNC_FAILED;
		}
	}

	if (is_dir)
	{
		source_arg = g_strconcat(source, "/", NULL);
		dest_arg = g_strconcat(dest_path, "/", NULL);
	}
	else
	{
		source_arg = g_strdup(source);
		dest_arg = g_strdup(dest_path);
	}

	argv[argc++] = "rsync";
	argv[argc++] = "-a";
	argv[argc++] = "--partial";
	if (conflict_policy == CONFLICT_SKIP_EXISTING)
		argv[argc++] = "--ignore-existing";
	else if (conflict_policy == CONFLICT_UPDATE_NEWER)
		argv[argc++] = "--update";
	if (remove_source)
		argv[argc++] = "--remove-source-files";
	argv[argc++] = "--";
	argv[argc++] = source_arg;
	argv[argc++] = dest_arg;
	argv[argc] = NULL;

	ok = rox_spawn_sync(NULL, (gchar **) argv, NULL,
		G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL,
		NULL, NULL, NULL, &errors, &status, &spawn_error);

	if (!ok)
	{
		printf_send(_("!Failed to start rsync: %s\n"),
			spawn_error ? spawn_error->message : _("Unknown error"));
		if (spawn_error)
			g_error_free(spawn_error);
	}
	else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		if (errors && *errors)
			printf_send(_("!rsync failed while processing '%s':\n%s\n"),
				source, errors);
		else
			printf_send(_("!rsync failed while processing '%s'\n"), source);
		ok = FALSE;
	}

	g_free(errors);
	g_free(source_arg);
	g_free(dest_arg);
	return ok ? RSYNC_DONE : RSYNC_FAILED;
}

/* Agregado por josejp2424 (2026): copia varias selecciones con una sola
 * ejecución de rsync. Si existe un cambio incompatible archivo/carpeta se
 * vuelve al recorrido individual, que puede resolverlo de forma segura. */
static gboolean can_batch_rsync_copy(GList *paths, const char *dest)
{
	GList *iter;

	for (iter = paths; iter; iter = iter->next)
	{
		const char *source = (const char *) iter->data;
		gchar *base = g_path_get_basename(source);
		gchar *target = g_build_filename(dest, base, NULL);
		struct stat source_info;
		struct stat dest_info;

		if (mc_lstat(source, &source_info) != 0)
		{
			g_free(target);
			g_free(base);
			return FALSE;
		}
		if (mc_lstat(target, &dest_info) == 0 &&
		    S_ISDIR(source_info.st_mode) != S_ISDIR(dest_info.st_mode))
		{
			g_free(target);
			g_free(base);
			return FALSE;
		}
		g_free(target);
		g_free(base);
	}
	return TRUE;
}

static RsyncResult run_rsync_batch_copy(GList *paths, const char *dest)
{
	gint count = g_list_length(paths);
	gchar **argv;
	gint argc = 0;
	gint i;
	GList *iter;
	gchar *dest_arg;
	gchar *errors = NULL;
	GError *spawn_error = NULL;
	gint status = 0;
	gboolean ok;

	argv = g_new0(gchar *, count + 10);
	argv[argc++] = g_strdup("rsync");
	argv[argc++] = g_strdup("-a");
	argv[argc++] = g_strdup("--partial");
	if (conflict_policy == CONFLICT_SKIP_EXISTING)
		argv[argc++] = g_strdup("--ignore-existing");
	else if (conflict_policy == CONFLICT_UPDATE_NEWER)
		argv[argc++] = g_strdup("--update");
	argv[argc++] = g_strdup("--");
	for (iter = paths; iter; iter = iter->next)
		argv[argc++] = g_strdup((const gchar *) iter->data);
	dest_arg = g_strconcat(dest, "/", NULL);
	argv[argc++] = dest_arg;
	argv[argc] = NULL;

	ok = rox_spawn_sync(NULL, argv, NULL,
		G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL,
		NULL, NULL, NULL, &errors, &status, &spawn_error);
	if (!ok)
	{
		printf_send(_("!Failed to start rsync: %s\n"),
			spawn_error ? spawn_error->message : _("Unknown error"));
		if (spawn_error)
			g_error_free(spawn_error);
	}
	else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		if (errors && *errors)
			printf_send(_("!rsync batch copy failed:\n%s\n"), errors);
		else
			printf_send(_("!rsync batch copy failed\n"));
		ok = FALSE;
	}

	g_free(errors);
	for (i = 0; i < argc; i++)
		g_free(argv[i]);
	g_free(argv);
	return ok ? RSYNC_DONE : RSYNC_FAILED;
}

static void rsync_copy_list_cb(gpointer data)
{
	GList *paths = (GList *) data;

	if (!can_batch_rsync_copy(paths, action_dest))
	{
		list_cb(data);
		return;
	}

	send_dir(action_dest);
	if (run_rsync_batch_copy(paths, action_dest) == RSYNC_DONE)
	{
		printf_send(_("'Fast rsync copy completed\n"));
		send_check_path(action_dest);
	}
	printf_send("%%100");
	send_done();
}

/* Agregado por josejp2424 (2026): rsync --remove-source-files conserva las
 * carpetas. Se eliminan sólo las que hayan quedado realmente vacías. */
static void remove_empty_source_dirs(const char *path)
{
	DIR *dir;
	struct dirent *entry;

	dir = mc_opendir(path);
	if (!dir)
		return;

	while ((entry = readdir(dir)) != NULL)
	{
		gchar *child;
		struct stat info;

		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0)
			continue;
		child = g_build_filename(path, entry->d_name, NULL);
		if (mc_lstat(child, &info) == 0 && S_ISDIR(info.st_mode))
			remove_empty_source_dirs(child);
		g_free(child);
	}
	closedir(dir);
	rmdir(path);
}

static gboolean paths_on_same_filesystem(const char *source, const char *dest_dir)
{
	struct stat source_info;
	struct stat dest_info;

	if (mc_lstat(source, &source_info) != 0 || mc_stat(dest_dir, &dest_info) != 0)
		return FALSE;
	return source_info.st_dev == dest_info.st_dev;
}

/* Agregado por josejp2424 (2026): motor híbrido de copia. */
static void do_copy_fast(const char *path, const char *dest)
{
	gchar *dest_path = g_strdup(make_dest_path(path, dest));

	RsyncResult result;

	check_flags();
	result = run_rsync_operation(path, dest_path, FALSE);
	if (result == RSYNC_DONE)
	{
		printf_send(_("'Copied '%s' with rsync\n"), path);
		send_check_path(dest_path);
	}
	g_free(dest_path);
}

/* Agregado por josejp2424 (2026): rename/mv dentro de la misma unidad y
 * rsync --remove-source-files para movimientos entre unidades o fusiones. */
static void do_move_fast(const char *path, const char *dest)
{
	gchar *dest_path = g_strdup(make_dest_path(path, dest));
	struct stat dest_info;
	gboolean dest_exists = (mc_lstat(dest_path, &dest_info) == 0);

	check_flags();
	if (!dest_exists && paths_on_same_filesystem(path, dest))
	{
		g_free(dest_path);
		do_move2(path, dest);
		return;
	}

	{
		RsyncResult result = run_rsync_operation(path, dest_path, TRUE);
		if (result == RSYNC_DONE)
		{
			struct stat source_info;
			printf_send(_("'Moved '%s' with rsync\n"), path);
			if (mc_lstat(path, &source_info) == 0 && S_ISDIR(source_info.st_mode))
				remove_empty_source_dirs(path);
			send_check_path(dest_path);
			send_check_path(path);
		}
	}
	g_free(dest_path);
}

/* We want to copy 'object' into directory 'dir'. If 'action_leaf'
 * is set then that is the new leafname, otherwise the leafname stays
 * the same.
 */
static const char *make_dest_path(const char *object, const char *dir)
{
	const char *leaf;

	if (action_leaf)
		leaf = action_leaf;
	else
	{
		leaf = strrchr(object, '/');
		if (!leaf)
			leaf = object;		/* Error? */
		else
			leaf++;
	}

	return make_path(dir, leaf);
}

/* If action_leaf is not NULL it specifies the new leaf name */
static void do_copy2(const char *path, const char *dest)
{
	const char	*dest_path;
	struct stat 	info;
	struct stat 	dest_info;

	check_flags();

	dest_path = make_dest_path(path, dest);

	if (mc_lstat(path, &info))
	{
		send_error();
		return;
	}

	if (mc_lstat(dest_path, &dest_info) == 0)
	{
		int		err;
		gboolean	merge;

		merge = S_ISDIR(info.st_mode) && S_ISDIR(dest_info.st_mode);

		if (conflict_policy == CONFLICT_SKIP_EXISTING)
			return;
		else if (conflict_policy == CONFLICT_UPDATE_NEWER &&
				 !S_ISDIR(info.st_mode) &&
				 info.st_mtime <= dest_info.st_mtime)
			return;
		else if (conflict_policy == CONFLICT_REPLACE_ALL ||
			 conflict_policy == CONFLICT_UPDATE_NEWER ||
			 (merge && o_merge))
		{
			/* Política automática; continuar sin otra pregunta. */
		}
		else
		{
			printf_send("<%s", path);
			printf_send(">%s", dest_path);


			if (!printf_conflict_reply(from_parent,
				_("'%s' already exists - %s?"),
				dest_path,
				merge ? _("merge contents")
					: _("overwrite")))
				return;
		}

		if (!merge)
		{
			if (S_ISDIR(dest_info.st_mode))
				err = rmdir(dest_path);
			else
				err = unlink(dest_path);

			if (err)
			{
				send_error();
				if (errno != ENOENT)
					return;
				printf_send(_("'Trying copy anyway...\n"));
			}
		}
	}
	else if (!quiet)
	{
		printf_send("<%s", path);
		printf_send(">");
		if (!printf_reply(from_parent, FALSE,
				  _("?Copy %s as %s?"), path, dest_path))
			return;
	}
	else if (!o_brief || S_ISDIR(info.st_mode))
		printf_send(_("'Copying %s as %s\n"), path, dest_path);

	if (S_ISDIR(info.st_mode))
	{
		mode_t	mode = info.st_mode;
		char *safe_path, *safe_dest;
		struct stat 	dest_info;
		gboolean	exists;

		safe_path = g_strdup(path);
		safe_dest = g_strdup(dest_path);

		exists = !mc_lstat(dest_path, &dest_info);

		if (exists && !S_ISDIR(dest_info.st_mode))
			printf_send(_("!ERROR: Destination already exists, "
				      "but is not a directory\n"));
		else if (exists == FALSE && mkdir(dest_path, 0700 | mode))
			send_error();
		else
		{
			if (!exists)
			{
				/* (just been created then) */
				lchown(safe_dest, info.st_uid, info.st_gid);
				xattr_copy(safe_path, safe_dest);
				send_check_path(safe_dest);
			}

			action_leaf = NULL;
			for_dir_contents(do_copy2, safe_path, safe_dest);
			/* Note: dest_path now invalid... */

			if (!exists)
			{
				struct utimbuf utb;

				/* We may have created the directory with
				 * more permissions than the source so that
				 * we could write to it... change it back now.
				 */
				if (chmod(safe_dest, mode))
				{
					/* Some filesystems don't support
					 * SetGID and SetUID bits. Ignore
					 * these errors.
					 */
					if (errno != EPERM)
						send_error();
				}

				/* Also, try to preserve the timestamps */
				utb.actime = info.st_atime;
				utb.modtime = info.st_mtime;

				utime(safe_dest, &utb);
			}
		}

		g_free(safe_path);
		g_free(safe_dest);
	}
	else if (S_ISLNK(info.st_mode))
	{
		char	*target;

		/* Not all versions of cp(1) can make symlinks,
		 * so we special-case it.
		 */

		target = readlink_dup(path);
		if (target)
		{
			if (symlink(target, dest_path))
				send_error();
			else
			{
				lchown(dest_path, info.st_uid, info.st_gid);
				send_check_path(dest_path);
			}

			g_free(target);
		}
		else
			send_error();
	}
	else
	{
		guchar	*error;

		error = copy_file(path, dest_path);

		if (error)
		{
			printf_send(_("!%s\nFailed to copy '%s'\n"),
							error, path);
			g_free(error);
		}
		else
			send_check_path(dest_path);
	}
}

/* If action_leaf is not NULL it specifies the new leaf name */
static void do_move2(const char *path, const char *dest)
{
	const char	*dest_path;
	const char	*argv[] = {"mv", "-f", NULL, NULL, NULL};
	struct stat 	info;
	struct stat 	dest_info;
	guchar		*error = NULL;

	check_flags();

	dest_path = make_dest_path(path, dest);

	if (mc_lstat(path, &info))
	{
		send_error();
		return;
	}

	if (mc_lstat(dest_path, &dest_info) == 0)
	{
		int		err;
		gboolean	merge;

		merge = S_ISDIR(info.st_mode) && S_ISDIR(dest_info.st_mode);

		if (conflict_policy == CONFLICT_SKIP_EXISTING)
			return;
		else if (conflict_policy == CONFLICT_UPDATE_NEWER &&
				 !S_ISDIR(info.st_mode) &&
				 info.st_mtime <= dest_info.st_mtime)
			return;
		else if (conflict_policy == CONFLICT_REPLACE_ALL ||
			 conflict_policy == CONFLICT_UPDATE_NEWER ||
			 (merge && o_merge))
		{
			/* Política automática; continuar sin otra pregunta. */
		}
		else
		{
			printf_send("<%s", path);
			printf_send(">%s", dest_path);


			if (!printf_conflict_reply(from_parent,
				_("'%s' already exists - %s?"),
				dest_path,
				merge ? _("merge contents")
					: _("overwrite")))
				return;
		}

		if (!merge)
		{
			if (S_ISDIR(dest_info.st_mode))
				err = rmdir(dest_path);
			else
				err = unlink(dest_path);

			if (err)
			{
				send_error();
				if (errno != ENOENT)
					return;
				printf_send(_("'Trying move anyway...\n"));
			}
		}
	}
	else if (!quiet)
	{
		printf_send("<%s", path);
		printf_send(">");
		if (!printf_reply(from_parent, FALSE,
				  _("?Move %s as %s?"), path, dest_path))
			return;
	}
	else if (!o_brief || S_ISDIR(info.st_mode))
		printf_send(_("'Moving %s as %s\n"), path, dest_path);

	argv[2] = path;
	argv[3] = dest_path;

	if (S_ISDIR(info.st_mode))
	{
		char *safe_path, *safe_dest;
		struct stat 	dest_info;
		gboolean	exists;

		safe_path = g_strdup(path);
		safe_dest = g_strdup(dest_path);

		exists = !mc_lstat(dest_path, &dest_info);

		if (exists && !S_ISDIR(dest_info.st_mode))
			printf_send(_("!ERROR: Destination already exists, "
				      "but is not a directory\n"));
		else
		{
			if (exists)
			{
				action_leaf = NULL;
				for_dir_contents(do_move2, safe_path, safe_dest);
				/* Note: dest_path now invalid... */

				/* If rmdir cannot delete the directory because it is not empty
				 * it is probably because some files failed to be moved,
				 * so not treating it as an error. */
				rmdir(safe_path);
			}
			else
			{
				/* Do actual move. */
				error = fork_exec_wait(argv);
			}
		}

		g_free(safe_path);
		g_free(safe_dest);
	}
	else
	{
		/* Do actual move. */
		error = fork_exec_wait(argv);
	}

	if (error)
	{
		printf_send(_("!%s\nFailed to move %s as %s\n"),
			error, path, dest_path);
		g_free(error);
		error = NULL;
	}
	else
	{
		send_check_path(dest_path);

		if (S_ISDIR(info.st_mode))
			send_mount_path(path);
		else
			send_check_path(path);
	}
}

/* Copy path to dest.
 * Check that path not copied into itself.
 */
static void do_copy(const char *path, const char *dest)
{
	if (is_sub_dir(make_dest_path(path, dest), path))
		printf_send(_("!ERROR: Can't copy object into itself\n"));
	else
	{
		do_copy2(path, dest);
		send_check_path(dest);
	}
}

/* Move path to dest.
 * Check that path not moved into itself.
 */
static void do_move(const char *path, const char *dest)
{
	if (is_sub_dir(make_dest_path(path, dest), path))
		printf_send(
		     _("!ERROR: Can't move/rename object into itself\n"));
	else
	{
		do_move2(path, dest);
		send_check_path(dest);
	}
}

/* Common code for do_link_relative() and do_link_absolute(). */
static void do_link(const char *path, const char *dest_path)
{
	if (quiet)
		printf_send(_("'Linking %s as %s\n"), path, dest_path);
	else {
		printf_send("<%s", path);
		printf_send(">");
		if (!printf_reply(from_parent, FALSE,
				  _("?Link %s as %s?"), path, dest_path))
			return;
	}

	if (symlink(path, dest_path))
		send_error();
	else
		send_check_path(dest_path);
}

static void do_link_relative(const char *path, const char *dest)
{
	char *rel_path;
	const char *dest_path;

	dest_path = make_dest_path(path, dest);

	check_flags();

	rel_path = get_relative_path(dest_path, path);
	do_link(rel_path, dest_path);
	g_free(rel_path);
}

static void do_link_absolute(const char *path, const char *dest)
{
	check_flags();
	do_link(path, make_dest_path(path, dest));
}

/* Mount/umount this item (depending on 'mount') */
static void do_mount(const guchar *path, gboolean mount)
{
	const char *argv[] = {"sh", "-c", NULL, NULL};
	char *err;

	check_flags();

	argv[2] = build_command_with_path(mount ? o_action_mount_command.value
					  : o_action_umount_command.value,
					  path);

	if (quiet)
		printf_send(mount ? _("'Mounting %s\n")
			          : _("'Unmounting %s\n"),
			    path);
	else if (!printf_reply(from_parent, FALSE,
			       mount ? _("?Mount %s?")
				     : _("?Unmount %s?"),
			       path))
		return;

	if (!mount)
	{
		char c = '?';
		/* Need to close all sub-directories now, or we
		 * can't unmount if dnotify is used.
		 */
		printf_send("X%s", path);
		/* Wait until it's safe... */
		read(from_parent, &c, 1);
		g_return_if_fail(c == 'X');
	}

	err = fork_exec_wait(argv);
	g_free((gchar *) argv[2]);
	if (err)
	{
		printf_send(mount ?
			_("!%s\nMount failed\n") :
			_("!%s\nUnmount failed\n"), err);
		g_free(err);

		/* Mount may have worked even on error, eg if we try to mount
		 * a read-only disk read/write, it gets mounted read-only
		 * with an error.
		 */
		if (mount && mount_is_mounted(path, NULL, NULL))
			printf_send(_("'(seems to be mounted now anyway)\n"));
		else
			return;
	}

	printf_send("M%s", path);
	if (mount && mount_open_dir)
		printf_send("o%s", path);
}

/*			CHILD MAIN LOOPS			*/

/* After forking, the child calls one of these functions */

/* We use a double for total size in order to count beyond 4Gb */
static void usage_cb(gpointer data)
{
	GList *paths = (GList *) data;
	double	total_size = 0;
	int n, i, per;
	gchar *base;

	n=g_list_length(paths);
	dir_counter = file_counter = 0;

	for (i=0; paths; paths = paths->next, i++)
	{
		guchar	*path = (guchar *) paths->data;

		send_dir(path);

		size_tally = 0;

		if(n>1 && i>0)
		{
			per=100*i/n;
			printf_send("%%%d", per);
		}
		do_usage(path, NULL);

		base = g_path_get_basename(path);
		printf_send("'%s: %s\n",
			    base,
			    format_double_size(size_tally));
		g_free(base);
		total_size += size_tally;
	}
	printf_send("%%-1");

	g_string_printf(message, _("'\nTotal: %s ("),
			format_double_size(total_size));

	if (file_counter)
		g_string_append_printf(message,
				"%ld %s%s", file_counter,
				file_counter == 1 ? _("file") : _("files"),
				dir_counter ? ", " : ")\n");

	if (file_counter == 0 && dir_counter == 0)
		g_string_append(message, _("no directories)\n"));
	else if (dir_counter)
		g_string_append_printf(message,
				"%ld %s)\n", dir_counter,
				dir_counter == 1 ? _("directory")
						 : _("directories"));

	send_msg();
}

#ifdef DO_MOUNT_POINTS
static void mount_cb(gpointer data)
{
	GList 		*paths = (GList *) data;
	gboolean	mount_points = FALSE;
	int n, i, per;

	n=g_list_length(paths);
	for (i=0; paths; paths = paths->next, i++)
	{
		guchar *path = (guchar *) paths->data;
		guchar *target;

		target = pathdup(path);
		if (!target)
			target = path;

		if(n>1 && i>0)
		{
			per=100*i/n;
			printf_send("%%%d", per);
		}
		if (mount_is_mounted(target, NULL, NULL) ||
		    g_hash_table_lookup(fstab_mounts, target))
		{
			mount_points = TRUE;
			do_mount(target, mount_mount);	/* Mount */
		}

		if (target != path)
			g_free(target);
	}

	if (mount_points)
		send_done();
	else
		printf_send(_("!No mount points selected!\n"));
}
#endif

/* Devuelve el directorio padre de una ruta. */
static guchar *dirname(guchar *path)
{
	guchar	*slash;

	slash = strrchr(path, '/');
	g_return_val_if_fail(slash != NULL, g_strdup(path));

	if (slash != path)
		return g_strndup(path, slash - path);
	return g_strdup("/");
}

static void delete_cb(gpointer data)
{
	GList	*paths = (GList *) data;
	int n, i, per;

	n=g_list_length(paths);
	for (i=0; paths; paths = paths->next, i++)
	{
		guchar	*path = (guchar *) paths->data;
		guchar	*dir;

		dir = dirname(path);
		send_dir(dir);

		if(n>1 && i>0)
		{
			per=100*i/n;
			printf_send("%%%d", per);
		}
		do_delete(path, dir);
		if (delete_batch_mode)
		{
			send_check_path(path);
			send_mount_path(path);
		}
		g_free(dir);
	}

	send_done();
}

/* Agregado por josejp2424 (2026): mueve cada elemento seleccionado a la
 * papelera estándar usada por GIO, PCManFM y otros gestores compatibles con
 * la especificación Freedesktop. No recorre el contenido de las carpetas: el
 * backend de GIO realiza el movimiento del elemento completo. */
static void trash_cb(gpointer data)
{
	GList *paths = (GList *) data;
	const gint total = g_list_length(paths);
	gint index = 0;

	for (; paths; paths = paths->next, index++)
	{
		const gchar *path = paths->data;
		gchar *parent = g_path_get_dirname(path);
		GFile *file = g_file_new_for_path(path);
		GError *error = NULL;

		send_dir(parent);
		if (total > 1 && index > 0)
			printf_send("%%%d", (100 * index) / total);

		if (!rox_trash_file(file, &error))
		{
			if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED))
				printf_send(_("!The filesystem containing '%s' does not support the standard Trash.\n"), path);
			else
				printf_send(_("!Unable to move '%s' to the Trash: %s\n"),
					path, error ? error->message : _("Unknown error"));
			g_clear_error(&error);
		}
		else
		{
			/* Sólo se actualiza el elemento superior; no se generan miles de
			 * notificaciones por el contenido interno de una carpeta. */
			send_check_path(path);
		}

		g_object_unref(file);
		g_free(parent);
	}

	send_done();
}

static void eject_cb(gpointer data)
{
	GList	*paths = (GList *) data;
	int n, i, per;

	n=g_list_length(paths);

	for (i=0; paths; paths = paths->next, i++)
	{
		guchar	*path = (guchar *) paths->data;

		if(n>1 && i>0)
		{
			per=100*i/n;
			printf_send("%%%d", per);
		}
		send_dir(path);

		do_eject(path);
	}

	send_done();
}

static void find_cb(gpointer data)
{
	GList *all_paths = (GList *) data;
	GList *paths;

	while (1)
	{
		for (paths = all_paths; paths; paths = paths->next)
		{
			guchar	*path = (guchar *) paths->data;

			send_dir(path);

			do_find(path, NULL);
		}

		if (!printf_reply(from_parent, TRUE,
				  _("?Another search?")))
			break;
		printf_send("#");
	}

	send_done();
}

static void chmod_cb(gpointer data)
{
	GList *paths = (GList *) data;
	int n, i, per;
	gchar *base;

	n=g_list_length(paths);

	for (i=0; paths; paths = paths->next, i++)
	{
		guchar	*path = (guchar *) paths->data;
		struct stat info;

		if(n>1 && i>0)
		{
			per=100*i/n;
			printf_send("%%%d", per);
		}
		send_dir(path);

		if (mc_stat(path, &info) != 0)
			send_error();
		else if (S_ISLNK(info.st_mode)) {
			base = g_path_get_basename(path);
			printf_send(_("!'%s' is a symbolic link\n"), base);
			g_free(base);
		}
		else
			do_chmod(path, NULL);
	}

	send_done();
}

static void settype_cb(gpointer data)
{
	GList *paths = (GList *) data;
	int n, i, per;
	gchar *base;

	n=g_list_length(paths);

	for (i=0; paths; paths = paths->next, i++)
	{
		guchar	*path = (guchar *) paths->data;
		struct stat info;

		if(n>1 && i>0)
		{
			per=100*i/n;
			printf_send("%%%d", per);
		}
		send_dir(path);

		if (mc_stat(path, &info) != 0)
			send_error();
		else if (S_ISLNK(info.st_mode)) {
			base = g_path_get_basename(path);
			printf_send(_("!'%s' is a symbolic link\n"), base);
			g_free(base);
		}
		else
			do_settype(path, NULL);
	}

	send_done();
}

static void list_cb(gpointer data)
{
	GList	*paths = (GList *) data;
	int n, i, per;

	n=g_list_length(paths);

	for (i=0; paths; paths = paths->next, i++)
	{
		if(n>1 && i>0)
		{
			per=100*i/n;
			printf_send("%%%d", per);
		}
		send_dir((char *) paths->data);

		action_do_func((char *) paths->data, action_dest);
	}

	if (n > 0)
		printf_send("%%100");
	send_done();
}

/*			EXTERNAL INTERFACE			*/

void action_find(GList *paths)
{
	GUIside		*gui_side;
	GtkWidget	*abox;

	if (!paths)
	{
		report_error(_("You need to select some items "
				"to search through"));
		return;
	}

	if (!last_find_string)
		last_find_string = g_strdup("'core'");

	new_entry_string = last_find_string;

	abox = abox_new(_("Find"), FALSE);
	gui_side = start_action(abox, find_cb, paths,
					 o_action_force.int_value,
					 o_action_brief.int_value,
					 o_action_recurse.int_value,
					 o_action_merge.int_value,
					 o_action_newer.int_value,
					 o_action_ignore.int_value);
	if (!gui_side)
		return;

	abox_add_results(ABOX(abox));

	gui_side->default_string = &last_find_string;
	abox_add_entry(ABOX(abox), last_find_string,
				new_help_button(show_condition_help, NULL));
	g_signal_connect(ABOX(abox)->entry, "changed",
			G_CALLBACK(entry_changed), gui_side);
	set_find_string_colour(ABOX(abox)->entry, last_find_string);

	gui_side->show_info = TRUE;
	gui_side->entry_string_func = set_find_string_colour;

	number_of_windows++;
	gtk_widget_show(abox);
}

/* Count disk space used by selected items */
void action_usage(GList *paths)
{
	GUIside *gui_side;
	GtkWidget *abox;

	if (!paths)
	{
		report_error(_("You need to select some items to count"));
		return;
	}

	abox = abox_new(_("Disk Usage"), TRUE);
	if(paths && paths->next)
		abox_set_percentage(ABOX(abox), 0);

	gui_side = start_action(abox, usage_cb, paths,
					 o_action_force.int_value,
					 o_action_brief.int_value,
					 o_action_recurse.int_value,
					 o_action_merge.int_value,
					 o_action_newer.int_value,
					 o_action_ignore.int_value);
	if (!gui_side)
		return;

	gui_side->show_info = TRUE;

	number_of_windows++;

	gtk_widget_show(abox);
}

/* Mount/unmount listed items (paths).
 * Free the list after this function returns.
 * If open_dir is TRUE and the dir is successfully mounted, open it.
 * quiet can be -1 for default.
 */
void action_mount(GList	*paths, gboolean open_dir, gboolean mount, int quiet)
{
#ifdef DO_MOUNT_POINTS
	GUIside		*gui_side;
	GtkWidget	*abox;

	if (quiet == -1)
	 	quiet = o_action_mount.int_value;

	mount_open_dir = open_dir;
	mount_mount = mount;

	abox = abox_new(_("Mount / Unmount"), quiet);
	if(paths && paths->next)
		abox_set_percentage(ABOX(abox), 0);
	gui_side = start_action(abox, mount_cb, paths,
					 o_action_force.int_value,
					 o_action_brief.int_value,
					 o_action_recurse.int_value,
					 o_action_merge.int_value,
					 o_action_newer.int_value,
					 o_action_ignore.int_value);
	if (!gui_side)
		return;

	log_info_paths("Mount", paths, NULL);

	number_of_windows++;
	gtk_widget_show(abox);
#else
	report_error(
		_("Rox-Filer2 does not yet support mount points on your "
			"system. Sorry."));
#endif /* DO_MOUNT_POINTS */
}

/* Agregado por josejp2424 (2026): confirmaciones compactas al estilo ROX. */
static gboolean confirm_trash_paths(GList *paths)
{
	gint count = g_list_length(paths);
	gchar *message;
	gboolean accepted;

	if (count == 1)
	{
		gchar *leaf = g_path_get_basename((const gchar *) paths->data);
		message = g_strdup_printf(_("Move '%s' to the Trash?"), leaf);
		g_free(leaf);
	}
	else
		message = g_strdup_printf(_("Move %d selected items to the Trash?"), count);

	accepted = confirm(message, ROX_ICON_TRASH, _("_Move to Trash"));
	g_free(message);
	return accepted;
}

static gboolean confirm_permanent_delete_paths(GList *paths)
{
	gint count = g_list_length(paths);
	gchar *message;
	gboolean accepted;

	if (count == 1)
	{
		gchar *leaf = g_path_get_basename((const gchar *) paths->data);
		message = g_strdup_printf(
			_("Permanently delete '%s'?\n\nThis action cannot be undone."), leaf);
		g_free(leaf);
	}
	else
		message = g_strdup_printf(
			_("Permanently delete %d selected items?\n\nThis action cannot be undone."), count);

	accepted = confirm(message, ROX_ICON_DELETE, _("_Delete Permanently"));
	g_free(message);
	return accepted;
}

/* Agregado por josejp2424 (2026): borrado normal compatible con la papelera
 * estándar de Freedesktop. GIO selecciona la papelera del usuario o la
 * papelera local del sistema de archivos, igual que otros gestores GTK. */
void action_trash(GList *paths)
{
	GUIside *gui_side;
	GtkWidget *abox;

	if (!paths || !remove_pinned_ok(paths) || !confirm_trash_paths(paths))
		return;

	abox = abox_new(_("Move to Trash"), TRUE);
	/* Agregado por josejp2424 (2026): animación nativa de borrado. */
	abox_set_operation_animation(ABOX(abox), "rox_delet.gif");
	if (paths->next)
		abox_set_percentage(ABOX(abox), 0);

	gui_side = start_action(abox, trash_cb, paths,
				  TRUE, TRUE,
				  o_action_recurse.int_value,
				  o_action_merge.int_value,
				  o_action_newer.int_value,
				  o_action_ignore.int_value);
	if (!gui_side)
		return;

	log_info_paths("Trash", paths, NULL);
	number_of_windows++;
	gtk_widget_show(abox);
}

/* Modificado por josejp2424 (2026): conserva el motor histórico para usos
 * internos que todavía necesitan su diálogo y sus opciones tradicionales. */
void action_delete(GList *paths)
{
	GUIside		*gui_side;
	GtkWidget	*abox;

	if (!remove_pinned_ok(paths))
		return;

	delete_batch_mode = FALSE;
	abox = abox_new(_("Delete"), o_action_delete.int_value);
	/* Agregado por josejp2424 (2026): animación nativa de borrado. */
	abox_set_operation_animation(ABOX(abox), "rox_delet.gif");
	if(paths && paths->next)
		abox_set_percentage(ABOX(abox), 0);
	gui_side = start_action(abox, delete_cb, paths,
					 o_action_force.int_value,
					 o_action_brief.int_value,
					 o_action_recurse.int_value,
					 o_action_merge.int_value,
					 o_action_newer.int_value,
					 o_action_ignore.int_value);
	if (!gui_side)
		return;

	abox_add_flag(ABOX(abox),
		_("Force"), _("Don't confirm deletion of non-writeable items"),
		'F', o_action_force.int_value);
	abox_add_flag(ABOX(abox),
		_("Brief"), _("Only log directories being deleted"),
		'B', o_action_brief.int_value);

	log_info_paths("Delete", paths, NULL);

	number_of_windows++;
	gtk_widget_show(abox);
}

/* Agregado por josejp2424 (2026): Shift+Delete confirma una sola vez y luego
 * elimina la selección completa sin pedir confirmación por cada archivo. */
void action_delete_permanently(GList *paths)
{
	GUIside *gui_side;
	GtkWidget *abox;

	if (!paths || !remove_pinned_ok(paths) ||
	    !confirm_permanent_delete_paths(paths))
		return;

	delete_batch_mode = TRUE;
	abox = abox_new(_("Delete Permanently"), TRUE);
	/* Agregado por josejp2424 (2026): animación nativa de borrado. */
	abox_set_operation_animation(ABOX(abox), "rox_delet.gif");
	if (paths->next)
		abox_set_percentage(ABOX(abox), 0);

	gui_side = start_action(abox, delete_cb, paths,
				  TRUE, TRUE,
				  o_action_recurse.int_value,
				  o_action_merge.int_value,
				  o_action_newer.int_value,
				  o_action_ignore.int_value);
	delete_batch_mode = FALSE;
	if (!gui_side)
		return;

	log_info_paths("Delete permanently", paths, NULL);
	number_of_windows++;
	gtk_widget_show(abox);
}

/* Change the permissions of the selected items */
void action_chmod(GList *paths, gboolean force_recurse, const char *action)
{
	GtkWidget	*abox;
	GUIside		*gui_side;
	static GList	*presets = NULL;
	gboolean	recurse = force_recurse || o_action_recurse.int_value;

	if (!paths)
	{
		report_error(_("You need to select the items "
				"whose permissions you want to change"));
		return;
	}

	if (!presets)
	{
		presets = g_list_append(presets, (gchar *)
				_("a+x (Make executable/searchable)"));
		presets = g_list_append(presets, (gchar *)
				_("a-x (Make non-executable/non-searchable)"));
		presets = g_list_append(presets, (gchar *)
				_("u+rw (Give owner read+write)"));
		presets = g_list_append(presets, (gchar *)
				_("go-rwx (Private - owner access only)"));
		presets = g_list_append(presets, (gchar *)
				_("go=u-w (Public access, not write)"));
	}

	if (!last_chmod_string)
		last_chmod_string = g_strdup((guchar *) presets->data);

	if (action)
		new_entry_string = g_strdup(action);
	else
		new_entry_string = g_strdup(last_chmod_string);

	abox = abox_new(_("Permissions"), FALSE);
	if(paths && paths->next)
		abox_set_percentage(ABOX(abox), 0);
	gui_side = start_action(abox, chmod_cb, paths,
				o_action_force.int_value,
				o_action_brief.int_value,
				recurse,
				o_action_merge.int_value,
				o_action_newer.int_value,
				o_action_ignore.int_value);

	if (!gui_side)
		goto out;

	abox_add_flag(ABOX(abox),
		_("Brief"), _("Don't list processed files"),
		'B', o_action_brief.int_value);
	abox_add_flag(ABOX(abox),
		_("Recurse"), _("Also change contents of subdirectories"),
		'R', recurse);

	gui_side->default_string = &last_chmod_string;
	abox_add_combo(ABOX(abox), _("Command:"), presets, new_entry_string,
				new_help_button(show_chmod_help, NULL));

	g_signal_connect(ABOX(abox)->entry, "changed",
			G_CALLBACK(entry_changed), gui_side);
#if 0
	g_signal_connect_swapped(gui_side->entry, "activate",
			G_CALLBACK(gtk_button_clicked),
			gui_side->yes);
#endif

	log_info_paths("Change permissions", paths, NULL);

	number_of_windows++;
	gtk_widget_show(abox);

out:
	null_g_free(&new_entry_string);
}

/* Set the MIME type of the selected items */
void action_settype(GList *paths, gboolean force_recurse, const char *oldtype)
{
	GtkWidget	*abox;
	GUIside		*gui_side;
	GList		*presets = NULL;
	gboolean	recurse = force_recurse || o_action_recurse.int_value;

	if (!paths)
	{
		report_error(_("You need to select the items "
				"whose type you want to change"));
		return;
	}

	if (!last_settype_string)
		last_settype_string = g_strdup("text/plain");

	if (oldtype)
		new_entry_string = g_strdup(oldtype);
	else
		new_entry_string = g_strdup(last_settype_string);

	abox = abox_new(_("Set type"), FALSE);
	if(paths && paths->next)
		abox_set_percentage(ABOX(abox), 0);
	gui_side = start_action(abox, settype_cb, paths,
				o_action_force.int_value,
				o_action_brief.int_value,
				recurse,
				o_action_merge.int_value,
				o_action_newer.int_value,
				o_action_ignore.int_value);

	if (!gui_side)
		goto out;

	abox_add_flag(ABOX(abox),
		_("Brief"), _("Don't list processed files"),
		'B', o_action_brief.int_value);
	abox_add_flag(ABOX(abox),
		_("Recurse"), _("Change contents of subdirectories"),
		'R', recurse);

	gui_side->default_string = &last_settype_string;

	/* Note: get the list again each time -- it can change */
	presets = mime_type_name_list(TRUE);
	abox_add_combo(ABOX(abox), _("Type:"), presets, new_entry_string,
				new_help_button(show_settype_help, NULL));
	g_list_free(presets);

	g_signal_connect(ABOX(abox)->entry, "changed",
			G_CALLBACK(entry_changed), gui_side);

	log_info_paths("Set file type", paths, NULL);

	number_of_windows++;
	gtk_widget_show(abox);

out:
	null_g_free(&new_entry_string);
}

static void log_info_paths_leaf(const gchar *message, GList *paths,
				const gchar *dest, const char *leaf)
{
	if (leaf == NULL)
	{
		log_info_paths(message, paths, dest);
	}
	else
	{
		char *new_dest;
		new_dest = g_build_filename(dest, leaf, NULL);
		log_info_paths(message, paths, new_dest);
		g_free(new_dest);
	}
}

/* If leaf is NULL then the copy has the same name as the original.
 * quiet can be -1 for default.
 */
void action_copy(GList *paths, const char *dest, const char *leaf, int quiet)
{
	GUIside		*gui_side;
	GtkWidget	*abox;

	if (quiet == -1)
		quiet = o_action_copy.int_value;

	rsync_available = rsync_is_available();
	if (destination_has_conflicts(paths, dest, leaf))
		conflict_policy = choose_conflict_policy(_("Copy"));
	else
		conflict_policy = CONFLICT_REPLACE_ALL;
	if (conflict_policy == CONFLICT_CANCELLED)
		return;
	use_rsync_engine = rsync_available && conflict_policy != CONFLICT_ASK;

	action_dest = dest;
	action_leaf = leaf;
	action_do_func = use_rsync_engine ? do_copy_fast : do_copy;

	abox = abox_new(_("Copy"), quiet);
	/* Agregado por josejp2424 (2026): animación nativa de copia. */
	abox_set_operation_animation(ABOX(abox), "rox_copi.gif");
	if(paths && paths->next)
		abox_set_percentage(ABOX(abox), 0);
	gui_side = start_action(abox,
		(use_rsync_engine && paths && paths->next && leaf == NULL)
			? rsync_copy_list_cb : list_cb,
		paths,
					 o_action_force.int_value,
					 o_action_brief.int_value,
					 o_action_recurse.int_value,
					 FALSE,
					 FALSE,
					 FALSE);
	if (!gui_side)
		return;

	if (!rsync_available)
		abox_log(ABOX(abox),
			_("rsync is not installed; using the classic Rox-Filer2 engine.\n"),
			NULL);

	abox_add_flag(ABOX(abox),
		_("Brief"), _("Only log directories as they are copied"),
		'B', o_action_brief.int_value);

	log_info_paths_leaf("Copy", paths, dest, leaf);

	number_of_windows++;
	gtk_widget_show(abox);
}

/* If leaf is NULL then the file is not renamed.
 * quiet can be -1 for default.
 */
void action_move(GList *paths, const char *dest, const char *leaf, int quiet)
{
	GUIside		*gui_side;
	GtkWidget	*abox;

	if (quiet == -1)
		quiet = o_action_move.int_value;

	rsync_available = rsync_is_available();
	if (destination_has_conflicts(paths, dest, leaf))
		conflict_policy = choose_conflict_policy(_("Move"));
	else
		conflict_policy = CONFLICT_REPLACE_ALL;
	if (conflict_policy == CONFLICT_CANCELLED)
		return;
	use_rsync_engine = rsync_available && conflict_policy != CONFLICT_ASK;

	action_dest = dest;
	action_leaf = leaf;
	action_do_func = use_rsync_engine ? do_move_fast : do_move;

	abox = abox_new(_("Move"), quiet);
	/* Agregado por josejp2424 (2026): mover comparte la misma animación de
	 * transferencia usada por copiar. */
	abox_set_operation_animation(ABOX(abox), "rox_copi.gif");
	if(paths && paths->next)
		abox_set_percentage(ABOX(abox), 0);
	gui_side = start_action(abox, list_cb, paths,
					 o_action_force.int_value,
					 o_action_brief.int_value,
					 o_action_recurse.int_value,
					 FALSE,
					 FALSE,
					 FALSE);
	if (!gui_side)
		return;

	if (!rsync_available)
		abox_log(ABOX(abox),
			_("rsync is not installed; using the classic Rox-Filer2 engine.\n"),
			NULL);

	abox_add_flag(ABOX(abox),
		_("Brief"), _("Don't log each file as it is moved"),
		'B', o_action_brief.int_value);

	log_info_paths_leaf("Move", paths, dest, leaf);

	number_of_windows++;
	gtk_widget_show(abox);
}

/* If leaf is NULL then the link will have the same name */
void action_link(GList *paths, const char *dest, const char *leaf,
		 gboolean relative)
{
	GtkWidget	*abox;
	GUIside		*gui_side;

	action_dest = dest;
	action_leaf = leaf;
	if (relative)
		action_do_func = do_link_relative;
	else
		action_do_func = do_link_absolute;

	abox = abox_new(_("Link"), o_action_link.int_value);
	if(paths && paths->next)
		abox_set_percentage(ABOX(abox), 0);
	gui_side = start_action(abox, list_cb, paths,
					 o_action_force.int_value,
					 o_action_brief.int_value,
					 o_action_recurse.int_value,
					 o_action_merge.int_value,
					 o_action_newer.int_value,
					 o_action_ignore.int_value);
	if (!gui_side)
		return;

	log_info_paths_leaf("Link", paths, dest, leaf);

	number_of_windows++;
	gtk_widget_show(abox);
}

/* Eject these paths */
void action_eject(GList *paths)
{
	GUIside		*gui_side;
	GtkWidget	*abox;

	abox = abox_new(_("Eject"), TRUE);
	if(paths && paths->next)
		abox_set_percentage(ABOX(abox), 0);
	gui_side = start_action(abox, eject_cb, paths,
					 o_action_force.int_value,
					 o_action_brief.int_value,
					 o_action_recurse.int_value,
					 o_action_merge.int_value,
					 o_action_newer.int_value,
					 o_action_ignore.int_value);
	if (!gui_side)
		return;

	log_info_paths("Eject", paths, NULL);

	number_of_windows++;
	gtk_widget_show(abox);
}

void action_init(void)
{
	option_add_int(&o_action_copy, "action_copy", 1);
	option_add_int(&o_action_move, "action_move", 1);
	option_add_int(&o_action_link, "action_link", 1);
	option_add_int(&o_action_delete, "action_delete", 0);
	option_add_int(&o_action_mount, "action_mount", 1);

	option_add_int(&o_action_force, "action_force", FALSE);
	option_add_int(&o_action_brief, "action_brief", FALSE);
	option_add_int(&o_action_recurse, "action_recurse", FALSE);
	option_add_int(&o_action_merge, "action_merge", FALSE);
	option_add_int(&o_action_newer, "action_newer", FALSE);
	option_add_int(&o_action_ignore, "action_ignore", FALSE);

	option_add_string(&o_action_mount_command,
			  "action_mount_command", "mount");
	option_add_string(&o_action_umount_command,
			  "action_umount_command", "umount");
	option_add_string(&o_action_eject_command,
			  "action_eject_command", "eject");
}

#define MAX_ASK 4

/* Check to see if any of the selected items (or their children) are
 * referenced by a legacy panel. If so, ask for confirmation.
 *
 * TRUE if it's OK to lose them.
 */
static gboolean remove_pinned_ok(GList *paths)
{
	GList		*ask = NULL, *next;
	GString		*message;
	int		i, ask_n = 0;
	gboolean	retval;

	for (; paths; paths = paths->next)
	{
		guchar	*path = (guchar *) paths->data;

		if (icons_require(path))
		{
			if (++ask_n > MAX_ASK)
				break;
			ask = g_list_append(ask, path);
		}
	}

	if (!ask)
		return TRUE;

	if (ask_n > MAX_ASK)
	{
		message = g_string_new(_("Deleting items such as "));
		ask_n--;
	}
	else if (ask_n == 1)
		message = g_string_new(_("Deleting the item "));
	else
		message = g_string_new(_("Deleting the items "));

	i = 0;
	for (next = ask; next; next = next->next)
	{
		guchar	*path = (guchar *) next->data;
		guchar	*leaf;

		leaf = strrchr(path, '/');
		if (leaf)
			leaf++;
		else
			leaf = path;

		g_string_append_c(message, '`');
		g_string_append(message, leaf);
		g_string_append_c(message, '\'');
		i++;
		if (i == ask_n - 1 && i > 0)
			g_string_append(message, _(" and "));
		else if (i < ask_n)
			g_string_append(message, ", ");
	}

	g_list_free(ask);

	if (ask_n == 1)
		message = g_string_append(message,
				_(" will affect some items on the pinboard "
				  "or panel - really delete it?"));
	else
	{
		if (ask_n > MAX_ASK)
			message = g_string_append_c(message, ',');
		message = g_string_append(message,
				_(" will affect some items on the pinboard "
					"or panel - really delete them?"));
	}

	retval = confirm(message->str, ROX_ICON_DELETE, NULL);

	g_string_free(message, TRUE);

	return retval;
}

void set_find_string_colour(GtkWidget *widget, const guchar *string)
{
	FindCondition *cond;

	cond = find_compile(string);
	entry_set_error(widget, !cond);

	find_condition_free(cond);
}
