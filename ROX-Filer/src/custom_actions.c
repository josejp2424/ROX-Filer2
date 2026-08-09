/*
 * ROX-Filer GTK3 - XDG-style custom actions.
 * Copyright (C) 2026 josejp2424.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Action files are stored below:
 *   g_get_user_data_dir()/rox-filer/actions
 * They use a normal Desktop Entry plus an X-ROX-Filer Action section.
 */

#include "config.h"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "global.h"
#include "custom_actions.h"
#include "xdg_apps.h"
#include "options.h"
#include "support.h"
#include "gui_support.h"
#include "run.h"
#include "filer.h"
#include "type.h"

#define ACTION_GROUP "X-ROX-Filer Action"
#define ACTION_PREFIX "rox-action-"

enum {
    ACTION_COL_NAME = 0,
    ACTION_COL_COMMAND,
    ACTION_COL_TARGETS,
    ACTION_COL_MIME,
    ACTION_COL_ENABLED,
    ACTION_COL_PATH,
    ACTION_N_COLUMNS
};

typedef struct {
    gchar *path;
    gchar *name;
    gchar *command;
    gchar *icon;
    gchar *mime_types;
    gchar *targets;
    gchar *selection;
    gboolean terminal;
    gboolean enabled;
} ActionInfo;

typedef struct {
    gchar *action_path;
    GList *paths;
} ActionLaunchData;

typedef struct {
    GtkWidget *entry;
    GtkWindow *parent;
} ActionIconData;

static GList *build_custom_action_tools(Option *option, xmlNode *node,
                                        guchar *label);
static void add_action_clicked(GtkButton *button, gpointer data);
static void show_manager_clicked(GtkButton *button, gpointer data);
static void open_actions_folder_clicked(GtkButton *button, gpointer data);
static void action_manager_add(GtkButton *button, gpointer data);
static void action_manager_edit(GtkButton *button, gpointer data);
static void action_manager_remove(GtkButton *button, gpointer data);
static void action_manager_toggle(GtkCellRendererToggle *renderer,
                                  gchar *tree_path, gpointer data);

static gchar *actions_directory(void)
{
    return g_build_filename(g_get_user_data_dir(), "rox-filer", "actions", NULL);
}

static GList *copy_paths(GList *paths)
{
    GList *copy = NULL;
    GList *node;
    for (node = paths; node; node = node->next)
        copy = g_list_append(copy, g_strdup((const gchar *) node->data));
    return copy;
}

static void action_info_free(ActionInfo *info)
{
    if (!info)
        return;
    g_free(info->path);
    g_free(info->name);
    g_free(info->command);
    g_free(info->icon);
    g_free(info->mime_types);
    g_free(info->targets);
    g_free(info->selection);
    g_free(info);
}

static ActionInfo *action_info_load(const gchar *path)
{
    GKeyFile *key = g_key_file_new();
    GError *error = NULL;
    ActionInfo *info;

    if (!g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, &error)) {
        g_clear_error(&error);
        g_key_file_unref(key);
        return NULL;
    }

    {
        gchar *entry_type = g_key_file_get_string(key, "Desktop Entry", "Type", NULL);
        gboolean valid_type = g_strcmp0(entry_type, "Application") == 0;
        g_free(entry_type);
        if (!valid_type) {
            g_key_file_unref(key);
            return NULL;
        }
    }

    info = g_new0(ActionInfo, 1);
    info->path = g_strdup(path);
    info->name = g_key_file_get_locale_string(key, "Desktop Entry", "Name",
                                               NULL, NULL);
    info->command = g_key_file_get_string(key, "Desktop Entry", "Exec", NULL);
    info->icon = g_key_file_get_string(key, "Desktop Entry", "Icon", NULL);
    info->terminal = g_key_file_get_boolean(key, "Desktop Entry", "Terminal", NULL);
    info->mime_types = g_key_file_get_string(key, ACTION_GROUP, "MimeTypes", NULL);
    info->targets = g_key_file_get_string(key, ACTION_GROUP, "Targets", NULL);
    info->selection = g_key_file_get_string(key, ACTION_GROUP, "Selection", NULL);
    info->enabled = !g_key_file_has_key(key, ACTION_GROUP, "Enabled", NULL) ||
                    g_key_file_get_boolean(key, ACTION_GROUP, "Enabled", NULL);

    if (!info->name)
        info->name = g_path_get_basename(path);
    if (!info->command) {
        action_info_free(info);
        info = NULL;
    }

    g_key_file_unref(key);
    return info;
}

