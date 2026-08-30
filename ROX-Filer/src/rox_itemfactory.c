/* rox_itemfactory.c - Internal GTK3 menu-table builder for ROX-Filer.
 *
 * This is a ROX-owned API.  It preserves the compact declarative menu tables
 * used by the original code while exposing only ROX-owned GTK3 types.
 *
 * It implements the operations currently needed by ROX-Filer:
 *   - rox_item_factory_new
 *   - rox_item_factory_create_items
 *   - rox_item_factory_get_widget
 *
 * Supported item types (as used by ROX-Filer):
 *   - <Branch>     submenu (get_widget returns the GtkMenu; extra_data icon)
 *   - <Separator>  separator
 *   - <ToggleItem> check menu item (optional extra_data icon)
 *   - <IconItem>   menu item with an icon name (best-effort)
 *   - NULL/other   normal menu item
 */

/*
 * Modificado por josejp2424 (2026):
 * port a GTK3 y adaptaciones específicas de esta versión.
 */

#include "config.h"
#include <gtk/gtk.h>

#include "global.h"

/* app_dir se declara en main.h (directorio de la app). */
#include "main.h"
#include "gui_support.h"

#if defined(ROX_USING_GTK3)

#include <string.h>

typedef struct _RoxIFItemData {
	RoxItemFactoryCallback cb;
	gpointer cb_data;
	guint action;
} RoxIFItemData;

struct _RoxItemFactory {
	gchar *root;               /* e.g. "<filer>" */
	GtkWidget *menu;           /* GtkMenu */
	GtkAccelGroup *accel;      /* accel group */
	GHashTable *widgets;       /* full path -> GtkWidget* (root/menuitems/submenus) */
	GHashTable *submenus;      /* full path -> GtkWidget* GtkMenu (for branches) */
};

static void rox_if_itemdata_free(gpointer p)
{
	g_free(p);
}

static void rox_if_activate(GtkWidget *widget, gpointer user_data)
{
	RoxIFItemData *d = (RoxIFItemData *) user_data;
	if (!d || !d->cb)
		return;
	/* RoxItemFactory callback signature: (callback_data, callback_action, widget) */
	d->cb(d->cb_data, d->action, widget);
}

static gboolean rox_if_is(const char *type, const char *needle)
{
	return type && needle && strcmp(type, needle) == 0;
}

static void rox_if_add_accel(RoxItemFactory *ifactory, GtkWidget *item, const char *accel)
{
	guint keyval = 0;
	GdkModifierType mods = 0;

	if (!ifactory || !item || !accel || !*accel || !ifactory->accel)
		return;

	gtk_accelerator_parse(accel, &keyval, &mods);
	if (keyval == 0)
		return;

	gtk_widget_add_accelerator(item, "activate", ifactory->accel, keyval, mods, GTK_ACCEL_VISIBLE);
}

static GtkWidget *rox_if_find_or_make_branch(RoxItemFactory *ifactory, GtkWidget *parent_menu,
					   const gchar *parent_fullpath, const gchar *label)
{
	GtkWidget *submenu;
	GtkWidget *mi;
	gchar *branch_path;

	branch_path = g_strdup_printf("%s/%s", parent_fullpath, label);

	submenu = g_hash_table_lookup(ifactory->submenus, branch_path);
	if (submenu)
	{
		g_free(branch_path);
		return submenu;
	}

	mi = menu_item_new_label(label);
	submenu = rox_menu_new();
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi), submenu);
	gtk_menu_shell_append(GTK_MENU_SHELL(parent_menu), mi);
	gtk_widget_show(mi);
	gtk_widget_show(submenu);

	/* For branches, ROX expects get_widget("<root>/Branch") to return the GtkMenu */
	g_hash_table_insert(ifactory->submenus, g_strdup(branch_path), submenu);
	g_hash_table_insert(ifactory->widgets, g_strdup(branch_path), submenu);

	g_free(branch_path);
	return submenu;
}

RoxItemFactory *rox_item_factory_new(GType container_type, const gchar *path, GtkAccelGroup *accel_group)
{
	RoxItemFactory *ifactory;
	(void)container_type;

	ifactory = g_malloc0(sizeof(*ifactory));
	ifactory->root = g_strdup(path ? path : "<menu>");
	ifactory->menu = rox_menu_new();
	ifactory->accel = accel_group;
	ifactory->widgets = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
	ifactory->submenus = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

	/* Root mapping */
	g_hash_table_insert(ifactory->widgets, g_strdup(ifactory->root), ifactory->menu);

	return ifactory;
}

