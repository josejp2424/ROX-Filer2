# Rox-Filer2 2.13.0-8 translations

English is the source language. Complete runtime catalogues are maintained
for: ar, ca, de, es, fr, hu, it, ja, pt, ru and zh.

## State

All eleven catalogues are at **1655/1655 translated**, with `msgfmt -c` and
`check-po-formats.py` clean.


## 2.13.0-8

Six new Options strings were added for the Drives startup automount controls and explanatory text. All are translated in ar, ca, de, es, fr, hu, it, ja, pt, ru and zh.
- Runtime catalog target: **1655/1655** translated entries in all 11 locales, with **0 fuzzy** entries.

## 2.13.0-6

- Added translations for the Samba `Shared Folders...` manager, its actions,
  empty state, guest column and usershare validation messages.
- Runtime catalog target: **1649/1649** translated entries in all 11 locales,
  with **0 fuzzy** entries.

## 2.13.0-5

- No new runtime strings. Modern now opens Options through the same shared menu entry point as Classic, and the Options window has a 900x540 minimum with a 960x600 initial size.
- All eleven catalogues remain complete at **1640/1640 translated**, with no fuzzy entries.

## 2.13.0-4

- No new runtime strings. The Options window geometry change reuses the existing interface and translations.
- All eleven catalogues remain complete.


## 2.13.0-2

One new Options string, **New Rox-Filer2 instances:**, was added for the Modern
window/tab routing preference. The Close Window label already existed in the
runtime catalogue, so the optional toolbar close button reuses that translation.
All eleven catalogues are complete at **1640/1640 translated**, with no fuzzy
entries.

## 2.13.0-1

Twenty-two runtime strings were added for the native Samba usershare dialog,
validation and error reporting, including **Share Folder...**, **Share this folder**,
write/guest controls and rootless `net usershare` diagnostics. All eleven catalogues
are complete at **1639/1639 translated**, with no fuzzy entries.

## 2.12.2-99

No new runtime strings were added by the Classic keyboard-focus fix. Type-ahead now accepts ordinary printable typing while non-text toolbar controls own focus, while editable text widgets remain excluded. All eleven catalogues remain **1617/1617 translated**, with no fuzzy entries.

## 2.12.2-98

No new runtime strings were added by type-ahead selection. The feature is implemented in the shared filer backend used by Classic and Modern, so all eleven catalogues remain **1617/1617 translated**, with no fuzzy entries.

## 2.12.2-97

No new runtime strings were added by the packaging portability fix. All eleven
catalogue headers were synchronized to 2.12.2-97 and remain **1617/1617
translated**, with no fuzzy entries.

## 2.12.2-95

Three new runtime labels, `Portable Devices`, `MTP device` and `iPhone / iPad`,
were added for the portable-device integration and its fallback names. All eleven
runtime catalogues contain translations, their release headers were synchronized
to 2.12.2-95, and they are complete at **1617/1617 translated**, with no fuzzy
entries.

## 2.12.2-94

No new runtime strings were required for the privilege-aware window icon.
All eleven catalogue headers were synchronized to 2.12.2-94 and remained
**1614/1614 translated**, with no fuzzy entries.


## 2.12.2-93

The previous build-time message `This build has no libsmbclient support.` was
replaced by the runtime-accurate `libsmbclient is not available on this system.`
All eleven catalogues were updated and their release headers synchronized to
2.12.2-93. The total remains **1614/1614 translated**, with no fuzzy entries.


## 2.12.2-92

One new runtime label, `Mounted Images`, was added for the Classic Partitions
popover and the Modern Devices sidebar. All eleven runtime catalogues contain
the translation, the bundled `.mo` files were regenerated, and release headers
are synchronized to 2.12.2-92. The catalogues are complete at **1614/1614**,
with no fuzzy entries.

## 2.12.2-91

Two new Options labels were added for independent Trash and permanent-delete
confirmation. All eleven runtime catalogues contain translations for both new
strings and the release headers are synchronized to 2.12.2-91. The catalogues
are complete at **1613/1613**, with no fuzzy entries.


## 2.12.2-90