static gboolean token_list_contains(const gchar *list, const gchar *token)
{
    gchar **values;
    gint i;
    gboolean found = FALSE;

    if (!list || !*list)
        return TRUE;
    values = g_strsplit_set(list, ";, \t\r\n", -1);
    for (i = 0; values[i]; i++) {
        gchar *value = g_strstrip(values[i]);
        if (g_strcmp0(value, token) == 0 || g_strcmp0(value, "all") == 0 ||
            g_strcmp0(value, "*") == 0) {
            found = TRUE;
            break;
        }
    }
    g_strfreev(values);
    return found;
}

static gboolean mime_pattern_matches(const gchar *pattern,
                                     const gchar *mime_type)
{
    const gchar *slash;

    if (!pattern || !*pattern || g_strcmp0(pattern, "*") == 0 ||
        g_strcmp0(pattern, "*/*") == 0)
        return TRUE;
    if (g_strcmp0(pattern, mime_type) == 0)
        return TRUE;
    slash = strchr(pattern, '/');
    if (slash && slash[1] == '*' && slash[2] == '\0') {
        gchar *prefix = g_strndup(pattern, (slash - pattern) + 1);
        gboolean matches = g_str_has_prefix(mime_type, prefix);
        g_free(prefix);
        return matches;
    }
    return FALSE;
}

static gboolean mime_list_matches(const gchar *list, const gchar *mime_type)
{
    gchar **values;
    gint i;
    gboolean found = FALSE;

    if (!list || !*list)
        return TRUE;
    values = g_strsplit_set(list, ";, \t\r\n", -1);
    for (i = 0; values[i]; i++) {
        gchar *value = g_strstrip(values[i]);
        if (*value && mime_pattern_matches(value, mime_type)) {
            found = TRUE;
            break;
        }
    }
    g_strfreev(values);
    return found;
}

static const gchar *target_for_path(const gchar *path)
{
    struct stat info;

    if (lstat(path, &info) != 0)
        return "files";
    if (S_ISLNK(info.st_mode))
        return "links";
    if (S_ISDIR(info.st_mode))
        return "directories";
    return "files";
}

static gboolean action_matches(ActionInfo *info, GList *paths)
{
    GList *node;
    guint count = g_list_length(paths);

    if (!info || !info->enabled || count == 0)
        return FALSE;
    if (g_strcmp0(info->selection, "single") == 0 && count != 1)
        return FALSE;
    if (g_strcmp0(info->selection, "multiple") == 0 && count < 2)
        return FALSE;

    for (node = paths; node; node = node->next) {
        const gchar *path = node->data;
        const gchar *target = target_for_path(path);
        MIME_type *type;
        gchar *mime;
        gboolean matches;

        if (!token_list_contains(info->targets, target))
            return FALSE;
        type = type_from_path(path);
        if (!type)
            continue;
        mime = g_strconcat(type->media_type, "/", type->subtype, NULL);
        matches = mime_list_matches(info->mime_types, mime);
        g_free(mime);
        if (!matches)
            return FALSE;
    }
    return TRUE;
}

static void action_launch_data_free(gpointer data)
{
    ActionLaunchData *launch = data;
    if (!launch)
        return;
    g_free(launch->action_path);
    destroy_glist(&launch->paths);
    g_free(launch);
}

static void action_activate(GtkMenuItem *item, gpointer data)
{
    ActionLaunchData *launch = data;
    const gchar **args;
    GList *node;
    guint i = 0;
    guint count;

    (void) item;
    if (!launch)
        return;
    count = g_list_length(launch->paths);
    args = g_new0(const gchar *, count + 1);
    for (node = launch->paths; node; node = node->next)
        args[i++] = node->data;
    {
        const gchar *first_path = launch->paths
            ? (const gchar *) launch->paths->data : NULL;
        gchar *working_dir = first_path
            ? g_path_get_dirname(first_path) : g_strdup(g_get_home_dir());

        if (!run_desktop_entry(launch->action_path, args, working_dir))
            report_error(_("Unable to run file action '%s'."),
                         launch->action_path);
        g_free(working_dir);
    }
    g_free(args);
}

