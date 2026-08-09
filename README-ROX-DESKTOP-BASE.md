# ROX Desktop r49 notes

- Clears the rectangular native popup window to transparent before GTK renders
  a rounded menu, removing the black blocks that remained in all four corners.
- Uses a rounded X11 window shape as a fallback when no compositor is active.
- The fix is shared by main menus and every submenu without overriding the
  active GTK3 theme colours.

# ROX Desktop r48 notes

- Selected filenames in light themes are rendered entirely by the active GTK
  selected state, including the background, frame and foreground.
- The Drive Icons submenu includes a full EssoraWM-style arrangement dialog and
  a one-click Realign Drive Icons action.
- Drive alignment supports left, centre or right; top, centre or bottom;
  horizontal or vertical packing; icon size; spacing; offsets; labels; frame;
  reverse order; and device filters.
- ROX popup menus use RGBA-aware windows with no forced outer border, fixing
  opaque square corners without overriding theme colours.

# ROX Desktop r45 notes

- Normal and selected filenames in filer windows use the active GTK3 theme.
  The custom collection view resolves the selected foreground and background
  as a matching theme pair, while the details view lets `GtkTreeView` render
  its own colours.
- Drive icons support the EssoraWM-compatible positioning keys `Vertical`,
  `ReversePack`, `SpacingX`, `SpacingY`, `XOffset`, `YOffset`, `XPos` and
  `YPos`, plus internal/removable/network filters, labels and frames.
- The default drive geometry is a 32-pixel horizontal row at the bottom-left,
  using `SpacingX=87`, `SpacingY=87`, `XOffset=20`, `YOffset=-40`, `XPos=0.0`
  and `YPos=1.0`.
- The wallpaper chooser stays open after applying or double-clicking a
  wallpaper, allowing several backgrounds to be previewed in one session.

# ROX Desktop r44 notes

Desktop and drive labels are rendered by GtkLabel again. The custom Cairo/Pango draw callback was removed because it could hide all text on some Puppy builds. GTK CSS now supplies the white text and black outline without a rectangular background.

The safe out-of-tree build correction remains included: `AppRun --compile` invokes `../src/configure` from the build directory, keeping `srcdir` relative even when the extracted package path contains spaces.

- Normal filer colours now come from the active GTK theme; ROX-specific file
  type and pinboard colour controls were removed.
- Desktop and drive labels use white outlined text without translucent slabs.
  Selected items use the active GTK selection colours.

- Default applications are resolved exclusively through GIO/XDG and
  `~/.config/mimeapps.list`; legacy ROX `MIME-types` files are ignored.
- Desktop items reserve the actual drive-icon area and are automatically
  reflowed when a saved position overlaps drives, another icon or the panel.
- Ctrl+click adds or removes desktop items from a multiple selection.
- Delayed geometry refreshes run after startup and wbar changes.

# ROX Desktop experimental base

This development base adds a native desktop mode to Rox-Filer2 while keeping the
normal Rox-Filer2 build process unchanged.

## Compile

Compile in the same way as the rest of this GTK3 fork. The generated binary
remains inside the Rox-Filer2 application directory, and an existing link in
`/usr/bin` can continue to point to it.

## Run the binary directly

```sh
Rox-Filer2 --desktop
```

`AppRun` is not required to launch the desktop mode. The binary discovers the
ROX application directory itself.

If `--desktop` is reported as an unrecognized option, the installed executable
is an older binary and must be rebuilt from this source tree.

## Desktop background

Right-click an empty area of ROX Desktop and select:

```text
Change Desktop Background...
```

The GTK3 selector scans `/usr/share/backgrounds`, shows thumbnails and applies
the selected image immediately. It also provides `Choose Image...` for images
stored elsewhere. The wallpaper style can be set to Fill, Fit, Stretch, Center
or Tile. ROX Desktop reads the monitor layout through GDK/XRandR and draws the
background over the complete screen, including multi-monitor layouts.

The existing image context action remains available inside normal Rox-Filer2
windows:

```text
Copy to Backgrounds...
```

## Desktop programs

Right-click an empty area of ROX Desktop and select:

```text
Add Programs to Desktop...
```

The integrated GTK3 manager is based on the workflow used by EssoraWM. It
scans the XDG application directories, provides a search field and shows each
application with the icon from the active GTK theme. **Add** copies the
selected `.desktop` launcher into the real `XDG_DESKTOP_DIR`; **Remove**
deletes that desktop copy after confirmation. No PuppyPin entry or external
script is required.

Launchers copied this way show their XDG application name and icon on ROX
Desktop and are started through `GDesktopAppInfo` when activated. They can be
moved freely by dragging them. Their coordinates are stored in
`desktop-positions.conf` and restored on the next desktop start.

