/*
 * Rox-Filer2 Samba usershare integration.
 *
 * The design follows the same user-facing model as thunar-shares-plugin:
 * folders are exported with `net usershare` so the file manager itself never
 * needs to become root.  No Thunar/XFCE libraries are required.
 *
 * Copyright (C) 2026 Rox-Filer2 contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <glib/gstdio.h>

/* global.h provides the GTK3 compatibility menu types used by i18n.h. */
#include "global.h"
#include "i18n.h"
#include "samba_share.h"
#include "filer.h"
#include "dir.h"
#include "gui_support.h"

typedef struct {
	gchar *path;
	gchar *name;
	gchar *comment;
	gboolean writable;
	gboolean guest_ok;
} SambaShareInfo;

typedef struct {
	GtkWidget *share;
	GtkWidget *name;
	GtkWidget *comment;
	GtkWidget *writable;
	GtkWidget *guest;
	gboolean guest_allowed;
} ShareDialogWidgets;

/* A short-lived cache keeps directory scans cheap: one `net usershare info`
 * refresh serves all visible folders instead of spawning Samba once per icon. */
static GHashTable *shared_path_cache = NULL;
static gint64 shared_path_cache_time = 0;
static GMutex shared_path_cache_mutex;
#define SHARED_PATH_CACHE_USEC (5 * G_USEC_PER_SEC)

static GQuark samba_share_error_quark(void)
{
	return g_quark_from_static_string("rox-samba-share-error");
}

static void share_info_free(SambaShareInfo *info)
{
	if (!info)
		return;
	g_free(info->path);
	g_free(info->name);
	g_free(info->comment);
	g_free(info);
}

static SambaShareInfo *share_info_copy(const SambaShareInfo *info)
{
	SambaShareInfo *copy;

	if (!info)
		return NULL;
	copy = g_new0(SambaShareInfo, 1);
	copy->path = g_strdup(info->path);
	copy->name = g_strdup(info->name);
	copy->comment = g_strdup(info->comment);
	copy->writable = info->writable;
	copy->guest_ok = info->guest_ok;
	return copy;
}

static gchar *stderr_to_utf8(const gchar *text)
{
	gchar *utf8;

	if (!text || !*text)
		return g_strdup("");
	if (g_utf8_validate(text, -1, NULL))
		return g_strdup(text);
	utf8 = g_locale_to_utf8(text, -1, NULL, NULL, NULL);
	return utf8 ? utf8 : g_strdup(text);
}

static gboolean run_command(gchar **argv, gchar **stdout_ret, GError **error)
{
	gchar *stdout_text = NULL;
	gchar *stderr_text = NULL;
	gchar *error_text = NULL;
	gint wait_status = 0;
	GError *spawn_error = NULL;
	gboolean spawned;

	g_return_val_if_fail(argv && argv[0], FALSE);
	if (stdout_ret)
		*stdout_ret = NULL;

	spawned = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
		NULL, NULL, &stdout_text, &stderr_text, &wait_status, &spawn_error);
	if (!spawned)
	{
		g_propagate_error(error, spawn_error);
		g_free(stdout_text);
		g_free(stderr_text);
		return FALSE;
	}

	if (!WIFEXITED(wait_status) || WEXITSTATUS(wait_status) != 0)
	{
		error_text = stderr_to_utf8(stderr_text);
		if (!WIFEXITED(wait_status))
			g_set_error(error, samba_share_error_quark(), 1,
				"%s", (error_text && *error_text) ? error_text : _("Command failed."));
		else if (error_text && *error_text)
			g_set_error(error, samba_share_error_quark(), WEXITSTATUS(wait_status),
				"%s", g_strstrip(error_text));
		else
			g_set_error(error, samba_share_error_quark(), WEXITSTATUS(wait_status),
				_("Command failed with exit status %d."), WEXITSTATUS(wait_status));
		g_free(error_text);
		g_free(stderr_text);
		if (stdout_ret)
			*stdout_ret = stdout_text;
		else
			g_free(stdout_text);
		return FALSE;
	}

	g_free(stderr_text);
	if (stdout_ret)
		*stdout_ret = stdout_text;
	else
		g_free(stdout_text);
	return TRUE;
}