static GtkWidget *action_menu_item(ActionInfo *info)
{
    GtkWidget *item = gtk_image_menu_item_new_with_label(info->name);
    GtkWidget *image;

    if (info->icon && *info->icon) {
        if (g_path_is_absolute(info->icon))
            image = gtk_image_new_from_file(info->icon);
        else
            image = gtk_image_new_from_icon_name(info->icon,
                                                  GTK_ICON_SIZE_MENU);
    } else {
        image = gtk_image_new_from_icon_name("system-run",
                                              GTK_ICON_SIZE_MENU);
    }
    gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), image);
    gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(item), TRUE);
    return item;
}

GList *custom_actions_create_items(GList *paths, GtkWindow *parent)
{
    gchar *directory = actions_directory();
    GDir *dir = g_dir_open(directory, 0, NULL);
    const gchar *leaf;
    GList *items = NULL;

    (void) parent;
    if (dir) {
        while ((leaf = g_dir_read_name(dir)) != NULL) {
            gchar *path;
            ActionInfo *info;
            GtkWidget *item;
            ActionLaunchData *launch;

            if (!g_str_has_suffix(leaf, ".desktop"))
                continue;
            path = g_build_filename(directory, leaf, NULL);
            info = action_info_load(path);
            g_free(path);
            if (!action_matches(info, paths)) {
                action_info_free(info);
                continue;
            }
            item = action_menu_item(info);
            launch = g_new0(ActionLaunchData, 1);
            launch->action_path = g_strdup(info->path);
            launch->paths = copy_paths(paths);
            g_signal_connect(item, "activate", G_CALLBACK(action_activate), launch);
            g_object_set_data_full(G_OBJECT(item), "rox-action-launch",
                                   launch, action_launch_data_free);
            items = g_list_append(items, item);
            action_info_free(info);
        }
        g_dir_close(dir);
    }
    g_free(directory);
    return items;
}

GtkWidget *custom_actions_create_menu(GList *paths, GtkWindow *parent)
{
    GList *items = custom_actions_create_items(paths, parent);
    GList *node;
    GtkWidget *menu;

    if (!items)
        return NULL;

    menu = gtk_menu_new();
    for (node = items; node; node = node->next)
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), GTK_WIDGET(node->data));
    g_list_free(items);
    gtk_widget_show_all(menu);
    return menu;
}


static gchar *sanitize_action_id(const gchar *name)
{
    GString *out = g_string_new(ACTION_PREFIX);
    const gchar *p;
    gboolean dash = FALSE;

    for (p = name ? name : "action"; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (c < 128 && g_ascii_isalnum((gchar)c)) {
            g_string_append_c(out, g_ascii_tolower((gchar)c));
            dash = FALSE;
        } else if (!dash) {
            g_string_append_c(out, '-');
            dash = TRUE;
        }
    }
    while (out->len && out->str[out->len - 1] == '-')
        g_string_truncate(out, out->len - 1);
    if (out->len == strlen(ACTION_PREFIX))
        g_string_append(out, "action");
    return g_string_free(out, FALSE);
}

static gchar *command_with_field_code(const gchar *command)
{
    if (strstr(command, "%f") || strstr(command, "%F") ||
        strstr(command, "%u") || strstr(command, "%U"))
        return g_strdup(command);
    return g_strconcat(command, " %F", NULL);
}

static gboolean validate_action_command(const gchar *command, GError **error)
{
    gchar **argv = NULL;
    gint argc = 0;
    gchar *program = NULL;
    gboolean valid = FALSE;

    if (!command || !*command) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                    "%s", _("The command is empty."));
        return FALSE;
    }
    if (!g_shell_parse_argv(command, &argc, &argv, error))
        return FALSE;
    if (argc > 0) {
        gchar *clean = g_strdup(argv[0]);
        gchar *percent = strchr(clean, '%');
        if (percent)
            *percent = '\0';
        if (g_path_is_absolute(clean))
            valid = g_file_test(clean, G_FILE_TEST_IS_EXECUTABLE);
        else {
            program = g_find_program_in_path(clean);
            valid = program != NULL;
        }
        if (!valid)
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                        _("Command '%s' was not found."), clean);
        g_free(clean);
    }
    g_free(program);
    g_strfreev(argv);
    return valid;
}

static void choose_action_icon(GtkButton *button, gpointer data)
{
    ActionIconData *ctx = data;
    const gchar *current = gtk_entry_get_text(GTK_ENTRY(ctx->entry));
    gchar *path = xdg_apps_choose_icon(ctx->parent, current);
    (void) button;
    if (path) {
        gtk_entry_set_text(GTK_ENTRY(ctx->entry), path);
        g_free(path);
    }
}

