#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Genera ROX-Filer/Help/Manual*.html para todos los idiomas de la interfaz.

Agregado en 2.12.2-82.

Hasta 2.12.2-81 el menu Ayuda > Manual llamaba a run_by_path() sobre
app_dir/Help/Manual-<lang>.html, pero ROX-Filer/Help/ solo contenia COPYING y
README: el manual no existia y el menu no hacia nada. Desde 2.12.2-83 el menu
Moderno deja el acceso directo al manual fuera de Ayuda: los HTML se conservan
en Help y se abren externamente con el navegador apropiado del sistema. Las fuentes historicas
en ROX-Filer/src/Docs/ son DocBook de 2005 que describen el ROX-Filer antiguo
y no cubren la interfaz Moderna, SMB, el escritorio ni Wayland.

Este generador escribe un manual actual desde una sola fuente estructurada,
para que las once traducciones no se desincronicen entre si.

Uso:  python3 tools/make-manual.py [directorio-de-salida]
"""

import html
import os
import shutil
import sys

# Idiomas de la interfaz.  "en" produce Manual.html (respaldo de menu.c).
LANGS = ["en", "es", "pt", "fr", "it", "ca", "de", "hu", "ru", "ja", "zh", "ar"]
RTL = {"ar"}

VERSION = "2.13.0-8"

CSS = """
:root {
  --bg: #ffffff; --fg: #1c1f24; --muted: #5a6270;
  --accent: #2f6fb0; --rule: #dde2e8; --code-bg: #f4f6f8;
}
@media (prefers-color-scheme: dark) {
  :root { --bg: #1b1e23; --fg: #e6e9ee; --muted: #9aa4b2;
          --accent: #6fa8dc; --rule: #333941; --code-bg: #23272e; }
}
* { box-sizing: border-box; }
body {
  margin: 0; padding: 0 1.2rem 4rem;
  background: var(--bg); color: var(--fg);
  font-family: system-ui, "DejaVu Sans", "Noto Sans", sans-serif;
  font-size: 16px; line-height: 1.65;
}
main { max-width: 46rem; margin: 0 auto; }
header { border-bottom: 2px solid var(--accent); margin: 2rem 0 1.5rem; }
.brand { display: flex; align-items: center; gap: 1rem; margin-bottom: 1rem; }
.manual-logo { width: 72px; height: 72px; flex: 0 0 72px; object-fit: contain; }
.brand-copy { min-width: 0; }
h1 { font-size: 1.9rem; margin: 0 0 .3rem; }
.sub { color: var(--muted); margin: 0; font-size: .95rem; }
@media (max-width: 34rem) {
  .manual-logo { width: 56px; height: 56px; flex-basis: 56px; }
  h1 { font-size: 1.55rem; }
}
h2 {
  font-size: 1.25rem; margin: 2.2rem 0 .6rem;
  padding-bottom: .25rem; border-bottom: 1px solid var(--rule);
}
p { margin: .7rem 0; }
ul { margin: .6rem 0 .6rem 1.3rem; padding: 0; }
li { margin: .3rem 0; }
code, kbd {
  background: var(--code-bg); border: 1px solid var(--rule);
  border-radius: 4px; padding: .08em .35em;
  font-family: "DejaVu Sans Mono", ui-monospace, monospace; font-size: .9em;
}
nav.toc {
  background: var(--code-bg); border: 1px solid var(--rule);
  border-radius: 8px; padding: .9rem 1.2rem; margin: 1.5rem 0;
}
nav.toc ol { margin: .3rem 0 0 1.2rem; padding: 0; }
nav.toc a { color: var(--accent); text-decoration: none; }
nav.toc a:hover { text-decoration: underline; }
footer {
  margin-top: 3rem; padding-top: 1rem; border-top: 1px solid var(--rule);
  color: var(--muted); font-size: .88rem;
}
[dir="rtl"] ul, [dir="rtl"] nav.toc ol { margin: .6rem 1.3rem .6rem 0; }
"""

# Orden de las secciones.  Cada idioma debe definir exactamente estas claves.
SECTION_ORDER = [
    "intro", "interfaces", "window", "desktop", "drives",
    "images", "network", "trash", "mime", "actions", "tools",
    "options", "cmdline", "diagnostics", "support",
]

TEXTS = {}

TEXTS["en"] = {
    "_title": "Rox-Filer2 Manual",
    "_sub": "Fast and lightweight file manager for X11 and Wayland, continued from ROX-Filer.",
    "_toc": "Contents",
    "_footer": ("Rox-Filer2 is free software distributed under the GNU General "
                "Public License, version 2 or later. Original ROX-Filer "
                "&copy; 2005 Thomas Leonard and contributors; Rox-Filer2 "
                "continuation &copy; 2026 josejp2424."),
    "intro": ("What is Rox-Filer2", [
        "<p>Rox-Filer2 is a continuation of ROX-Filer, brought up to GTK3 and "
        "extended with a modern interface, native SMB support, drive icons and "
        "a desktop mode that works on both X11 and Wayland.</p>",
        "<p>It does one job in one process: it manages files, and optionally it "
        "manages your desktop. There is no daemon, no virtual filesystem layer "
        "and no dependency on a desktop environment. Everything you see is "
        "reading ordinary POSIX paths.</p>",
    ]),
    "interfaces": ("Classic and Modern interfaces", [
        "<p>Rox-Filer2 ships two interface styles that share the same file, MIME, "
        "mount, Trash and Desktop backends.</p>",
        "<ul>"
        "<li><b>Classic ROX</b> keeps the traditional ROX-Filer layout and "
        "toolbar. If you are coming from ROX-Filer, nothing moved.</li>"
        "<li><b>Modern</b> adds a menu bar, a navigation row, tabs and a sidebar "
        "with Places, Devices and Network.</li>"
        "</ul>",
        "<p>Choose the permanent style in <b>Options &gt; Interface</b>. The "
        "change applies to windows opened after you restart Rox-Filer2.</p>",
        "<p>To try a style once without changing the saved preference, launch "
        "<code>rox --classic</code> or <code>rox --modern</code>.</p>",
    ]),
    "window": ("The filer window", [
        "<p>Open directories by double-clicking them. A single click selects; "
        "drag with the left button on empty space to rubber-band select.</p>",
        "<ul>"
        "<li>The right button opens the context menu for the item under the "
        "pointer, or for the whole window on empty space.</li>"
        "<li>The middle button opens a directory in a new window.</li>"
        "<li><kbd>Backspace</kbd> goes up one level; <kbd>Ctrl+L</kbd> focuses "
        "the path entry.</li>"
        "<li>In Modern, <kbd>Ctrl+T</kbd> opens a tab and <kbd>Ctrl+W</kbd> "
        "closes it. Each tab keeps its own path and history.</li>"
        "</ul>",
        "<p>Switch between the icon view and the detailed list view from the "
        "view switcher, and change icon size with the slider.</p>",
    ]),
    "desktop": ("ROX Desktop", [
        "<p>Start the desktop with <code>rox --desktop</code>. Only one desktop "
        "instance runs at a time. It draws the wallpaper, shows the icons of "
        "your desktop directory, and can show drive icons.</p>",
        "<ul>"
        "<li><b>Wallpaper:</b> <code>rox --desktop-wallpaper</code>, or right-click "
        "the desktop. Modes are fill, fit, stretch, centre and tile.</li>"
        "<li><b>Applications:</b> <code>rox --desktop-apps</code> manages the "
        "launchers shown on the desktop.</li>"
        "<li><b>Drive icons:</b> <code>rox --desktop-drive-icon-layout</code> "
        "arranges them.</li>"
        "<li><b>Preferences:</b> <code>rox --desktop-preferences</code>.</li>"
        "</ul>",
        "<p>On X11 the desktop uses a normal desktop-hint window. On Wayland it "
        "uses the Layer Shell background layer, which needs "
        "<code>gtk-layer-shell</code> installed and a compositor that publishes "
        "<code>zwlr_layer_shell_v1</code>, such as Labwc. Force a backend with "
        "<code>rox-x11 --desktop</code> or <code>rox-wayland --desktop</code>.</p>",
        "<p>To also set the X11 root window background, so that pseudo-transparent "
        "terminals and conky see the same image, install <code>feh</code> or "
        "<code>xwallpaper</code>.</p>",
    ]),
    "drives": ("Drives and mounting", [
        "<p>Local and removable drives appear in the Modern sidebar under "
        "<b>Devices</b>, and optionally as desktop icons. Click a drive to mount "
        "and open it; use the context menu to unmount it.</p>",
        "<p>Drive information comes from <code>lsblk</code>, from "
        "<code>/proc/mounts</code> and from sysfs, so no background service is "
        "required. Mounting an unmounted drive as a normal user needs "
        "<code>udisksctl</code> or an fstab entry that permits it.</p>",
        "<p>Always unmount removable media before unplugging it. Rox-Filer2 does "
        "not silently write to a device after you close its window, but the "
        "kernel may still hold unflushed data.</p>",
    ]),
    "images": ("Disk images (ISO, IMG, SFS, SquashFS)", [
        "<p>Right-click an image file and choose <b>Mount Image</b>. Rox-Filer2 "
        "attaches it to a loop device and mounts it read-only, then opens the "
        "result like any other directory. Supported extensions are "
        "<code>.iso</code>, <code>.img</code>, <code>.sfs</code>, "
        "<code>.squashfs</code>, <code>.sqfs</code> and <code>.sqsh</code>.</p>",
        "<p>Once mounted, the same menu offers <b>Open Mounted Image</b> and "
        "<b>Unmount Image</b>. Mounts live under "
        "<code>/media/rox-filer2-images/</code>, one directory per image.</p>",
        "<ul>"
        "<li><b>Always read-only.</b> The loop device, the block device and the "
        "mount are all set read-only, so an image can never be modified by "
        "accident.</li>"
        "<li><b>As root</b> (the usual Puppy and Essora model) it uses "
        "<code>losetup</code> and <code>mount</code> directly. As a normal user "
        "it uses <code>udisksctl</code>, which must be installed.</li>"
        "<li><b>Raw <code>.img</code> files</b> may contain a partition table. "
        "When more than one partition is found, Rox-Filer2 asks which to mount "
        "and shows each partition's real number, filesystem, size and label. "
        "ISO and SquashFS images are always mounted whole, even when a hybrid "
        "ISO exposes a partition table.</li>"
        "<li><b>No GVfs.</b> The image becomes an ordinary local mount, so "
        "copying, drag and drop, MIME handling and custom actions all behave "
        "exactly as they do on any other directory.</li>"
        "</ul>",
        "<p>Mounting runs in the background: a small window with a spinner "
        "appears while the loop device is set up and the partitions are "
        "scanned, and the filer stays responsive.</p>",
        "<p>Unmount before deleting or moving the image file. If a mount "
        "disappears behind Rox-Filer2's back, the menu entries return to "
        "offering <b>Mount Image</b> again.</p>",
    ]),
    "network": ("SMB and Windows shares", [
        "<p>Open <b>Go &gt; Connect to SMB Share</b>, or <b>Browse Network</b> in "
        "the Modern sidebar. Enter the server and, if you know it, the share; "
        "the list button asks the server which shares it offers.</p>",
        "<p>Rox-Filer2 mounts the share as an ordinary local mount using "
        "<code>mount.cifs</code>, rather than presenting it through a virtual "
        "filesystem. That means copying, drag and drop, MIME handling, Trash and "
        "custom actions all work exactly as they do on local files.</p>",
        "<ul>"
        "<li><code>cifs-utils</code> must be installed.</li>"
        "<li>Mounting needs root. As a normal user, Rox-Filer2 tries "
        "<code>sudo -n</code>; a passwordless rule for <code>mount.cifs</code> "
        "effectively grants root, so grant it deliberately or run the mount "
        "yourself.</li>"
        "<li>Your password is never written to the configuration. It is passed "
        "through a temporary credentials file readable only by you, which is "
        "deleted as soon as <code>mount.cifs</code> returns. Only the server, "
        "share, user and domain are remembered.</li>"
        "</ul>",
    ]),
    "trash": ("Trash", [
        "<p>Deleting through the menu moves items to the XDG Trash, so they can "
        "be restored. Restore from the Trash directory's context menu.</p>",
        "<p>Trash only works within the same filesystem as the trash directory. "
        "Deleting a file on another partition or on a removable device may not "
        "be recoverable, and Rox-Filer2 will tell you when that is the case.</p>",
    ]),
    "mime": ("File types and Open With", [
        "<p>File types come from the shared MIME database, plus any extended "
        "attribute set on the file itself. The context menu lists the "
        "applications registered for that type under <b>Open With</b>.</p>",
        "<p>Set a permanent handler, or a custom icon, from the item's "
        "<b>Set Icon</b> and <b>Set Run Action</b> entries. These are stored in "
        "your own configuration and never modify the system database.</p>",
    ]),
    "actions": ("Custom actions and templates", [
        "<p>Custom actions add your own commands to the context menu, receiving "
        "the selected paths as arguments. They are ordinary desktop entries, so "
        "you can copy one between machines.</p>",
        "<p>Templates let you create a new empty file of a given type from the "
        "context menu. Add your own by dropping a file into the Templates "
        "directory of the application directory.</p>",
    ]),
    "tools": ("Search, bookmarks and paired windows", [
        "<ul>"
        "<li><b>Search:</b> <code>rox-find</code> searches by name, size, type "
        "and content, and hands results back to the filer.</li>"
        "<li><b>Bookmarks:</b> keep frequently used directories one click away; "
        "in Modern they also appear under Places.</li>"
        "<li><b>Paired windows:</b> <code>rox --pair</code> opens two windows "
        "side by side for copying between directories, and "
        "<code>rox --pair-realign</code> re-aligns them.</li>"
        "</ul>",
    ]),
    "options": ("Options and configuration files", [
        "<p>Open <b>Options</b> from the Edit menu, from the desktop menu or with "
        "<code>rox --config-rox</code>.</p>",
        "<p>Your settings live under the XDG configuration directory, normally "
        "<code>~/.config/rox.sourceforge.net/ROX-Filer/</code>. The files are "
        "plain XML, INI and text, so they can be backed up, edited or copied "
        "between machines. Deleting a file restores that group of defaults.</p>",
    ]),
    "cmdline": ("Command line", [
        "<ul>"
        "<li><code>rox</code> &mdash; selects the X11 or Wayland backend "
        "automatically.</li>"
        "<li><code>rox-x11</code>, <code>rox-wayland</code> &mdash; force one "
        "backend.</li>"
        "<li><code>rox DIR</code> &mdash; open a directory.</li>"
        "<li><code>rox -d DIR</code> &mdash; open as a directory even if it is an "
        "application directory.</li>"
        "<li><code>rox -D DIR</code> &mdash; close that directory and its "
        "subdirectories.</li>"
        "<li><code>rox --classic</code>, <code>rox --modern</code> &mdash; "
        "temporary interface override.</li>"
        "<li><code>rox --desktop</code> &mdash; start the desktop; "
        "<code>--desktop-refresh</code> refreshes a running one.</li>"
        "<li><code>rox --help</code> &mdash; the complete list.</li>"
        "</ul>",
    ]),
    "diagnostics": ("Diagnostics", [
        "<p>Logging is off by default. Start with <code>rox --debug</code> to "
        "write a technical log, <code>--log-file=FILE</code> to choose where, and "
        "<code>--log-level=LEVEL</code> with one of error, warning, info, debug "
        "or trace. <code>rox --clear-logs</code> removes the logs Rox-Filer2 "
        "manages automatically.</p>",
        "<p>Include that log when reporting a problem. It records which desktop "
        "backend was selected, which interface style is active and why an SMB "
        "mount or a drive scan failed.</p>",
    ]),
    "support": ("Reporting problems", [
        "<p>Report bugs and suggestions at the project page, "
        "<a href=\"https://github.com/josejp2424/ROX-Filer-gtk3\">"
        "github.com/josejp2424/ROX-Filer-gtk3</a>.</p>",
        "<p>Please say which version you are running (see <b>Help &gt; About "
        "Rox-Filer2</b>), whether you use X11 or Wayland, which interface style, "
        "and the exact steps that reproduce the problem.</p>",
    ]),
}

TEXTS["es"] = {
    "_title": "Manual de Rox-Filer2",
    "_sub": "Gestor de archivos rápido y liviano para X11 y Wayland, continuación de ROX-Filer.",
    "_toc": "Contenido",
    "_footer": ("Rox-Filer2 es software libre distribuido bajo la Licencia Pública "
                "General de GNU, versión 2 o posterior. ROX-Filer original "
                "&copy; 2005 Thomas Leonard y colaboradores; continuación "
                "Rox-Filer2 &copy; 2026 josejp2424."),
    "intro": ("Qué es Rox-Filer2", [
        "<p>Rox-Filer2 es la continuación de ROX-Filer, puesta al día en GTK3 y "
        "ampliada con una interfaz moderna, soporte SMB nativo, iconos de "
        "unidades y un modo escritorio que funciona tanto en X11 como en "
        "Wayland.</p>",
        "<p>Hace una sola cosa en un solo proceso: gestiona archivos y, si usted "
        "quiere, el escritorio. No hay demonio, ni capa de sistema de archivos "
        "virtual, ni dependencia de un entorno de escritorio. Todo lo que ve son "
        "rutas POSIX corrientes.</p>",
    ]),
    "interfaces": ("Interfaces Clásica y Moderna", [
        "<p>Rox-Filer2 incluye dos aspectos de interfaz que comparten los mismos "
        "motores de archivos, MIME, montaje, Papelera y Escritorio.</p>",
        "<ul>"
        "<li><b>Clásica ROX</b> conserva la disposición y la barra de "
        "herramientas tradicionales de ROX-Filer. Si viene de ROX-Filer, nada "
        "cambió de lugar.</li>"
        "<li><b>Moderna</b> agrega barra de menús, fila de navegación, pestañas y "
        "una barra lateral con Lugares, Dispositivos y Red.</li>"
        "</ul>",
        "<p>Elija el estilo permanente en <b>Opciones &gt; Interfaz</b>. El cambio "
        "se aplica a las ventanas que abra después de reiniciar Rox-Filer2.</p>",
        "<p>Para probar un estilo una sola vez sin tocar la preferencia guardada, "
        "ejecute <code>rox --classic</code> o <code>rox --modern</code>.</p>",
    ]),
    "window": ("La ventana del gestor", [
        "<p>Abra directorios con doble clic. Un clic simple selecciona; arrastre "
        "con el botón izquierdo sobre espacio vacío para seleccionar con goma "
        "elástica.</p>",
        "<ul>"
        "<li>El botón derecho abre el menú contextual del elemento bajo el "
        "puntero, o el de la ventana entera si está sobre espacio vacío.</li>"
        "<li>El botón central abre un directorio en una ventana nueva.</li>"
        "<li><kbd>Retroceso</kbd> sube un nivel; <kbd>Ctrl+L</kbd> enfoca la "
        "entrada de ruta.</li>"
        "<li>En Moderna, <kbd>Ctrl+T</kbd> abre una pestaña y <kbd>Ctrl+W</kbd> "
        "la cierra. Cada pestaña mantiene su propia ruta e historial.</li>"
        "</ul>",
        "<p>Cambie entre la vista de iconos y la vista detallada con el selector "
        "de vista, y ajuste el tamaño de los iconos con el deslizador.</p>",
    ]),
    "desktop": ("Escritorio ROX", [
        "<p>Inicie el escritorio con <code>rox --desktop</code>. Solo corre una "
        "instancia a la vez. Dibuja el fondo de pantalla, muestra los iconos de "
        "su directorio de escritorio y puede mostrar iconos de unidades.</p>",
        "<ul>"
        "<li><b>Fondo de pantalla:</b> <code>rox --desktop-wallpaper</code>, o "
        "clic derecho en el escritorio. Los modos son llenar, ajustar, estirar, "
        "centrar y mosaico.</li>"
        "<li><b>Aplicaciones:</b> <code>rox --desktop-apps</code> gestiona los "
        "lanzadores del escritorio.</li>"
        "<li><b>Iconos de unidades:</b> <code>rox --desktop-drive-icon-layout</code> "
        "los ordena.</li>"
        "<li><b>Preferencias:</b> <code>rox --desktop-preferences</code>.</li>"
        "</ul>",
        "<p>En X11 el escritorio usa una ventana normal con sugerencia de tipo "
        "escritorio. En Wayland usa la capa de fondo de Layer Shell, que requiere "
        "<code>gtk-layer-shell</code> instalado y un compositor que publique "
        "<code>zwlr_layer_shell_v1</code>, como Labwc. Fuerce un backend con "
        "<code>rox-x11 --desktop</code> o <code>rox-wayland --desktop</code>.</p>",
        "<p>Para fijar además el fondo de la ventana raíz de X11, de modo que las "
        "terminales con transparencia falsa y conky vean la misma imagen, instale "
        "<code>feh</code> o <code>xwallpaper</code>.</p>",
    ]),
    "drives": ("Unidades y montaje", [
        "<p>Las unidades locales y extraíbles aparecen en la barra lateral de "
        "Moderna bajo <b>Dispositivos</b>, y opcionalmente como iconos en el "
        "escritorio. Haga clic en una unidad para montarla y abrirla; use el menú "
        "contextual para desmontarla.</p>",
        "<p>La información de unidades proviene de <code>lsblk</code>, de "
        "<code>/proc/mounts</code> y de sysfs, así que no hace falta ningún "
        "servicio en segundo plano. Montar una unidad como usuario normal "
        "requiere <code>udisksctl</code> o una entrada en fstab que lo "
        "permita.</p>",
        "<p>Desmonte siempre los medios extraíbles antes de desconectarlos. "
        "Rox-Filer2 no escribe en un dispositivo después de que usted cierra su "
        "ventana, pero el núcleo puede conservar datos sin volcar.</p>",
    ]),
    "images": ("Imágenes de disco (ISO, IMG, SFS, SquashFS)", [
        "<p>Haga clic derecho sobre un archivo de imagen y elija <b>Montar "
        "imagen</b>. Rox-Filer2 la asocia a un dispositivo de bucle y la monta "
        "en solo lectura, y luego abre el resultado como cualquier otro "
        "directorio. Las extensiones admitidas son <code>.iso</code>, "
        "<code>.img</code>, <code>.sfs</code>, <code>.squashfs</code>, "
        "<code>.sqfs</code> y <code>.sqsh</code>.</p>",
        "<p>Una vez montada, el mismo menú ofrece <b>Abrir imagen montada</b> y "
        "<b>Desmontar imagen</b>. Los montajes viven bajo "
        "<code>/media/rox-filer2-images/</code>, un directorio por imagen.</p>",
        "<ul>"
        "<li><b>Siempre en solo lectura.</b> El dispositivo de bucle, el "
        "dispositivo de bloque y el montaje se configuran los tres en solo "
        "lectura, de modo que una imagen nunca puede modificarse por "
        "accidente.</li>"
        "<li><b>Como root</b> (el modelo habitual de Puppy y Essora) usa "
        "<code>losetup</code> y <code>mount</code> directamente. Como usuario "
        "normal usa <code>udisksctl</code>, que debe estar instalado.</li>"
        "<li><b>Los <code>.img</code> crudos</b> pueden contener una tabla de "
        "particiones. Cuando se encuentra más de una, Rox-Filer2 pregunta cuál "
        "montar y muestra el número real de cada partición, su sistema de "
        "archivos, su tamaño y su etiqueta. Las imágenes ISO y SquashFS se "
        "montan siempre enteras, incluso cuando una ISO híbrida expone una "
        "tabla de particiones.</li>"
        "<li><b>Sin GVfs.</b> La imagen se convierte en un montaje local "
        "corriente, así que copiar, arrastrar y soltar, los tipos MIME y las "
        "acciones personalizadas se comportan igual que en cualquier otro "
        "directorio.</li>"
        "</ul>",
        "<p>El montaje ocurre en segundo plano: aparece una ventana pequeña con "
        "un indicador giratorio mientras se prepara el dispositivo de bucle y "
        "se exploran las particiones, y el gestor sigue respondiendo.</p>",
        "<p>Desmonte antes de borrar o mover el archivo de imagen. Si un montaje "
        "desaparece a espaldas de Rox-Filer2, las entradas del menú vuelven a "
        "ofrecer <b>Montar imagen</b>.</p>",
    ]),
    "network": ("SMB y recursos compartidos de Windows", [
        "<p>Abra <b>Ir &gt; Conectar a recurso SMB</b>, o <b>Explorar la red</b> "
        "en la barra lateral de Moderna. Escriba el servidor y, si lo conoce, el "
        "recurso; el botón de listado le pregunta al servidor qué recursos "
        "ofrece.</p>",
        "<p>Rox-Filer2 monta el recurso como un montaje local corriente mediante "
        "<code>mount.cifs</code>, en lugar de presentarlo a través de un sistema "
        "de archivos virtual. Por eso copiar, arrastrar y soltar, los tipos MIME, "
        "la Papelera y las acciones personalizadas funcionan exactamente igual "
        "que con archivos locales.</p>",
        "<ul>"
        "<li>Debe estar instalado <code>cifs-utils</code>.</li>"
        "<li>Montar requiere root. Como usuario normal, Rox-Filer2 intenta "
        "<code>sudo -n</code>; una regla sin contraseña para "
        "<code>mount.cifs</code> equivale a conceder root, así que otórguela a "
        "conciencia o haga el montaje usted mismo.</li>"
        "<li>Su contraseña nunca se escribe en la configuración. Se pasa por un "
        "archivo temporal de credenciales legible solo por usted, que se borra en "
        "cuanto <code>mount.cifs</code> termina. Solo se recuerdan el servidor, "
        "el recurso, el usuario y el dominio.</li>"
        "</ul>",
    ]),
    "trash": ("Papelera", [
        "<p>Borrar desde el menú mueve los elementos a la Papelera XDG, de modo "
        "que se pueden restaurar. Restaure desde el menú contextual del "
        "directorio de la Papelera.</p>",
        "<p>La Papelera solo funciona dentro del mismo sistema de archivos que el "
        "directorio de papelera. Borrar un archivo en otra partición o en un "
        "dispositivo extraíble puede ser irreversible, y Rox-Filer2 se lo avisa "
        "cuando ocurre.</p>",
    ]),
    "mime": ("Tipos de archivo y Abrir con", [
        "<p>Los tipos de archivo provienen de la base de datos MIME compartida, "
        "más cualquier atributo extendido puesto en el archivo mismo. El menú "
        "contextual lista las aplicaciones registradas para ese tipo en "
        "<b>Abrir con</b>.</p>",
        "<p>Fije un manejador permanente, o un icono propio, desde las entradas "
        "<b>Establecer icono</b> y <b>Establecer acción</b> del elemento. Se "
        "guardan en su propia configuración y nunca modifican la base de datos "
        "del sistema.</p>",
    ]),
    "actions": ("Acciones personalizadas y plantillas", [
        "<p>Las acciones personalizadas agregan sus propios comandos al menú "
        "contextual, recibiendo las rutas seleccionadas como argumentos. Son "
        "entradas de escritorio corrientes, así que puede copiar una entre "
        "máquinas.</p>",
        "<p>Las plantillas permiten crear un archivo nuevo vacío de un tipo dado "
        "desde el menú contextual. Agregue las suyas colocando un archivo en el "
        "directorio Templates del directorio de la aplicación.</p>",
    ]),
    "tools": ("Búsqueda, marcadores y ventanas emparejadas", [
        "<ul>"
        "<li><b>Búsqueda:</b> <code>rox-find</code> busca por nombre, tamaño, "
        "tipo y contenido, y devuelve los resultados al gestor.</li>"
        "<li><b>Marcadores:</b> mantienen a un clic los directorios de uso "
        "frecuente; en Moderna también aparecen en Lugares.</li>"
        "<li><b>Ventanas emparejadas:</b> <code>rox --pair</code> abre dos "
        "ventanas lado a lado para copiar entre directorios, y "
        "<code>rox --pair-realign</code> las vuelve a alinear.</li>"
        "</ul>",
    ]),
    "options": ("Opciones y archivos de configuración", [
        "<p>Abra <b>Opciones</b> desde el menú Editar, desde el menú del "
        "escritorio o con <code>rox --config-rox</code>.</p>",
        "<p>Sus ajustes viven bajo el directorio de configuración XDG, "
        "normalmente <code>~/.config/rox.sourceforge.net/ROX-Filer/</code>. Los "
        "archivos son XML, INI y texto plano, así que puede respaldarlos, "
        "editarlos o copiarlos entre máquinas. Borrar un archivo restaura ese "
        "grupo de valores predeterminados.</p>",
    ]),
    "cmdline": ("Línea de comandos", [
        "<ul>"
        "<li><code>rox</code> &mdash; elige automáticamente el backend X11 o "
        "Wayland.</li>"
        "<li><code>rox-x11</code>, <code>rox-wayland</code> &mdash; fuerzan un "
        "backend.</li>"
        "<li><code>rox DIR</code> &mdash; abre un directorio.</li>"
        "<li><code>rox -d DIR</code> &mdash; lo abre como directorio aunque sea "
        "un directorio de aplicación.</li>"
        "<li><code>rox -D DIR</code> &mdash; cierra ese directorio y sus "
        "subdirectorios.</li>"
        "<li><code>rox --classic</code>, <code>rox --modern</code> &mdash; "
        "anulación temporal de la interfaz.</li>"
        "<li><code>rox --desktop</code> &mdash; inicia el escritorio; "
        "<code>--desktop-refresh</code> refresca el que ya corre.</li>"
        "<li><code>rox --help</code> &mdash; la lista completa.</li>"
        "</ul>",
    ]),
    "diagnostics": ("Diagnóstico", [
        "<p>El registro está apagado por omisión. Inicie con "
        "<code>rox --debug</code> para escribir un registro técnico, "
        "<code>--log-file=ARCHIVO</code> para elegir dónde, y "
        "<code>--log-level=NIVEL</code> con error, warning, info, debug o trace. "
        "<code>rox --clear-logs</code> borra los registros que Rox-Filer2 "
        "gestiona automáticamente.</p>",
        "<p>Adjunte ese registro al informar un problema. Anota qué backend de "
        "escritorio se seleccionó, qué estilo de interfaz está activo y por qué "
        "falló un montaje SMB o un escaneo de unidades.</p>",
    ]),
    "support": ("Informar problemas", [
        "<p>Informe errores y sugerencias en la página del proyecto, "
        "<a href=\"https://github.com/josejp2424/ROX-Filer-gtk3\">"
        "github.com/josejp2424/ROX-Filer-gtk3</a>.</p>",
        "<p>Indique qué versión usa (vea <b>Ayuda &gt; Acerca de Rox-Filer2</b>), "
        "si usa X11 o Wayland, qué estilo de interfaz y los pasos exactos que "
        "reproducen el problema.</p>",
    ]),
}


# Las traducciones restantes viven en modulos aparte para que este archivo
# siga siendo legible.  Cada uno expone un diccionario T con la misma
# estructura que TEXTS["en"].
for _module in ("lang_pt_fr_it", "lang_it_ca_de", "lang_hu_ru", "lang_ja_zh_ar"):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    TEXTS.update(__import__(_module).T)


def render(lang):
    t = TEXTS[lang]
    direction = "rtl" if lang in RTL else "ltr"
    out = []
    out.append("<!DOCTYPE html>")
    out.append('<html lang="%s" dir="%s">' % (lang, direction))
    out.append("<head>")
    out.append('<meta charset="utf-8">')
    out.append('<meta name="viewport" content="width=device-width, initial-scale=1">')
    out.append("<title>%s</title>" % html.escape(t["_title"]))
    out.append("<style>%s</style>" % CSS)
    out.append("</head>")
    out.append("<body>")
    out.append("<main>")
    out.append("<header>")
    out.append('<div class="brand">')
    out.append('<img class="manual-logo" src="rox-filer2.svg" alt="Rox-Filer2">')
    out.append('<div class="brand-copy">')
    out.append("<h1>%s</h1>" % html.escape(t["_title"]))
    out.append('<p class="sub">%s &mdash; %s</p>' % (html.escape(t["_sub"]), VERSION))
    out.append("</div>")
    out.append("</div>")
    out.append("</header>")

    out.append('<nav class="toc"><b>%s</b><ol>' % html.escape(t["_toc"]))
    for key in SECTION_ORDER:
        out.append('<li><a href="#%s">%s</a></li>' % (key, html.escape(t[key][0])))
    out.append("</ol></nav>")

    for key in SECTION_ORDER:
        title, blocks = t[key]
        out.append('<h2 id="%s">%s</h2>' % (key, html.escape(title)))
        out.extend(blocks)

    out.append("<footer>%s</footer>" % t["_footer"])
    out.append("</main>")
    out.append("</body>")
    out.append("</html>")
    return "\n".join(out) + "\n"


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    default_out = os.path.join(os.path.dirname(here), "ROX-Filer", "Help")
    out_dir = sys.argv[1] if len(sys.argv) > 1 else default_out
    os.makedirs(out_dir, exist_ok=True)

    project_root = os.path.dirname(here)
    logo_src = os.path.join(project_root, "data", "icons", "hicolor",
                            "scalable", "apps", "rox-filer2.svg")
    logo_dst = os.path.join(out_dir, "rox-filer2.svg")
    if not os.path.isfile(logo_src):
        sys.stderr.write("ERROR: falta el logo del manual: %s\n" % logo_src)
        return 1
    shutil.copyfile(logo_src, logo_dst)

    missing = []
    for lang in LANGS:
        if lang not in TEXTS:
            missing.append(lang)
            continue
        for key in SECTION_ORDER:
            if key not in TEXTS[lang]:
                missing.append("%s:%s" % (lang, key))
    if missing:
        sys.stderr.write("ERROR: faltan traducciones: %s\n" % ", ".join(missing))
        return 1

    written = []
    for lang in LANGS:
        name = "Manual.html" if lang == "en" else "Manual-%s.html" % lang
        path = os.path.join(out_dir, name)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(render(lang))
        written.append(name)
    # menu.c prueba Manual-en.html antes del respaldo; darselo tambien.
    with open(os.path.join(out_dir, "Manual-en.html"), "w", encoding="utf-8") as fh:
        fh.write(render("en"))
    written.append("Manual-en.html")

    print("Manual generado en %s: %s" % (out_dir, " ".join(sorted(written))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
