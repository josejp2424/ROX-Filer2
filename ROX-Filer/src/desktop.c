/*
 * Agregado por josejp2424 (2026): ROX Desktop nativo para GTK3.
 *
 * Gestiona el wallpaper, los archivos del directorio ~/Desktop y las
 * unidades detectadas por el mismo modelo usado en la GUI de Particiones.
 * Este es el único módulo de escritorio de ROX-Filer. La implementación
 * heredada de pinboard fue retirada en r66; los backends X11/Wayland deben
 * quedar detrás de esta interfaz.
 */
#include "config.h"

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "desktop.h"
#include "desktop-backend.h"
#include "desktop_apps.h"
#include "trash.h"
#include "desktop_wbar.h"
#include "drives.h"
#include "rox_config.h"
#include "filer.h"
#include "i18n.h"
#include "gui_support.h"
#include "options.h"
#include "debug_log.h"
#include "menu.h"

#define DESKTOP_CONFIG "desktop.conf"
#define DESKTOP_POSITIONS_CONFIG "desktop-positions.conf"
#define SYSTEM_BACKGROUNDS_DIR "/usr/share/backgrounds"
#define DRIVE_POLL_SECONDS 8
#define DEFAULT_DESKTOP_ICON_SIZE 48
#define DEFAULT_DRIVE_ICON_SIZE 48
#define DEFAULT_DRIVE_MARGIN 12
#define DEFAULT_DRIVE_SPACING 10
#define DESKTOP_ICON_MARGIN 12
#define DESKTOP_ICON_LABEL_WIDTH 18
#define DESKTOP_DRAG_THRESHOLD 5

/* Modificado por josejp2424 (2026): los archivos reales de
 * XDG_DESKTOP_DIR se representan ahora con widgets libres dentro de GtkFixed.
 * Esto permite arrastrarlos, guardar su posición y restaurarla al iniciar,
 * algo que GtkIconView no puede hacer porque siempre ordena en una cuadrícula. */
typedef struct {
    gchar *uri;
    gchar *display_name;
    gboolean launcher;
    gboolean home;
    gboolean browser;
    gboolean console;
    gboolean trash;
    GtkWidget *widget;
    GtkWidget *image;
    gint x;
    gint y;
    gint press_root_x;
    gint press_root_y;
    gint start_x;
    gint start_y;
    gboolean dragging;
    gboolean selected;
} DesktopItem;

enum {
    WP_COL_PIXBUF,
    WP_COL_NAME,
    WP_COL_PATH,
    WP_N_COLS
};

typedef enum {
    WALLPAPER_FILL,
    WALLPAPER_FIT,
    WALLPAPER_STRETCH,
    WALLPAPER_CENTER,
    WALLPAPER_TILE
} DesktopWallpaperMode;

typedef enum {
    DRIVE_POSITION_BOTTOM_LEFT,
    DRIVE_POSITION_BOTTOM_RIGHT,
    DRIVE_POSITION_TOP_LEFT,
    DRIVE_POSITION_TOP_RIGHT
} DesktopDrivePosition;

typedef struct {
    gchar *device;
} DesktopDriveAction;

static GtkWidget *desktop_window;
static GtkWidget *desktop_overlay;
static GtkWidget *desktop_icon_layer;
static GtkWidget *desktop_drive_layer;
static GtkWidget *desktop_drive_box;
static GList *desktop_items;
static DesktopItem *desktop_selected_item;
static GFileMonitor *desktop_monitor;
static GFileMonitor *desktop_config_monitor;
static GFileMonitor *trash_monitor;
static GVolumeMonitor *volume_monitor;
static GdkScreen *desktop_screen;
static gulong desktop_screen_monitors_handler;
static gulong desktop_screen_size_handler;
static guint wallpaper_reload_source;
static guint drive_poll_source;
static guint geometry_reload_source;
static guint environment_refresh_source;
static guint environment_refresh_round;
static gchar *drive_signature;
static gboolean drive_scan_in_progress;
static guint drive_scan_serial;
static gchar *desktop_dir;
static gchar *wallpaper_path;
static GdkPixbuf *wallpaper_pixbuf;
static gboolean show_volumes = TRUE;
static gboolean desktop_show_home = TRUE;
static gboolean desktop_show_browser = TRUE;
static gboolean desktop_show_console = TRUE;
static gboolean desktop_show_trash = TRUE;
static gboolean show_drive_quick_action = TRUE;
static gboolean drive_show_internal = TRUE;
static gboolean drive_show_removable = TRUE;
static gboolean drive_show_network = FALSE;
static gboolean drive_show_labels = TRUE;
static gboolean drive_show_frame = FALSE;
static gboolean drive_reverse_pack = TRUE;
static DesktopWallpaperMode wallpaper_mode = WALLPAPER_FILL;
static DesktopDrivePosition drive_position = DRIVE_POSITION_BOTTOM_LEFT;
static GtkOrientation drive_orientation = GTK_ORIENTATION_HORIZONTAL;
static gint desktop_icon_size = DEFAULT_DESKTOP_ICON_SIZE;
static gboolean desktop_single_click = FALSE;
static gboolean desktop_snap_to_grid = TRUE;
static gint drive_icon_size = 32;
static gint drive_margin = DEFAULT_DRIVE_MARGIN;
static gint drive_spacing = DEFAULT_DRIVE_SPACING;
static gint drive_spacing_x = 87;
static gint drive_spacing_y = 87;
static gint drive_x_offset = 20;
static gint drive_y_offset = -40;
static gdouble drive_x_pos = 0.0;
static gdouble drive_y_pos = 1.0;
static GdkRectangle desktop_geometry = {0, 0, 1, 1};
static GdkRectangle desktop_workarea = {0, 0, 1, 1};
static GdkRectangle desktop_drive_reserved = {0, 0, 0, 0};
static const DesktopBackend *desktop_backend;

static void desktop_reload(void);
static gboolean desktop_reload_idle(gpointer data);
static void desktop_load_wallpaper_from_config(void);
static void desktop_show_wallpaper_dialog(GtkWindow *parent);
static void desktop_apply_drive_layout(void);
static gboolean desktop_drive_is_visible(const RoxDriveInfo *drive);
static void desktop_calculate_drive_rect(gint width, gint height,
                                         GdkRectangle *rect);
static void desktop_update_drive_reservation(void);
static void desktop_reflow_items(gboolean save_positions);
static void desktop_arrange_items(gboolean save_positions);
static void desktop_request_drive_scan(void);
static void desktop_force_drive_refresh(void);
static void desktop_rebuild_drive_box_from_list(GPtrArray *drives);
static void desktop_schedule_geometry_update(void);
static gboolean desktop_environment_refresh_cb(gpointer data);
static void desktop_show_preferences_dialog(GtkWindow *parent);
static void desktop_show_drive_layout_dialog(GtkWindow *parent);
static void desktop_realign_drive_icons(void);
static void desktop_rebuild_icon_layer(void);
static GList *build_desktop_tools(Option *option, xmlNode *node, guchar *label);
static void desktop_item_activate(DesktopItem *item);
static void show_desktop_item_menu(DesktopItem *item, GdkEventButton *event);
static void show_desktop_menu(GdkEventButton *event);

extern int number_of_windows;

static gboolean desktop_item_is_builtin(const DesktopItem *item)
{
    return item && (item->home || item->browser ||
                    item->console || item->trash);
}

static void desktop_backend_refresh(gpointer data)
{
    (void)data;
    desktop_refresh_now();
}

static void show_desktop_error(const gchar *primary, const gchar *secondary)
{
    GtkWidget *dialog;

    dialog = gtk_message_dialog_new(desktop_window ? GTK_WINDOW(desktop_window) : NULL,
        GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
        "%s", primary ? primary : _("ROX Desktop error"));
    gtk_window_set_position(GTK_WINDOW(dialog), desktop_window ?
        GTK_WIN_POS_CENTER_ON_PARENT : GTK_WIN_POS_CENTER_ALWAYS);
    if (secondary && *secondary)
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
            "%s", secondary);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static const gchar *wallpaper_mode_name(DesktopWallpaperMode mode)
{
    switch (mode) {
        case WALLPAPER_FIT: return "fit";
        case WALLPAPER_STRETCH: return "stretch";
        case WALLPAPER_CENTER: return "center";
        case WALLPAPER_TILE: return "tile";
        case WALLPAPER_FILL:
        default: return "fill";
    }
}

static DesktopWallpaperMode wallpaper_mode_from_name(const gchar *name)
{
    if (!g_strcmp0(name, "fit"))
        return WALLPAPER_FIT;
    if (!g_strcmp0(name, "stretch"))
        return WALLPAPER_STRETCH;
    if (!g_strcmp0(name, "center"))
        return WALLPAPER_CENTER;
    if (!g_strcmp0(name, "tile"))
        return WALLPAPER_TILE;
    return WALLPAPER_FILL;
}

static const gchar *drive_position_name(DesktopDrivePosition position)
{
    switch (position) {
        case DRIVE_POSITION_BOTTOM_RIGHT: return "bottom-right";
        case DRIVE_POSITION_TOP_LEFT: return "top-left";
        case DRIVE_POSITION_TOP_RIGHT: return "top-right";
        case DRIVE_POSITION_BOTTOM_LEFT:
        default: return "bottom-left";
    }
}

static DesktopDrivePosition drive_position_from_name(const gchar *name)
{
    if (!g_strcmp0(name, "bottom-right"))
        return DRIVE_POSITION_BOTTOM_RIGHT;
    if (!g_strcmp0(name, "top-left"))
        return DRIVE_POSITION_TOP_LEFT;
    if (!g_strcmp0(name, "top-right"))
        return DRIVE_POSITION_TOP_RIGHT;
    return DRIVE_POSITION_BOTTOM_LEFT;
}

/* Leer primero las claves nuevas de ROX Desktop y aceptar después los nombres
 * usados por EssoraWM en la sección [Main]. Esto permite copiar la misma
 * geometría de unidades sin hacer depender ROX de otro proceso. */
static gboolean drive_config_boolean(GKeyFile *kf, const gchar *new_key,
                                     const gchar *compat_key, gboolean fallback)
{
    if (g_key_file_has_key(kf, "DesktopDrives", new_key, NULL))
        return g_key_file_get_boolean(kf, "DesktopDrives", new_key, NULL);
    if (compat_key && g_key_file_has_key(kf, "Main", compat_key, NULL))
        return g_key_file_get_boolean(kf, "Main", compat_key, NULL);
    return fallback;
}

static gint drive_config_integer(GKeyFile *kf, const gchar *new_key,
                                 const gchar *compat_key, gint fallback)
{
    if (g_key_file_has_key(kf, "DesktopDrives", new_key, NULL))
        return g_key_file_get_integer(kf, "DesktopDrives", new_key, NULL);
    if (compat_key && g_key_file_has_key(kf, "Main", compat_key, NULL))
        return g_key_file_get_integer(kf, "Main", compat_key, NULL);
    return fallback;
}

static gdouble drive_config_double(GKeyFile *kf, const gchar *new_key,
                                   const gchar *compat_key, gdouble fallback)
{
    if (g_key_file_has_key(kf, "DesktopDrives", new_key, NULL))
        return g_key_file_get_double(kf, "DesktopDrives", new_key, NULL);
    if (compat_key && g_key_file_has_key(kf, "Main", compat_key, NULL))
        return g_key_file_get_double(kf, "Main", compat_key, NULL);
    return fallback;
}

static void drive_position_from_coordinates(void)
{
    if (drive_x_pos >= 0.5)
        drive_position = drive_y_pos >= 0.5
            ? DRIVE_POSITION_BOTTOM_RIGHT : DRIVE_POSITION_TOP_RIGHT;
    else
        drive_position = drive_y_pos >= 0.5
            ? DRIVE_POSITION_BOTTOM_LEFT : DRIVE_POSITION_TOP_LEFT;
}

static void drive_coordinates_from_position(void)
{
    switch (drive_position) {
        case DRIVE_POSITION_BOTTOM_RIGHT:
            drive_x_pos = 1.0; drive_y_pos = 1.0; break;
        case DRIVE_POSITION_TOP_LEFT:
            drive_x_pos = 0.0; drive_y_pos = 0.0; break;
        case DRIVE_POSITION_TOP_RIGHT:
            drive_x_pos = 1.0; drive_y_pos = 0.0; break;
        case DRIVE_POSITION_BOTTOM_LEFT:
        default:
            drive_x_pos = 0.0; drive_y_pos = 1.0; break;
    }
}

static gboolean desktop_save_wallpaper(GError **error)
{
    GKeyFile *kf;
    gboolean saved;

    kf = rox_config_load(DESKTOP_CONFIG);
    if (wallpaper_path && *wallpaper_path)
        g_key_file_set_string(kf, "Desktop", "Wallpaper", wallpaper_path);
    g_key_file_set_string(kf, "Desktop", "WallpaperMode",
                          wallpaper_mode_name(wallpaper_mode));
    saved = rox_config_save(kf, DESKTOP_CONFIG, error);
    g_key_file_unref(kf);
    return saved;
}

static gboolean desktop_save_preferences(GError **error)
{
    GKeyFile *kf;
    gboolean saved;

    kf = rox_config_load(DESKTOP_CONFIG);
    if (desktop_dir && *desktop_dir)
        g_key_file_set_string(kf, "Desktop", "Directory", desktop_dir);
    if (wallpaper_path && *wallpaper_path)
        g_key_file_set_string(kf, "Desktop", "Wallpaper", wallpaper_path);
    g_key_file_set_string(kf, "Desktop", "WallpaperMode",
                          wallpaper_mode_name(wallpaper_mode));
    g_key_file_set_boolean(kf, "Desktop", "ShowVolumes", show_volumes);

    g_key_file_set_boolean(kf, "DesktopIcons", "ShowHome", desktop_show_home);
    g_key_file_set_boolean(kf, "DesktopIcons", "ShowBrowser", desktop_show_browser);
    g_key_file_set_boolean(kf, "DesktopIcons", "ShowConsole", desktop_show_console);
    g_key_file_set_boolean(kf, "DesktopIcons", "ShowTrash", desktop_show_trash);
    g_key_file_set_integer(kf, "DesktopIcons", "IconSize", desktop_icon_size);
    g_key_file_set_boolean(kf, "DesktopIcons", "SingleClick", desktop_single_click);
    g_key_file_set_boolean(kf, "DesktopIcons", "SnapToGrid", desktop_snap_to_grid);

    g_key_file_set_string(kf, "DesktopDrives", "Position",
                          drive_position_name(drive_position));
    g_key_file_set_string(kf, "DesktopDrives", "Orientation",
                          drive_orientation == GTK_ORIENTATION_VERTICAL
                              ? "vertical" : "horizontal");
    g_key_file_set_integer(kf, "DesktopDrives", "IconSize", drive_icon_size);
    g_key_file_set_boolean(kf, "DesktopDrives", "ShowQuickAction",
                           show_drive_quick_action);
    g_key_file_set_boolean(kf, "DesktopDrives", "ShowInternal",
                           drive_show_internal);
    g_key_file_set_boolean(kf, "DesktopDrives", "ShowRemovable",
                           drive_show_removable);
    g_key_file_set_boolean(kf, "DesktopDrives", "ShowNetwork",
                           drive_show_network);
    g_key_file_set_boolean(kf, "DesktopDrives", "ShowLabels",
                           drive_show_labels);
    g_key_file_set_boolean(kf, "DesktopDrives", "ShowFrame",
                           drive_show_frame);
    g_key_file_set_boolean(kf, "DesktopDrives", "ReversePack",
                           drive_reverse_pack);
    g_key_file_set_integer(kf, "DesktopDrives", "SpacingX", drive_spacing_x);
    g_key_file_set_integer(kf, "DesktopDrives", "SpacingY", drive_spacing_y);
    g_key_file_set_integer(kf, "DesktopDrives", "XOffset", drive_x_offset);
    g_key_file_set_integer(kf, "DesktopDrives", "YOffset", drive_y_offset);
    g_key_file_set_double(kf, "DesktopDrives", "XPos", drive_x_pos);
    g_key_file_set_double(kf, "DesktopDrives", "YPos", drive_y_pos);

    /* Alias compatibles con la configuración de unidades de EssoraWM. */
    g_key_file_set_boolean(kf, "Main", "desktop_drive_icons", show_volumes);
    g_key_file_set_boolean(kf, "Main", "desktop_drive_show_internal",
                           drive_show_internal);
    g_key_file_set_boolean(kf, "Main", "desktop_drive_show_removable",
                           drive_show_removable);
    g_key_file_set_boolean(kf, "Main", "desktop_drive_show_network",
                           drive_show_network);
    g_key_file_set_integer(kf, "Main", "desktop_drive_icon_size",
                           drive_icon_size);
    g_key_file_set_boolean(kf, "Main", "ShowLabels", drive_show_labels);
    g_key_file_set_boolean(kf, "Main", "ShowFrame", drive_show_frame);
    g_key_file_set_boolean(kf, "Main", "Vertical",
                           drive_orientation == GTK_ORIENTATION_VERTICAL);
    g_key_file_set_boolean(kf, "Main", "ReversePack", drive_reverse_pack);
    g_key_file_set_integer(kf, "Main", "SpacingX", drive_spacing_x);
    g_key_file_set_integer(kf, "Main", "SpacingY", drive_spacing_y);
    g_key_file_set_integer(kf, "Main", "XOffset", drive_x_offset);
    g_key_file_set_integer(kf, "Main", "YOffset", drive_y_offset);
    g_key_file_set_double(kf, "Main", "XPos", drive_x_pos);
    g_key_file_set_double(kf, "Main", "YPos", drive_y_pos);

    /* Mantener las claves antiguas para no romper configuraciones r44. */
    g_key_file_set_integer(kf, "DesktopDrives", "Margin", drive_margin);
    g_key_file_set_integer(kf, "DesktopDrives", "Spacing", drive_spacing);

    saved = rox_config_save(kf, DESKTOP_CONFIG, error);
    g_key_file_unref(kf);
    return saved;
}

/* Agregado por josejp2424 (2026): calcular el rectángulo virtual completo
 * mediante GDK. En X11, GDK obtiene estos datos de XRandR; no se ejecuta ni
 * se analiza el comando externo xrandr. El área útil del monitor principal
 * se conserva por separado para evitar colocar unidades debajo del panel. */