static gboolean save_action(const gchar *existing_path,
                            const gchar *name,
                            const gchar *command,
                            const gchar *icon_source,
                            const gchar *mime_types,
                            gboolean target_files,
                            gboolean target_dirs,
                            gboolean target_links,
                            gboolean single,
                            gboolean multiple,
                            gboolean terminal,
                            gboolean enabled)
{
    gchar *directory = actions_directory();
    gchar *action_id;
    gchar *leaf;
    gchar *path;
    gchar *exec;
    gchar *icon;
    GString *targets;
    const gchar *selection;
    GKeyFile *key;
    gchar *data;
    gsize length;
    GError *error = NULL;

    if (!name || !*name || !command || !*command) {
        report_error("%s", _("Name and command are required."));
        g_free(directory);
        return FALSE;
    }
    if (!validate_action_command(command, &error)) {
        report_error("%s", error ? error->message : _("Invalid command."));
        g_clear_error(&error);
        g_free(directory);
        return FALSE;
    }
    if (!target_files && !target_dirs && !target_links) {
        report_error("%s", _("Select at least one target type."));
        g_free(directory);
        return FALSE;
    }
    if (!single && !multiple) {
        report_error("%s", _("Select single, multiple or both selection modes."));
        g_free(directory);
        return FALSE;
    }
    if (g_mkdir_with_parents(directory, 0755) != 0 && errno != EEXIST) {
        report_error(_("Unable to create actions directory '%s': %s"),
                     directory, g_strerror(errno));
        g_free(directory);
        return FALSE;
    }

    if (existing_path) {
        path = g_strdup(existing_path);
        leaf = g_path_get_basename(existing_path);
        action_id = g_strndup(leaf,
            strlen(leaf) - (g_str_has_suffix(leaf, ".desktop") ? 8 : 0));
        g_free(leaf);
    } else {
        action_id = sanitize_action_id(name);
        leaf = g_strconcat(action_id, ".desktop", NULL);
        path = g_build_filename(directory, leaf, NULL);
        g_free(leaf);
    }

    icon = xdg_apps_install_user_icon(icon_source, action_id, &error);
    if (!icon) {
        report_error("%s", error ? error->message : _("Unable to install icon."));
        g_clear_error(&error);
        g_free(action_id);
        g_free(path);
        g_free(directory);
        return FALSE;
    }
    exec = command_with_field_code(command);
    targets = g_string_new(NULL);
    if (target_files) g_string_append(targets, "files;");
    if (target_dirs) g_string_append(targets, "directories;");
    if (target_links) g_string_append(targets, "links;");
    selection = single && multiple ? "any" : (single ? "single" : "multiple");

    key = g_key_file_new();
    g_key_file_set_string(key, "Desktop Entry", "Type", "Application");
    g_key_file_set_string(key, "Desktop Entry", "Name", name);
    g_key_file_set_string(key, "Desktop Entry", "Exec", exec);
    g_key_file_set_string(key, "Desktop Entry", "Icon", icon);
    g_key_file_set_boolean(key, "Desktop Entry", "Terminal", terminal);
    g_key_file_set_boolean(key, "Desktop Entry", "NoDisplay", TRUE);
    g_key_file_set_string(key, ACTION_GROUP, "MimeTypes",
                          mime_types ? mime_types : "");
    g_key_file_set_string(key, ACTION_GROUP, "Targets", targets->str);
    g_key_file_set_string(key, ACTION_GROUP, "Selection", selection);
    g_key_file_set_boolean(key, ACTION_GROUP, "Enabled", enabled);

    data = g_key_file_to_data(key, &length, &error);
    g_key_file_unref(key);
    if (!data || !g_file_set_contents(path, data, length, &error)) {
        report_error(_("Unable to save file action '%s': %s"), path,
                     error ? error->message : _("Unknown error"));
        g_clear_error(&error);
        g_free(data);
        g_string_free(targets, TRUE);
        g_free(exec);
        g_free(icon);
        g_free(action_id);
        g_free(path);
        g_free(directory);
        return FALSE;
    }
    chmod(path, 0644);
    g_free(data);
    g_string_free(targets, TRUE);
    g_free(exec);
    g_free(icon);
    g_free(action_id);
    g_free(path);
    g_free(directory);
    return TRUE;
}