static gboolean run_net(const gchar * const *args, gchar **stdout_ret, GError **error)
{
	GPtrArray *argv;
	gchar *net;
	guint i;
	gboolean ok;

	net = g_find_program_in_path("net");
	if (!net)
	{
		g_set_error_literal(error, samba_share_error_quark(), 1,
			_("The 'net' command was not found. Install Samba's command-line tools."));
		return FALSE;
	}

	argv = g_ptr_array_new();
	g_ptr_array_add(argv, net);
	g_ptr_array_add(argv, (gpointer) "usershare");
	for (i = 0; args && args[i]; i++)
		g_ptr_array_add(argv, (gpointer) args[i]);
	g_ptr_array_add(argv, NULL);

	ok = run_command((gchar **) argv->pdata, stdout_ret, error);
	g_ptr_array_free(argv, TRUE);
	g_free(net);
	return ok;
}

gboolean samba_share_available(void)
{
	gchar *net = g_find_program_in_path("net");
	gboolean available = net != NULL;
	g_free(net);
	return available;
}

static void share_info_list_free(GList *list)
{
	g_list_free_full(list, (GDestroyNotify) share_info_free);
}

static gboolean acl_is_writable(const gchar *acl);
static gboolean guest_string_true(const gchar *value);

/* `net usershare info` emits an ini-like listing.  It must NOT be parsed with
 * GKeyFile: GKeyFile unescapes values, so a comment such as "C:\temp" comes
 * back mangled, an invalid escape makes the value vanish, and a literal "\n"
 * turns into a real newline that corrupts the usershare file on the next save.
 * Parse the lines verbatim instead. */
static GList *parse_usershare_info(const gchar *text)
{
	GList *list = NULL;
	GList *node;
	GList *next;
	SambaShareInfo *current = NULL;
	gchar **lines;
	guint i;

	lines = g_strsplit(text, "\n", -1);
	for (i = 0; lines[i]; i++)
	{
		gchar *line = lines[i];
		gchar *equals;
		gchar *key;
		const gchar *value;

		g_strchomp(line);
		if (!*line)
			continue;

		if (line[0] == '[')
		{
			gchar *end = strrchr(line, ']');

			if (!end)
				continue;
			*end = '\0';
			current = g_new0(SambaShareInfo, 1);
			current->name = g_strdup(line + 1);
			current->comment = g_strdup("");
			list = g_list_prepend(list, current);
			continue;
		}

		if (!current)
			continue;
		equals = strchr(line, '=');
		if (!equals)
			continue;
		*equals = '\0';
		key = g_strstrip(line);
		value = equals + 1;

		if (strcmp(key, "path") == 0)
		{
			g_free(current->path);
			current->path = g_strdup(value);
		}
		else if (strcmp(key, "comment") == 0)
		{
			g_free(current->comment);
			current->comment = g_strdup(value);
		}
		else if (strcmp(key, "usershare_acl") == 0)
			current->writable = acl_is_writable(value);
		else if (strcmp(key, "guest_ok") == 0)
			current->guest_ok = guest_string_true(value);
	}
	g_strfreev(lines);

	list = g_list_reverse(list);
	/* Drop stanzas without a path: `net` also prints diagnostics for broken
	 * usershare files, and those must not be mistaken for real shares. */
	for (node = list; node; node = next)
	{
		SambaShareInfo *info = node->data;

		next = node->next;
		if (!info->path || !*info->path)
		{
			share_info_free(info);
			list = g_list_delete_link(list, node);
		}
	}
	return list;
}

static gboolean load_usershares(GList **shares_ret, GError **error)
{
	static const gchar * const args[] = { "info", NULL };
	gchar *output = NULL;

	GError *run_error = NULL;
	GList *shares = NULL;
	gboolean ok;

	g_return_val_if_fail(shares_ret != NULL, FALSE);
	*shares_ret = NULL;
	ok = run_net(args, &output, &run_error);

	if (output && *output && g_utf8_validate(output, -1, NULL))
		shares = parse_usershare_info(output);
	g_free(output);

	if (!ok && !shares)
	{
		g_propagate_error(error, run_error);
		return FALSE;
	}
	/* `net` exits non-zero when any usershare file on the system is malformed,
	 * but it still lists the well formed ones.  Keep going with those. */
	g_clear_error(&run_error);
	*shares_ret = shares;
	return TRUE;
}

void samba_share_cache_invalidate(void)
{
	g_mutex_lock(&shared_path_cache_mutex);
	shared_path_cache_time = 0;
	g_mutex_unlock(&shared_path_cache_mutex);
}

