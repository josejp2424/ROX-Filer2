/*
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

/* global.h is included by most of the other source files, just after
 * including config.h and the system header files, but before any other
 * ROX-Filer header files.
 */

/*
 * Modificado por josejp2424 (2026):
 * port a GTK3 y adaptaciones específicas de esta versión.
 */

#include <glib.h>

/* GTK3 port starter compat */
#include "gtk3_support.h"

//#define UNIT_TESTS

/* We put all the global typedefs here to avoid creating dependencies
 * between header files.
 */

/* Each filer window has one of these all to itself */
typedef struct _FilerWindow FilerWindow;

/* There is one Directory object per cached disk directory inode number.
 * Multiple FilerWindows may share a single Directory. Directories
 * are cached, so a Directory may exist without any filer windows
 * referencing it at all.
 */
typedef struct _Directory Directory;

/* Each item in a directory has a DirItem. This contains information from
 * stat()ing the file, plus a few other bits. There may be several of these
 * for a single file, if it appears (hard-linked) in several directories.
 * Each panel icon also has one of these (not shared).
 */
typedef struct _DirItem DirItem;

/* Widgets which can display directories implement the View interface.
 * This should be used in preference to the old collection interface because
 * it isn't specific to a particular type of display.
 */
typedef struct _ViewIface ViewIface;

/* A ViewIter specifies a single item in a View, rather like an index.
 * They can be used to iterate over all the items in a View, and remain
 * valid until the View is changed. If allocated on the stack, they do not need
 * to be freed.
 */
typedef struct _ViewIter ViewIter;

/* This contains the pixbufs for an image, in various sizes.
 * Despite the name, it now contains neither pixmaps nor masks!
 */
typedef struct _MaskedPixmap MaskedPixmap;

/* Each MIME type (eg 'text/plain') has one of these. It contains
 * a link to the image and the type's name (used so that the image can
 * be refreshed, among other things).
 */
typedef struct _MIME_type MIME_type;

/* Icon is an abstract base class for panel icons.
 * It contains the name and path of the icon, as well as its DirItem.
 */
typedef struct _Icon Icon;

/* There is one of these for each panel window open. Panels work rather
 * as fixed edge launchers with a rigid layout.
 */
typedef struct _Panel Panel;

/* Each option has a static Option structure. This is initialised by
 * calling option_add_int() or similar. See options.c for details.
 * This structure is read-only.
 */
typedef struct _Option Option;

/* A filesystem cache provides a quick and easy way to load files.
 * When a cache is created, functions to load and update files are
 * registered to it. Requesting an object from the cache will load
 * or update it as needed, or return the cached copy if the current
 * version of the file is already cached.
 * Caches are used to access directories, images and XML files.
 */
typedef struct _GFSCache GFSCache;

/* Each cached XML file is represented by one of these */
typedef struct _XMLwrapper XMLwrapper;

/* This holds a pre-parsed version of a filename, which can be quickly
 * compared with another CollateKey for intelligent sorting.
 */
typedef struct _CollateKey CollateKey;

/* Like a regular GtkLabel, except that the text can be wrapped to any
 * width. Used for freely positioned icons.
 */
typedef struct _WrappedLabel WrappedLabel;

/* A filename where " " has been replaced by "%20", etc.
 * This is really just a string, but we try to catch type errors.
 */
typedef struct _EscapedPath EscapedPath;

/* The minibuffer is a text field which appears at the bottom of
 * a filer window. It has various modes of operation:
 */
typedef enum {
	MINI_NONE,
	MINI_PATH,
	MINI_SHELL,
	MINI_SELECT_IF,
	MINI_FILTER,
	MINI_SELECT_BY_NAME,
} MiniType;

/* The next three correspond to the styles on the Display submenu: */

typedef enum {		/* Values used in options, must start at 0 */
	LARGE_ICONS	= 0,
	SMALL_ICONS	= 1,
	HUGE_ICONS	= 2,
	AUTO_SIZE_ICONS	= 3,
	UNKNOWN_STYLE
} DisplayStyle;

typedef enum {		/* Values used in options, must start at 0 */
	DETAILS_NONE		= 0,
	DETAILS_SIZE		= 2,
	DETAILS_PERMISSIONS	= 3,
	DETAILS_TYPE		= 4,
	DETAILS_TIMES		= 5,
	DETAILS_UNKNOWN		= -1,
} DetailsType;

typedef enum {		/* Values used in options */
	SORT_NAME = 0,
	SORT_TYPE = 1,
	SORT_DATEM = 2,
	SORT_SIZE = 3,
	SORT_OWNER = 4,
	SORT_GROUP = 5,
	SORT_DATEC = 6,
	SORT_DATEA = 7,
} SortType;

/* Each DirItem has a base type with indicates what kind of object it is.
 * If the base_type is TYPE_FILE, then the MIME type field gives the exact
 * type.
 */