No new runtime `msgid` is required for the pkexec drive-mount correction.
The existing translated generic command/mount error strings are reused. All
eleven catalogues remain complete at **1611/1611**, with no fuzzy entries;
release headers are synchronized to 2.12.2-90.

## 2.12.2-89

No new runtime `msgid` is required for the menu-icon sizing or native package
builder changes. All eleven catalogues remain complete at **1611/1611**, with
no fuzzy entries; release headers are synchronized to 2.12.2-89.

## 2.12.2-88

No new runtime `msgid` is required for this compile-order correction. The
existing Image Mounter strings remain unchanged. All eleven PO catalogues stay
complete at **1611/1611**, with no fuzzy entries; their release headers are
synchronized to 2.12.2-88.

## What changed in 2.12.2-84

The Image Mounter adds twenty runtime strings covering **Mount Image**,
**Open Mounted Image**, **Unmount Image**, the multi-partition `.img` chooser
and mount/unmount errors. All eleven runtime catalogues (ar, ca, de, es, fr,
hu, it, ja, pt, ru and zh) contain complete translations with matching
printf placeholders. English remains the source language.

## What changed in 2.12.2-83

No new runtime msgid is required for the Help-menu change: `About` and
`Show Help Files` already exist in the catalogue. The inherited Arabic and
Catalan `Show _Help Files` translations were corrected instead of leaving
the English source text in `msgstr`. A focused Modern-UI audit also replaced
English carry-overs such as Desktop/Mount/Path/Scanning/Home where they were
not genuine target-language UI terms.

All eleven runtime catalogues were re-audited for empty translations and
fuzzy entries. The manual generator still requires every section in every
language and now also installs the official `rox-filer2.svg` logo beside the
HTML files so every localized manual has the same branded header.

## What changed in 2.12.2-82

`po/tips.py` was still Python 2. Because `update-po` chains its steps with
`&&` inside a subshell whose exit status nobody checks, extraction aborted
on the first line and `messages.pot` silently stopped tracking the source.
The catalogues reported 1564/1564 while eighteen visible strings had never
reached them.

Twenty-three strings were translated for this release across all eleven
languages, including the dialog buttons (`_OK`, `_Yes`, `_No`, `_Save`,
`_Delete`, `_Help`, `_Refresh`, `A_dd`), `Add to Bookmarks`, `Restore`,
`About`, `About Rox-Filer2`, `Unable to open Trash`, the `--help` usage
block, the `--version` copyright notice and the About dialog licence text.

`Classic ROX` and `Modern` are now marked with `N_()`. `style_card()`
applies `_(title)` to a parameter, which xgettext cannot extract, so these
two labels would have disappeared from the catalogue on the first correct
regeneration.

## Merge policy

The regenerated `.pot` is a union: freshly extracted entries plus 383
entries that only existed in the previous `messages.pot`. Those are kept
because several originate from `_(variable)` call sites invisible to static
extraction, and dropping them would break translations that work at
runtime. They are marked with a comment block in `messages.pot` and should
be audited against the sources by hand before being removed.

## Manual

`tools/make-manual.py` generates `ROX-Filer/Help/Manual*.html` for twelve
languages (the eleven above plus English) from one structured source, so
the versions cannot drift apart. Sections are keyed in `SECTION_ORDER`; the
generator refuses to run if any language is missing a key.

The ar, hu, ja, ru and zh manual texts have not had a native-speaker
review.

## Regenerating

    cd ROX-Filer/src/po && ./update-po      # catalogues
    python3 tools/make-manual.py            # manual
    ./release-gate.sh                       # verifies both are in sync

## 2.12.2-85

Four new strings from the image mounter were translated into all eleven
languages: the two progress captions (`Mounting image…`, `Unmounting
image…`), the refusal message when a state file points outside the managed
directory, and the message shown when a mount is undone because its state
could not be saved.

`messages.pot` was regenerated and passed through `msguniq`. That fixed a
duplicate `Mount Image` entry inherited from the -84 template which made
`msgmerge` abort with a fatal error on all eleven catalogues, so none of
them was being updated.

The manual gained a "Disk images" section in all twelve languages. As with
the rest of the manual, the ar, hu, ja, ru and zh texts have not had a
native-speaker review.