static void load_action_into_widgets(const gchar *path,
                                     GtkEntry *name,
                                     GtkEntry *command,
                                     GtkEntry *icon,
                                     GtkEntry *mime,
                                     GtkToggleButton *files,
                                     GtkToggleButton *dirs,
                                     GtkToggleButton *links,
                                     GtkToggleButton *single,
                                     GtkToggleButton *multiple,
                                     GtkToggleButton *terminal,
                                     GtkToggleButton *enabled)
{
    ActionInfo *info = action_info_load(path);
    if (!info)
        return;
    gtk_entry_set_text(name, info->name ? info->name : "");
    gtk_entry_set_text(command, info->command ? info->command : "");
    gtk_entry_set_text(icon, info->icon ? info->icon : "");
    gtk_entry_set_text(mime, info->mime_types ? info->mime_types : "");
    gtk_toggle_button_set_active(files,
        token_list_contains(info->targets, "files"));
    gtk_toggle_button_set_active(dirs,
        token_list_contains(info->targets, "directories"));
    gtk_toggle_button_set_active(links,
        token_list_contains(info->targets, "links"));
    gtk_toggle_button_set_active(single,
        !info->selection || g_strcmp0(info->selection, "multiple") != 0);
    gtk_toggle_button_set_active(multiple,
        !info->selection || g_strcmp0(info->selection, "single") != 0);
    gtk_toggle_button_set_active(terminal, info->terminal);
    gtk_toggle_button_set_active(enabled, info->enabled);
    action_info_free(info);
}

static gboolean action_editor(GtkWindow *parent, const gchar *existing_path,
                              const gchar *default_mime,
                              const gchar *default_target)
{
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *grid;
    GtkWidget *name;
    GtkWidget *command;
    GtkWidget *icon;
    GtkWidget *mime;
    GtkWidget *choose_icon;
    GtkWidget *files;
    GtkWidget *dirs;
    GtkWidget *links;
    GtkWidget *single;
    GtkWidget *multiple;
    GtkWidget *terminal;
    GtkWidget *enabled;
    ActionIconData icon_ctx;
    gboolean saved = FALSE;

    dialog = gtk_dialog_new_with_buttons(existing_path
        ? _("Edit File Action") : _("Add File Action"), parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        _("Cancel"), GTK_RESPONSE_CANCEL,
        _("Save"), GTK_RESPONSE_ACCEPT, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 580, 430);
    gtk_window_set_position(GTK_WINDOW(dialog), parent
        ? GTK_WIN_POS_CENTER_ON_PARENT : GTK_WIN_POS_CENTER);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 9);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 10);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);

    name = gtk_entry_new();
    command = gtk_entry_new();
    icon = gtk_entry_new();
    mime = gtk_entry_new();
    choose_icon = gtk_button_new_with_label(_("Choose..."));
    files = gtk_check_button_new_with_label(_("Files"));
    dirs = gtk_check_button_new_with_label(_("Directories"));
    links = gtk_check_button_new_with_label(_("Symbolic links"));
    single = gtk_check_button_new_with_label(_("Single selection"));
    multiple = gtk_check_button_new_with_label(_("Multiple selection"));
    terminal = gtk_check_button_new_with_label(_("Run in a terminal"));
    enabled = gtk_check_button_new_with_label(_("Enabled"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(files),
        !default_target || g_strcmp0(default_target, "files") == 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dirs),
        !default_target || g_strcmp0(default_target, "directories") == 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(links),
        default_target && g_strcmp0(default_target, "links") == 0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(single), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(multiple), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enabled), TRUE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(command), "program %F");
    gtk_entry_set_placeholder_text(GTK_ENTRY(mime), "image/*;text/plain;*/*");

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("Name:")), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), name, 1, 0, 3, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("Command:")), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), command, 1, 1, 3, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("Icon:")), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), icon, 1, 2, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), choose_icon, 3, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("MIME types:")), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), mime, 1, 3, 3, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("Targets:")), 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), files, 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), dirs, 2, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), links, 3, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("Selection:")), 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), single, 1, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), multiple, 2, 5, 2, 1);
    gtk_grid_attach(GTK_GRID(grid), terminal, 1, 6, 3, 1);
    gtk_grid_attach(GTK_GRID(grid), enabled, 1, 7, 3, 1);

    icon_ctx.entry = icon;
    icon_ctx.parent = GTK_WINDOW(dialog);
    g_signal_connect(choose_icon, "clicked", G_CALLBACK(choose_action_icon),
                     &icon_ctx);
    if (!existing_path && default_mime && *default_mime)
        gtk_entry_set_text(GTK_ENTRY(mime), default_mime);
    if (existing_path)
        load_action_into_widgets(existing_path,
            GTK_ENTRY(name), GTK_ENTRY(command), GTK_ENTRY(icon), GTK_ENTRY(mime),
            GTK_TOGGLE_BUTTON(files), GTK_TOGGLE_BUTTON(dirs),
            GTK_TOGGLE_BUTTON(links), GTK_TOGGLE_BUTTON(single),
            GTK_TOGGLE_BUTTON(multiple), GTK_TOGGLE_BUTTON(terminal),
            GTK_TOGGLE_BUTTON(enabled));

    gtk_widget_show_all(content);
    while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        if (save_action(existing_path,
                gtk_entry_get_text(GTK_ENTRY(name)),
                gtk_entry_get_text(GTK_ENTRY(command)),
                gtk_entry_get_text(GTK_ENTRY(icon)),
                gtk_entry_get_text(GTK_ENTRY(mime)),
                gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(files)),
                gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dirs)),
                gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(links)),
                gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(single)),
                gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(multiple)),
                gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(terminal)),
                gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(enabled)))) {
            saved = TRUE;
            break;
        }
    }
    gtk_widget_destroy(dialog);
    return saved;
}