static void refresh_shared_path_cache_locked(void)
{
	GList *shares = NULL;
	GList *node;
	GError *error = NULL;

	if (!shared_path_cache)
		shared_path_cache = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	g_hash_table_remove_all(shared_path_cache);

	if (samba_share_available() && load_usershares(&shares, &error))
	{
		for (node = shares; node; node = node->next)
		{
			SambaShareInfo *info = node->data;
			if (info->path && *info->path)
				g_hash_table_add(shared_path_cache,
					g_canonicalize_filename(info->path, NULL));
		}
	}
	g_clear_error(&error);
	share_info_list_free(shares);
	shared_path_cache_time = g_get_monotonic_time();
}

gboolean samba_share_path_is_shared(const gchar *path)
{
	gchar *canonical;
	gboolean shared = FALSE;
	gint64 now;

	if (!path || !*path)
		return FALSE;
	now = g_get_monotonic_time();
	g_mutex_lock(&shared_path_cache_mutex);
	if (!shared_path_cache || !shared_path_cache_time ||
	    now - shared_path_cache_time > SHARED_PATH_CACHE_USEC)
		refresh_shared_path_cache_locked();
	canonical = g_canonicalize_filename(path, NULL);
	if (shared_path_cache)
		shared = g_hash_table_contains(shared_path_cache, canonical);
	g_free(canonical);
	g_mutex_unlock(&shared_path_cache_mutex);
	return shared;
}

const gchar *samba_share_emblem_icon(void)
{
	GtkIconTheme *theme = gtk_icon_theme_get_default();

	if (!theme)
		return NULL;
	if (gtk_icon_theme_has_icon(theme, "emblem-shared"))
		return "emblem-shared";
	if (gtk_icon_theme_has_icon(theme, "emblem-shared-symbolic"))
		return "emblem-shared-symbolic";
	return NULL;
}

static gboolean acl_is_writable(const gchar *acl)
{
	return acl && strstr(acl, "Everyone:F") != NULL;
}

static gboolean guest_string_true(const gchar *value)
{
	return value &&
		(g_ascii_strcasecmp(value, "y") == 0 ||
		 g_ascii_strcasecmp(value, "yes") == 0 ||
		 g_ascii_strcasecmp(value, "true") == 0 ||
		 strcmp(value, "1") == 0);
}

static gboolean same_path(const gchar *a, const gchar *b)
{
	gchar *ca;
	gchar *cb;
	gboolean equal;

	if (!a || !b)
		return FALSE;
	ca = g_canonicalize_filename(a, NULL);
	cb = g_canonicalize_filename(b, NULL);
	equal = g_strcmp0(ca, cb) == 0;
	g_free(ca);
	g_free(cb);
	return equal;
}

static const SambaShareInfo *find_share_for_path(GList *shares, const gchar *path)
{
	GList *node;

	for (node = shares; node; node = node->next)
	{
		const SambaShareInfo *info = node->data;

		if (same_path(info->path, path))
			return info;
	}
	return NULL;
}

static gboolean share_name_in_use(GList *shares, const gchar *name,
				  const gchar *current_path)
{
	GList *node;

	for (node = shares; node; node = node->next)
	{
		const SambaShareInfo *info = node->data;

		if (g_ascii_strcasecmp(info->name, name) == 0)
			return !same_path(info->path, current_path);
	}
	return FALSE;
}

static gboolean testparm_boolean(const gchar *parameter, gboolean default_value,
				 gboolean *known_ret)
{
	gchar *testparm;
	gchar *option;
	gchar *output = NULL;
	gchar *argv[4];
	GError *error = NULL;
	gboolean value = default_value;

	if (known_ret)
		*known_ret = FALSE;
	testparm = g_find_program_in_path("testparm");
	if (!testparm)
		return default_value;

	option = g_strdup_printf("--parameter-name=%s", parameter);
	argv[0] = testparm;
	argv[1] = (gchar *) "-s";
	argv[2] = option;
	argv[3] = NULL;
	if (run_command(argv, &output, &error))
	{
		gchar *stripped = output ? g_strstrip(output) : (gchar *) "";
		if (g_ascii_strcasecmp(stripped, "yes") == 0 ||
		    g_ascii_strcasecmp(stripped, "true") == 0 || strcmp(stripped, "1") == 0)
			value = TRUE;
		else if (g_ascii_strcasecmp(stripped, "no") == 0 ||
			 g_ascii_strcasecmp(stripped, "false") == 0 || strcmp(stripped, "0") == 0)
			value = FALSE;
		if (known_ret)
			*known_ret = TRUE;
	}
	g_clear_error(&error);
	g_free(output);
	g_free(option);
	g_free(testparm);
	return value;
}