static void desktop_query_geometry(void)
{
    GdkDisplay *display;
    GdkMonitor *primary = NULL;
    gint count;
    gint i;
    gboolean have_geometry = FALSE;

    display = gdk_display_get_default();
    count = display ? gdk_display_get_n_monitors(display) : 0;

    for (i = 0; i < count; i++) {
        GdkMonitor *monitor = gdk_display_get_monitor(display, i);
        GdkRectangle geometry;

        if (!monitor)
            continue;
        gdk_monitor_get_geometry(monitor, &geometry);
        if (!have_geometry) {
            desktop_geometry = geometry;
            have_geometry = TRUE;
        } else {
            gint left = MIN(desktop_geometry.x, geometry.x);
            gint top = MIN(desktop_geometry.y, geometry.y);
            gint right = MAX(desktop_geometry.x + desktop_geometry.width,
                             geometry.x + geometry.width);
            gint bottom = MAX(desktop_geometry.y + desktop_geometry.height,
                              geometry.y + geometry.height);
            desktop_geometry.x = left;
            desktop_geometry.y = top;
            desktop_geometry.width = MAX(1, right - left);
            desktop_geometry.height = MAX(1, bottom - top);
        }
    }

    if (!have_geometry) {
        /* Modificado por josejp2424 (2026): no usar las funciones
         * obsoletas gdk_screen_get_width()/height(). Con un GdkDisplay
         * válido siempre debería existir al menos un monitor; 1x1 queda
         * únicamente como protección para una sesión gráfica incompleta. */
        desktop_geometry.x = 0;
        desktop_geometry.y = 0;
        desktop_geometry.width = 1;
        desktop_geometry.height = 1;
        desktop_workarea = desktop_geometry;
        return;
    }

    primary = gdk_display_get_primary_monitor(display);
    if (!primary && count > 0)
        primary = gdk_display_get_monitor(display, 0);
    if (primary)
        gdk_monitor_get_workarea(primary, &desktop_workarea);
    else
        desktop_workarea = desktop_geometry;
}

static void desktop_draw_scaled(cairo_t *cr, const GdkRectangle *rect,
                                gdouble scale_x, gdouble scale_y,
                                gboolean center)
{
    gint image_width = gdk_pixbuf_get_width(wallpaper_pixbuf);
    gint image_height = gdk_pixbuf_get_height(wallpaper_pixbuf);
    gint width = MAX(1, (gint)(image_width * scale_x + 0.5));
    gint height = MAX(1, (gint)(image_height * scale_y + 0.5));
    gint x = center ? rect->x + (rect->width - width) / 2 : rect->x;
    gint y = center ? rect->y + (rect->height - height) / 2 : rect->y;
    GdkPixbuf *scaled;

    scaled = gdk_pixbuf_scale_simple(wallpaper_pixbuf, width, height,
                                     GDK_INTERP_BILINEAR);
    if (!scaled)
        return;
    gdk_cairo_set_source_pixbuf(cr, scaled, x, y);
    cairo_paint(cr);
    g_object_unref(scaled);
}

static void desktop_draw_wallpaper_rect(cairo_t *cr, const GdkRectangle *rect)
{
    gint image_width;
    gint image_height;

    if (!wallpaper_pixbuf || !rect || rect->width <= 0 || rect->height <= 0)
        return;

    image_width = gdk_pixbuf_get_width(wallpaper_pixbuf);
    image_height = gdk_pixbuf_get_height(wallpaper_pixbuf);
    if (image_width <= 0 || image_height <= 0)
        return;

    cairo_save(cr);
    cairo_rectangle(cr, rect->x, rect->y, rect->width, rect->height);
    cairo_clip(cr);

    switch (wallpaper_mode) {
        case WALLPAPER_FIT: {
            gdouble scale = MIN((gdouble)rect->width / image_width,
                                (gdouble)rect->height / image_height);
            desktop_draw_scaled(cr, rect, scale, scale, TRUE);
            break;
        }
        case WALLPAPER_STRETCH:
            desktop_draw_scaled(cr, rect,
                (gdouble)rect->width / image_width,
                (gdouble)rect->height / image_height, FALSE);
            break;
        case WALLPAPER_CENTER:
            gdk_cairo_set_source_pixbuf(cr, wallpaper_pixbuf,
                rect->x + (rect->width - image_width) / 2,
                rect->y + (rect->height - image_height) / 2);
            cairo_paint(cr);
            break;
        case WALLPAPER_TILE: {
            gint x;
            gint y;
            for (y = rect->y; y < rect->y + rect->height; y += image_height) {
                for (x = rect->x; x < rect->x + rect->width; x += image_width) {
                    gdk_cairo_set_source_pixbuf(cr, wallpaper_pixbuf, x, y);
                    cairo_paint(cr);
                }
            }
            break;
        }
        case WALLPAPER_FILL:
        default: {
            gdouble scale = MAX((gdouble)rect->width / image_width,
                                (gdouble)rect->height / image_height);
            desktop_draw_scaled(cr, rect, scale, scale, TRUE);
            break;
        }
    }

    cairo_restore(cr);
}

static gboolean desktop_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    GdkDisplay *display;
    gint count;
    gint i;
    GtkAllocation allocation;
    (void)data;

    gtk_widget_get_allocation(widget, &allocation);
    cairo_set_source_rgb(cr, 0.12, 0.14, 0.18);
    cairo_paint(cr);

    display = gdk_display_get_default();
    count = display ? gdk_display_get_n_monitors(display) : 0;
    if (count <= 0) {
        GdkRectangle rect = {0, 0, allocation.width, allocation.height};
        desktop_draw_wallpaper_rect(cr, &rect);
        return FALSE;
    }

    /* Modificado por josejp2424 (2026): dibujar el wallpaper por monitor.
     * Esto evita el rectángulo pequeño observado al usar una geometría fija y
     * también mantiene un encuadre correcto en configuraciones multimonitor. */
    for (i = 0; i < count; i++) {
        GdkMonitor *monitor = gdk_display_get_monitor(display, i);
        GdkRectangle rect;
        if (!monitor)
            continue;
        gdk_monitor_get_geometry(monitor, &rect);
        rect.x -= desktop_geometry.x;
        rect.y -= desktop_geometry.y;
        desktop_draw_wallpaper_rect(cr, &rect);
    }
    return FALSE;
}

/* Agregado por josejp2424 (2026): aplica el wallpaper y guarda la selección
 * en la configuración tradicional de ROX. Esta función puede ser llamada
 * tanto desde ROX Desktop como desde una ventana normal de ROX-Filer. */
gboolean desktop_set_wallpaper(const gchar *path, gboolean save,
                               GError **error)
{
    GdkPixbuf *new_pixbuf;
    gchar *absolute_path;

    g_return_val_if_fail(path != NULL, FALSE);

    absolute_path = g_strdup(path);
    new_pixbuf = gdk_pixbuf_new_from_file(absolute_path, error);
    if (!new_pixbuf) {
        g_free(absolute_path);
        return FALSE;
    }

    if (wallpaper_pixbuf)
        g_object_unref(wallpaper_pixbuf);
    wallpaper_pixbuf = new_pixbuf;

    g_free(wallpaper_path);
    wallpaper_path = absolute_path;

    if (save && !desktop_save_wallpaper(error))
        return FALSE;

    if (desktop_window)
        gtk_widget_queue_draw(desktop_window);

    /* Agregado por josejp2424 (2026): sincronizar también el fondo raíz de
     * X11 y, si wbar está activo, actualizarlo sin bloquear el hilo GTK. */
    desktop_wbar_wallpaper_changed(wallpaper_path,
                                   wallpaper_mode_name(wallpaper_mode));

    return TRUE;
}

static void desktop_load_wallpaper_from_config(void)
{
    GKeyFile *kf;
    gchar *path;
    gchar *mode;
    DesktopWallpaperMode new_mode;
    GError *error = NULL;

    kf = rox_config_load(DESKTOP_CONFIG);
    path = g_key_file_get_string(kf, "Desktop", "Wallpaper", NULL);
    mode = g_key_file_get_string(kf, "Desktop", "WallpaperMode", NULL);
    new_mode = wallpaper_mode_from_name(mode);
    g_free(mode);
    g_key_file_unref(kf);

    /* desktop.conf stores much more than wallpaper settings. Saving icons,
     * drives or desktop tools therefore triggers the same file monitor.
     * If wallpaper path and mode are unchanged, do not reload the image and,
     * on X11/XLibre, do not restart wbar for an unrelated config write. */
    if (path && *path &&
        g_strcmp0(path, wallpaper_path) == 0 &&
        new_mode == wallpaper_mode) {
        ROX_LOG_DEBUG("desktop",
                      "desktop.conf changed; wallpaper unchanged, skipping reload");
        g_free(path);
        return;
    }

    wallpaper_mode = new_mode;

    if (!path || !*path) {
        if (!wallpaper_path && !wallpaper_pixbuf) {
            g_free(path);
            return;
        }

        g_clear_object(&wallpaper_pixbuf);
        g_clear_pointer(&wallpaper_path, g_free);
        if (desktop_window)
            gtk_widget_queue_draw(desktop_window);
        g_free(path);
        return;
    }

    ROX_LOG_DEBUG("desktop",
                  "wallpaper configuration changed; reloading path=%s mode=%s",
                  path, wallpaper_mode_name(wallpaper_mode));

    if (!desktop_set_wallpaper(path, FALSE, &error)) {
        g_warning("Unable to reload wallpaper '%s': %s", path,
                  error ? error->message : "unknown error");
        g_clear_error(&error);
    }
    g_free(path);
}

static gboolean reload_wallpaper_later(gpointer data)
{
    (void)data;
    wallpaper_reload_source = 0;
    desktop_load_wallpaper_from_config();
    return G_SOURCE_REMOVE;
}

static void desktop_config_changed(GFileMonitor *monitor, GFile *file,
                                   GFile *other_file,
                                   GFileMonitorEvent event_type,
                                   gpointer data)
{
    gchar *basename;

    (void)monitor;
    (void)other_file;
    (void)data;

    if (event_type != G_FILE_MONITOR_EVENT_CHANGED &&
        event_type != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT &&
        event_type != G_FILE_MONITOR_EVENT_CREATED &&
        event_type != G_FILE_MONITOR_EVENT_MOVED_IN &&
        event_type != G_FILE_MONITOR_EVENT_RENAMED &&
        event_type != G_FILE_MONITOR_EVENT_ATTRIBUTE_CHANGED)
        return;

    basename = g_file_get_basename(file);
    if (g_strcmp0(basename, DESKTOP_CONFIG) == 0 &&
        wallpaper_reload_source == 0)
        wallpaper_reload_source = g_timeout_add(120, reload_wallpaper_later, NULL);
    g_free(basename);
}

static GdkPixbuf *pixbuf_from_gicon(GIcon *icon, gint size)
{
    GtkIconInfo *ii;
    GdkPixbuf *pixbuf = NULL;

    if (!icon)
        return NULL;
    ii = gtk_icon_theme_lookup_by_gicon(gtk_icon_theme_get_default(), icon,
        size, GTK_ICON_LOOKUP_FORCE_SIZE);
    if (ii) {
        pixbuf = gtk_icon_info_load_icon(ii, NULL);
        g_object_unref(ii);
    }
    return pixbuf;
}

static GdkPixbuf *icon_for_file(GFile *file, gint size)
{
    GFileInfo *info;
    GIcon *icon;
    GdkPixbuf *pixbuf = NULL;

    info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_ICON,
                             G_FILE_QUERY_INFO_NONE, NULL, NULL);
    if (!info)
        return NULL;
    icon = g_file_info_get_icon(info);
    if (icon)
        pixbuf = pixbuf_from_gicon(icon, size);
    g_object_unref(info);
    return pixbuf;
}

static void desktop_item_free(DesktopItem *item)
{
    if (!item)
        return;
    g_free(item->uri);
    g_free(item->display_name);
    g_free(item);
}

static void desktop_items_clear(void)
{
    GList *node;

    /* Modificado por josejp2424 (2026): destruir únicamente los widgets de
     * archivos y lanzadores. Las unidades comparten ahora el mismo GtkFixed
     * para no bloquear los eventos del escritorio y deben conservarse durante
     * una recarga de ~/Desktop. */
    desktop_selected_item = NULL;
    for (node = desktop_items; node; node = node->next) {
        DesktopItem *item = node->data;
        if (item && item->widget)
            gtk_widget_destroy(item->widget);
    }
    g_list_free_full(desktop_items, (GDestroyNotify)desktop_item_free);
    desktop_items = NULL;
}

static gchar *desktop_position_group(const gchar *uri)
{
    gchar *checksum;
    gchar *group;

    checksum = g_compute_checksum_for_string(G_CHECKSUM_SHA256,
                                              uri ? uri : "", -1);
    group = g_strdup_printf("DesktopIcon-%s", checksum);
    g_free(checksum);
    return group;
}

static gboolean desktop_item_load_position(DesktopItem *item)
{
    GKeyFile *kf;
    gchar *group;
    GError *error = NULL;
    gboolean found = FALSE;

    if (!item || !item->uri)
        return FALSE;

    kf = rox_config_load(DESKTOP_POSITIONS_CONFIG);
    group = desktop_position_group(item->uri);
    if (g_key_file_has_key(kf, group, "X", NULL) &&
        g_key_file_has_key(kf, group, "Y", NULL)) {
        item->x = g_key_file_get_integer(kf, group, "X", &error);
        if (!error)
            item->y = g_key_file_get_integer(kf, group, "Y", &error);
        if (!error)
            found = TRUE;
    }
    g_clear_error(&error);
    g_free(group);
    g_key_file_unref(kf);
    return found;
}

static void desktop_item_save_position(DesktopItem *item)
{
    GKeyFile *kf;
    gchar *group;
    GError *error = NULL;

    if (!item || !item->uri)
        return;

    kf = rox_config_load(DESKTOP_POSITIONS_CONFIG);
    group = desktop_position_group(item->uri);
    g_key_file_set_string(kf, group, "URI", item->uri);
    g_key_file_set_integer(kf, group, "X", item->x);
    g_key_file_set_integer(kf, group, "Y", item->y);
    if (!rox_config_save(kf, DESKTOP_POSITIONS_CONFIG, &error)) {
        g_warning("Unable to save desktop icon position: %s",
                  error ? error->message : "unknown error");
        g_clear_error(&error);
    }
    g_free(group);
    g_key_file_unref(kf);
}

/* Agregado por josejp2424 (2026): eliminar también la posición persistente
 * cuando un lanzador se quita del escritorio. De esta forma, si se vuelve a
 * agregar más adelante, ROX Desktop buscará una posición libre y no conserva
 * coordenadas obsoletas. */
static void desktop_item_remove_saved_position(DesktopItem *item)
{
    GKeyFile *kf;
    gchar *group;
    GError *error = NULL;

    if (!item || !item->uri)
        return;

    kf = rox_config_load(DESKTOP_POSITIONS_CONFIG);
    group = desktop_position_group(item->uri);
    if (g_key_file_remove_group(kf, group, NULL) &&
        !rox_config_save(kf, DESKTOP_POSITIONS_CONFIG, &error)) {
        g_warning("Unable to remove desktop icon position: %s",
                  error ? error->message : "unknown error");
        g_clear_error(&error);
    }
    g_free(group);
    g_key_file_unref(kf);
}

static void desktop_item_bounds(gint *min_x, gint *min_y,
                                gint *max_x, gint *max_y,
                                gint item_width, gint item_height)
{
    gint left;
    gint top;

    left = MAX(0, desktop_workarea.x - desktop_geometry.x);
    top = MAX(0, desktop_workarea.y - desktop_geometry.y);
    if (min_x)
        *min_x = left + DESKTOP_ICON_MARGIN;
    if (min_y)
        *min_y = top + DESKTOP_ICON_MARGIN;
    if (max_x)
        *max_x = MAX(left + DESKTOP_ICON_MARGIN,
            left + desktop_workarea.width - item_width - DESKTOP_ICON_MARGIN);
    if (max_y)
        *max_y = MAX(top + DESKTOP_ICON_MARGIN,
            top + desktop_workarea.height - item_height - DESKTOP_ICON_MARGIN);
}

static void desktop_item_clamp(DesktopItem *item)
{
    GtkRequisition minimum;
    GtkRequisition natural;
    gint min_x;
    gint min_y;
    gint max_x;
    gint max_y;

    if (!item || !item->widget)
        return;
    gtk_widget_get_preferred_size(item->widget, &minimum, &natural);
    desktop_item_bounds(&min_x, &min_y, &max_x, &max_y,
                        MAX(minimum.width, natural.width),
                        MAX(minimum.height, natural.height));
    item->x = CLAMP(item->x, min_x, max_x);
    item->y = CLAMP(item->y, min_y, max_y);
}

static gboolean desktop_rectangles_overlap(gint ax, gint ay,
                                           gint aw, gint ah,
                                           gint bx, gint by,
                                           gint bw, gint bh)
{
    return ax < bx + bw && ax + aw > bx &&
           ay < by + bh && ay + ah > by;
}

static gboolean desktop_position_hits_drives(gint x, gint y)
{
    gint item_width = MAX(96, desktop_icon_size + 48);
    gint item_height = MAX(78, desktop_icon_size + 46);

    if (desktop_drive_reserved.width <= 0 ||
        desktop_drive_reserved.height <= 0)
        return FALSE;

    return desktop_rectangles_overlap(x, y, item_width, item_height,
        desktop_drive_reserved.x, desktop_drive_reserved.y,
        desktop_drive_reserved.width, desktop_drive_reserved.height);
}

static gboolean desktop_position_occupied_in_list(gint x, gint y,
                                                   DesktopItem *ignore,
                                                   GList *items)
{
    GList *node;
    gint step_x = MAX(92, desktop_icon_size + 48);
    gint step_y = MAX(82, desktop_icon_size + 48);

    if (desktop_position_hits_drives(x, y))
        return TRUE;

    for (node = items; node; node = node->next) {
        DesktopItem *other = node->data;
        if (other == ignore)
            continue;
        if (ABS(other->x - x) < step_x && ABS(other->y - y) < step_y)
            return TRUE;
    }
    return FALSE;
}

