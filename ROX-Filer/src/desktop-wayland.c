/*
 * Agregado por josejp2424 (2026): backend Wayland experimental para Labwc.
 *
 * Carga gtk-layer-shell en tiempo de ejecución para conservar un único
 * binario que también funcione en X11 aunque la biblioteca no esté instalada.
 * La primera etapa usa una superficie de fondo en la salida predeterminada;
 * la lógica de iconos, unidades, wallpaper y menús permanece en desktop.c.
 */
#include "config.h"

#include <gtk/gtk.h>
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "desktop-backend.h"
#include "debug_log.h"

#define ROX_WAYLAND_CONTROL_MESSAGE "refresh"
#define ROX_WAYLAND_NAMESPACE "rox-desktop"

/* Valores ABI públicos de gtk-layer-shell. Se declaran aquí para que
 * Rox-Filer2 no requiera sus cabeceras ni enlace obligatorio en X11. */
typedef enum {
    ROX_LAYER_BACKGROUND = 0,
    ROX_LAYER_BOTTOM = 1,
    ROX_LAYER_TOP = 2,
    ROX_LAYER_OVERLAY = 3
} RoxLayerShellLayer;

typedef enum {
    ROX_LAYER_EDGE_LEFT = 0,
    ROX_LAYER_EDGE_RIGHT = 1,
    ROX_LAYER_EDGE_TOP = 2,
    ROX_LAYER_EDGE_BOTTOM = 3
} RoxLayerShellEdge;

typedef gboolean (*LayerIsSupportedFunc)(void);
typedef void (*LayerInitForWindowFunc)(GtkWindow *window);
typedef void (*LayerSetLayerFunc)(GtkWindow *window, RoxLayerShellLayer layer);
typedef void (*LayerSetAnchorFunc)(GtkWindow *window, RoxLayerShellEdge edge,
                                    gboolean anchor_to_edge);
typedef void (*LayerSetExclusiveZoneFunc)(GtkWindow *window, gint exclusive_zone);
typedef void (*LayerSetNamespaceFunc)(GtkWindow *window, const gchar *name_space);
typedef void (*LayerSetKeyboardInteractivityFunc)(GtkWindow *window,
                                                   gboolean interactivity);
typedef void (*LayerSetKeyboardModeFunc)(GtkWindow *window, gint mode);

static void *layer_module;
static LayerIsSupportedFunc layer_is_supported;
static LayerInitForWindowFunc layer_init_for_window;
static LayerSetLayerFunc layer_set_layer;
static LayerSetAnchorFunc layer_set_anchor;
static LayerSetExclusiveZoneFunc layer_set_exclusive_zone;
static LayerSetNamespaceFunc layer_set_namespace;
static LayerSetKeyboardInteractivityFunc layer_set_keyboard_interactivity;
static LayerSetKeyboardModeFunc layer_set_keyboard_mode;
static gboolean layer_load_attempted;
static gchar *layer_load_error;

static gint control_fd = -1;
static GIOChannel *control_channel;
static guint control_source;
static gchar *control_path;
static DesktopBackendRefreshFunc control_refresh;
static gpointer control_refresh_data;

static GQuark desktop_wayland_error_quark(void)
{
    return g_quark_from_static_string("rox-desktop-wayland-error");
}

static gboolean load_symbol(void **target, const gchar *name)
{
    const gchar *error;

    dlerror();
    *target = dlsym(layer_module, name);
    error = dlerror();
    if (error != NULL || *target == NULL) {
        ROX_LOG_ERROR("wayland", "missing gtk-layer-shell symbol=%s error=%s",
                      name, error ? error : "unknown");
        g_free(layer_load_error);
        layer_load_error = g_strdup_printf("No se encontró %s en libgtk-layer-shell.so.0", name);
        return FALSE;
    }
    return TRUE;
}