static void show_error(GtkWindow *parent, const gchar *primary, const gchar *secondary)
{
	GtkWidget *dialog;

	dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", primary ? primary : _("Unable to update the Samba share."));
	if (secondary && *secondary)
		gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", secondary);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

static gboolean confirm_permissions(GtkWindow *parent, const gchar *path)
{
	GtkWidget *dialog;
	gint response;

	dialog = gtk_message_dialog_new(parent, GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s",
		_("Rox-Filer2 needs to add filesystem permissions to this folder before it can be shared. Continue?"));
	gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
		"%s\n%s", path,
		_("These permissions are not removed automatically when you stop sharing the folder."));
	gtk_dialog_add_buttons(GTK_DIALOG(dialog), _("_Cancel"), GTK_RESPONSE_CANCEL,
		_("_Apply"), GTK_RESPONSE_APPLY, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
	response = gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
	return response == GTK_RESPONSE_APPLY;
}

static gboolean ensure_folder_permissions(GtkWindow *parent, const gchar *path,
					 gboolean writable, gboolean guest_ok)
{
	struct stat st;
	mode_t wanted;
	mode_t missing;

	if (g_stat(path, &st) != 0)
	{
		gchar *detail = g_strdup_printf("%s: %s", path, g_strerror(errno));
		show_error(parent, _("Unable to change folder permissions."), detail);
		g_free(detail);
		return FALSE;
	}

	wanted = st.st_mode;
	if (guest_ok)
		wanted |= S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
	if (writable)
		wanted |= S_IWGRP | S_IWOTH;
	missing = wanted & ~st.st_mode;
	if (!missing)
		return TRUE;
	if (!confirm_permissions(parent, path))
		return FALSE;
	if (g_chmod(path, wanted) != 0)
	{
		gchar *detail = g_strdup_printf("%s: %s", path, g_strerror(errno));
		show_error(parent, _("Unable to change folder permissions."), detail);
		g_free(detail);
		return FALSE;
	}
	return TRUE;
}

static gboolean add_or_update_share(const SambaShareInfo *info,
				    gboolean guest_allowed, GError **error)
{
	gchar *acl;
	const gchar *args[8];
	guint n = 0;

	acl = info->writable ? g_strdup("Everyone:F") :
		g_strdup_printf("Everyone:R,%s:F", g_get_user_name());
	args[n++] = "add";
	args[n++] = "--long";
	args[n++] = info->name;
	args[n++] = info->path;
	args[n++] = info->comment ? info->comment : "";
	args[n++] = acl;
	if (guest_allowed)
		args[n++] = info->guest_ok ? "guest_ok=y" : "guest_ok=n";
	args[n] = NULL;

	if (!run_net(args, NULL, error))
	{
		g_free(acl);
		return FALSE;
	}
	g_free(acl);
	return TRUE;
}

static gboolean delete_share_name(const gchar *name, GError **error)
{
	const gchar *args[] = { "delete", name, NULL };
	return run_net(args, NULL, error);
}

static gboolean save_share(GtkWindow *parent, const SambaShareInfo *old_info,
			   const SambaShareInfo *new_info, gboolean guest_allowed,
			   GError **error)
{
	SambaShareInfo *backup = share_info_copy(old_info);
	gboolean renamed = old_info && g_ascii_strcasecmp(old_info->name, new_info->name) != 0;

	if (!ensure_folder_permissions(parent, new_info->path,
		new_info->writable, new_info->guest_ok && guest_allowed))
	{
		share_info_free(backup);
		return FALSE;
	}

	if (renamed && !delete_share_name(old_info->name, error))
	{
		share_info_free(backup);
		return FALSE;
	}
	if (!add_or_update_share(new_info, guest_allowed, error))
	{
		/* Best effort rollback when a rename removed the old usershare first. */
		if (renamed && backup)
		{
			GError *rollback_error = NULL;
			(void) add_or_update_share(backup, guest_allowed, &rollback_error);
			g_clear_error(&rollback_error);
		}
		share_info_free(backup);
		return FALSE;
	}
	share_info_free(backup);
	return TRUE;
}

static void share_toggle_changed(GtkToggleButton *button, gpointer user_data)
{
	ShareDialogWidgets *widgets = user_data;
	gboolean active = gtk_toggle_button_get_active(button);

	gtk_widget_set_sensitive(widgets->name, active);
	gtk_widget_set_sensitive(widgets->comment, active);
	gtk_widget_set_sensitive(widgets->writable, active);
	gtk_widget_set_sensitive(widgets->guest, active && widgets->guest_allowed);
}

static GtkWidget *left_label(const gchar *text)
{
	GtkWidget *label = gtk_label_new(text);
	gtk_widget_set_halign(label, GTK_ALIGN_START);
	return label;
}

void samba_share_show_dialog(GtkWindow *parent, const gchar *path)
{
	GList *shares = NULL;
	SambaShareInfo *old_info = NULL;
	GError *error = NULL;
	GtkWidget *dialog;
	GtkWidget *content;
	GtkWidget *grid;
	GtkWidget *path_label;
	ShareDialogWidgets widgets;
	gboolean guest_known = FALSE;
	gboolean owner_known = FALSE;
	gboolean owner_only;
	struct stat st;
	gchar *basename = NULL;
	gchar *display_name = NULL;
	gint response;
	gboolean changed = FALSE;

	if (!path || !g_file_test(path, G_FILE_TEST_IS_DIR))
		return;
	if (!samba_share_available())
	{
		show_error(parent, _("Samba usershare support is not available."),
			_("The 'net' command was not found. Install Samba's command-line tools."));
		return;
	}
	if (!load_usershares(&shares, &error))
	{
		show_error(parent, _("Samba usershare support is not available."),
			error ? error->message : NULL);
		g_clear_error(&error);
		return;
	}
	old_info = share_info_copy(find_share_for_path(shares, path));

	owner_only = testparm_boolean("usershare owner only", TRUE, &owner_known);
	if ((owner_only || !owner_known) && geteuid() != 0 &&
	    g_stat(path, &st) == 0 && st.st_uid != geteuid())
	{
		show_error(parent, _("You are not the owner of this folder."), path);
		share_info_free(old_info);
		share_info_list_free(shares);
		return;
	}

	memset(&widgets, 0, sizeof(widgets));
	widgets.guest_allowed = testparm_boolean("usershare allow guests", FALSE, &guest_known);
	if (!guest_known)
		widgets.guest_allowed = FALSE;

	dialog = gtk_dialog_new_with_buttons(_("Folder Sharing"), parent,
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		_("_Cancel"), GTK_RESPONSE_CANCEL,
		_("_Apply"), GTK_RESPONSE_APPLY,
		NULL);
	gtk_window_set_icon_name(GTK_WINDOW(dialog), rox_icon_name("folder-publicshare"));
	gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);
	gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_APPLY);

	content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
	gtk_container_set_border_width(GTK_CONTAINER(content), 14);
	grid = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
	gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
	gtk_container_add(GTK_CONTAINER(content), grid);

	path_label = gtk_label_new(path);
	gtk_label_set_ellipsize(GTK_LABEL(path_label), PANGO_ELLIPSIZE_MIDDLE);
	gtk_widget_set_halign(path_label, GTK_ALIGN_START);
	gtk_widget_set_hexpand(path_label, TRUE);
	gtk_grid_attach(GTK_GRID(grid), path_label, 0, 0, 2, 1);

	widgets.share = gtk_check_button_new_with_label(_("Share this folder"));
	gtk_grid_attach(GTK_GRID(grid), widgets.share, 0, 1, 2, 1);

	gtk_grid_attach(GTK_GRID(grid), left_label(_("Share name:")), 0, 2, 1, 1);
	widgets.name = gtk_entry_new();
	gtk_entry_set_activates_default(GTK_ENTRY(widgets.name), TRUE);
	gtk_widget_set_size_request(widgets.name, 300, -1);
	gtk_grid_attach(GTK_GRID(grid), widgets.name, 1, 2, 1, 1);

	gtk_grid_attach(GTK_GRID(grid), left_label(_("Comment:")), 0, 3, 1, 1);
	widgets.comment = gtk_entry_new();
	gtk_entry_set_activates_default(GTK_ENTRY(widgets.comment), TRUE);
	gtk_grid_attach(GTK_GRID(grid), widgets.comment, 1, 3, 1, 1);

	widgets.writable = gtk_check_button_new_with_label(
		_("Allow others to create and delete files in this folder"));
	gtk_grid_attach(GTK_GRID(grid), widgets.writable, 0, 4, 2, 1);
	widgets.guest = gtk_check_button_new_with_label(_("Guest access"));
	gtk_grid_attach(GTK_GRID(grid), widgets.guest, 0, 5, 2, 1);
	if (!widgets.guest_allowed)
		gtk_widget_set_tooltip_text(widgets.guest,
			_("Guest access is disabled by the Samba configuration."));

	if (old_info)
	{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets.share), TRUE);
		gtk_entry_set_text(GTK_ENTRY(widgets.name), old_info->name ? old_info->name : "");
		gtk_entry_set_text(GTK_ENTRY(widgets.comment), old_info->comment ? old_info->comment : "");
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets.writable), old_info->writable);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets.guest),
			old_info->guest_ok && widgets.guest_allowed);
	}
	else
	{
		basename = g_path_get_basename(path);
		display_name = g_filename_display_name(basename);
		gtk_entry_set_text(GTK_ENTRY(widgets.name), display_name ? display_name : basename);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widgets.share), FALSE);
	}
	g_free(display_name);
	g_free(basename);

	g_signal_connect(widgets.share, "toggled", G_CALLBACK(share_toggle_changed), &widgets);
	share_toggle_changed(GTK_TOGGLE_BUTTON(widgets.share), &widgets);
	gtk_widget_show_all(dialog);
	/* show_all() would otherwise re-enable visual sensitivity of the guest box. */
	share_toggle_changed(GTK_TOGGLE_BUTTON(widgets.share), &widgets);

	for (;;)
	{
		response = gtk_dialog_run(GTK_DIALOG(dialog));
		if (response != GTK_RESPONSE_APPLY)
			break;

		if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widgets.share)))
		{
			if (!old_info || delete_share_name(old_info->name, &error))
			{
				changed = old_info != NULL;
				break;
			}
			show_error(parent, _("Unable to update the Samba share."), error ? error->message : NULL);
			g_clear_error(&error);
			continue;
		}
		else
		{
			SambaShareInfo new_info;
			const gchar *name = gtk_entry_get_text(GTK_ENTRY(widgets.name));
			const gchar *comment;
			if (!name || strspn(name, " \t\r\n") == strlen(name))
			{
				show_error(parent, _("Share name cannot be empty."), NULL);
				continue;
			}
			if (strpbrk(name, "/\\[]\n\r"))
			{
				show_error(parent, _("Share name contains an invalid character."), NULL);
				continue;
			}
			comment = gtk_entry_get_text(GTK_ENTRY(widgets.comment));
			if (strpbrk(comment, "\n\r"))
			{
				show_error(parent, _("Comment contains an invalid character."), NULL);
				continue;
			}
			if (share_name_in_use(shares, name, path))
			{
				gchar *detail = g_strdup_printf(_("Another share already uses the name '%s'."), name);
				show_error(parent, detail, NULL);
				g_free(detail);
				continue;
			}

			memset(&new_info, 0, sizeof(new_info));
			new_info.path = (gchar *) path;
			new_info.name = (gchar *) name;
			new_info.comment = (gchar *) comment;
			new_info.writable = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widgets.writable));
			new_info.guest_ok = widgets.guest_allowed &&
				gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widgets.guest));
			if (save_share(parent, old_info, &new_info, widgets.guest_allowed, &error))
			{
				changed = TRUE;
				break;
			}
			if (error)
			{
				show_error(parent, _("Unable to update the Samba share."), error->message);
				g_clear_error(&error);
			}
		}
	}

	gtk_widget_destroy(dialog);
	if (changed)
	{
		samba_share_cache_invalidate();
		dir_force_update_path(path);
	}
	share_info_free(old_info);
	share_info_list_free(shares);
}