static gboolean desktop_position_occupied(gint x, gint y, DesktopItem *ignore)
{
    return desktop_position_occupied_in_list(x, y, ignore, desktop_items);
}

static void desktop_find_free_position_in_list(DesktopItem *item, GList *placed)
{
    gint item_width = MAX(96, desktop_icon_size + 48);
    gint item_height = MAX(78, desktop_icon_size + 46);
    gint min_x;
    gint min_y;
    gint max_x;
    gint max_y;
    gint step_x = MAX(100, desktop_icon_size + 52);
    gint step_y = MAX(88, desktop_icon_size + 52);
    gint x;
    gint y;

    desktop_item_bounds(&min_x, &min_y, &max_x, &max_y,
                        item_width, item_height);
    for (x = min_x; x <= max_x; x += step_x) {
        for (y = min_y; y <= max_y; y += step_y) {
            if (!desktop_position_occupied_in_list(x, y, item, placed)) {
                item->x = x;
                item->y = y;
                return;
            }
        }
    }
    item->x = min_x;
    item->y = min_y;
}

static void desktop_find_free_position(DesktopItem *item)
{
    desktop_find_free_position_in_list(item, desktop_items);
}

/* Modificado por josejp2424 (2026): conserva las posiciones manuales que
 * siguen siendo válidas, pero recoloca automáticamente cualquier icono que
 * quede sobre una unidad, fuera del área útil o encima de otro icono. */
static void desktop_reflow_items(gboolean save_positions)
{
    GList *node;
    GList *placed = NULL;

    for (node = desktop_items; node; node = node->next) {
        DesktopItem *item = node->data;
        gint old_x = item->x;
        gint old_y = item->y;

        desktop_item_clamp(item);
        if (desktop_position_occupied_in_list(item->x, item->y, item, placed))
            desktop_find_free_position_in_list(item, placed);

        if (desktop_icon_layer && item->widget)
            gtk_fixed_move(GTK_FIXED(desktop_icon_layer), item->widget,
                           item->x, item->y);
        if (save_positions && (old_x != item->x || old_y != item->y))
            desktop_item_save_position(item);

        placed = g_list_append(placed, item);
    }
    g_list_free(placed);
}

/* Agregado por josejp2424 (2026): "Refresh Desktop" también funciona como
 * organizador. Ignora las coordenadas manuales actuales y vuelve a colocar
 * todos los iconos del escritorio, en orden, sobre la grilla normal. De este
 * modo un icono suelto en medio de la pantalla o ligeramente desalineado
 * regresa a la secuencia del escritorio. Las unidades continúan reservando
 * su zona y no son movidas por este organizador. */
static void desktop_arrange_items(gboolean save_positions)
{
    GList *node;
    GList *placed = NULL;
    guint total = 0;
    guint moved = 0;

    for (node = desktop_items; node; node = node->next) {
        DesktopItem *item = node->data;
        gint old_x;
        gint old_y;

        if (!item)
            continue;

        old_x = item->x;
        old_y = item->y;
        desktop_find_free_position_in_list(item, placed);
        desktop_item_clamp(item);

        if (desktop_icon_layer && item->widget)
            gtk_fixed_move(GTK_FIXED(desktop_icon_layer), item->widget,
                           item->x, item->y);

        if (old_x != item->x || old_y != item->y) {
            moved++;
            if (save_positions)
                desktop_item_save_position(item);
        }

        placed = g_list_append(placed, item);
        total++;
    }

    g_list_free(placed);
    ROX_LOG_INFO("desktop",
                 "desktop icons arranged on grid items=%u moved=%u",
                 total, moved);
}


static void desktop_item_update_selection_style(DesktopItem *item)
{
    GtkStyleContext *context;

    if (!item || !item->widget)
        return;
    context = gtk_widget_get_style_context(item->widget);
    if (item->selected)
        gtk_style_context_add_class(context, "rox-desktop-item-selected");
    else
        gtk_style_context_remove_class(context, "rox-desktop-item-selected");
    gtk_widget_queue_draw(item->widget);
}

/* Desktop labels are drawn by GtkLabel itself. Their outline and selection
 * colours are provided by GTK CSS below. */

static guint desktop_selected_count(void)
{
    GList *node;
    guint count = 0;

    for (node = desktop_items; node; node = node->next) {
        DesktopItem *item = node->data;
        if (item && item->selected)
            count++;
    }
    return count;
}

static void desktop_clear_selection(void)
{
    GList *node;

    for (node = desktop_items; node; node = node->next) {
        DesktopItem *item = node->data;
        if (!item->selected)
            continue;
        item->selected = FALSE;
        desktop_item_update_selection_style(item);
    }
    desktop_selected_item = NULL;
}

/* Agregado por josejp2424 (2026): Ctrl+clic, Shift+clic y Ctrl+Alt+clic
 * alternan elementos sin borrar la selección existente. */
static gboolean desktop_extend_selection(GdkModifierType state)
{
    return (state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK)) != 0;
}

/* Rox-Filer2 2.12.2-2:
 * Desktop GtkEventBox widgets keep focus/hover/selection state after opening
 * a filer window in the same process. On X11/XLibre some GTK3 themes can then
 * render stale states in later popup menus. Release all desktop interaction
 * state once activation is complete. */
static void desktop_release_interaction_state(void)
{
    GList *node;

    desktop_clear_selection();

    for (node = desktop_items; node; node = node->next) {
        DesktopItem *item = node->data;
        GtkStyleContext *context;

        if (!item || !item->widget)
            continue;

        context = gtk_widget_get_style_context(item->widget);
        gtk_style_context_remove_class(context, "rox-desktop-item-hover");
        gtk_widget_unset_state_flags(item->widget,
                                     GTK_STATE_FLAG_PRELIGHT |
                                     GTK_STATE_FLAG_ACTIVE);
        gtk_widget_queue_draw(item->widget);
    }

    if (desktop_window && GTK_IS_WINDOW(desktop_window))
        gtk_window_set_focus(GTK_WINDOW(desktop_window), NULL);

    ROX_LOG_DEBUG("desktop",
                  "released desktop selection/hover/focus after activation");
}

static void desktop_item_set_selected(DesktopItem *item,
                                      gboolean extend,
                                      gboolean toggle)
{
    if (!item)
        return;

    if (!extend)
        desktop_clear_selection();

    if (toggle)
        item->selected = !item->selected;
    else
        item->selected = TRUE;

    desktop_item_update_selection_style(item);
    if (item->selected)
        desktop_selected_item = item;
    else if (desktop_selected_item == item)
        desktop_selected_item = NULL;
}

static gboolean desktop_spawn_first_available(const gchar *working_dir,
                                               const gchar * const *commands,
                                               GError **error)
{
    guint i;

    for (i = 0; commands && commands[i]; i++) {
        gchar *program = g_find_program_in_path(commands[i]);
        gchar *argv[2];
        gboolean ok;

        if (!program)
            continue;
        argv[0] = program;
        argv[1] = NULL;
        ok = g_spawn_async(working_dir, argv, NULL, G_SPAWN_DEFAULT,
                           NULL, NULL, NULL, error);
        g_free(program);
        return ok;
    }

    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                "%s", _("No suitable application was found"));
    return FALSE;
}

static void desktop_launch_browser(void)
{
    static const gchar * const commands[] = {
        "x-www-browser", "gnome-www-browser", NULL
    };
    GError *error = NULL;

    if (!desktop_spawn_first_available(g_get_home_dir(), commands, &error)) {
        show_desktop_error(_("Unable to start the web browser"),
                           error ? error->message : NULL);
        g_clear_error(&error);
    }
}

static void desktop_launch_console(void)
{
    /* Use the same Rox terminal preference as Open Terminal Here and
     * Run in Terminal. This keeps Console and filer actions consistent. */
    menu_open_terminal_at(g_get_home_dir());
}

static void desktop_item_activate(DesktopItem *item)
{
    GFile *file;
    gchar *local_path;

    if (!item || !item->uri)
        return;

    if (item->browser) {
        desktop_launch_browser();
        return;
    }
    if (item->console) {
        desktop_launch_console();
        return;
    }
    if (item->trash) {
        rox_trash_open(NULL);
        return;
    }

    file = g_file_new_for_uri(item->uri);
    local_path = g_file_get_path(file);
    if (local_path &&
        g_file_query_file_type(file, G_FILE_QUERY_INFO_NONE, NULL) ==
            G_FILE_TYPE_DIRECTORY) {
        filer_opendir(local_path, NULL, NULL);
    } else {
        GError *error = NULL;
        gboolean opened;

        if (item->launcher && local_path)
            opened = desktop_app_launch(local_path, &error);
        else
            opened = g_app_info_launch_default_for_uri(item->uri, NULL, &error);

        if (!opened) {
            show_desktop_error(_("Unable to open desktop item"),
                error ? error->message :
                        _("No application is associated with this file"));
            g_clear_error(&error);
        }
    }
    g_free(local_path);
    g_object_unref(file);
}

static void desktop_activate_selected_items(DesktopItem *fallback)
{
    GList *node;
    gboolean activated = FALSE;

    for (node = desktop_items; node; node = node->next) {
        DesktopItem *item = node->data;
        if (!item || !item->selected)
            continue;
        desktop_item_activate(item);
        activated = TRUE;
    }
    if (!activated && fallback) {
        desktop_item_activate(fallback);
        activated = TRUE;
    }

    if (activated)
        desktop_release_interaction_state();
}

/* Mover los elementos seleccionados a la papelera permite quitar accesos
 * directos y archivos del escritorio sin borrar permanentemente los datos. */
static void desktop_remove_selected_items(DesktopItem *fallback)
{
    GList *node;
    guint count;
    gchar *message;
    GString *errors;
    gboolean changed = FALSE;

    if (fallback && !fallback->selected)
        desktop_item_set_selected(fallback, FALSE, FALSE);

    count = 0;
    for (node = desktop_items; node; node = node->next) {
        DesktopItem *candidate = node->data;
        if (candidate && candidate->selected &&
            !desktop_item_is_builtin(candidate))
            count++;
    }
    if (count == 0)
        return;

    if (count == 1) {
        DesktopItem *selected = NULL;
        for (node = desktop_items; node; node = node->next) {
            DesktopItem *item = node->data;
            if (item && item->selected &&
                !desktop_item_is_builtin(item)) {
                selected = item;
                break;
            }
        }
        message = g_strdup_printf(_("Move '%s' to the Trash?"),
            selected && selected->display_name
                ? selected->display_name : (selected ? selected->uri : ""));
    } else {
        message = g_strdup_printf(_("Move %d selected items to the Trash?"),
                                  (gint)count);
    }

    if (!confirm(message, ROX_ICON_TRASH, _("_Move to Trash"))) {
        g_free(message);
        return;
    }
    g_free(message);

    errors = g_string_new(NULL);
    for (node = desktop_items; node; node = node->next) {
        DesktopItem *item = node->data;
        GFile *file;
        GError *error = NULL;

        if (!item || !item->selected || !item->uri ||
            desktop_item_is_builtin(item))
            continue;

        file = g_file_new_for_uri(item->uri);
        if (!rox_trash_file(file, &error)) {
            if (errors->len)
                g_string_append_c(errors, '\n');
            g_string_append_printf(errors, "%s: %s",
                item->display_name ? item->display_name : item->uri,
                error ? error->message : _("Unknown error"));
            g_clear_error(&error);
        } else {
            desktop_item_remove_saved_position(item);
            changed = TRUE;
        }
        g_object_unref(file);
    }

    if (errors->len)
        show_desktop_error(
            _("Unable to move one or more desktop items to the Trash"),
            errors->str);
    g_string_free(errors, TRUE);

    if (changed)
        g_idle_add(desktop_reload_idle, NULL);
}

static void desktop_item_menu_open(GtkMenuItem *menu_item, gpointer data)
{
    (void)menu_item;
    desktop_activate_selected_items((DesktopItem *)data);
}

static void desktop_item_menu_remove(GtkMenuItem *menu_item, gpointer data)
{
    (void)menu_item;
    desktop_remove_selected_items((DesktopItem *)data);
}

static void desktop_trash_menu_open(GtkMenuItem *menu_item, gpointer data)
{
    (void)menu_item;
    (void)data;
    rox_trash_open(NULL);
}

static void desktop_trash_menu_empty(GtkMenuItem *menu_item, gpointer data)
{
    (void)menu_item;
    (void)data;
    rox_trash_empty(desktop_window ? GTK_WINDOW(desktop_window) : NULL);
}

static void show_desktop_item_menu(DesktopItem *item, GdkEventButton *event)
{
    GtkWidget *menu;
    GtkWidget *menu_item;

    if (!item || !event)
        return;

    menu = rox_menu_new();
    if (item->trash) {
        menu_item = menu_item_new_with_icon(_("Open Trash"), ROX_ICON_TRASH);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
        g_signal_connect(menu_item, "activate",
                         G_CALLBACK(desktop_trash_menu_open), NULL);

        menu_item = menu_item_new_with_icon(_("Restore Items..."), ROX_ICON_UNDO);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
        g_signal_connect(menu_item, "activate",
                         G_CALLBACK(desktop_trash_menu_open), NULL);

        menu_item = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);

        menu_item = menu_item_new_with_icon(_("Empty Trash"), ROX_ICON_DELETE);
        gtk_widget_set_sensitive(menu_item, !rox_trash_is_empty());
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
        g_signal_connect(menu_item, "activate",
                         G_CALLBACK(desktop_trash_menu_empty), NULL);
    } else {
        menu_item = menu_item_new_with_icon(_("Open"), ROX_ICON_OPEN);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
        g_signal_connect(menu_item, "activate",
                         G_CALLBACK(desktop_item_menu_open), item);

        if (!desktop_item_is_builtin(item)) {
            menu_item = gtk_separator_menu_item_new();
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);

            menu_item = menu_item_new_with_icon(_("_Move to Trash"), ROX_ICON_TRASH);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
            g_signal_connect(menu_item, "activate",
                             G_CALLBACK(desktop_item_menu_remove), item);
        }
    }

    g_signal_connect_swapped(menu, "selection-done",
                             G_CALLBACK(gtk_widget_destroy), menu);
    gtk_menu_attach_to_widget(GTK_MENU(menu), item->widget, NULL);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
}

static void desktop_item_snap(DesktopItem *item)
{
    gint min_x;
    gint min_y;
    gint step_x = MAX(100, desktop_icon_size + 52);
    gint step_y = MAX(88, desktop_icon_size + 52);

    if (!desktop_snap_to_grid || !item)
        return;
    desktop_item_bounds(&min_x, &min_y, NULL, NULL,
                        MAX(96, desktop_icon_size + 48),
                        MAX(78, desktop_icon_size + 46));
    item->x = min_x + ((item->x - min_x + step_x / 2) / step_x) * step_x;
    item->y = min_y + ((item->y - min_y + step_y / 2) / step_y) * step_y;
    desktop_item_clamp(item);
}

static void desktop_prepare_selected_drag(DesktopItem *anchor,
                                          GdkEventButton *event)
{
    GList *node;

    anchor->press_root_x = (gint)event->x_root;
    anchor->press_root_y = (gint)event->y_root;
    for (node = desktop_items; node; node = node->next) {
        DesktopItem *item = node->data;
        if (!item || !item->selected)
            continue;
        item->start_x = item->x;
        item->start_y = item->y;
        item->dragging = FALSE;
    }
}

static gboolean desktop_item_button_press(GtkWidget *widget,
                                          GdkEventButton *event,
                                          gpointer data)
{
    DesktopItem *item = data;
    gboolean extend;

    if (!event)
        return FALSE;

    gtk_widget_grab_focus(widget);

    if (event->button == 3 && event->type == GDK_BUTTON_PRESS) {
        if (!item->selected)
            desktop_item_set_selected(item, FALSE, FALSE);
        show_desktop_item_menu(item, event);
        return TRUE;
    }
    if (event->button != 1)
        return FALSE;

    extend = desktop_extend_selection(event->state);
    if (extend) {
        desktop_item_set_selected(item, TRUE, TRUE);
        if (!item->selected)
            return TRUE;
    } else if (!item->selected) {
        desktop_item_set_selected(item, FALSE, FALSE);
    }

    desktop_prepare_selected_drag(item, event);

    if (!desktop_single_click && !extend &&
        event->type == GDK_2BUTTON_PRESS) {
        desktop_item_activate(item);
        desktop_release_interaction_state();
        return TRUE;
    }
    return TRUE;
}

static gboolean desktop_item_motion(GtkWidget *widget,
                                    GdkEventMotion *event,
                                    gpointer data)
{
    DesktopItem *anchor = data;
    GList *node;
    gint dx;
    gint dy;
    (void)widget;

    if (!event || !(event->state & GDK_BUTTON1_MASK))
        return FALSE;

    dx = (gint)event->x_root - anchor->press_root_x;
    dy = (gint)event->y_root - anchor->press_root_y;
    if (!anchor->dragging &&
        ABS(dx) < DESKTOP_DRAG_THRESHOLD &&
        ABS(dy) < DESKTOP_DRAG_THRESHOLD)
        return TRUE;

    anchor->dragging = TRUE;
    for (node = desktop_items; node; node = node->next) {
        DesktopItem *item = node->data;
        if (!item || !item->selected)
            continue;
        item->x = item->start_x + dx;
        item->y = item->start_y + dy;
        desktop_item_clamp(item);
        gtk_fixed_move(GTK_FIXED(desktop_icon_layer), item->widget,
                       item->x, item->y);
    }
    return TRUE;
}

static gboolean desktop_item_button_release(GtkWidget *widget,
                                            GdkEventButton *event,
                                            gpointer data)
{
    DesktopItem *anchor = data;
    GList *node;
    (void)widget;

    if (!event || event->button != 1)
        return FALSE;

    if (anchor->dragging) {
        gint before_x = anchor->x;
        gint before_y = anchor->y;
        gint snap_dx;
        gint snap_dy;

        desktop_item_snap(anchor);
        snap_dx = anchor->x - before_x;
        snap_dy = anchor->y - before_y;

        for (node = desktop_items; node; node = node->next) {
            DesktopItem *item = node->data;
            if (!item || !item->selected)
                continue;
            if (item != anchor) {
                item->x += snap_dx;
                item->y += snap_dy;
                desktop_item_clamp(item);
            }
            gtk_fixed_move(GTK_FIXED(desktop_icon_layer), item->widget,
                           item->x, item->y);
            desktop_item_save_position(item);
            item->dragging = FALSE;
        }
        desktop_reflow_items(TRUE);
    } else if (desktop_single_click &&
               !desktop_extend_selection(event->state)) {
        desktop_item_activate(anchor);
        desktop_release_interaction_state();
    }
    return TRUE;
}