The application manager also provides the basic desktop icon settings: 24, 32,
48 or 64 pixel icons, and one-click or double-click activation.

## Desktop drives

ROX Desktop uses the same lsblk/sysfs drive model as the Partitions button. It
therefore shows mounted and unmounted usable drives even on Puppy systems where
GVolumeMonitor has no GVfs/UDisks backend.

The icon is selected from the active GTK icon theme according to the device:
optical, USB/removable, SD/MMC, SSD/NVMe, network or internal disk. The same icon
selection is used in the normal ROX Partitions GUI.

Left-click mounts and opens an unmounted drive, or opens a mounted drive.
Right-click provides Mount or Unmount. Eject is shown only for optical drives.
Mounted drives also show a small action button in the upper-right corner: it
unmounts normal volumes and ejects optical media.

By default, drive icons are arranged horizontally at the bottom-left of the
primary monitor work area, above the desktop panel. Right-click the desktop and
open `Drive Icons` to show or hide them, open the complete arrangement dialog,
or realign the group immediately. The dialog supports left, centre or right;
top, centre or bottom; horizontal or vertical packing; icon size; horizontal
and vertical spacing; X/Y offsets; labels; frame; reverse order; device filters;
and the quick unmount overlay.

The drive detector inherits USB, SD/MMC and SSD/NVMe information from the
physical parent device through both `lsblk PKNAME` and sysfs. The desktop and
the normal Partitions GUI resolve the first matching icon that actually exists
in the active GTK theme, using the same shared code. USB icons prefer
`drive-removable-media-usb` and `media-flash-usb` before any generic hard-disk
fallback.

Drive polling runs in a worker thread. A slow `lsblk` or removable device can no
longer block the GTK main loop or delay the desktop context menu.

## wbar integration on X11

ROX Desktop draws its own desktop window, while wbar reads pseudo-transparency
from the X11 root pixmap. When the wallpaper changes, ROX mirrors it to the root
window with `hsetroot`, `feh` or `xwallpaper`. If wbar is already running, ROX
uses the same guarded refresh sequence as EssoraWM: detect live PIDs in `/proc`,
ignore zombies, stop the old instance completely, reapply the wallpaper and
start exactly one new wbar process. This work is asynchronous and does not block
menus or desktop interaction.

## Configuration

Desktop settings are stored in the traditional ROX location:

```text
~/.config/rox.sourceforge.net/ROX-Filer/desktop.conf
```

Movable desktop icon positions are stored in:

```text
~/.config/rox.sourceforge.net/ROX-Filer/desktop-positions.conf
```

Right-click the desktop and open **Desktop Preferences...** to configure icon
sizes, one-click or double-click activation, grid snapping and drive layout.

Hidden drives are stored in:

```text
~/.config/rox.sourceforge.net/ROX-Filer/hidden-drives
```

`Rox-Filer2 --desktop` is the only public desktop entry point. The old named
pinboard command-line and SOAP interfaces were removed in r64. Since r66, the
classic pinboard implementation and its task list are no longer compiled or
shipped; classic panel compatibility remains available through `desktop.c`.

## Centred operation windows and animations

Rox-Filer2 centres its operation and auxiliary dialogs when they are shown.
Copy and Move display `Rox-Filer2/images/rox_copi.gif`; Trash and permanent or
legacy deletion display `Rox-Filer2/images/rox_delet.gif`. These GIF files are
loaded internally by GTK3 and do not require SendTo scripts or external
programs. The normal ROX progress bar and operation log remain active so the
animation is decorative and does not replace real progress or error details.
## Revision r31 fixes

ROX Desktop and the normal **Partitions** GUI now resolve drive icons through
one explicit GTK-theme lookup. USB, SD/MMC, optical, SSD/NVMe, network and
internal devices therefore use the same available icon in both interfaces.

Right-click a `.desktop` launcher on the desktop and choose **Remove** to delete
only its copy from `XDG_DESKTOP_DIR`. The installed application and its system
launcher are not modified.

Animated copy, move and delete windows hide the empty operation log by default.
Use **Details** to expand it. ROX opens the details automatically when an error
or a conflict question must be shown.

Desktop label visibility fix: GtkLabel performs the normal text rendering and GTK CSS supplies the outline. No custom draw callback or rectangular backing is used.


## Desktop interaction in r52

Desktop files and launchers can be opened, dragged and moved to Trash again.
Use Ctrl-click, Shift-click or Ctrl+Alt-click to select multiple icons. Dragging
one selected icon moves the selected group; Delete removes the selection and
Enter opens it. Drive icons share the same fixed layer without blocking input.


## r66: módulo de escritorio único