static gchar *common_mime_for_paths(GList *paths)
{
    GList *node;
    gchar *common = NULL;

    for (node = paths; node; node = node->next) {
        MIME_type *type = type_from_path((const gchar *) node->data);
        gchar *mime;

        if (!type)
            continue;
        mime = g_strconcat(type->media_type, "/", type->subtype, NULL);
        if (!common)
            common = mime;
        else if (g_strcmp0(common, mime) != 0) {
            g_free(mime);
            g_free(common);
            return g_strdup("*/*");
        } else {
            g_free(mime);
        }
    }
    return common ? common : g_strdup("*/*");
}

void custom_actions_add_for_paths(GList *paths, GtkWindow *parent)
{
    const gchar *first = paths ? (const gchar *) paths->data : NULL;
    const gchar *target = first ? target_for_path(first) : NULL;
    gchar *mime = common_mime_for_paths(paths);

    action_editor(parent, NULL, mime, target);
    g_free(mime);
}

static void populate_actions_store(GtkListStore *store)
{
    gchar *directory = actions_directory();
    GDir *dir = g_dir_open(directory, 0, NULL);
    const gchar *leaf;

    gtk_list_store_clear(store);
    if (!dir) {
        g_free(directory);
        return;
    }
    while ((leaf = g_dir_read_name(dir)) != NULL) {
        gchar *path;
        ActionInfo *info;
        GtkTreeIter iter;
        if (!g_str_has_suffix(leaf, ".desktop"))
            continue;
        path = g_build_filename(directory, leaf, NULL);
        info = action_info_load(path);
        g_free(path);
        if (!info)
            continue;
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter,
            ACTION_COL_NAME, info->name,
            ACTION_COL_COMMAND, info->command,
            ACTION_COL_TARGETS, info->targets ? info->targets : "",
            ACTION_COL_MIME, info->mime_types ? info->mime_types : "",
            ACTION_COL_ENABLED, info->enabled,
            ACTION_COL_PATH, info->path,
            -1);
        action_info_free(info);
    }
    g_dir_close(dir);
    g_free(directory);
}

static gchar *selected_action_path(GtkTreeView *tree)
{
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tree);
    GtkTreeModel *model;
    GtkTreeIter iter;
    gchar *path = NULL;
    if (gtk_tree_selection_get_selected(selection, &model, &iter))
        gtk_tree_model_get(model, &iter, ACTION_COL_PATH, &path, -1);
    return path;
}

static void set_action_enabled(const gchar *path, gboolean enabled)
{
    GKeyFile *key = g_key_file_new();
    gchar *data;
    gsize length;
    if (!g_key_file_load_from_file(key, path, G_KEY_FILE_KEEP_COMMENTS, NULL)) {
        g_key_file_unref(key);
        return;
    }
    g_key_file_set_boolean(key, ACTION_GROUP, "Enabled", enabled);
    data = g_key_file_to_data(key, &length, NULL);
    if (data) {
        g_file_set_contents(path, data, length, NULL);
        g_free(data);
    }
    g_key_file_unref(key);
}