static gboolean desktop_item_key_press(GtkWidget *widget,
                                       GdkEventKey *event,
                                       gpointer data)
{
    DesktopItem *item = data;
    (void)widget;

    if (!event)
        return FALSE;

    switch (event->keyval) {
        case GDK_KEY_Delete:
        case GDK_KEY_KP_Delete:
            if (item && item->trash)
                rox_trash_empty(desktop_window ? GTK_WINDOW(desktop_window) : NULL);
            else if (item && !desktop_item_is_builtin(item))
                desktop_remove_selected_items(item);
            return TRUE;
        case GDK_KEY_Return:
        case GDK_KEY_KP_Enter:
            desktop_activate_selected_items(item);
            return TRUE;
        case GDK_KEY_Escape:
            desktop_clear_selection();
            return TRUE;
        default:
            return FALSE;
    }
}

/* Apply the same visible hover feedback used by drive/partition
 * buttons to every normal desktop object (files, launchers, Home and Trash).
 * GtkEventBox does not reliably enter GTK_STATE_FLAG_PRELIGHT on every
 * GTK3 backend/theme, so use an explicit CSS class on enter/leave. */
static gboolean desktop_item_enter_notify(GtkWidget *widget,
                                          GdkEventCrossing *event,
                                          gpointer data)
{
    (void)event;
    (void)data;
    gtk_style_context_add_class(gtk_widget_get_style_context(widget),
                                "rox-desktop-item-hover");
    return FALSE;
}

static gboolean desktop_item_leave_notify(GtkWidget *widget,
                                          GdkEventCrossing *event,
                                          gpointer data)
{
    (void)event;
    (void)data;
    gtk_style_context_remove_class(gtk_widget_get_style_context(widget),
                                   "rox-desktop-item-hover");
    return FALSE;
}

static GtkWidget *desktop_item_widget_new(GdkPixbuf *icon,
                                          const gchar *name,
                                          DesktopItem *item)
{
    GtkWidget *event_box;
    GtkWidget *box;
    GtkWidget *image;
    GtkWidget *label;

    event_box = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(event_box), TRUE);
    gtk_widget_set_can_focus(event_box, TRUE);
    gtk_widget_set_size_request(event_box,
                                MAX(96, desktop_icon_size + 48),
                                MAX(78, desktop_icon_size + 46));
    gtk_widget_add_events(event_box,
        GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
        GDK_POINTER_MOTION_MASK | GDK_BUTTON1_MOTION_MASK |
        GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
    gtk_style_context_add_class(gtk_widget_get_style_context(event_box),
                                "rox-desktop-item");

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_add(GTK_CONTAINER(event_box), box);

    image = icon ? gtk_image_new_from_pixbuf(icon)
                 : gtk_image_new_from_icon_name("text-x-generic",
                                                GTK_ICON_SIZE_DIALOG);
    if (item)
        item->image = image;
    if (!icon)
        gtk_image_set_pixel_size(GTK_IMAGE(image), desktop_icon_size);
    gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);

    label = gtk_label_new(name ? name : "");
    gtk_style_context_add_class(gtk_widget_get_style_context(label),
                                "rox-desktop-label");
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars(GTK_LABEL(label), DESKTOP_ICON_LABEL_WIDTH);
    gtk_label_set_lines(GTK_LABEL(label), 2);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);

    g_signal_connect(event_box, "button-press-event",
                     G_CALLBACK(desktop_item_button_press), item);
    g_signal_connect(event_box, "button-release-event",
                     G_CALLBACK(desktop_item_button_release), item);
    g_signal_connect(event_box, "motion-notify-event",
                     G_CALLBACK(desktop_item_motion), item);
    g_signal_connect(event_box, "enter-notify-event",
                     G_CALLBACK(desktop_item_enter_notify), item);
    g_signal_connect(event_box, "leave-notify-event",
                     G_CALLBACK(desktop_item_leave_notify), item);
    g_signal_connect(event_box, "key-press-event",
                     G_CALLBACK(desktop_item_key_press), item);
    return event_box;
}

static gint desktop_sort_names(gconstpointer a, gconstpointer b)
{
    return g_utf8_collate(*(const gchar * const *)a,
                          *(const gchar * const *)b);
}

static void desktop_add_builtin_item(DesktopItem *item, GIcon *gicon)
{
    GdkPixbuf *icon;

    if (!desktop_icon_layer || !item || !gicon) {
        if (item)
            desktop_item_free(item);
        if (gicon)
            g_object_unref(gicon);
        return;
    }

    icon = pixbuf_from_gicon(gicon, desktop_icon_size);
    item->widget = desktop_item_widget_new(icon, item->display_name, item);
    desktop_items = g_list_append(desktop_items, item);

    /* Built-in desktop objects have a deterministic priority order.
     * Do not reuse an old saved coordinate here: Home must always be the
     * first desktop icon when enabled, followed by Browser, Console and
     * Trash. Normal ~/Desktop files are added afterwards and reflow around
     * these reserved first slots. */
    desktop_find_free_position(item);
    desktop_item_save_position(item);
    desktop_item_clamp(item);
    gtk_fixed_put(GTK_FIXED(desktop_icon_layer), item->widget,
                  item->x, item->y);

    g_clear_object(&icon);
    g_object_unref(gicon);
}

/* Rox-Filer2: Home is a built-in desktop item, like Trash.
 * It points to the real user home directory but is not itself a file in
 * ~/Desktop. Its icon comes from the active GTK/Freedesktop icon theme. */
static void add_desktop_home(void)
{
    DesktopItem *item = g_new0(DesktopItem, 1);
    GFile *file = g_file_new_for_path(g_get_home_dir());
    GIcon *gicon;

    item->uri = g_file_get_uri(file);
    item->display_name = g_strdup(_("Home"));
    item->home = TRUE;
    gicon = g_themed_icon_new("user-home");
    g_themed_icon_append_name(G_THEMED_ICON(gicon), "folder-home");
    g_themed_icon_append_name(G_THEMED_ICON(gicon), "folder");
    desktop_add_builtin_item(item, gicon);
    g_object_unref(file);
}

static void add_desktop_browser(void)
{
    DesktopItem *item = g_new0(DesktopItem, 1);
    GIcon *gicon;

    item->uri = g_strdup("rox-special://browser");
    item->display_name = g_strdup(_("Browser"));
    item->browser = TRUE;
    gicon = g_themed_icon_new("web-browser");
    g_themed_icon_append_name(G_THEMED_ICON(gicon), "internet-web-browser");
    g_themed_icon_append_name(G_THEMED_ICON(gicon), "applications-internet");
    desktop_add_builtin_item(item, gicon);
}

static void add_desktop_console(void)
{
    DesktopItem *item = g_new0(DesktopItem, 1);
    GIcon *gicon;

    item->uri = g_strdup("rox-special://console");
    item->display_name = g_strdup(_("Console"));
    item->console = TRUE;
    gicon = g_themed_icon_new("utilities-terminal");
    g_themed_icon_append_name(G_THEMED_ICON(gicon), "terminal");
    g_themed_icon_append_name(G_THEMED_ICON(gicon), "xterm");
    desktop_add_builtin_item(item, gicon);
}

static void add_desktop_trash(void)
{
    DesktopItem *item = g_new0(DesktopItem, 1);
    GIcon *gicon;

    item->uri = g_strdup("trash:///");
    item->display_name = g_strdup(_("Trash"));
    item->trash = TRUE;
    gicon = g_themed_icon_new(rox_trash_icon_name());
    g_themed_icon_append_name(G_THEMED_ICON(gicon), "user-trash");
    desktop_add_builtin_item(item, gicon);
}

static void add_desktop_files(void)
{
    GFile *dir;
    GFileEnumerator *en;
    GFileInfo *info;
    GPtrArray *names;
    guint i;

    if (!desktop_icon_layer || !desktop_dir)
        return;

    dir = g_file_new_for_path(desktop_dir);
    en = g_file_enumerate_children(dir,
        G_FILE_ATTRIBUTE_STANDARD_NAME,
        G_FILE_QUERY_INFO_NONE, NULL, NULL);
    names = g_ptr_array_new_with_free_func(g_free);
    if (en) {
        while ((info = g_file_enumerator_next_file(en, NULL, NULL))) {
            const gchar *name = g_file_info_get_name(info);
            if (name && *name)
                g_ptr_array_add(names, g_strdup(name));
            g_object_unref(info);
        }
        g_object_unref(en);
    }
    g_ptr_array_sort(names, desktop_sort_names);

    for (i = 0; i < names->len; i++) {
        const gchar *name = g_ptr_array_index(names, i);
        GFile *child = g_file_get_child(dir, name);
        gchar *uri = g_file_get_uri(child);
        gchar *local_path = g_file_get_path(child);
        gchar *launcher_name = NULL;
        GIcon *launcher_icon = NULL;
        GdkPixbuf *icon = NULL;
        GFileInfo *display_info;
        const gchar *display_name = name;
        gboolean launcher = FALSE;
        DesktopItem *item;

        display_info = g_file_query_info(child,
            G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
            G_FILE_QUERY_INFO_NONE, NULL, NULL);
        if (display_info && g_file_info_get_display_name(display_info))
            display_name = g_file_info_get_display_name(display_info);

        if (local_path && g_str_has_suffix(local_path, ".desktop"))
            launcher = desktop_app_get_metadata(local_path,
                                                &launcher_name,
                                                &launcher_icon);
        if (launcher_icon)
            icon = pixbuf_from_gicon(launcher_icon, desktop_icon_size);
        if (!icon)
            icon = icon_for_file(child, desktop_icon_size);

        item = g_new0(DesktopItem, 1);
        item->uri = g_strdup(uri);
        item->display_name = g_strdup(
            launcher_name ? launcher_name : display_name);
        item->launcher = launcher;
        item->widget = desktop_item_widget_new(icon,
            item->display_name, item);
        desktop_items = g_list_append(desktop_items, item);

        if (!desktop_item_load_position(item)) {
            desktop_find_free_position(item);
            desktop_item_save_position(item);
        }
        desktop_item_clamp(item);
        gtk_fixed_put(GTK_FIXED(desktop_icon_layer), item->widget,
                      item->x, item->y);

        g_clear_object(&launcher_icon);
        g_clear_object(&icon);
        g_free(launcher_name);
        g_clear_object(&display_info);
        g_free(local_path);
        g_free(uri);
        g_object_unref(child);
    }

    g_ptr_array_free(names, TRUE);
    g_object_unref(dir);
}

static gchar *desktop_drive_signature_from_list(GPtrArray *drives)
{
    GString *signature;
    guint i;

    signature = g_string_new(NULL);
    for (i = 0; drives && i < drives->len; i++) {
        RoxDriveInfo *drive = g_ptr_array_index(drives, i);
        g_string_append_printf(signature,
            "%s|%s|%s|%s|%d|%d|%d;",
            drive->device ? drive->device : "",
            drive->mountpoint ? drive->mountpoint : "",
            drive->transport ? drive->transport : "",
            drive->type ? drive->type : "",
            drive->removable, drive->optical, drive->solid_state);
    }
    return g_string_free(signature, FALSE);
}

/* Agregado por josejp2424 (2026): lsblk y la consulta de sysfs no deben
 * ejecutarse en el hilo de GTK. En algunos equipos o dispositivos lentos,
 * el sondeo síncrono bloqueaba el menú contextual del escritorio. */
static void desktop_drive_scan_thread(GTask *task, gpointer source_object,
                                      gpointer task_data,
                                      GCancellable *cancellable)
{
    GError *error = NULL;
    GPtrArray *drives;
    (void)source_object;
    (void)task_data;
    (void)cancellable;

    drives = rox_drives_read(&error);
    if (!drives) {
        if (error)
            g_task_return_error(task, error);
        else
            g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED,
                                    "%s", _("No usable partitions found"));
        return;
    }
    g_task_return_pointer(task, drives, (GDestroyNotify)g_ptr_array_unref);
}

static void desktop_drive_scan_done(GObject *source_object,
                                    GAsyncResult *result,
                                    gpointer user_data)
{
    GPtrArray *drives;
    GError *error = NULL;
    gchar *current;
    guint serial;
    (void)source_object;
    (void)user_data;

    serial = GPOINTER_TO_UINT(g_task_get_task_data(G_TASK(result)));
    drives = g_task_propagate_pointer(G_TASK(result), &error);
    if (serial != drive_scan_serial || !desktop_window) {
        if (drives)
            g_ptr_array_unref(drives);
        g_clear_error(&error);
        return;
    }
    drive_scan_in_progress = FALSE;
    if (!drives) {
        if (error) {
            g_warning("Unable to scan desktop drives: %s", error->message);
            g_clear_error(&error);
        }
        return;
    }

    current = desktop_drive_signature_from_list(drives);
    if (g_strcmp0(current, drive_signature) != 0 || !drive_signature) {
        g_free(drive_signature);
        drive_signature = current;
        desktop_rebuild_drive_box_from_list(drives);
    } else {
        g_free(current);
    }
    g_ptr_array_unref(drives);
}

static void desktop_request_drive_scan(void)
{
    GTask *task;

    if (!desktop_window || drive_scan_in_progress)
        return;
    drive_scan_in_progress = TRUE;
    drive_scan_serial++;
    task = g_task_new(NULL, NULL, desktop_drive_scan_done, NULL);
    g_task_set_task_data(task, GUINT_TO_POINTER(drive_scan_serial), NULL);
    g_task_set_return_on_cancel(task, TRUE);
    g_task_run_in_thread(task, desktop_drive_scan_thread);
    g_object_unref(task);
}

static void desktop_force_drive_refresh(void)
{
    g_clear_pointer(&drive_signature, g_free);
    desktop_request_drive_scan();
}

static gboolean desktop_drive_poll(gpointer data)
{
    (void)data;
    desktop_request_drive_scan();
    return G_SOURCE_CONTINUE;
}

static void desktop_rebuild_icon_layer(void)
{
    if (!desktop_icon_layer)
        return;
    desktop_items_clear();

    /* Built-ins are deliberately inserted before normal Desktop files.
     * This guarantees their visual priority: Home first, then Browser,
     * Console and finally Trash. */
    if (desktop_show_home)
        add_desktop_home();
    if (desktop_show_browser)
        add_desktop_browser();
    if (desktop_show_console)
        add_desktop_console();
    if (desktop_show_trash)
        add_desktop_trash();

    add_desktop_files();
    gtk_widget_show_all(desktop_icon_layer);
    desktop_reflow_items(TRUE);
}

static void desktop_reload(void)
{
    desktop_rebuild_icon_layer();
    desktop_force_drive_refresh();
}

static gboolean desktop_reload_idle(gpointer data)
{
    (void)data;
    desktop_reload();
    return G_SOURCE_REMOVE;
}

static void monitor_changed(GFileMonitor *m, GFile *f, GFile *other,
                            GFileMonitorEvent event, gpointer data)
{
    (void)m; (void)f; (void)other; (void)event; (void)data;
    /* Modificado por josejp2424 (2026): un cambio en XDG Desktop sólo
     * requiere reconstruir sus iconos; no volver a ejecutar lsblk. */
    desktop_rebuild_icon_layer();
}

static void volume_changed(GVolumeMonitor *m, gpointer object, gpointer data)
{
    (void)m; (void)object; (void)data;
    desktop_force_drive_refresh();
}

static RoxDriveInfo *desktop_lookup_drive(const gchar *device)
{
    GError *error = NULL;
    RoxDriveInfo *drive;

    drive = rox_drive_find_by_device(device, &error);
    if (error) {
        show_desktop_error(_("Unable to read the device"), error->message);
        g_clear_error(&error);
    }
    return drive;
}

static void desktop_open_drive_device(const gchar *device)
{
    RoxDriveInfo *drive;
    gchar *mountpoint;
    gchar *error_text = NULL;

    drive = desktop_lookup_drive(device);
    if (!drive)
        return;

    mountpoint = rox_drive_find_mountpoint(drive->device);
    if (!mountpoint)
        mountpoint = rox_drive_mount(drive, &error_text);

    if (!mountpoint) {
        show_desktop_error(_("The partition could not be mounted."), error_text);
    } else {
        filer_opendir(mountpoint, NULL, NULL);
    }

    g_free(error_text);
    g_free(mountpoint);
    rox_drive_info_free(drive);
    desktop_force_drive_refresh();
}


static void desktop_drive_action_free(gpointer data)
{
    DesktopDriveAction *action = data;
    if (!action)
        return;
    g_free(action->device);
    g_free(action);
}

static void desktop_drive_open_menu(GtkMenuItem *item, gpointer data)
{
    DesktopDriveAction *action = data;
    (void)item;
    desktop_open_drive_device(action->device);
}

static void desktop_drive_mount_menu(GtkMenuItem *item, gpointer data)
{
    DesktopDriveAction *action = data;
    RoxDriveInfo *drive;
    gchar *mountpoint;
    gchar *error_text = NULL;
    (void)item;

    drive = desktop_lookup_drive(action->device);
    if (!drive)
        return;
    mountpoint = rox_drive_mount(drive, &error_text);
    if (!mountpoint)
        show_desktop_error(_("The partition could not be mounted."), error_text);
    g_free(mountpoint);
    g_free(error_text);
    rox_drive_info_free(drive);
    desktop_force_drive_refresh();
}

static void desktop_drive_unmount_menu(GtkMenuItem *item, gpointer data)
{
    DesktopDriveAction *action = data;
    RoxDriveInfo *drive;
    gchar *error_text = NULL;
    (void)item;

    drive = desktop_lookup_drive(action->device);
    if (!drive)
        return;
    if (!rox_drive_unmount(drive, &error_text))
        show_desktop_error(_("The partition could not be unmounted."), error_text);
    g_free(error_text);
    rox_drive_info_free(drive);
    desktop_force_drive_refresh();
}