enum {
	MANAGER_COL_NAME,
	MANAGER_COL_PATH,
	MANAGER_COL_WRITABLE,
	MANAGER_COL_GUEST,
	MANAGER_N_COLS
};

typedef struct {
	GtkWindow *parent;
	GtkWidget *dialog;
	GtkWidget *tree;
	GtkWidget *empty_label;
	GtkWidget *open_button;
	GtkWidget *edit_button;
	GtkWidget *stop_button;
	GtkListStore *store;
} SambaShareManager;

static gboolean manager_get_selected(SambaShareManager *manager,
		gchar **name_ret, gchar **path_ret)
{
	GtkTreeSelection *selection;
	GtkTreeModel *model;
	GtkTreeIter iter;

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(manager->tree));
	if (!gtk_tree_selection_get_selected(selection, &model, &iter))
		return FALSE;
	if (name_ret)
		gtk_tree_model_get(model, &iter, MANAGER_COL_NAME, name_ret, -1);
	if (path_ret)
		gtk_tree_model_get(model, &iter, MANAGER_COL_PATH, path_ret, -1);
	return TRUE;
}

static void manager_selection_changed(GtkTreeSelection *selection, gpointer data)
{
	SambaShareManager *manager = data;
	gboolean active = gtk_tree_selection_count_selected_rows(selection) > 0;

	gtk_widget_set_sensitive(manager->open_button, active);
	gtk_widget_set_sensitive(manager->edit_button, active);
	gtk_widget_set_sensitive(manager->stop_button, active);
}