void custom_actions_show_manager(GtkWindow *parent)
{
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *box;
    GtkWidget *scroll;
    GtkWidget *tree;
    GtkWidget *buttons;
    GtkWidget *add;
    GtkWidget *edit;
    GtkWidget *remove;
    GtkWidget *open_folder;
    GtkListStore *store;
    GtkCellRenderer *renderer;

    dialog = gtk_dialog_new_with_buttons(_("File Actions"), parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        _("Close"), GTK_RESPONSE_CLOSE, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 820, 450);
    gtk_window_set_position(GTK_WINDOW(dialog), parent
        ? GTK_WIN_POS_CENTER_ON_PARENT : GTK_WIN_POS_CENTER);
    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 10);
    gtk_box_pack_start(GTK_BOX(content), box, TRUE, TRUE, 0);

    store = gtk_list_store_new(ACTION_N_COLUMNS,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_BOOLEAN, G_TYPE_STRING);
    tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    renderer = gtk_cell_renderer_toggle_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
        gtk_tree_view_column_new_with_attributes(_("Enabled"), renderer,
                                                  "active", ACTION_COL_ENABLED, NULL));
    g_signal_connect(renderer, "toggled", G_CALLBACK(action_manager_toggle), tree);
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
        gtk_tree_view_column_new_with_attributes(_("Name"), renderer,
                                                  "text", ACTION_COL_NAME, NULL));
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
        gtk_tree_view_column_new_with_attributes(_("Command"), renderer,
                                                  "text", ACTION_COL_COMMAND, NULL));
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
        gtk_tree_view_column_new_with_attributes(_("Targets"), renderer,
                                                  "text", ACTION_COL_TARGETS, NULL));
    renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree),
        gtk_tree_view_column_new_with_attributes(_("MIME Types"), renderer,
                                                  "text", ACTION_COL_MIME, NULL));

    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), tree);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);

    buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_START);
    add = gtk_button_new_with_label(_("Add"));
    edit = gtk_button_new_with_label(_("Edit"));
    remove = gtk_button_new_with_label(_("Remove"));
    open_folder = gtk_button_new_with_label(_("Open Actions Folder"));
    gtk_container_add(GTK_CONTAINER(buttons), add);
    gtk_container_add(GTK_CONTAINER(buttons), edit);
    gtk_container_add(GTK_CONTAINER(buttons), remove);
    gtk_container_add(GTK_CONTAINER(buttons), open_folder);
    gtk_box_pack_start(GTK_BOX(box), buttons, FALSE, FALSE, 0);

    g_signal_connect(add, "clicked", G_CALLBACK(action_manager_add), tree);
    g_signal_connect(edit, "clicked", G_CALLBACK(action_manager_edit), tree);
    g_signal_connect(remove, "clicked", G_CALLBACK(action_manager_remove), tree);
    g_signal_connect(open_folder, "clicked",
                     G_CALLBACK(open_actions_folder_clicked), NULL);

    populate_actions_store(store);
    gtk_widget_show_all(content);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_object_unref(store);
}

static void action_manager_add(GtkButton *button, gpointer data)
{
    GtkWidget *tree = data;
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
    if (action_editor(parent, NULL, NULL, NULL))
        populate_actions_store(GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(tree))));
}

static void action_manager_edit(GtkButton *button, gpointer data)
{
    GtkWidget *tree = data;
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
    gchar *path = selected_action_path(GTK_TREE_VIEW(tree));
    if (!path)
        return;
    if (action_editor(parent, path, NULL, NULL))
        populate_actions_store(GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(tree))));
    g_free(path);
}