static void desktop_drive_eject_menu(GtkMenuItem *item, gpointer data)
{
    DesktopDriveAction *action = data;
    RoxDriveInfo *drive;
    gchar *error_text = NULL;
    (void)item;

    drive = desktop_lookup_drive(action->device);
    if (!drive)
        return;
    if (!rox_drive_eject(drive, &error_text))
        show_desktop_error(_("The device could not be ejected."), error_text);
    g_free(error_text);
    rox_drive_info_free(drive);
    desktop_force_drive_refresh();
}

static void show_drive_menu(const gchar *device, GdkEventButton *event)
{
    RoxDriveInfo *drive;
    DesktopDriveAction *action;
    GtkWidget *menu;
    GtkWidget *item;
    gchar *mountpoint;
    gboolean mounted;

    drive = desktop_lookup_drive(device);
    if (!drive)
        return;
    mountpoint = rox_drive_find_mountpoint(device);
    mounted = mountpoint != NULL;
    g_free(mountpoint);

    menu = rox_menu_new();
    action = g_new0(DesktopDriveAction, 1);
    action->device = g_strdup(device);
    g_object_set_data_full(G_OBJECT(menu), "rox-desktop-drive-action",
        action, desktop_drive_action_free);

    item = gtk_menu_item_new_with_label(_("Open"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect(item, "activate", G_CALLBACK(desktop_drive_open_menu), action);

    if (!mounted) {
        item = gtk_menu_item_new_with_label(_("Mount"));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        g_signal_connect(item, "activate", G_CALLBACK(desktop_drive_mount_menu), action);
    } else {
        item = gtk_menu_item_new_with_label(_("Unmount"));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        g_signal_connect(item, "activate", G_CALLBACK(desktop_drive_unmount_menu), action);
    }

    /* Modificado por josejp2424 (2026): Eject sólo pertenece a unidades
     * ópticas. Los USB muestran Mount/Unmount, no una acción óptica. */
    if (drive->optical) {
        item = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        item = gtk_menu_item_new_with_label(_("Eject"));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        g_signal_connect(item, "activate", G_CALLBACK(desktop_drive_eject_menu), action);
    }

    g_signal_connect_swapped(menu, "selection-done",
        G_CALLBACK(gtk_widget_destroy), menu);
    gtk_menu_attach_to_widget(GTK_MENU(menu), desktop_window, NULL);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
    rox_drive_info_free(drive);
}

static gboolean desktop_drive_button_press(GtkWidget *widget,
                                           GdkEventButton *event,
                                           gpointer data)
{
    DesktopDriveAction *action = data;
    (void)widget;

    if (!event || event->type != GDK_BUTTON_PRESS || event->button != 3)
        return FALSE;
    show_drive_menu(action->device, event);
    return TRUE;
}

static void desktop_drive_button_clicked(GtkButton *button, gpointer data)
{
    DesktopDriveAction *action = data;
    (void)button;
    desktop_open_drive_device(action->device);
}

/* Agregado por josejp2424 (2026): el pequeño botón de la esquina superior
 * derecha desmonta una unidad montada. En medios ópticos ejecuta Eject. */
static void desktop_drive_quick_action(GtkButton *button, gpointer data)
{
    DesktopDriveAction *action = data;
    RoxDriveInfo *drive;
    gchar *error_text = NULL;
    gboolean ok;
    (void)button;

    drive = desktop_lookup_drive(action->device);
    if (!drive)
        return;

    if (drive->optical)
        ok = rox_drive_eject(drive, &error_text);
    else
        ok = rox_drive_unmount(drive, &error_text);

    if (!ok)
        show_desktop_error(drive->optical
            ? _("The device could not be ejected.")
            : _("The partition could not be unmounted."), error_text);

    g_free(error_text);
    rox_drive_info_free(drive);
    desktop_force_drive_refresh();
}

static GtkWidget *desktop_drive_widget_new(const RoxDriveInfo *drive)
{
    GtkWidget *overlay;
    GtkWidget *button;
    GtkWidget *box;
    GtkWidget *image;
    GtkWidget *label;
    GtkWidget *quick_button;
    GtkWidget *quick_image;
    DesktopDriveAction *action;
    gchar *mountpoint;
    gchar *text;
    gboolean mounted;

    overlay = gtk_overlay_new();
    gtk_style_context_add_class(gtk_widget_get_style_context(overlay),
                                "rox-desktop-drive");
    if (drive_show_frame)
        gtk_style_context_add_class(gtk_widget_get_style_context(overlay),
                                    "rox-desktop-drive-framed");
    gtk_widget_set_size_request(overlay, drive_spacing_x, drive_spacing_y);

    action = g_new0(DesktopDriveAction, 1);
    action->device = g_strdup(drive->device);
    g_object_set_data_full(G_OBJECT(overlay), "rox-desktop-drive-action",
                           action, desktop_drive_action_free);

    button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    if (drive_show_frame)
        gtk_style_context_add_class(gtk_widget_get_style_context(button),
                                    "rox-desktop-drive-framed");
    gtk_widget_set_hexpand(button, TRUE);
    gtk_widget_set_vexpand(button, TRUE);
    gtk_widget_add_events(button, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(button, "clicked",
                     G_CALLBACK(desktop_drive_button_clicked), action);
    g_signal_connect(button, "button-press-event",
                     G_CALLBACK(desktop_drive_button_press), action);

    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_container_add(GTK_CONTAINER(button), box);

    /* Modificado por josejp2424 (2026): usar el mismo GIcon compartido que
     * la GUI de Particiones. */
    image = rox_drive_icon_widget_new(drive, drive_icon_size);
    gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);

    if (drive_show_labels) {
        if (drive->size && *drive->size)
            text = g_strdup_printf("%s\n%s", rox_drive_display_name(drive),
                                   drive->size);
        else
            text = g_strdup(rox_drive_display_name(drive));
        label = gtk_label_new(text);
        gtk_style_context_add_class(gtk_widget_get_style_context(label),
                                    "rox-desktop-drive-label");
        gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
        gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
        gtk_label_set_max_width_chars(GTK_LABEL(label),
                                      MAX(6, drive_spacing_x / 8));
        gtk_label_set_lines(GTK_LABEL(label), 2);
        gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
        g_free(text);
    }

    gtk_container_add(GTK_CONTAINER(overlay), button);

    mountpoint = rox_drive_find_mountpoint(drive->device);
    mounted = mountpoint != NULL;
    g_free(mountpoint);

    if (mounted && show_drive_quick_action) {
        quick_button = gtk_button_new();
        gtk_button_set_relief(GTK_BUTTON(quick_button), GTK_RELIEF_NONE);
        gtk_widget_set_halign(quick_button, GTK_ALIGN_END);
        gtk_widget_set_valign(quick_button, GTK_ALIGN_START);
        gtk_widget_set_margin_top(quick_button, 1);
        gtk_widget_set_margin_end(quick_button, 1);
        gtk_widget_set_size_request(quick_button, 24, 24);
        gtk_style_context_add_class(gtk_widget_get_style_context(quick_button),
                                    "rox-drive-quick");

        quick_image = gtk_image_new_from_icon_name("media-eject-symbolic",
                                                   GTK_ICON_SIZE_MENU);
        if (!gtk_icon_theme_has_icon(gtk_icon_theme_get_default(),
                                     "media-eject-symbolic"))
            gtk_image_set_from_icon_name(GTK_IMAGE(quick_image), "media-eject",
                                         GTK_ICON_SIZE_MENU);
        gtk_container_add(GTK_CONTAINER(quick_button), quick_image);
        gtk_widget_set_tooltip_text(quick_button,
            drive->optical ? _("Eject") : _("Unmount"));
        g_signal_connect(quick_button, "clicked",
                         G_CALLBACK(desktop_drive_quick_action), action);
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), quick_button);
        gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(overlay),
                                             quick_button, FALSE);
    }

    gtk_widget_set_tooltip_text(button,
        mounted ? _("Open mounted volume") : _("Mount and open volume"));
    return overlay;
}

static gboolean desktop_drive_is_visible(const RoxDriveInfo *drive)
{
    gboolean removable;

    if (!drive)
        return FALSE;
    if (drive->network)
        return drive_show_network;

    removable = drive->removable || drive->optical ||
        (drive->transport &&
         (!g_ascii_strcasecmp(drive->transport, "usb") ||
          !g_ascii_strcasecmp(drive->transport, "mmc") ||
          !g_ascii_strcasecmp(drive->transport, "sd")));
    return removable ? drive_show_removable : drive_show_internal;
}

/* Calcular la posición con el mismo concepto usado por EssoraWM: XPos/YPos
 * son anclas normalizadas dentro del área útil y XOffset/YOffset permiten el
 * ajuste fino. El tamaño completo del grupo se descuenta cuando el ancla está
 * en el borde derecho o inferior. */
static void desktop_calculate_drive_rect(gint width, gint height,
                                         GdkRectangle *rect)
{
    gint left;
    gint top;
    gint right;
    gint bottom;
    gint anchor_x;
    gint anchor_y;
    gint x;
    gint y;

    g_return_if_fail(rect != NULL);

    left = MAX(0, desktop_workarea.x - desktop_geometry.x);
    top = MAX(0, desktop_workarea.y - desktop_geometry.y);
    right = left + desktop_workarea.width;
    bottom = top + desktop_workarea.height;

    anchor_x = left + (gint)(drive_x_pos * desktop_workarea.width) +
               drive_x_offset;
    anchor_y = top + (gint)(drive_y_pos * desktop_workarea.height) +
               drive_y_offset;

    x = drive_x_pos > 0.5 ? anchor_x - width : anchor_x;
    y = drive_y_pos > 0.5 ? anchor_y - height : anchor_y;

    x = CLAMP(x, left, MAX(left, right - width));
    y = CLAMP(y, top, MAX(top, bottom - height));

    *rect = (GdkRectangle){x, y, MAX(0, width), MAX(0, height)};
}

static void desktop_update_drive_reservation(void)
{
    GtkRequisition minimum;
    GtkRequisition natural;
    GList *children;
    GdkRectangle rect;
    gint width;
    gint height;
    gint padding = 6;

    desktop_drive_reserved = (GdkRectangle){0, 0, 0, 0};
    if (!desktop_drive_box || !show_volumes)
        return;

    children = gtk_container_get_children(GTK_CONTAINER(desktop_drive_box));
    if (!children)
        return;
    g_list_free(children);

    gtk_widget_get_preferred_size(desktop_drive_box, &minimum, &natural);
    width = MAX(minimum.width, natural.width);
    height = MAX(minimum.height, natural.height);
    if (width <= 0 || height <= 0)
        return;

    desktop_calculate_drive_rect(width, height, &rect);
    desktop_drive_reserved.x = MAX(0, rect.x - padding);
    desktop_drive_reserved.y = MAX(0, rect.y - padding);
    desktop_drive_reserved.width = MIN(desktop_geometry.width -
                                       desktop_drive_reserved.x,
                                       rect.width + padding * 2);
    desktop_drive_reserved.height = MIN(desktop_geometry.height -
                                        desktop_drive_reserved.y,
                                        rect.height + padding * 2);
}

static void desktop_apply_drive_layout(void)
{
    GtkRequisition minimum;
    GtkRequisition natural;
    GdkRectangle rect;

    if (!desktop_drive_box || !desktop_drive_layer)
        return;

    gtk_orientable_set_orientation(GTK_ORIENTABLE(desktop_drive_box),
                                   drive_orientation);
    /* Cada widget ocupa su propia celda SpacingX x SpacingY. No se añade una
     * separación extra de GtkBox porque duplicaría la distancia configurada. */
    gtk_box_set_spacing(GTK_BOX(desktop_drive_box), 0);
    gtk_widget_queue_resize(desktop_drive_box);

    gtk_widget_get_preferred_size(desktop_drive_box, &minimum, &natural);
    desktop_calculate_drive_rect(MAX(minimum.width, natural.width),
                                 MAX(minimum.height, natural.height), &rect);
    gtk_fixed_move(GTK_FIXED(desktop_drive_layer), desktop_drive_box,
                   rect.x, rect.y);

    desktop_update_drive_reservation();
    desktop_reflow_items(TRUE);
}

static void desktop_rebuild_drive_box_from_list(GPtrArray *drives)
{
    GList *children;
    guint n;
    guint visible = 0;

    if (!desktop_drive_box)
        return;

    children = gtk_container_get_children(GTK_CONTAINER(desktop_drive_box));
    g_list_free_full(children, (GDestroyNotify)gtk_widget_destroy);

    if (!show_volumes) {
        gtk_widget_hide(desktop_drive_box);
        desktop_drive_reserved = (GdkRectangle){0, 0, 0, 0};
        desktop_reflow_items(TRUE);
        return;
    }

    for (n = 0; drives && n < drives->len; n++) {
        gboolean reverse_order;
        guint i;
        RoxDriveInfo *drive;
        GtkWidget *widget;

        /* Igualar el empaquetado de EssoraWM. En horizontal el primer disco
         * permanece a la izquierda cuando XPos está en el borde izquierdo y
         * a la derecha cuando se ancla al borde derecho. ReversePack se usa
         * para la columna vertical ascendente desde la parte inferior. */
        reverse_order = drive_orientation == GTK_ORIENTATION_VERTICAL
            ? drive_reverse_pack : drive_x_pos > 0.5;
        i = reverse_order ? drives->len - 1 - n : n;
        drive = g_ptr_array_index(drives, i);

        if (!desktop_drive_is_visible(drive))
            continue;
        widget = desktop_drive_widget_new(drive);
        gtk_box_pack_start(GTK_BOX(desktop_drive_box), widget,
                           FALSE, FALSE, 0);
        visible++;
    }

    if (visible == 0) {
        gtk_widget_hide(desktop_drive_box);
        desktop_drive_reserved = (GdkRectangle){0, 0, 0, 0};
        desktop_reflow_items(TRUE);
        return;
    }

    gtk_widget_show_all(desktop_drive_box);
    desktop_apply_drive_layout();
}

static gboolean wallpaper_name_supported(const gchar *name)
{
    gchar *lower;
    gboolean supported;

    if (!name)
        return FALSE;
    lower = g_ascii_strdown(name, -1);
    supported = g_str_has_suffix(lower, ".jpg") ||
                g_str_has_suffix(lower, ".jpeg") ||
                g_str_has_suffix(lower, ".png") ||
                g_str_has_suffix(lower, ".svg") ||
                g_str_has_suffix(lower, ".webp");
    g_free(lower);
    return supported;
}

static void wallpaper_store_add(GtkListStore *store, const gchar *path)
{
    GdkPixbuf *pixbuf;
    GtkTreeIter iter;
    gchar *name;

    pixbuf = gdk_pixbuf_new_from_file_at_scale(path, 180, 110, TRUE, NULL);
    if (!pixbuf)
        return;
    name = g_path_get_basename(path);
    gtk_list_store_append(store, &iter);
    gtk_list_store_set(store, &iter,
        WP_COL_PIXBUF, pixbuf,
        WP_COL_NAME, name,
        WP_COL_PATH, path,
        -1);
    g_object_unref(pixbuf);
    g_free(name);
}

static void wallpaper_scan_directory(GtkListStore *store, const gchar *directory,
                                     guint depth)
{
    GDir *dir;
    const gchar *name;

    if (depth > 2)
        return;
    dir = g_dir_open(directory, 0, NULL);
    if (!dir)
        return;

    while ((name = g_dir_read_name(dir)) != NULL) {
        gchar *path = g_build_filename(directory, name, NULL);
        if (g_file_test(path, G_FILE_TEST_IS_DIR))
            wallpaper_scan_directory(store, path, depth + 1);
        else if (wallpaper_name_supported(name))
            wallpaper_store_add(store, path);
        g_free(path);
    }
    g_dir_close(dir);
}

static gchar *wallpaper_selected_path(GtkIconView *view)
{
    GList *selected;
    gchar *path = NULL;

    selected = gtk_icon_view_get_selected_items(view);
    if (selected) {
        GtkTreeIter iter;
        GtkTreePath *tree_path = selected->data;
        if (gtk_tree_model_get_iter(gtk_icon_view_get_model(view), &iter,
                                    tree_path))
            gtk_tree_model_get(gtk_icon_view_get_model(view), &iter,
                               WP_COL_PATH, &path, -1);
    }
    g_list_free_full(selected, (GDestroyNotify)gtk_tree_path_free);
    return path;
}

static void wallpaper_activate(GtkIconView *view, GtkTreePath *path,
                               gpointer data)
{
    GtkDialog *dialog = data;
    (void)view; (void)path;
    gtk_dialog_response(dialog, GTK_RESPONSE_APPLY);
}