static gboolean desktop_wayland_load_layer_shell(GError **error)
{
    if (!layer_load_attempted) {
        layer_load_attempted = TRUE;
        ROX_LOG_INFO("wayland", "loading gtk-layer-shell dynamically");
        layer_module = dlopen("libgtk-layer-shell.so.0", RTLD_NOW | RTLD_LOCAL);
        if (!layer_module)
            layer_module = dlopen("libgtk-layer-shell.so", RTLD_NOW | RTLD_LOCAL);
        if (!layer_module) {
            const gchar *detail = dlerror();
            ROX_LOG_ERROR("wayland", "unable to load gtk-layer-shell: %s",
                          detail ? detail : "library unavailable");
            layer_load_error = g_strdup_printf(
                "No se pudo cargar libgtk-layer-shell.so.0: %s",
                detail ? detail : "biblioteca no instalada");
        } else if (!load_symbol((void **)&layer_is_supported,
                                "gtk_layer_is_supported") ||
                   !load_symbol((void **)&layer_init_for_window,
                                "gtk_layer_init_for_window") ||
                   !load_symbol((void **)&layer_set_layer,
                                "gtk_layer_set_layer") ||
                   !load_symbol((void **)&layer_set_anchor,
                                "gtk_layer_set_anchor") ||
                   !load_symbol((void **)&layer_set_exclusive_zone,
                                "gtk_layer_set_exclusive_zone") ||
                   !load_symbol((void **)&layer_set_namespace,
                                "gtk_layer_set_namespace")) {
            dlclose(layer_module);
            layer_module = NULL;
        } else {
            /* API nueva primero; la función booleana antigua se conserva
             * como respaldo para versiones previas de gtk-layer-shell. */
            dlerror();
            layer_set_keyboard_mode = (LayerSetKeyboardModeFunc)
                dlsym(layer_module, "gtk_layer_set_keyboard_mode");
            dlerror();
            layer_set_keyboard_interactivity =
                (LayerSetKeyboardInteractivityFunc)
                dlsym(layer_module, "gtk_layer_set_keyboard_interactivity");
            ROX_LOG_DEBUG("wayland", "gtk-layer-shell loaded; keyboard_mode=%d legacy_keyboard=%d",
                          layer_set_keyboard_mode != NULL,
                          layer_set_keyboard_interactivity != NULL);
            if (!layer_set_keyboard_mode &&
                !layer_set_keyboard_interactivity) {
                g_free(layer_load_error);
                layer_load_error = g_strdup(
                    "La biblioteca no ofrece control de teclado Layer Shell");
                dlclose(layer_module);
                layer_module = NULL;
            }
        }
    }

    if (!layer_module) {
        ROX_LOG_ERROR("wayland", "gtk-layer-shell unavailable: %s",
                      layer_load_error ? layer_load_error : "unknown");
        g_set_error(error, desktop_wayland_error_quark(), 1,
                    "%s. Instale el paquete gtk-layer-shell para usar "
                    "rox-wayland --desktop.",
                    layer_load_error ? layer_load_error :
                    "GTK Layer Shell no está disponible");
        return FALSE;
    }

    if (!layer_is_supported()) {
        ROX_LOG_ERROR("wayland", "compositor does not publish zwlr_layer_shell_v1");
        g_set_error(error, desktop_wayland_error_quark(), 2,
                    "El compositor Wayland no publica "
                    "zwlr_layer_shell_v1. Esta versión experimental está "
                    "destinada a Labwc y otros compositores compatibles.");
        return FALSE;
    }

    ROX_LOG_INFO("wayland", "gtk-layer-shell and compositor protocol available");
    return TRUE;
}

static gboolean desktop_wayland_supports_display(GdkDisplay *display)
{
#ifdef GDK_WINDOWING_WAYLAND
    return display != NULL && GDK_IS_WAYLAND_DISPLAY(display);
#else
    (void)display;
    return FALSE;
#endif
}

static gboolean desktop_wayland_prepare_display(GdkDisplay *display,
                                                  GError **error)
{
    if (!desktop_wayland_supports_display(display)) {
        ROX_LOG_ERROR("wayland", "display is not a native Wayland GdkDisplay: %s",
                      display ? G_OBJECT_TYPE_NAME(display) : "none");
        g_set_error(error, desktop_wayland_error_quark(), 3,
                    "GTK no está usando Wayland. Inicie con rox-wayland "
                    "dentro de una sesión Labwc.");
        return FALSE;
    }
    ROX_LOG_INFO("wayland", "preparing display name=%s",
                 gdk_display_get_name(display));
    return desktop_wayland_load_layer_shell(error);
}

static void desktop_wayland_configure_window(GtkWindow *window)
{
    gint edge;

    g_return_if_fail(GTK_IS_WINDOW(window));
    g_return_if_fail(layer_module != NULL);

    ROX_LOG_INFO("wayland", "configuring Layer Shell window namespace=%s layer=background",
                 ROX_WAYLAND_NAMESPACE);
    /* Debe ejecutarse antes de que GTK realice la ventana. */
    layer_init_for_window(window);
    layer_set_namespace(window, ROX_WAYLAND_NAMESPACE);
    layer_set_layer(window, ROX_LAYER_BACKGROUND);
    for (edge = ROX_LAYER_EDGE_LEFT; edge <= ROX_LAYER_EDGE_BOTTOM; edge++)
        layer_set_anchor(window, (RoxLayerShellEdge)edge, TRUE);

    /* -1 permite que el wallpaper llegue debajo de paneles Layer Shell sin
     * reservar espacio de trabajo. Los widgets siguen recibiendo puntero. */
    layer_set_exclusive_zone(window, -1);
    if (layer_set_keyboard_mode)
        layer_set_keyboard_mode(window, 0); /* NONE */
    else
        layer_set_keyboard_interactivity(window, FALSE);

    gtk_window_set_skip_taskbar_hint(window, TRUE);
    gtk_window_set_skip_pager_hint(window, TRUE);
    gtk_window_set_accept_focus(window, TRUE);
    gtk_window_set_focus_on_map(window, FALSE);
}