void rox_item_factory_create_items(RoxItemFactory *ifactory, guint n_entries,
				  RoxItemFactoryEntry *entries, gpointer callback_data)
{
	guint i;

	if (!ifactory || !entries)
		return;

	for (i = 0; i < n_entries; i++)
	{
		RoxItemFactoryEntry *e = &entries[i];
		GtkWidget *parent_menu;
		GtkWidget *item = NULL;
		gchar **parts;
		gint depth, last;
		gchar *parent_path;
		gchar *full_path;
		const char *label;
		gboolean is_separator;

		if (!e->path)
			continue;

		/* e->path comes from translate_entries(): always starts with '/'. */
		if (e->path[0] != '/')
			continue;

		parts = g_strsplit(e->path, "/", -1);
		/* parts[0] is always empty */
		depth = g_strv_length(parts) - 1;
		if (depth <= 0)
		{
			g_strfreev(parts);
			continue;
		}
		last = depth;

		label = parts[last];
		is_separator = rox_if_is(e->item_type, "<Separator>") || (label && *label == 0);

		/* Walk or create intermediate branches */
		parent_menu = ifactory->menu;
		parent_path = g_strdup(ifactory->root);
		for (gint d = 1; d < last; d++)
		{
			GtkWidget *submenu;
			submenu = g_hash_table_lookup(ifactory->submenus, parent_path);
			if (!submenu)
				submenu = parent_menu;

			parent_menu = rox_if_find_or_make_branch(ifactory, submenu, parent_path, parts[d]);

			/* advance parent_path */
			{
				gchar *np = g_strdup_printf("%s/%s", parent_path, parts[d]);
				g_free(parent_path);
				parent_path = np;
			}
		}

		/* Now create the actual item in parent_menu */
		if (rox_if_is(e->item_type, "<Branch>"))
		{
			GtkWidget *submenu;
			GtkWidget *mi;

			if (e->extra_data)
				mi = menu_item_new_with_icon(label,
					(const char *) e->extra_data);
			else
				mi = menu_item_new_label(label);
			submenu = rox_menu_new();
			gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi), submenu);
			gtk_menu_shell_append(GTK_MENU_SHELL(parent_menu), mi);
			gtk_widget_show(mi);
			gtk_widget_show(submenu);

			full_path = g_strdup_printf("%s%s", ifactory->root, e->path);
			g_hash_table_insert(ifactory->submenus, g_strdup(full_path), submenu);
			g_hash_table_insert(ifactory->widgets, g_strdup(full_path), submenu);
			g_free(full_path);
		}
		else if (is_separator)
		{
			item = gtk_separator_menu_item_new();
			gtk_menu_shell_append(GTK_MENU_SHELL(parent_menu), item);
			gtk_widget_show(item);
		}
		else if (rox_if_is(e->item_type, "<ToggleItem>"))
		{
			item = check_menu_item_new_label(label);
			if (e->extra_data)
				menu_item_set_icon(item, (const char *) e->extra_data);
			gtk_menu_shell_append(GTK_MENU_SHELL(parent_menu), item);
			gtk_widget_show(item);
		}
		else if (rox_if_is(e->item_type, "<IconItem>"))
		{
			const char *icon_name = (const char *) e->extra_data;
			item = menu_item_new_with_icon(label, icon_name);
			gtk_menu_shell_append(GTK_MENU_SHELL(parent_menu), item);
			gtk_widget_show(item);
		}
		else
		{
			item = menu_item_new_label(label);
			gtk_menu_shell_append(GTK_MENU_SHELL(parent_menu), item);
			gtk_widget_show(item);
		}

		if (item)
		{
			RoxIFItemData *d = g_malloc0(sizeof(*d));
			d->cb = e->callback;
			d->cb_data = callback_data;
			d->action = e->callback_action;
			g_signal_connect_data(item, "activate", G_CALLBACK(rox_if_activate), d,
						 (GClosureNotify) rox_if_itemdata_free, 0);

			rox_if_add_accel(ifactory, item, e->accelerator);

			full_path = g_strdup_printf("%s%s", ifactory->root, e->path);
			g_hash_table_insert(ifactory->widgets, full_path, item);
		}

		g_free(parent_path);
		g_strfreev(parts);
	}
}

GtkWidget *rox_item_factory_get_widget(RoxItemFactory *ifactory, const gchar *path)
{
	if (!ifactory || !path)
		return NULL;
	return g_hash_table_lookup(ifactory->widgets, path);
}

void rox_item_factory_free(RoxItemFactory *ifactory)
{
	if (!ifactory)
		return;

	/* The widget maps store borrowed pointers owned by the root menu tree.
	 * Destroy the root first, then release only the lookup containers. */
	if (ifactory->menu)
		gtk_widget_destroy(ifactory->menu);
	if (ifactory->widgets)
		g_hash_table_destroy(ifactory->widgets);
	if (ifactory->submenus)
		g_hash_table_destroy(ifactory->submenus);
	g_free(ifactory->root);
	g_free(ifactory);
}

#endif /* ROX_USING_GTK3 */