static void desktop_show_wallpaper_dialog(GtkWindow *parent)
{
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *label;
    GtkWidget *style_box;
    GtkWidget *style_label;
    GtkWidget *style_combo;
    GtkWidget *scrolled;
    GtkWidget *view;
    GtkListStore *store;
    gboolean running = TRUE;

    dialog = gtk_dialog_new_with_buttons(_("Desktop Background"), parent,
        GTK_DIALOG_DESTROY_WITH_PARENT,
        _("_Close"), GTK_RESPONSE_CLOSE,
        _("Choose _Image..."), GTK_RESPONSE_ACCEPT,
        _("_Apply"), GTK_RESPONSE_APPLY,
        NULL);
    gtk_window_set_position(GTK_WINDOW(dialog), parent ?
        GTK_WIN_POS_CENTER_ON_PARENT : GTK_WIN_POS_CENTER_ALWAYS);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 760, 500);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_APPLY);

    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 10);
    label = gtk_label_new(_("Select a wallpaper from /usr/share/backgrounds"));
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 4);

    style_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    style_label = gtk_label_new(_("Wallpaper style:"));
    gtk_label_set_xalign(GTK_LABEL(style_label), 0.0);
    gtk_box_pack_start(GTK_BOX(style_box), style_label, FALSE, FALSE, 0);
    style_combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(style_combo), "fill", _("Fill"));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(style_combo), "fit", _("Fit"));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(style_combo), "stretch", _("Stretch"));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(style_combo), "center", _("Center"));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(style_combo), "tile", _("Tile"));
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(style_combo),
                                wallpaper_mode_name(wallpaper_mode));
    gtk_box_pack_start(GTK_BOX(style_box), style_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), style_box, FALSE, FALSE, 2);

    store = gtk_list_store_new(WP_N_COLS, GDK_TYPE_PIXBUF,
                               G_TYPE_STRING, G_TYPE_STRING);
    wallpaper_scan_directory(store, SYSTEM_BACKGROUNDS_DIR, 0);

    view = gtk_icon_view_new_with_model(GTK_TREE_MODEL(store));
    gtk_icon_view_set_pixbuf_column(GTK_ICON_VIEW(view), WP_COL_PIXBUF);
    gtk_icon_view_set_text_column(GTK_ICON_VIEW(view), WP_COL_NAME);
    gtk_icon_view_set_item_width(GTK_ICON_VIEW(view), 190);
    gtk_icon_view_set_selection_mode(GTK_ICON_VIEW(view), GTK_SELECTION_SINGLE);
    g_signal_connect(view, "item-activated", G_CALLBACK(wallpaper_activate), dialog);

    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(content), scrolled, TRUE, TRUE, 4);
    gtk_container_add(GTK_CONTAINER(scrolled), view);

    gtk_widget_show_all(dialog);

    /* Aplicar un fondo ya no destruye el selector. El usuario puede probar
     * varias imágenes seguidas y cerrar la ventana cuando termine. */
    while (running) {
        gint response = gtk_dialog_run(GTK_DIALOG(dialog));
        gchar *selected_path = NULL;

        switch (response) {
            case GTK_RESPONSE_APPLY:
                selected_path = wallpaper_selected_path(GTK_ICON_VIEW(view));
                break;
            case GTK_RESPONSE_ACCEPT:
            {
                GtkWidget *chooser;
                GtkFileFilter *filter;

                chooser = gtk_file_chooser_dialog_new(_("Choose Desktop Background"),
                    GTK_WINDOW(dialog), GTK_FILE_CHOOSER_ACTION_OPEN,
                    _("_Cancel"), GTK_RESPONSE_CANCEL,
                    _("_Open"), GTK_RESPONSE_ACCEPT,
                    NULL);
                gtk_window_set_position(GTK_WINDOW(chooser),
                                        GTK_WIN_POS_CENTER_ON_PARENT);
                filter = gtk_file_filter_new();
                gtk_file_filter_set_name(filter, _("Image files"));
                gtk_file_filter_add_mime_type(filter, "image/jpeg");
                gtk_file_filter_add_mime_type(filter, "image/png");
                gtk_file_filter_add_mime_type(filter, "image/svg+xml");
                gtk_file_filter_add_mime_type(filter, "image/webp");
                gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(chooser), filter);
                if (gtk_dialog_run(GTK_DIALOG(chooser)) == GTK_RESPONSE_ACCEPT)
                    selected_path = gtk_file_chooser_get_filename(
                        GTK_FILE_CHOOSER(chooser));
                gtk_widget_destroy(chooser);
                break;
            }
            case GTK_RESPONSE_CLOSE:
            case GTK_RESPONSE_CANCEL:
            case GTK_RESPONSE_DELETE_EVENT:
            case GTK_RESPONSE_NONE:
            default:
                running = FALSE;
                break;
        }

        if (running && (response == GTK_RESPONSE_APPLY ||
                        response == GTK_RESPONSE_ACCEPT)) {
            const gchar *mode = gtk_combo_box_get_active_id(
                GTK_COMBO_BOX(style_combo));
            GError *error = NULL;

            wallpaper_mode = wallpaper_mode_from_name(mode);
            if (selected_path) {
                if (!desktop_set_wallpaper(selected_path, TRUE, &error)) {
                    show_desktop_error(_("Unable to set the desktop background"),
                        error ? error->message : NULL);
                    g_clear_error(&error);
                }
            } else if (response == GTK_RESPONSE_APPLY &&
                       !desktop_save_preferences(&error)) {
                show_desktop_error(_("Unable to save desktop settings"),
                    error ? error->message : NULL);
                g_clear_error(&error);
            } else if (desktop_window) {
                gtk_widget_queue_draw(desktop_window);
            }
        }

        g_free(selected_path);
    }

    gtk_widget_destroy(dialog);
    g_object_unref(store);
}

static void desktop_menu_wallpaper(GtkMenuItem *item, gpointer data)
{
    (void)item; (void)data;
    desktop_show_wallpaper_dialog(GTK_WINDOW(desktop_window));
}

static void desktop_menu_add_programs(GtkMenuItem *item, gpointer data)
{
    (void)item; (void)data;

    /* Agregado por josejp2424 (2026): gestor integrado similar al de
     * EssoraWM, pero copiando lanzadores al verdadero XDG_DESKTOP_DIR. */
    if (desktop_apps_show_manager(GTK_WINDOW(desktop_window), desktop_dir,
                                  &desktop_icon_size,
                                  &desktop_single_click)) {
        GError *error = NULL;
        if (!desktop_save_preferences(&error)) {
            show_desktop_error(_("Unable to save desktop settings"),
                               error ? error->message : NULL);
            g_clear_error(&error);
        }
    }
    desktop_reload();
}

static void desktop_menu_refresh(GtkMenuItem *item, gpointer data)
{
    (void)item; (void)data;
    desktop_reload();
    desktop_arrange_items(TRUE);
    desktop_schedule_geometry_update();
}

static void desktop_menu_show_drives(GtkCheckMenuItem *item, gpointer data)
{
    GError *error = NULL;
    (void)data;

    show_volumes = gtk_check_menu_item_get_active(item);
    if (!desktop_save_preferences(&error)) {
        show_desktop_error(_("Unable to save desktop settings"),
                           error ? error->message : NULL);
        g_clear_error(&error);
    }
    desktop_force_drive_refresh();
}

static void desktop_menu_drive_quick_action(GtkCheckMenuItem *item,
                                                   gpointer data)
{
    GError *error = NULL;
    (void)data;

    show_drive_quick_action = gtk_check_menu_item_get_active(item);
    if (!desktop_save_preferences(&error)) {
        show_desktop_error(_("Unable to save desktop settings"),
                           error ? error->message : NULL);
        g_clear_error(&error);
    }
    desktop_force_drive_refresh();
}

static GtkWidget *desktop_size_combo_new(gint size)
{
    GtkWidget *combo;
    gchar *active;

    combo = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "24", "24");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "32", "32");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "48", "48");
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(combo), "64", "64");
    active = g_strdup_printf("%d", size);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), active);
    g_free(active);
    return combo;
}

static gint desktop_size_combo_value(GtkWidget *combo, gint fallback)
{
    const gchar *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(combo));
    gint value = id ? atoi(id) : fallback;

    if (value != 24 && value != 32 && value != 48 && value != 64)
        value = fallback;
    return value;
}

/* Agregado por josejp2424 (2026): preferencias reunidas en una ventana
 * compacta. Las mismas opciones básicas de tamaño y activación también están
 * disponibles al agregar programas al escritorio. */
static void desktop_show_preferences_dialog(GtkWindow *parent)
{
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *grid;
    GtkWidget *desktop_size;
    GtkWidget *activation;
    GtkWidget *snap;
    GtkWidget *drive_size;
    GtkWidget *show_drives;
    GtkWidget *quick_action;
    GtkWidget *position;
    GtkWidget *orientation;
    gint response;

    dialog = gtk_dialog_new_with_buttons(_("ROX Desktop Preferences"), parent,
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        _("_Cancel"), GTK_RESPONSE_CANCEL,
        _("_Apply"), GTK_RESPONSE_APPLY,
        NULL);
    gtk_window_set_position(GTK_WINDOW(dialog), parent ?
        GTK_WIN_POS_CENTER_ON_PARENT : GTK_WIN_POS_CENTER_ALWAYS);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 460, 360);

    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 14);
    gtk_box_pack_start(GTK_BOX(content), grid, TRUE, TRUE, 0);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("Desktop icon size:")),
                    0, 0, 1, 1);
    desktop_size = desktop_size_combo_new(desktop_icon_size);
    gtk_grid_attach(GTK_GRID(grid), desktop_size, 1, 0, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("Open desktop icons:")),
                    0, 1, 1, 1);
    activation = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(activation), "single",
                              _("With one click"));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(activation), "double",
                              _("With double click"));
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(activation),
                                desktop_single_click ? "single" : "double");
    gtk_grid_attach(GTK_GRID(grid), activation, 1, 1, 1, 1);

    snap = gtk_check_button_new_with_label(_("Snap desktop icons to grid"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(snap), desktop_snap_to_grid);
    gtk_grid_attach(GTK_GRID(grid), snap, 0, 2, 2, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("Drive icon size:")),
                    0, 3, 1, 1);
    drive_size = desktop_size_combo_new(drive_icon_size);
    gtk_grid_attach(GTK_GRID(grid), drive_size, 1, 3, 1, 1);

    show_drives = gtk_check_button_new_with_label(_("Show drive icons"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(show_drives), show_volumes);
    gtk_grid_attach(GTK_GRID(grid), show_drives, 0, 4, 2, 1);

    quick_action = gtk_check_button_new_with_label(
        _("Show quick unmount button on mounted drives"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(quick_action),
                                 show_drive_quick_action);
    gtk_grid_attach(GTK_GRID(grid), quick_action, 0, 5, 2, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("Drive position:")),
                    0, 6, 1, 1);
    position = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(position), "bottom-left",
                              _("Bottom left"));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(position), "bottom-right",
                              _("Bottom right"));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(position), "top-left",
                              _("Top left"));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(position), "top-right",
                              _("Top right"));
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(position),
                                drive_position_name(drive_position));
    gtk_grid_attach(GTK_GRID(grid), position, 1, 6, 1, 1);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new(_("Drive orientation:")),
                    0, 7, 1, 1);
    orientation = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(orientation), "horizontal",
                              _("Horizontal"));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(orientation), "vertical",
                              _("Vertical"));
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(orientation),
        drive_orientation == GTK_ORIENTATION_VERTICAL ? "vertical" : "horizontal");
    gtk_grid_attach(GTK_GRID(grid), orientation, 1, 7, 1, 1);

    gtk_widget_show_all(dialog);
    response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_APPLY) {
        const gchar *activation_id;
        const gchar *position_id;
        const gchar *orientation_id;
        GError *error = NULL;

        desktop_icon_size = desktop_size_combo_value(desktop_size,
                                                      DEFAULT_DESKTOP_ICON_SIZE);
        drive_icon_size = desktop_size_combo_value(drive_size,
                                                    DEFAULT_DRIVE_ICON_SIZE);
        activation_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(activation));
        desktop_single_click = !g_strcmp0(activation_id, "single");
        desktop_snap_to_grid = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(snap));
        show_volumes = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(show_drives));
        show_drive_quick_action = gtk_toggle_button_get_active(
            GTK_TOGGLE_BUTTON(quick_action));
        position_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(position));
        {
            DesktopDrivePosition requested_position =
                drive_position_from_name(position_id);
            /* No destruir XPos/YPos personalizados sólo por abrir y aplicar
             * Preferencias. Las coordenadas vuelven a una esquina exacta
             * únicamente cuando el usuario cambia realmente la esquina. */
            if (requested_position != drive_position) {
                drive_position = requested_position;
                drive_coordinates_from_position();
            }
        }
        orientation_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(orientation));
        drive_orientation = !g_strcmp0(orientation_id, "vertical")
            ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL;

        if (!desktop_save_preferences(&error)) {
            show_desktop_error(_("Unable to save desktop settings"),
                               error ? error->message : NULL);
            g_clear_error(&error);
        }
        desktop_rebuild_icon_layer();
        desktop_force_drive_refresh();
    }
    gtk_widget_destroy(dialog);
}


static GtkWidget *drive_layout_label(const gchar *text)
{
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    return label;
}

static GtkWidget *drive_layout_spin(gint value, gint minimum,
                                    gint maximum, gint step)
{
    GtkWidget *spin = gtk_spin_button_new_with_range(minimum, maximum, step);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), value);
    gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
    return spin;
}

static void desktop_realign_drive_icons(void)
{
    /* Modificado por josejp2424 (2026): reaplicar la geometría completa como
     * hace EssoraWM. También se vuelve a reservar el rectángulo de unidades
     * para que los programas del escritorio nunca queden debajo. */
    desktop_apply_drive_layout();
    desktop_update_drive_reservation();
    desktop_reflow_items(TRUE);
    if (desktop_window)
        gtk_widget_queue_draw(desktop_window);
}

static void drive_layout_set_defaults(GtkWidget *enabled,
                                      GtkWidget *internal,
                                      GtkWidget *removable,
                                      GtkWidget *network,
                                      GtkWidget *labels,
                                      GtkWidget *frame,
                                      GtkWidget *reverse,
                                      GtkWidget *quick,
                                      GtkWidget *orientation,
                                      GtkWidget *horizontal,
                                      GtkWidget *vertical,
                                      GtkWidget *icon_size,
                                      GtkWidget *spacing_x,
                                      GtkWidget *spacing_y,
                                      GtkWidget *offset_x,
                                      GtkWidget *offset_y)
{
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enabled), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(internal), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(removable), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(network), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(labels), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(frame), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(reverse), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(quick), TRUE);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(orientation), "horizontal");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(horizontal), "left");
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(vertical), "bottom");
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(icon_size), 32);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spacing_x), 87);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spacing_y), 87);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(offset_x), 20);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(offset_y), -40);
}

typedef struct {
    GtkWidget *offset_x;
    GtkWidget *offset_y;
    gboolean changing;
} DriveOffsetPreview;

/* Modificado por josejp2424 (2026): XOffset/YOffset son ajustes finos reales,
 * como en EssoraWM. Cada pulsación del GtkSpinButton mueve el grupo un píxel
 * y la vista previa se aplica inmediatamente sin cerrar el diálogo. */
static void desktop_drive_offset_preview_changed(GtkSpinButton *button,
                                                 gpointer data)
{
    DriveOffsetPreview *preview = data;

    (void)button;
    if (!preview || preview->changing)
        return;

    drive_x_offset = gtk_spin_button_get_value_as_int(
        GTK_SPIN_BUTTON(preview->offset_x));
    drive_y_offset = gtk_spin_button_get_value_as_int(
        GTK_SPIN_BUTTON(preview->offset_y));
    desktop_apply_drive_layout();
    if (desktop_window)
        gtk_widget_queue_draw(desktop_window);
}

