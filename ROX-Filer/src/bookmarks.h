/*
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _BOOKMARKS_H
#define _BOOKMARKS_H

void bookmarks_init(void);

void bookmarks_show_menu(FilerWindow *filer_window);
void bookmarks_edit(void);
void bookmarks_add_history(const gchar *path);
void bookmarks_add_uri(const EscapedPath *uri);
void bookmarks_add_path(const gchar *path);
typedef struct
{
	gchar *path;
	gchar *title;
} RoxBookmarkInfo;

GPtrArray *bookmarks_get_entries(void);
void rox_bookmark_info_free(RoxBookmarkInfo *info);

#endif /* _BOOKMARKS_H */
