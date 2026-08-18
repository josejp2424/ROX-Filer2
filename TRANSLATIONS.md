# Rox-Filer2 2.12.2-19 translation coverage

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

For 2.12.2-19 the current strings added around video thumbnails, audio
hover preview, browser/console selection and numeric toolbar ordering remain
translated in all 13 maintained locales. The new native operation indicator
uses GtkSpinner/GtkProgressBar and existing translated ROX labels, so it adds
no untranslated user-visible strings. Their compiled GNU gettext catalogues
are included under `Rox-Filer/Messages/<locale>/LC_MESSAGES/ROX-Filer.mo`.

`po/options_strings.py` is now part of the translation update workflow. It
extracts the user-visible strings from `Options.xml`, so future `update-po`
runs no longer miss labels and help text that live only in the Options file.

The other historical ROX-Filer catalogues remain included unchanged. If a
newer Rox-Filer2-specific string is not translated in one of those legacy
catalogues, gettext falls back to the English source text.


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