static void remove_action_file(const gchar *path)
{
    GKeyFile *key = g_key_file_new();
    gchar *icon = NULL;

    if (g_key_file_load_from_file(key, path, G_KEY_FILE_NONE, NULL))
        icon = g_key_file_get_string(key, "Desktop Entry", "Icon", NULL);
    g_key_file_unref(key);

    unlink(path);
    if (icon && g_str_has_prefix(icon, ACTION_PREFIX)) {
        gchar *svg_leaf = g_strconcat(icon, ".svg", NULL);
        gchar *svgz_leaf = g_strconcat(icon, ".svgz", NULL);
        gchar *png_leaf = g_strconcat(icon, ".png", NULL);
        gchar *svg = g_build_filename(g_get_user_data_dir(), "icons", "hicolor",
                                      "scalable", "apps", svg_leaf, NULL);
        gchar *svgz = g_build_filename(g_get_user_data_dir(), "icons", "hicolor",
                                       "scalable", "apps", svgz_leaf, NULL);
        gchar *png = g_build_filename(g_get_user_data_dir(), "icons", "hicolor",
                                      "128x128", "apps", png_leaf, NULL);
        unlink(svg);
        unlink(svgz);
        unlink(png);
        g_free(svg);
        g_free(svgz);
        g_free(png);
        g_free(svg_leaf);
        g_free(svgz_leaf);
        g_free(png_leaf);
        if (gtk_icon_theme_get_default())
            gtk_icon_theme_rescan_if_needed(gtk_icon_theme_get_default());
    }
    g_free(icon);
}

static void action_manager_remove(GtkButton *button, gpointer data)
{
    GtkWidget *tree = data;
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
    gchar *path = selected_action_path(GTK_TREE_VIEW(tree));
    GtkWidget *confirm;
    if (!path)
        return;
    confirm = gtk_message_dialog_new(parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
        "%s", _("Remove the selected file action?"));
    gtk_dialog_add_button(GTK_DIALOG(confirm), _("Cancel"), GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(confirm), _("Remove"), GTK_RESPONSE_ACCEPT);
    gtk_window_set_position(GTK_WINDOW(confirm), GTK_WIN_POS_CENTER_ON_PARENT);
    if (gtk_dialog_run(GTK_DIALOG(confirm)) == GTK_RESPONSE_ACCEPT) {
        remove_action_file(path);
        populate_actions_store(GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(tree))));
    }
    gtk_widget_destroy(confirm);
    g_free(path);
}

static void action_manager_toggle(GtkCellRendererToggle *renderer,
                                  gchar *tree_path, gpointer data)
{
    GtkTreeView *tree = data;
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    GtkTreeIter iter;
    GtkTreePath *path = gtk_tree_path_new_from_string(tree_path);
    gboolean enabled;
    gchar *file = NULL;
    (void) renderer;
    if (gtk_tree_model_get_iter(model, &iter, path)) {
        gtk_tree_model_get(model, &iter,
                           ACTION_COL_ENABLED, &enabled,
                           ACTION_COL_PATH, &file, -1);
        enabled = !enabled;
        gtk_list_store_set(GTK_LIST_STORE(model), &iter,
                           ACTION_COL_ENABLED, enabled, -1);
        if (file)
            set_action_enabled(file, enabled);
    }
    g_free(file);
    gtk_tree_path_free(path);
}

static void add_action_clicked(GtkButton *button, gpointer data)
{
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
    (void) data;
    custom_actions_add_for_paths(NULL, parent);
}

static void show_manager_clicked(GtkButton *button, gpointer data)
{
    GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
    (void) data;
    custom_actions_show_manager(parent);
}

static void open_actions_folder_clicked(GtkButton *button, gpointer data)
{
    gchar *directory = actions_directory();
    (void) button;
    (void) data;
    g_mkdir_with_parents(directory, 0755);
    filer_opendir(directory, NULL, NULL);
    g_free(directory);
}

static GList *build_custom_action_tools(Option *option, xmlNode *node,
                                        guchar *label)
{
    GtkWidget *box;
    GtkWidget *add;
    GtkWidget *manage;
    GtkWidget *folder;
    (void) option;
    (void) node;
    (void) label;

    box = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(box), GTK_BUTTONBOX_START);
    add = gtk_button_new_with_label(_("Add File Action..."));
    manage = gtk_button_new_with_label(_("Manage File Actions..."));
    folder = gtk_button_new_with_label(_("Open Actions Folder"));
    gtk_container_add(GTK_CONTAINER(box), add);
    gtk_container_add(GTK_CONTAINER(box), manage);
    gtk_container_add(GTK_CONTAINER(box), folder);
    g_signal_connect(add, "clicked", G_CALLBACK(add_action_clicked), NULL);
    g_signal_connect(manage, "clicked", G_CALLBACK(show_manager_clicked), NULL);
    g_signal_connect(folder, "clicked",
                     G_CALLBACK(open_actions_folder_clicked), NULL);
    return g_list_append(NULL, box);
}

void custom_actions_init(void)
{
    option_register_widget("custom-action-tools", build_custom_action_tools);
}