static void manager_refresh(SambaShareManager *manager)
{
	GList *shares = NULL;
	GList *node;
	GError *error = NULL;
	guint count = 0;

	gtk_list_store_clear(manager->store);
	if (!load_usershares(&shares, &error))
	{
		show_error(manager->parent, _("Samba usershare support is not available."),
			error ? error->message : NULL);
		g_clear_error(&error);
	}
	else
	{
		for (node = shares; node; node = node->next)
		{
			SambaShareInfo *info = node->data;
			GtkTreeIter iter;
			gtk_list_store_append(manager->store, &iter);
			gtk_list_store_set(manager->store, &iter,
				MANAGER_COL_NAME, info->name ? info->name : "",
				MANAGER_COL_PATH, info->path ? info->path : "",
				MANAGER_COL_WRITABLE, info->writable,
				MANAGER_COL_GUEST, info->guest_ok,
				-1);
			count++;
		}
	}
	share_info_list_free(shares);
	gtk_widget_set_visible(manager->empty_label, count == 0);
}

static void manager_open_clicked(GtkButton *button, gpointer data)
{
	SambaShareManager *manager = data;
	gchar *path = NULL;
	(void) button;
	if (manager_get_selected(manager, NULL, &path) && path && *path)
		filer_opendir(path, NULL, NULL);
	g_free(path);
}