Desde r66, `desktop.c` y `desktop.h` son la única implementación de escritorio
que se compila. El antiguo `pinboard.c`, su tasklist y el DND exclusivo del
pinboard fueron retirados. Los paneles heredados consultan la ventana del
escritorio mediante `desktop_get_gdk_window()`, sin volver a introducir
pinboards con nombre.

## r67: backend X11 separado

Desde r67, `desktop.c` conserva la lógica común del escritorio: iconos,
posiciones, wallpaper, unidades, menús y monitores. La integración propia de
X11 fue trasladada a `desktop-x11.c` detrás de `desktop-backend.h`:

- propiedades `_ROX_DESKTOP_WINDOW` y `_ROX_DESKTOP_REFRESH`;
- mensajes `ClientMessage` entre instancias;
- sugerencias EWMH de ventana de escritorio;
- colocación al fondo de la pila X11.

`desktop-backend.c` selecciona el backend compatible con el `GdkDisplay`. Cuando
el display todavía no tiene backend propio, el selector usa temporalmente un
backend GTK genérico. Esta separación prepara `desktop-wayland.c` sin
cambiar el comando público `rox --desktop`.



## r68: menú y diagnóstico MIME

Cortar, Copiar y Pegar se muestran juntos en el menú principal del filer. La
operación Cortar reutiliza el portapapeles XDG ya existente y mueve los archivos
al ejecutar Pegar. También se protege la consulta de `fstab_mounts` durante
`Rox-Filer2 -m`, evitando el aviso crítico observado al consultar carpetas.


## r70: apertura XDG y diagnóstico de terminal

Abrir con ejecuta primero el `Exec=` del archivo `.desktop` y conserva GIO como
respaldo. Ejecutar en terminal interpreta de forma explícita los shebang,
incluidos CRLF y `/usr/bin/env -S`. El diagnóstico 1.3 prueba ambos caminos con
aplicaciones y scripts temporales sin modificar las asociaciones del usuario.
También acepta `--test-script FILE` (repetible) y `--geany-file FILE` para
registrar casos reales indicados por el usuario.

## r69: barra de tamaño y terminal

La barra del filer muestra controles separados para reducir iconos, usar el
tamaño automático y aumentar iconos. Ejecutar en terminal usa ahora un runner
temporal y adapta el separador de ejecución a terminales comunes, incluyendo
los wrappers `defaultterminal` y `x-terminal-emulator`.

## r71: runtime reliability

This release does not change the desktop backend boundary. It fixes shared file
manager behavior: interpreter selection for scripts, standards-first XDG app
launching, and reliable presentation of the Rename dialog.

## r72: diagnóstico de la sesión real

El lanzador de scripts cambia explícitamente al directorio del archivo antes de
ejecutarlo. El registro opcional `ROX_DEBUG_LOG` permite comprobar qué aplicación
MIME, terminal, intérprete y argumentos usa realmente la sesión. El diagnóstico
1.5 también avisa cuando sigue activo un proceso ROX iniciado antes de instalar
el binario nuevo.


## Backend Wayland experimental para Labwc (r73)

La r73 añade un primer backend `desktop-wayland.c` que carga GTK Layer Shell
en tiempo de ejecución. El mismo binario puede iniciarse mediante:

```sh
Rox-Filer2 --desktop     # selección automática según el display GTK
rox-x11 --desktop       # fuerza X11/Xwayland
rox-wayland --desktop   # fuerza Wayland + Layer Shell
```

La primera etapa está dirigida a una sesión Labwc de una sola salida. Conserva
la lógica común de iconos, unidades, wallpaper, menús y operaciones en
`desktop.c`. Requiere `libgtk-layer-shell.so.0` y que Labwc publique
`zwlr_layer_shell_v1`. Si falta alguno, ROX muestra un error y no cae
silenciosamente a una ventana Wayland normal ni a Xwayland.

Para probar dentro de Labwc:

```sh
GDK_BACKEND=wayland rox-wayland --desktop
```

Una segunda ejecución envía una actualización a la instancia existente usando
un socket Unix en `$XDG_RUNTIME_DIR`.


## Registro técnico opcional (r74, ampliado en r75)

El registro está desactivado por defecto. Se activa únicamente con `--debug`,
`--log-file` o `--log-level`. Sin esas opciones Rox-Filer2 no crea archivos de
registro. La ruta predeterminada sigue XDG: `$XDG_STATE_HOME/rox-filer/` o
`~/.local/state/rox-filer/`. Se conservan como máximo cinco archivos de 2 MiB.

```sh
rox-x11 --desktop --debug
rox-wayland --desktop --debug --log-level=trace
Rox-Filer2 --clear-logs
```

El log registra la selección del backend, X11, GTK Layer Shell, geometría,
wallpaper, iconos, unidades, asociaciones MIME, ejecución de aplicaciones,
terminales, intérpretes de scripts y renombrado.
