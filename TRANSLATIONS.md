# Rox-Filer2 2.12.2-28 translation coverage

Rox-Filer2 keeps the historical ROX-Filer catalogues, and actively maintains
these 13 locales for the current GTK3/Desktop additions:

- `ar`
- `ca`
- `de`
- `es`
- `fr`
- `hu`
- `it`
- `ja`
- `pt_BR`
- `pt_PT`
- `ru`
- `zh_CN`
- `zh_TW`

For 2.12.2-27 the Desktop icons page, file-operation progress labels and
desktop Paste action are translated in all 13 maintained locales. The
operation animation uses only GTK theme icons and no GIF/custom artwork. The
existing historical catalogues remain available as before.

`po/options_strings.py` is now part of the translation update workflow. It
extracts the user-visible strings from `Options.xml`, so future `update-po`
runs no longer miss labels and help text that live only in the Options file.

For 2.12.2-28 there are no new user-visible strings; the maintained translations from 2.12.2-27 remain valid.

The other historical ROX-Filer catalogues remain included unchanged. If a
newer Rox-Filer2-specific string is not translated in one of those legacy
catalogues, gettext falls back to the English source text.


## 2.12.2-28

2.12.2-28 is a GTK3/X11 compatibility fix only. It adds no new user-visible
strings, so the 13 actively maintained locales remain complete.


## 2.12.2-27

2.12.2-27 completes the newest translation pass. `Browse`, the command-line
help headings, the X11/Wayland desktop backend descriptions and the remaining
new Desktop GUI labels are translated in all 13 actively maintained locales.
The corresponding `.mo` files were regenerated.


## 2.12.2-26

2.12.2-26 adds desktop Paste, Cut visual feedback and the new hicolor
application icon set. These changes reuse the existing translated `Paste`
label and do not introduce new user-visible strings. The 13 maintained
catalogues remain complete for the new behaviour.

## 2.12.2-25

2.12.2-25 adds direct editing of the four built-in desktop icons and their
optional command overrides inside ROX Desktop Preferences. It also expands the
file-operation dialog with current-file/source/destination information, real
percentage, remaining-time estimate and a lightweight GTK3 file-transfer/delete
animation. The new labels are translated in all 13 actively maintained locales.


## 2.12.2-24

2.12.2-24 adds a fixed Options button to the far right of the filer toolbar,
adds `--config-rox` and `--desktop-preferences`, and simplifies file-operation
dialogs to one animated GtkProgressBar with a static themed document icon.
The implementation reuses existing translated strings, so all 13 actively
maintained locales remain complete.


## 2.12.2-23

2.12.2-23 adds icon selection and reset controls for Home, Browser, Console
and Trash in Desktop Preferences. The controls reuse existing translated
strings. Browser and Console labels were completed for all 13 maintained
locales; Arabic/Catalan Reset and Traditional Chinese Trash were also completed.


## 2.12.2-22

2.12.2-22 fixes preservation of `~/.config/mimeapps.list` when changing
Open With/default application associations. No new user-visible strings were
added, so the existing translation catalogs remain complete.

## 2.12.2-20

2.12.2-20 replaces the GtkComboBox controls used by the native desktop dialogs
with a shared GtkMenuButton + GtkPopover dropdown anchored to each control.
The change reuses the existing labels and stored values, so no new translated
strings are required and all 13 maintained locales remain complete.


## 2.12.2-19

2.12.2-19 moves the existing item/hidden/selection information from the toolbar
to the compact bottom status row opposite About. It reuses the already translated
ROX strings for item/items, hidden and selected counts, so no new user-visible
strings are required. The previously untranslated Arabic and Catalan variants of
the status-count strings were completed, and all 13 maintained locales now carry
localized item/hidden/selection status text.


## 2.12.2-18

2.12.2-18 gives the native desktop its own New submenu instead of reusing the
filer template menu. It offers Directory, File and Launcher. Launcher creates
a custom .desktop entry with Name, Command and Icon fields; the command chooser
starts in /usr/bin and the icon chooser starts in /usr/share/pixmaps, while both
can browse the whole filesystem. The new "Launcher" label is translated in all
13 actively maintained locales and the compiled .mo catalogues are included.


## 2.12.2-17

2.12.2-17 adds no new user-visible strings. It fixes a GTK3 type mismatch in
the Wayland activation popover callback and removes two unused desktop helpers.
The 2.12.2-16 translations remain valid.

## 2.12.2-16

2.12.2-16 adds no new user-visible strings. It fixes the Wayland activation
popover visibility, keeps the desktop New/Templates submenu isolated from the
active filer window, makes X11 IPC fallback safe without stealing ownership,
and accepts COPY as well as LINK offers when dropping local files onto the
desktop. The existing 13 maintained translations therefore remain complete.

## 2.12.2-15

2.12.2-15 adds three user-visible drag-and-drop error messages for the native
desktop. They are translated in all 13 actively maintained locales and their
compiled `.mo` catalogues are included. The new desktop menu actions, New
submenu, rubber-band selection and Wayland single/double-click popover reuse
existing translated strings.

## 2.12.2-14

2.12.2-14 adds no new user-visible strings; the toolbar overflow submenu fix reuses the existing translated `New` string and the existing New-menu entries.
