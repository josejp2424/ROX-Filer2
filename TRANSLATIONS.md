# Rox-Filer2 2.12.2-89 translations

English is the source language. Complete runtime catalogues are maintained
for: ar, ca, de, es, fr, hu, it, ja, pt, ru and zh.

## State

All eleven catalogues are at **1611/1611 translated**, with `msgfmt -c` and
`check-po-formats.py` clean.





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