static void desktop_show_drive_layout_dialog(GtkWindow *parent)
{
    GtkWidget *dialog;
    GtkWidget *content;
    GtkWidget *main_grid;
    GtkWidget *visibility_frame;
    GtkWidget *visibility_grid;
    GtkWidget *layout_frame;
    GtkWidget *layout_grid;
    GtkWidget *enabled;
    GtkWidget *internal;
    GtkWidget *removable;
    GtkWidget *network;
    GtkWidget *labels;
    GtkWidget *frame;
    GtkWidget *reverse;
    GtkWidget *quick;
    GtkWidget *orientation;
    GtkWidget *horizontal;
    GtkWidget *vertical;
    GtkWidget *icon_size;
    GtkWidget *spacing_x;
    GtkWidget *spacing_y;
    GtkWidget *offset_x;
    GtkWidget *offset_y;
    DriveOffsetPreview preview = {0};
    gint committed_x_offset = drive_x_offset;
    gint committed_y_offset = drive_y_offset;
    gint response;

    dialog = gtk_dialog_new_with_buttons(_("Drive Icon Layout"), parent,
        GTK_DIALOG_DESTROY_WITH_PARENT,
        _("Defaults"), GTK_RESPONSE_REJECT,
        _("Apply"), GTK_RESPONSE_APPLY,
        _("Close"), GTK_RESPONSE_CLOSE,
        NULL);
    gtk_window_set_position(GTK_WINDOW(dialog), parent ?
        GTK_WIN_POS_CENTER_ON_PARENT : GTK_WIN_POS_CENTER_ALWAYS);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 760, 500);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_APPLY);

    content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 12);
    main_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(main_grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(main_grid), 12);
    gtk_box_pack_start(GTK_BOX(content), main_grid, TRUE, TRUE, 0);

    visibility_frame = gtk_frame_new(_("Visibility"));
    visibility_grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(visibility_grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(visibility_grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(visibility_grid), 12);
    gtk_container_add(GTK_CONTAINER(visibility_frame), visibility_grid);
    gtk_grid_attach(GTK_GRID(main_grid), visibility_frame, 0, 0, 1, 1);

    enabled = gtk_check_button_new_with_label(_("Show drive icons"));
    internal = gtk_check_button_new_with_label(_("Internal drives"));
    removable = gtk_check_button_new_with_label(_("Removable drives"));
    network = gtk_check_button_new_with_label(_("Network drives"));
    labels = gtk_check_button_new_with_label(_("Show labels"));
    frame = gtk_check_button_new_with_label(_("Label frame"));
    reverse = gtk_check_button_new_with_label(_("Reverse order"));
    quick = gtk_check_button_new_with_label(_("Show quick unmount button"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(enabled), show_volumes);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(internal), drive_show_internal);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(removable), drive_show_removable);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(network), drive_show_network);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(labels), drive_show_labels);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(frame), drive_show_frame);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(reverse), drive_reverse_pack);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(quick), show_drive_quick_action);
    gtk_grid_attach(GTK_GRID(visibility_grid), enabled, 0, 0, 2, 1);
    gtk_grid_attach(GTK_GRID(visibility_grid), internal, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(visibility_grid), removable, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(visibility_grid), network, 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(visibility_grid), labels, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(visibility_grid), frame, 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(visibility_grid), reverse, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(visibility_grid), quick, 0, 4, 2, 1);

    layout_frame = gtk_frame_new(_("Layout"));
    layout_grid = gtk_grid_new();
    gtk_container_set_border_width(GTK_CONTAINER(layout_grid), 10);
    gtk_grid_set_row_spacing(GTK_GRID(layout_grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(layout_grid), 10);
    gtk_container_add(GTK_CONTAINER(layout_frame), layout_grid);
    gtk_grid_attach(GTK_GRID(main_grid), layout_frame, 1, 0, 1, 1);

    orientation = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(orientation), "horizontal", _("Horizontal"));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(orientation), "vertical", _("Vertical"));
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(orientation),
        drive_orientation == GTK_ORIENTATION_VERTICAL ? "vertical" : "horizontal");

    horizontal = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(horizontal), "left", _("Left"));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(horizontal), "center", _("Center"));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(horizontal), "right", _("Right"));
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(horizontal),
        drive_x_pos < 0.25 ? "left" : drive_x_pos > 0.75 ? "right" : "center");

    vertical = gtk_combo_box_text_new();
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(vertical), "top", _("Top"));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(vertical), "center", _("Center"));
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(vertical), "bottom", _("Bottom"));
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(vertical),
        drive_y_pos < 0.25 ? "top" : drive_y_pos > 0.75 ? "bottom" : "center");

    icon_size = drive_layout_spin(drive_icon_size, 16, 128, 8);
    spacing_x = drive_layout_spin(drive_spacing_x, 48, 320, 8);
    spacing_y = drive_layout_spin(drive_spacing_y, 48, 320, 8);
    offset_x = drive_layout_spin(drive_x_offset, -500, 500, 1);
    offset_y = drive_layout_spin(drive_y_offset, -500, 500, 1);
    gtk_spin_button_set_increments(GTK_SPIN_BUTTON(offset_x), 1, 10);
    gtk_spin_button_set_increments(GTK_SPIN_BUTTON(offset_y), 1, 10);
    gtk_widget_set_tooltip_text(offset_x,
        _("Move the drive icons left or right one pixel at a time"));
    gtk_widget_set_tooltip_text(offset_y,
        _("Move the drive icons up or down one pixel at a time"));
    preview.offset_x = offset_x;
    preview.offset_y = offset_y;
    g_signal_connect(offset_x, "value-changed",
        G_CALLBACK(desktop_drive_offset_preview_changed), &preview);
    g_signal_connect(offset_y, "value-changed",
        G_CALLBACK(desktop_drive_offset_preview_changed), &preview);

    gtk_grid_attach(GTK_GRID(layout_grid), drive_layout_label(_("Orientation")), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(layout_grid), orientation, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(layout_grid), drive_layout_label(_("Horizontal position")), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(layout_grid), horizontal, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(layout_grid), drive_layout_label(_("Vertical position")), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(layout_grid), vertical, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(layout_grid), drive_layout_label(_("Icon size")), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(layout_grid), icon_size, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(layout_grid), drive_layout_label(_("Horizontal spacing")), 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(layout_grid), spacing_x, 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(layout_grid), drive_layout_label(_("Vertical spacing")), 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(layout_grid), spacing_y, 1, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(layout_grid), drive_layout_label(_("Horizontal offset")), 0, 6, 1, 1);
    gtk_grid_attach(GTK_GRID(layout_grid), offset_x, 1, 6, 1, 1);
    gtk_grid_attach(GTK_GRID(layout_grid), drive_layout_label(_("Vertical offset")), 0, 7, 1, 1);
    gtk_grid_attach(GTK_GRID(layout_grid), offset_y, 1, 7, 1, 1);

    gtk_widget_show_all(dialog);
    while ((response = gtk_dialog_run(GTK_DIALOG(dialog))) != GTK_RESPONSE_CLOSE &&
           response != GTK_RESPONSE_DELETE_EVENT && response != GTK_RESPONSE_NONE)
    {
        if (response == GTK_RESPONSE_REJECT)
        {
            drive_layout_set_defaults(enabled, internal, removable, network,
                labels, frame, reverse, quick, orientation, horizontal,
                vertical, icon_size, spacing_x, spacing_y, offset_x, offset_y);
            continue;
        }
        if (response == GTK_RESPONSE_APPLY)
        {
            const gchar *orientation_id;
            const gchar *horizontal_id;
            const gchar *vertical_id;
            GError *error = NULL;

            show_volumes = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(enabled));
            drive_show_internal = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(internal));
            drive_show_removable = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(removable));
            drive_show_network = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(network));
            drive_show_labels = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(labels));
            drive_show_frame = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(frame));
            drive_reverse_pack = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(reverse));
            show_drive_quick_action = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(quick));

            orientation_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(orientation));
            horizontal_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(horizontal));
            vertical_id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(vertical));
            drive_orientation = !g_strcmp0(orientation_id, "vertical")
                ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL;
            drive_x_pos = !g_strcmp0(horizontal_id, "right") ? 1.0
                : !g_strcmp0(horizontal_id, "center") ? 0.5 : 0.0;
            drive_y_pos = !g_strcmp0(vertical_id, "bottom") ? 1.0
                : !g_strcmp0(vertical_id, "center") ? 0.5 : 0.0;
            drive_position_from_coordinates();
            drive_icon_size = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(icon_size));
            drive_spacing_x = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spacing_x));
            drive_spacing_y = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spacing_y));
            drive_x_offset = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(offset_x));
            drive_y_offset = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(offset_y));
            committed_x_offset = drive_x_offset;
            committed_y_offset = drive_y_offset;

            if (!desktop_save_preferences(&error))
            {
                show_desktop_error(_("Unable to save desktop settings"),
                    error ? error->message : NULL);
                g_clear_error(&error);
            }
            desktop_force_drive_refresh();
            desktop_realign_drive_icons();
        }
    }

    /* Si el usuario movió los controles pero cerró sin Aplicar, recuperar la
     * última posición confirmada. Después de cada Aplicar, estos valores se
     * actualizan y pasan a ser la nueva base. */
    preview.changing = TRUE;
    drive_x_offset = committed_x_offset;
    drive_y_offset = committed_y_offset;
    desktop_apply_drive_layout();
    preview.changing = FALSE;

    gtk_widget_destroy(dialog);
}

static void desktop_menu_drive_layout(GtkMenuItem *item, gpointer data)
{
    (void)item;
    (void)data;
    desktop_show_drive_layout_dialog(GTK_WINDOW(desktop_window));
}

static void desktop_menu_drive_realign(GtkMenuItem *item, gpointer data)
{
    (void)item;
    (void)data;
    desktop_realign_drive_icons();
}

static void desktop_menu_preferences(GtkMenuItem *item, gpointer data)
{
    (void)item;
    (void)data;
    desktop_show_preferences_dialog(GTK_WINDOW(desktop_window));
}



static GtkWidget *desktop_drive_settings_menu(void)
{
    GtkWidget *menu;
    GtkWidget *item;

    menu = rox_menu_new();

    item = gtk_check_menu_item_new_with_label(_("Show drive icons"));
    menu_item_set_icon(item, "drive-harddisk");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), show_volumes);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect(item, "toggled", G_CALLBACK(desktop_menu_show_drives), NULL);

    item = gtk_check_menu_item_new_with_label(_("Show quick unmount button"));
    menu_item_set_icon(item, "media-eject");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),
                                   show_drive_quick_action);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect(item, "toggled",
                     G_CALLBACK(desktop_menu_drive_quick_action), NULL);

    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = menu_item_new_with_icon(_("Arrange Drive Icons..."),
                                   "preferences-desktop-icons");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect(item, "activate",
                     G_CALLBACK(desktop_menu_drive_layout), NULL);

    item = menu_item_new_with_icon(_("Realign Drive Icons"), "view-refresh");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect(item, "activate",
                     G_CALLBACK(desktop_menu_drive_realign), NULL);

    return menu;
}

static void show_desktop_menu(GdkEventButton *event)
{
    GtkWidget *menu;
    GtkWidget *item;
    GtkWidget *submenu;

    menu = rox_menu_new();

    item = menu_item_new_with_icon(_("Change Desktop Background..."),
                                   "preferences-desktop-wallpaper");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect(item, "activate", G_CALLBACK(desktop_menu_wallpaper), NULL);

    item = menu_item_new_with_icon(_("Drive Icons"), "drive-harddisk");
    submenu = desktop_drive_settings_menu();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), submenu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = menu_item_new_with_icon(_("Add Programs to Desktop..."), ROX_ICON_ADD);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect(item, "activate", G_CALLBACK(desktop_menu_add_programs), NULL);

    item = menu_item_new_with_icon(_("Desktop Preferences..."), ROX_ICON_PREFERENCES);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect(item, "activate", G_CALLBACK(desktop_menu_preferences), NULL);

    item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = menu_item_new_with_icon(_("Refresh Desktop"), ROX_ICON_REFRESH);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    g_signal_connect(item, "activate", G_CALLBACK(desktop_menu_refresh), NULL);

    g_signal_connect_swapped(menu, "selection-done",
        G_CALLBACK(gtk_widget_destroy), menu);
    gtk_menu_attach_to_widget(GTK_MENU(menu), desktop_window, NULL);
    gtk_widget_show_all(menu);
    gtk_menu_popup_at_pointer(GTK_MENU(menu), (GdkEvent *)event);
}

static gboolean desktop_button_press(GtkWidget *widget, GdkEventButton *event,
                                     gpointer data)
{
    (void)widget; (void)data;

    if (!event || event->type != GDK_BUTTON_PRESS)
        return FALSE;

    if (event->button == 1) {
        if (!(event->state & GDK_CONTROL_MASK))
            desktop_clear_selection();
        return FALSE;
    }
    if (event->button != 3)
        return FALSE;

    desktop_clear_selection();
    show_desktop_menu(event);
    return TRUE;
}

static void load_settings(void)
{
    GKeyFile *kf = rox_config_load(DESKTOP_CONFIG);
    gchar *text;
    GError *error = NULL;

    /* Modificado por josejp2424 (2026): ROX Desktop usa siempre ~/Desktop.
     * No se acepta una ruta XDG traducida como ~/Escritorio ni una ruta antigua
     * guardada en desktop.conf; al guardar las preferencias también queda
     * normalizada la configuración para los siguientes inicios. */
    desktop_dir = g_build_filename(g_get_home_dir(), "Desktop", NULL);

    show_volumes = g_key_file_has_key(kf, "Desktop", "ShowVolumes", NULL)
        ? g_key_file_get_boolean(kf, "Desktop", "ShowVolumes", NULL)
        : drive_config_boolean(kf, "Enabled", "desktop_drive_icons", TRUE);

    desktop_show_home = !g_key_file_has_key(kf, "DesktopIcons", "ShowHome", NULL) ||
        g_key_file_get_boolean(kf, "DesktopIcons", "ShowHome", NULL);
    desktop_show_browser = !g_key_file_has_key(kf, "DesktopIcons", "ShowBrowser", NULL) ||
        g_key_file_get_boolean(kf, "DesktopIcons", "ShowBrowser", NULL);
    desktop_show_console = !g_key_file_has_key(kf, "DesktopIcons", "ShowConsole", NULL) ||
        g_key_file_get_boolean(kf, "DesktopIcons", "ShowConsole", NULL);
    desktop_show_trash = !g_key_file_has_key(kf, "DesktopIcons", "ShowTrash", NULL) ||
        g_key_file_get_boolean(kf, "DesktopIcons", "ShowTrash", NULL);

    if (g_key_file_has_key(kf, "DesktopIcons", "IconSize", NULL))
        desktop_icon_size = g_key_file_get_integer(kf, "DesktopIcons",
                                                   "IconSize", NULL);
    desktop_icon_size = desktop_icon_size == 24 || desktop_icon_size == 32 ||
                        desktop_icon_size == 48 || desktop_icon_size == 64
        ? desktop_icon_size : DEFAULT_DESKTOP_ICON_SIZE;
    desktop_single_click = g_key_file_has_key(kf, "DesktopIcons",
                                               "SingleClick", NULL)
        ? g_key_file_get_boolean(kf, "DesktopIcons", "SingleClick", NULL)
        : FALSE;
    desktop_snap_to_grid = g_key_file_has_key(kf, "DesktopIcons",
                                               "SnapToGrid", NULL)
        ? g_key_file_get_boolean(kf, "DesktopIcons", "SnapToGrid", NULL)
        : TRUE;

    text = g_key_file_get_string(kf, "Desktop", "WallpaperMode", NULL);
    wallpaper_mode = wallpaper_mode_from_name(text);
    g_free(text);

    text = g_key_file_get_string(kf, "DesktopDrives", "Position", NULL);
    drive_position = drive_position_from_name(text);
    g_free(text);

    text = g_key_file_get_string(kf, "DesktopDrives", "Orientation", NULL);
    if (text) {
        drive_orientation = !g_strcmp0(text, "vertical")
            ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL;
    } else {
        drive_orientation = drive_config_boolean(kf, "Vertical", "Vertical", FALSE)
            ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL;
    }
    g_free(text);

    drive_icon_size = CLAMP(drive_config_integer(kf, "IconSize",
                                            "desktop_drive_icon_size", 32),
                            16, 128);
    show_drive_quick_action = drive_config_boolean(kf, "ShowQuickAction",
                                                    NULL, TRUE);
    drive_show_internal = drive_config_boolean(kf, "ShowInternal",
                                                "desktop_drive_show_internal", TRUE);
    drive_show_removable = drive_config_boolean(kf, "ShowRemovable",
                                                 "desktop_drive_show_removable", TRUE);
    drive_show_network = drive_config_boolean(kf, "ShowNetwork",
                                               "desktop_drive_show_network", FALSE);
    drive_show_labels = drive_config_boolean(kf, "ShowLabels", "ShowLabels", TRUE);
    drive_show_frame = drive_config_boolean(kf, "ShowFrame", "ShowFrame", FALSE);
    drive_reverse_pack = drive_config_boolean(kf, "ReversePack", "ReversePack", TRUE);
    drive_spacing_x = CLAMP(drive_config_integer(kf, "SpacingX", "SpacingX", 87),
                            48, 320);
    drive_spacing_y = CLAMP(drive_config_integer(kf, "SpacingY", "SpacingY", 87),
                            48, 320);
    drive_x_offset = CLAMP(drive_config_integer(kf, "XOffset", "XOffset", 20),
                           -500, 500);
    drive_y_offset = CLAMP(drive_config_integer(kf, "YOffset", "YOffset", -40),
                           -500, 500);

    if (g_key_file_has_key(kf, "DesktopDrives", "XPos", NULL) ||
        g_key_file_has_key(kf, "Main", "XPos", NULL) ||
        g_key_file_has_key(kf, "DesktopDrives", "YPos", NULL) ||
        g_key_file_has_key(kf, "Main", "YPos", NULL)) {
        drive_x_pos = CLAMP(drive_config_double(kf, "XPos", "XPos", 0.0),
                            0.0, 1.0);
        drive_y_pos = CLAMP(drive_config_double(kf, "YPos", "YPos", 1.0),
                            0.0, 1.0);
        drive_position_from_coordinates();
    } else {
        drive_coordinates_from_position();
    }

    if (g_key_file_has_key(kf, "DesktopDrives", "Margin", NULL))
        drive_margin = CLAMP(g_key_file_get_integer(kf, "DesktopDrives",
                             "Margin", NULL), 0, 128);
    if (g_key_file_has_key(kf, "DesktopDrives", "Spacing", NULL))
        drive_spacing = CLAMP(g_key_file_get_integer(kf, "DesktopDrives",
                              "Spacing", NULL), 0, 64);

    g_key_file_unref(kf);

    g_mkdir_with_parents(desktop_dir, 0700);
    desktop_load_wallpaper_from_config();

    if (!desktop_save_preferences(&error)) {
        g_warning("Unable to save desktop configuration: %s",
                  error ? error->message : "unknown error");
        g_clear_error(&error);
    }
}

static void desktop_lower_window(void)
{
    if (!desktop_window || !gtk_widget_get_realized(desktop_window))
        return;
    if (desktop_backend && desktop_backend->lower_window)
        desktop_backend->lower_window(desktop_window);
}

static gboolean desktop_apply_geometry(gpointer data)
{
    (void)data;
    geometry_reload_source = 0;

    if (!desktop_window)
        return G_SOURCE_REMOVE;

    desktop_query_geometry();
    gtk_window_move(GTK_WINDOW(desktop_window),
                    desktop_geometry.x, desktop_geometry.y);
    gtk_window_resize(GTK_WINDOW(desktop_window),
                      desktop_geometry.width, desktop_geometry.height);
    {
        GList *node;
        for (node = desktop_items; node; node = node->next) {
            DesktopItem *item = node->data;
            desktop_item_clamp(item);
            if (desktop_icon_layer && item->widget)
                gtk_fixed_move(GTK_FIXED(desktop_icon_layer), item->widget,
                               item->x, item->y);
        }
    }
    desktop_apply_drive_layout();
    gtk_widget_queue_draw(desktop_window);
    desktop_lower_window();
    return G_SOURCE_REMOVE;
}

static void desktop_schedule_geometry_update(void)
{
    if (geometry_reload_source == 0)
        geometry_reload_source = g_idle_add(desktop_apply_geometry, NULL);
}

/* JWM, panels and wbar can take a few cycles to publish the final work area.
 * This refresh is deliberately geometry-only. Existing desktop GtkWidgets
 * must survive it: rebuilding the icon layer here used to destroy/recreate
 * Home, Browser, Console, Trash and normal desktop items, disturbing GTK3
 * focus/prelight state on X11/XLibre. */
static gboolean desktop_environment_refresh_cb(gpointer data)
{
    guint delay;
    (void)data;

    environment_refresh_source = 0;
    if (!desktop_window) {
        environment_refresh_round = 0;
        return G_SOURCE_REMOVE;
    }

    desktop_query_geometry();
    desktop_schedule_geometry_update();
    desktop_apply_drive_layout();

    if (environment_refresh_round == 0)
        ROX_LOG_DEBUG("desktop",
                      "environment refresh applying geometry only; desktop widgets preserved");

    environment_refresh_round++;
    if (environment_refresh_round < 4) {
        static const guint delays[] = {300, 650, 1200};
        delay = delays[MIN(environment_refresh_round - 1,
                           G_N_ELEMENTS(delays) - 1)];
        environment_refresh_source = g_timeout_add(
            delay, desktop_environment_refresh_cb, NULL);
    } else {
        environment_refresh_round = 0;
    }
    return G_SOURCE_REMOVE;
}

void desktop_refresh_after_environment_change(void)
{
    if (!desktop_window)
        return;
    ROX_LOG_DEBUG("desktop", "environment refresh scheduled");

    if (environment_refresh_source) {
        g_source_remove(environment_refresh_source);
        environment_refresh_source = 0;
    }
    environment_refresh_round = 0;
    environment_refresh_source = g_timeout_add(
        100, desktop_environment_refresh_cb, NULL);
}