static void manager_edit_clicked(GtkButton *button, gpointer data)
{
	SambaShareManager *manager = data;
	gchar *path = NULL;
	(void) button;
	if (manager_get_selected(manager, NULL, &path) && path && *path)
	{
		samba_share_show_dialog(GTK_WINDOW(manager->dialog), path);
		manager_refresh(manager);
	}
	g_free(path);
}

static void manager_stop_clicked(GtkButton *button, gpointer data)
{
	SambaShareManager *manager = data;
	gchar *name = NULL;
	gchar *path = NULL;
	GtkWidget *confirm;
	gint response;
	GError *error = NULL;
	(void) button;

	if (!manager_get_selected(manager, &name, &path))
		return;
	confirm = gtk_message_dialog_new(GTK_WINDOW(manager->dialog),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
		_("Stop sharing '%s'?"), name ? name : "");
	gtk_dialog_add_buttons(GTK_DIALOG(confirm), _("_Cancel"), GTK_RESPONSE_CANCEL,
		_("Stop Sharing"), GTK_RESPONSE_ACCEPT, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(confirm), GTK_RESPONSE_CANCEL);
	response = gtk_dialog_run(GTK_DIALOG(confirm));
	gtk_widget_destroy(confirm);
	if (response == GTK_RESPONSE_ACCEPT)
	{
		if (!delete_share_name(name, &error))
		{
			show_error(GTK_WINDOW(manager->dialog),
				_("Unable to update the Samba share."), error ? error->message : NULL);
			g_clear_error(&error);
		}
		else
		{
			samba_share_cache_invalidate();
			if (path && *path)
				dir_force_update_path(path);
			manager_refresh(manager);
		}
	}
	g_free(name);
	g_free(path);
}