enum
{
	/* Base types - this also determines the sort order */
	TYPE_ERROR,
	TYPE_UNKNOWN,		/* Not scanned yet */
	TYPE_DIRECTORY,
	TYPE_PIPE,
	TYPE_SOCKET,
	TYPE_FILE,
	TYPE_CHAR_DEVICE,
	TYPE_BLOCK_DEVICE,
	TYPE_DOOR,

	/* These are purely for colour allocation */
	TYPE_EXEC,
	TYPE_APPDIR,
};

/* The namespaces for the SOAP messages */
#define SOAP_ENV_NS_OLD "http://www.w3.org/2001/06/soap-envelope"
#define SOAP_ENV_NS "http://www.w3.org/2001/12/soap-envelope"
#define SOAP_RPC_NS "http://www.w3.org/2001/12/soap-rpc"
#define ROX_NS "http://rox.sourceforge.net/SOAP/ROX-Filer"

/* Namespace for configuration */
#define SITE "rox.sourceforge.net"

/* Icon-theme names.  Standard actions use Freedesktop names; ROX-specific
 * emblems keep their AppDir image names as fallbacks. */
#define ROX_ICON_OPEN             "document-open"
#define ROX_ICON_SAVE             "document-save"
#define ROX_ICON_CANCEL           "process-stop"
#define ROX_ICON_DELETE           "edit-delete"
#define ROX_ICON_TRASH            "user-trash"
#define ROX_ICON_TRASH_FULL       "user-trash-full"
#define ROX_ICON_PREFERENCES      "preferences-system"
#define ROX_ICON_REFRESH          "view-refresh"
#define ROX_ICON_COPY             "edit-copy"
#define ROX_ICON_CUT              "edit-cut"
#define ROX_ICON_PASTE            "edit-paste"
#define ROX_ICON_CLOSE            "window-close"
#define ROX_ICON_HOME             "user-home"
#define ROX_ICON_HELP             "help-browser"
#define ROX_ICON_ZOOM_IN          "zoom-in"
#define ROX_ICON_ZOOM_OUT         "zoom-out"
#define ROX_ICON_ZOOM_FIT         "zoom-fit-best"
#define ROX_ICON_EXECUTE          "system-run"
#define ROX_ICON_FIND             "edit-find"
#define ROX_ICON_PROPERTIES       "document-properties"
#define ROX_ICON_CLEAR            "edit-clear"
#define ROX_ICON_ADD              "list-add"
#define ROX_ICON_REMOVE           "list-remove"
/* Agregado por josejp2424 (2026): iconos estándar del historial. */
#define ROX_ICON_GO_BACK          "go-previous"
#define ROX_ICON_GO_FORWARD       "go-next"
#define ROX_ICON_GO_UP            "go-up"
#define ROX_ICON_GO_DOWN          "go-down"
#define ROX_ICON_GO_LAST          "go-last"
#define ROX_ICON_JUMP_TO          "go-jump"
#define ROX_ICON_UNDO             "edit-undo"
#define ROX_ICON_NEW              "document-new"
#define ROX_ICON_DIALOG_WARNING   "dialog-warning"
#define ROX_ICON_DIALOG_QUESTION  "dialog-question"
#define ROX_ICON_DIALOG_INFO      "dialog-information"
#define ROX_ICON_DND_MULTIPLE     "edit-copy"
#define ROX_ICON_YES              "emblem-ok-symbolic"
#define ROX_ICON_NO               "process-stop"
#define ROX_ICON_OK               "emblem-ok-symbolic"
#define ROX_ICON_APPLY            "emblem-ok-symbolic"
#define ROX_ICON_DIRECTORY        "folder"
#define ROX_ICON_SORT_ASCENDING   "view-sort-ascending"
#define ROX_ICON_INFO             "dialog-information"
#define ROX_ICON_BOOKMARKS        "user-bookmarks"
/* Agregado por josejp2424: nombres estándar para la integración de iconos GTK3. */
#define ROX_ICON_TERMINAL         "utilities-terminal"

#define ROX_ICON_SHOW_DETAILS     "view-list"
/* Modificado por josejp2424 (2026): usar primero cab_view, presente en los
 * temas de iconos de Puppy, para que el botón Oculto siga el tema activo.
 * gui_support.c conserva alternativas GTK/Freedesktop y el recurso interno
 * únicamente como último respaldo. */
#define ROX_ICON_SHOW_HIDDEN      "cab_view"
#define ROX_ICON_SELECT           "edit-select-all"
#define ROX_ICON_MOUNT            "drive-harddisk"
#define ROX_ICON_MOUNTED          "media-eject"
#define ROX_ICON_XATTR            "document-properties"
#define ROX_ICON_SYMLINK          "emblem-symbolic-link"

#include <libxml/tree.h>