static void desktop_screen_changed(GdkScreen *screen, gpointer data)
{
    (void)screen; (void)data;
    desktop_schedule_geometry_update();
}

static gboolean desktop_map_event(GtkWidget *widget, GdkEvent *event,
                                  gpointer data)
{
    (void)widget; (void)event; (void)data;
    ROX_LOG_INFO("desktop", "desktop window mapped");
    desktop_schedule_geometry_update();
    return FALSE;
}

static void desktop_destroyed(GtkWidget *widget, gpointer data)
{
    (void)widget; (void)data;

    ROX_LOG_INFO("desktop", "desktop window destroyed; cleaning resources");
    if (desktop_backend && desktop_backend->unregister_control)
        desktop_backend->unregister_control(desktop_window);
    if (drive_poll_source) {
        g_source_remove(drive_poll_source);
        drive_poll_source = 0;
    }
    if (wallpaper_reload_source) {
        g_source_remove(wallpaper_reload_source);
        wallpaper_reload_source = 0;
    }
    if (geometry_reload_source) {
        g_source_remove(geometry_reload_source);
        geometry_reload_source = 0;
    }
    if (environment_refresh_source) {
        g_source_remove(environment_refresh_source);
        environment_refresh_source = 0;
    }
    environment_refresh_round = 0;
    if (desktop_screen) {
        if (desktop_screen_monitors_handler)
            g_signal_handler_disconnect(desktop_screen,
                                        desktop_screen_monitors_handler);
        if (desktop_screen_size_handler)
            g_signal_handler_disconnect(desktop_screen,
                                        desktop_screen_size_handler);
    }
    desktop_screen = NULL;
    desktop_screen_monitors_handler = 0;
    desktop_screen_size_handler = 0;
    g_clear_object(&desktop_monitor);
    g_clear_object(&desktop_config_monitor);
    g_clear_object(&trash_monitor);
    g_clear_object(&volume_monitor);
    desktop_selected_item = NULL;
    g_list_free_full(desktop_items, (GDestroyNotify)desktop_item_free);
    desktop_items = NULL;
    g_clear_pointer(&drive_signature, g_free);
    drive_scan_serial++;
    drive_scan_in_progress = FALSE;
    g_clear_pointer(&desktop_dir, g_free);
    g_clear_pointer(&wallpaper_path, g_free);
    g_clear_object(&wallpaper_pixbuf);
    desktop_drive_box = NULL;
    desktop_drive_layer = NULL;
    desktop_overlay = NULL;
    desktop_icon_layer = NULL;
    desktop_window = NULL;
    desktop_backend = NULL;
    if (number_of_windows > 0)
        number_of_windows--;
}

void desktop_init(void)
{
    ROX_LOG_DEBUG("desktop", "initializing desktop module");
    rox_config_init();
    option_register_widget("desktop-tools", build_desktop_tools);
}

void desktop_start(void)
{
    GFile *dir;
    GtkCssProvider *css;
    GError *backend_error = NULL;

    if (desktop_window) {
        ROX_LOG_INFO("desktop", "desktop already exists; presenting current window");
        gtk_window_present(GTK_WINDOW(desktop_window));
        desktop_lower_window();
        return;
    }

    load_settings();
    ROX_LOG_INFO("desktop", "settings directory=%s wallpaper=%s show_volumes=%d icon_size=%d",
                 desktop_dir ? desktop_dir : "",
                 wallpaper_path ? wallpaper_path : "", show_volumes,
                 desktop_icon_size);
    desktop_query_geometry();
    ROX_LOG_INFO("desktop", "geometry x=%d y=%d width=%d height=%d workarea=%d,%d %dx%d",
                 desktop_geometry.x, desktop_geometry.y,
                 desktop_geometry.width, desktop_geometry.height,
                 desktop_workarea.x, desktop_workarea.y,
                 desktop_workarea.width, desktop_workarea.height);
    desktop_backend = desktop_backend_select(gdk_display_get_default());
    if (desktop_backend && desktop_backend->prepare_display &&
        !desktop_backend->prepare_display(gdk_display_get_default(),
                                          &backend_error)) {
        ROX_LOG_ERROR("desktop", "backend preparation failed backend=%s error=%s",
                      desktop_backend ? desktop_backend->name : "none",
                      backend_error ? backend_error->message : "unknown");
        show_desktop_error(_("Unable to start ROX Desktop"),
                           backend_error ? backend_error->message : NULL);
        g_clear_error(&backend_error);
        desktop_backend = NULL;
        return;
    }

    ROX_LOG_INFO("desktop", "creating desktop window backend=%s",
                 desktop_backend ? desktop_backend->name : "none");
    desktop_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(desktop_window), _("ROX Desktop"));
    gtk_window_set_decorated(GTK_WINDOW(desktop_window), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(desktop_window), FALSE);
    gtk_window_set_accept_focus(GTK_WINDOW(desktop_window), TRUE);
    gtk_window_set_focus_on_map(GTK_WINDOW(desktop_window), FALSE);
    if (desktop_backend && desktop_backend->configure_window)
        desktop_backend->configure_window(GTK_WINDOW(desktop_window));
    gtk_window_set_default_size(GTK_WINDOW(desktop_window),
                                desktop_geometry.width,
                                desktop_geometry.height);
    gtk_window_move(GTK_WINDOW(desktop_window),
                    desktop_geometry.x, desktop_geometry.y);
    gtk_widget_set_app_paintable(desktop_window, TRUE);
    g_signal_connect(desktop_window, "draw", G_CALLBACK(desktop_draw), NULL);
    g_signal_connect(desktop_window, "destroy",
                     G_CALLBACK(desktop_destroyed), NULL);
    g_signal_connect(desktop_window, "map-event",
                     G_CALLBACK(desktop_map_event), NULL);

    desktop_overlay = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(desktop_window), desktop_overlay);

    /* Modificado por josejp2424 (2026): GtkFixed permite ubicar y mover
     * libremente los lanzadores y archivos del escritorio. */
    desktop_icon_layer = gtk_fixed_new();
    gtk_widget_set_hexpand(desktop_icon_layer, TRUE);
    gtk_widget_set_vexpand(desktop_icon_layer, TRUE);
    gtk_container_add(GTK_CONTAINER(desktop_overlay), desktop_icon_layer);
    gtk_widget_add_events(desktop_window, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(desktop_window, "button-press-event",
                     G_CALLBACK(desktop_button_press), NULL);

    /* Modificado por josejp2424 (2026): las unidades y los programas usan
     * el mismo GtkFixed. La antigua capa overlay ocupaba toda la pantalla y
     * capturaba los clics antes que los iconos, impidiendo abrir, mover o
     * eliminar elementos del escritorio. */
    desktop_drive_layer = desktop_icon_layer;
    desktop_drive_box = gtk_box_new(drive_orientation, 0);
    gtk_fixed_put(GTK_FIXED(desktop_drive_layer), desktop_drive_box, 0, 0);
    desktop_apply_drive_layout();

    css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        ".rox-desktop-item { background-color: transparent; border-radius: 5px; padding: 3px; }"
        ".rox-desktop-label { color: #ffffff; background-color: transparent; padding: 1px 3px; text-shadow: 1px 1px 2px #000000; }"
        ".rox-desktop-item:hover, .rox-desktop-item-hover { background-color: alpha(@theme_selected_bg_color,0.35); }"
        ".rox-desktop-item-selected { background-color: alpha(@theme_selected_bg_color,0.58); }"
        ".rox-desktop-item-selected .rox-desktop-label { color: @theme_selected_fg_color; text-shadow: none; }"
        ".rox-desktop-drive button { background-image: none; background-color: transparent; border-color: transparent; box-shadow: none; padding: 2px; }"
        ".rox-desktop-drive button:hover { background-color: alpha(@theme_selected_bg_color,0.35); }"
        ".rox-desktop-drive-framed { background-color: alpha(@theme_bg_color,0.72); border: 1px solid alpha(@theme_fg_color,0.32); border-radius: 5px; }"
        ".rox-desktop-drive-label { color: #ffffff; background-color: transparent; text-shadow: 1px 1px 2px #000000; }"
        ".rox-desktop-drive .rox-drive-quick { background-color: alpha(@theme_bg_color,0.78); border-radius: 12px; padding: 2px; }",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    dir = g_file_new_for_path(desktop_dir);
    desktop_monitor = g_file_monitor_directory(dir, G_FILE_MONITOR_NONE,
                                                NULL, NULL);
    if (desktop_monitor)
        g_signal_connect(desktop_monitor, "changed",
                         G_CALLBACK(monitor_changed), NULL);
    g_object_unref(dir);

    trash_monitor = rox_trash_monitor_new(NULL);
    if (trash_monitor)
        g_signal_connect(trash_monitor, "changed",
                         G_CALLBACK(monitor_changed), NULL);

    dir = g_file_new_for_path(rox_config_dir());
    desktop_config_monitor = g_file_monitor_directory(dir,
        G_FILE_MONITOR_NONE, NULL, NULL);
    if (desktop_config_monitor)
        g_signal_connect(desktop_config_monitor, "changed",
                         G_CALLBACK(desktop_config_changed), NULL);
    g_object_unref(dir);

    /* GVolumeMonitor acelera los cambios cuando existe un backend. El sondeo
     * lsblk compartido sigue siendo el respaldo para Puppy sin GVfs/UDisks. */
    volume_monitor = g_volume_monitor_get();
    if (volume_monitor) {
        g_signal_connect(volume_monitor, "mount-added",
                         G_CALLBACK(volume_changed), NULL);
        g_signal_connect(volume_monitor, "mount-removed",
                         G_CALLBACK(volume_changed), NULL);
        g_signal_connect(volume_monitor, "mount-changed",
                         G_CALLBACK(volume_changed), NULL);
        g_signal_connect(volume_monitor, "volume-added",
                         G_CALLBACK(volume_changed), NULL);
        g_signal_connect(volume_monitor, "volume-removed",
                         G_CALLBACK(volume_changed), NULL);
        g_signal_connect(volume_monitor, "volume-changed",
                         G_CALLBACK(volume_changed), NULL);
    }

    desktop_screen = gtk_window_get_screen(GTK_WINDOW(desktop_window));
    if (desktop_screen) {
        desktop_screen_monitors_handler = g_signal_connect(desktop_screen,
            "monitors-changed", G_CALLBACK(desktop_screen_changed), NULL);
        desktop_screen_size_handler = g_signal_connect(desktop_screen,
            "size-changed", G_CALLBACK(desktop_screen_changed), NULL);
    }

    g_clear_pointer(&drive_signature, g_free);
    drive_scan_in_progress = FALSE;
    drive_poll_source = g_timeout_add_seconds(DRIVE_POLL_SECONDS,
                                               desktop_drive_poll, NULL);

    desktop_reload();
    ROX_LOG_INFO("desktop", "desktop contents loaded items=%u",
                 g_list_length(desktop_items));
    number_of_windows++;
    gtk_widget_show_all(desktop_window);
    if (desktop_backend && desktop_backend->register_control)
        desktop_backend->register_control(desktop_window,
                                          desktop_backend_refresh, NULL);
    desktop_schedule_geometry_update();
    desktop_refresh_after_environment_change();
    ROX_LOG_INFO("desktop", "desktop startup completed backend=%s",
                 desktop_backend ? desktop_backend->name : "none");
}

static void desktop_prepare_standalone_tool(void)
{
    if (!desktop_dir)
        load_settings();
}

static void desktop_wallpaper_button(GtkButton *button, gpointer data)
{
    (void)button; (void)data;
    desktop_open_wallpaper_manager();
}

static void desktop_apps_button(GtkButton *button, gpointer data)
{
    (void)button; (void)data;
    desktop_open_apps_manager();
}

static void desktop_drives_button(GtkButton *button, gpointer data)
{
    (void)button; (void)data;
    desktop_prepare_standalone_tool();
    desktop_show_drive_layout_dialog(desktop_window ? GTK_WINDOW(desktop_window) : NULL);
}

enum {
    DESKTOP_TOGGLE_HOME,
    DESKTOP_TOGGLE_BROWSER,
    DESKTOP_TOGGLE_CONSOLE,
    DESKTOP_TOGGLE_TRASH
};

static void desktop_builtin_toggle(GtkToggleButton *button, gpointer data)
{
    gboolean active = gtk_toggle_button_get_active(button);
    GError *error = NULL;

    switch (GPOINTER_TO_INT(data)) {
        case DESKTOP_TOGGLE_HOME: desktop_show_home = active; break;
        case DESKTOP_TOGGLE_BROWSER: desktop_show_browser = active; break;
        case DESKTOP_TOGGLE_CONSOLE: desktop_show_console = active; break;
        case DESKTOP_TOGGLE_TRASH: desktop_show_trash = active; break;
        default: return;
    }

    if (!desktop_save_preferences(&error)) {
        show_desktop_error(_("Unable to save desktop settings"),
                           error ? error->message : NULL);
        g_clear_error(&error);
        return;
    }
    if (desktop_window)
        desktop_reload();
}

static GList *build_desktop_tools(Option *option, xmlNode *node, guchar *label)
{
    GtkWidget *box, *icons_label, *grid, *hint;
    GtkWidget *home, *browser, *console, *trash;
    GtkWidget *tools_label, *tools_grid, *wallpaper, *apps, *drives;
    (void)node; (void)label;
    g_return_val_if_fail(option == NULL, NULL);

    desktop_prepare_standalone_tool();
    box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);

    icons_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(icons_label), _("<b>Desktop icons</b>"));
    gtk_widget_set_halign(icons_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), icons_label, FALSE, FALSE, 0);

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 4);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 24);

    home = gtk_check_button_new_with_label(_("Home"));
    browser = gtk_check_button_new_with_label(_("Browser"));
    console = gtk_check_button_new_with_label(_("Console"));
    trash = gtk_check_button_new_with_label(_("Trash"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(home), desktop_show_home);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(browser), desktop_show_browser);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(console), desktop_show_console);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(trash), desktop_show_trash);

    gtk_grid_attach(GTK_GRID(grid), home, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), browser, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), console, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), trash, 1, 1, 1, 1);

    g_signal_connect(home, "toggled", G_CALLBACK(desktop_builtin_toggle),
                     GINT_TO_POINTER(DESKTOP_TOGGLE_HOME));
    g_signal_connect(browser, "toggled", G_CALLBACK(desktop_builtin_toggle),
                     GINT_TO_POINTER(DESKTOP_TOGGLE_BROWSER));
    g_signal_connect(console, "toggled", G_CALLBACK(desktop_builtin_toggle),
                     GINT_TO_POINTER(DESKTOP_TOGGLE_CONSOLE));
    g_signal_connect(trash, "toggled", G_CALLBACK(desktop_builtin_toggle),
                     GINT_TO_POINTER(DESKTOP_TOGGLE_TRASH));
    gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);

    hint = gtk_label_new(_("Built-in desktop icons use the active system icon theme."));
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "dim-label");
    gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 0);

    tools_label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(tools_label), _("<b>Desktop settings</b>"));
    gtk_widget_set_halign(tools_label, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), tools_label, FALSE, FALSE, 4);

    tools_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(tools_grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(tools_grid), 6);

    wallpaper = gtk_button_new_with_label(_("Wallpaper"));
    gtk_button_set_image(GTK_BUTTON(wallpaper),
        gtk_image_new_from_icon_name("preferences-desktop-wallpaper", GTK_ICON_SIZE_BUTTON));
    apps = gtk_button_new_with_label(_("Desktop Applications"));
    gtk_button_set_image(GTK_BUTTON(apps),
        gtk_image_new_from_icon_name("applications-other", GTK_ICON_SIZE_BUTTON));
    drives = gtk_button_new_with_label(_("Drive Icon Layout"));
    gtk_button_set_image(GTK_BUTTON(drives),
        gtk_image_new_from_icon_name("drive-harddisk", GTK_ICON_SIZE_BUTTON));

    g_signal_connect(wallpaper, "clicked", G_CALLBACK(desktop_wallpaper_button), NULL);
    g_signal_connect(apps, "clicked", G_CALLBACK(desktop_apps_button), NULL);
    g_signal_connect(drives, "clicked", G_CALLBACK(desktop_drives_button), NULL);

    gtk_widget_set_hexpand(wallpaper, TRUE);
    gtk_widget_set_hexpand(apps, TRUE);
    gtk_widget_set_hexpand(drives, TRUE);
    gtk_grid_attach(GTK_GRID(tools_grid), wallpaper, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(tools_grid), apps, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(tools_grid), drives, 0, 1, 2, 1);
    gtk_box_pack_start(GTK_BOX(box), tools_grid, FALSE, FALSE, 0);

    return g_list_append(NULL, box);
}

void desktop_open_wallpaper_manager(void)
{
    desktop_prepare_standalone_tool();
    desktop_show_wallpaper_dialog(desktop_window
        ? GTK_WINDOW(desktop_window) : NULL);
}

void desktop_open_apps_manager(void)
{
    GError *error = NULL;

    desktop_prepare_standalone_tool();
    if (desktop_apps_show_manager(desktop_window
            ? GTK_WINDOW(desktop_window) : NULL,
            desktop_dir, &desktop_icon_size, &desktop_single_click)) {
        if (!desktop_save_preferences(&error)) {
            show_desktop_error(_("Unable to save desktop settings"),
                               error ? error->message : NULL);
            g_clear_error(&error);
        }
    }
    if (desktop_window)
        desktop_reload();
}

void desktop_refresh_now(void)
{
    if (!desktop_window)
        return;
    desktop_reload();
    desktop_arrange_items(TRUE);
    desktop_schedule_geometry_update();
    desktop_refresh_after_environment_change();
}

gboolean desktop_send_refresh_request(void)
{
    const DesktopBackend *backend;
    GdkDisplay *display = gdk_display_get_default();

    backend = desktop_backend_select(display);
    if (!backend || !backend->send_refresh_request)
        return FALSE;
    return backend->send_refresh_request(display);
}

gboolean desktop_is_running(void)
{
    return desktop_window != NULL;
}

GdkWindow *desktop_get_gdk_window(void)
{
    if (!desktop_window || !gtk_widget_get_realized(desktop_window))
        return NULL;
    return gtk_widget_get_window(desktop_window);
}