static gchar *desktop_wayland_control_path(void)
{
    const gchar *runtime = g_get_user_runtime_dir();

    if (!runtime || !*runtime)
        runtime = g_get_tmp_dir();
    return g_strdup_printf("%s/rox-filer-desktop-wayland-%lu.sock",
                           runtime, (gulong)getuid());
}

static gboolean desktop_wayland_control_cb(GIOChannel *source,
                                            GIOCondition condition,
                                            gpointer data)
{
    gchar buffer[64];
    ssize_t size;
    (void)source;
    (void)data;

    if (condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL))
        return TRUE;

    size = recv(control_fd, buffer, sizeof(buffer) - 1, 0);
    if (size <= 0)
        return TRUE;
    buffer[size] = '\0';
    if (g_str_has_prefix(buffer, ROX_WAYLAND_CONTROL_MESSAGE) &&
        control_refresh) {
        ROX_LOG_INFO("wayland", "received desktop refresh request");
        control_refresh(control_refresh_data);
    }
    return TRUE;
}

static void desktop_wayland_unregister_control(GtkWidget *window)
{
    (void)window;

    if (control_source) {
        g_source_remove(control_source);
        control_source = 0;
    }
    if (control_channel) {
        g_io_channel_unref(control_channel);
        control_channel = NULL;
    }
    if (control_fd >= 0) {
        close(control_fd);
        control_fd = -1;
    }
    if (control_path) {
        unlink(control_path);
        g_clear_pointer(&control_path, g_free);
    }
    control_refresh = NULL;
    control_refresh_data = NULL;
    ROX_LOG_DEBUG("wayland", "desktop control socket unregistered");
}

static void desktop_wayland_register_control(GtkWidget *window,
                                              DesktopBackendRefreshFunc refresh,
                                              gpointer user_data)
{
    struct sockaddr_un address;
    gint flags;
    (void)window;

    desktop_wayland_unregister_control(NULL);
    control_path = desktop_wayland_control_path();
    control_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (control_fd < 0) {
        ROX_LOG_ERROR("wayland", "unable to create control socket: %s",
                      g_strerror(errno));
        return;
    }

    flags = fcntl(control_fd, F_GETFD, 0);
    if (flags >= 0)
        fcntl(control_fd, F_SETFD, flags | FD_CLOEXEC);

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (strlen(control_path) >= sizeof(address.sun_path)) {
        ROX_LOG_ERROR("wayland", "control socket path is too long: %s", control_path);
        desktop_wayland_unregister_control(NULL);
        return;
    }
    g_strlcpy(address.sun_path, control_path, sizeof(address.sun_path));

    /* desktop_send_refresh_request ya comprobó la instancia existente.
     * Si quedó un socket huérfano de una caída anterior, se reemplaza. */
    unlink(control_path);
    if (bind(control_fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        ROX_LOG_ERROR("wayland", "unable to bind control socket %s: %s",
                      control_path, g_strerror(errno));
        desktop_wayland_unregister_control(NULL);
        return;
    }

    control_refresh = refresh;
    control_refresh_data = user_data;
    control_channel = g_io_channel_unix_new(control_fd);
    g_io_channel_set_encoding(control_channel, NULL, NULL);
    g_io_channel_set_buffered(control_channel, FALSE);
    control_source = g_io_add_watch(control_channel,
        G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
        desktop_wayland_control_cb, NULL);
    ROX_LOG_INFO("wayland", "desktop control socket ready path=%s", control_path);
}

static void desktop_wayland_lower_window(GtkWidget *window)
{
    /* La capa BACKGROUND determina el orden; Wayland no permite bajar una
     * superficie mediante coordenadas o operaciones globales. */
    (void)window;
}

static gboolean desktop_wayland_send_refresh_request(GdkDisplay *display)
{
    struct sockaddr_un address;
    gchar *path;
    gint fd;
    ssize_t sent;
    (void)display;

    path = desktop_wayland_control_path();
    fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        g_free(path);
        return FALSE;
    }

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof(address.sun_path)) {
        close(fd);
        g_free(path);
        return FALSE;
    }
    g_strlcpy(address.sun_path, path, sizeof(address.sun_path));
    sent = sendto(fd, ROX_WAYLAND_CONTROL_MESSAGE,
                  strlen(ROX_WAYLAND_CONTROL_MESSAGE), 0,
                  (struct sockaddr *)&address, sizeof(address));
    close(fd);

    if (sent < 0 && (errno == ENOENT || errno == ECONNREFUSED))
        unlink(path);
    ROX_LOG_DEBUG("wayland", "refresh request path=%s result=%d error=%s",
                  path, sent >= 0, sent < 0 ? g_strerror(errno) : "none");
    g_free(path);
    return sent >= 0;
}

const DesktopBackend *desktop_wayland_backend_get(void)
{
    static const DesktopBackend backend = {
        "wayland-layer-shell",
        desktop_wayland_supports_display,
        desktop_wayland_prepare_display,
        desktop_wayland_configure_window,
        desktop_wayland_register_control,
        desktop_wayland_unregister_control,
        desktop_wayland_lower_window,
        desktop_wayland_send_refresh_request
    };

    return &backend;
}