static void manager_row_activated(GtkTreeView *tree, GtkTreePath *path,
		GtkTreeViewColumn *column, gpointer data)
{
	(void) tree;
	(void) path;
	(void) column;
	manager_open_clicked(NULL, data);
}

void samba_share_show_manager(GtkWindow *parent)
{
	SambaShareManager manager;
	GtkWidget *content;
	GtkWidget *box;
	GtkWidget *scroller;
	GtkWidget *buttons;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;

	if (!samba_share_available())
	{
		show_error(parent, _("Samba usershare support is not available."),
			_("The 'net' command was not found. Install Samba's command-line tools."));
		return;
	}

	memset(&manager, 0, sizeof(manager));
	manager.parent = parent;
	manager.dialog = gtk_dialog_new_with_buttons(_("Shared Folders"), parent,
		GTK_DIALOG_DESTROY_WITH_PARENT, _("_Close"), GTK_RESPONSE_CLOSE, NULL);
	gtk_window_set_default_size(GTK_WINDOW(manager.dialog), 720, 420);
	gtk_window_set_icon_name(GTK_WINDOW(manager.dialog), rox_icon_name("folder-publicshare"));
	content = gtk_dialog_get_content_area(GTK_DIALOG(manager.dialog));
	gtk_container_set_border_width(GTK_CONTAINER(content), 12);
	box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_add(GTK_CONTAINER(content), box);

	manager.store = gtk_list_store_new(MANAGER_N_COLS,
		G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN);
	manager.tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(manager.store));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(manager.tree), TRUE);

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(_("Name"), renderer, "text", MANAGER_COL_NAME, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(manager.tree), column);
	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(_("Path"), renderer, "text", MANAGER_COL_PATH, NULL);
	gtk_tree_view_column_set_expand(column, TRUE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(manager.tree), column);
	renderer = gtk_cell_renderer_toggle_new();
	g_object_set(renderer, "activatable", FALSE, NULL);
	column = gtk_tree_view_column_new_with_attributes(_("Write"), renderer, "active", MANAGER_COL_WRITABLE, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(manager.tree), column);
	renderer = gtk_cell_renderer_toggle_new();
	g_object_set(renderer, "activatable", FALSE, NULL);
	column = gtk_tree_view_column_new_with_attributes(_("Guest"), renderer, "active", MANAGER_COL_GUEST, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(manager.tree), column);

	scroller = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_set_vexpand(scroller, TRUE);
	gtk_container_add(GTK_CONTAINER(scroller), manager.tree);
	gtk_box_pack_start(GTK_BOX(box), scroller, TRUE, TRUE, 0);
	manager.empty_label = gtk_label_new(_("No folders are currently shared."));
	gtk_widget_set_halign(manager.empty_label, GTK_ALIGN_START);
	gtk_box_pack_start(GTK_BOX(box), manager.empty_label, FALSE, FALSE, 0);

	buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
	gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_START);
	manager.open_button = gtk_button_new_with_mnemonic(_("_Open"));
	manager.edit_button = gtk_button_new_with_mnemonic(_("_Edit"));
	manager.stop_button = gtk_button_new_with_label(_("Stop Sharing"));
	gtk_container_add(GTK_CONTAINER(buttons), manager.open_button);
	gtk_container_add(GTK_CONTAINER(buttons), manager.edit_button);
	gtk_container_add(GTK_CONTAINER(buttons), manager.stop_button);
	gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(manager.tree));
	g_signal_connect(selection, "changed", G_CALLBACK(manager_selection_changed), &manager);
	g_signal_connect(manager.open_button, "clicked", G_CALLBACK(manager_open_clicked), &manager);
	g_signal_connect(manager.edit_button, "clicked", G_CALLBACK(manager_edit_clicked), &manager);
	g_signal_connect(manager.stop_button, "clicked", G_CALLBACK(manager_stop_clicked), &manager);
	g_signal_connect(manager.tree, "row-activated", G_CALLBACK(manager_row_activated), &manager);
	manager_selection_changed(selection, &manager);
	manager_refresh(&manager);

	gtk_widget_show_all(manager.dialog);
	gtk_widget_set_visible(manager.empty_label,
		gtk_tree_model_iter_n_children(GTK_TREE_MODEL(manager.store), NULL) == 0);
	gtk_dialog_run(GTK_DIALOG(manager.dialog));
	gtk_widget_destroy(manager.dialog);
	g_object_unref(manager.store);
}
