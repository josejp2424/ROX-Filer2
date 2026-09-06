Rox-Filer2 2.12.2-89 release validation

## 2.12.2-89 focused checks

- Menu application/action icons are passed through the shared 18 px maximum
  clamp while preserving their actual icon and aspect ratio.
- Absolute-path desktop icons are handled as GFileIcon/file pixbufs, preventing
  a native 128/256 px icon from controlling menu-row height.
- `build_arch.sh` and `build_void.sh` are separate native package builders and
  pass `bash -n`; `build-package.sh` is not replaced by either one.
- No new translatable strings were added; catalogue count remains 1611.


Before publishing, ./release-gate.sh must finish with "Release gate: PASS".
As of 2.12.2-83 the gate is automated end to end; the manual checks below
cover only what a script cannot see.

Automated by release-gate.sh (do not re-check by hand):
- msgfmt -c, check-po-formats.py and check-po-completeness.py on all eleven
  catalogues (no empty translations and no fuzzy entries).
- messages.pot is in sync with the sources (this is what silently broke
  in -81; po/tips.py was Python 2).
- All thirteen Manual*.html files exist, are non-empty and reference the
  bundled Rox-Filer2 SVG logo.
- Release build compiles.
- AddressSanitizer/UBSan build starts --classic, --modern and --desktop
  headless with no memory errors and no GTK/GLib criticals. This is the
  check that catches the class of bug fixed in -82.
- The historical AppRun/autoconf path still compiles.
- The .deb builds and passes lintian with no errors.

Manual checks:

Help and About
- Modern: Help contains Show Help Files and About; the former direct Manual
  item is intentionally absent.
- Modern: Help > About opens the native About dialog and shows version
  2.12.2-89.
- Classic About remains unchanged.
- Open Manual-es.html, Manual-ja.html and Manual-ar.html from the bundled Help
  directory and confirm they open in `defaultbrowser` on Puppy and
  `x-www-browser` on Essora/other distributions.
- Confirm a missing preferred wrapper falls back cleanly rather than failing.
- Confirm ordinary non-manual HTML files still use their normal MIME action.
- Confirm every Manual*.html header displays the Rox-Filer2 logo and Arabic
  remains right-to-left.

Wallpaper performance (the -81 regression)
- Set a wallpaper of at least 3840x2160.
- Rubber-band select across the desktop and confirm the selection follows
  the pointer without stepping. On a slow machine, -81 ran at a few frames
  per second here.
- Repeat on a two-monitor setup.
- Change wallpaper and mode and confirm the new image appears immediately
  (the cache must invalidate).
- Change screen resolution while the desktop is running.

Packaging
- Create /usr/local/apps/Rox-Filer as a real directory with a file inside,
  install the package, and confirm the directory and its contents are
  still there afterwards. In -81 preinst deleted them.
- Confirm the package installs nothing under /usr/local.
- Confirm rox, ROX-Filer and Rox-Filer2 all resolve to /usr/bin/roxfiler.
- Install without feh and without xwallpaper and confirm the desktop still
  draws its own wallpaper; install feh and confirm the X11 root window
  background is set too.

Image Mounter
- Right-click `.iso`, `.sfs`, `.squashfs`, `.sqfs` and `.img` files and confirm
  the system-themed **Mount Image** action appears only for one supported file.
- Mount ISO/SFS/SquashFS read-only and confirm Rox-Filer2 opens the result.
- In Modern confirm the mount opens in a new tab; in Classic confirm it opens
  in a normal filer window.
- In Modern confirm the new tab actually navigates to the mountpoint: the
  location entry must show the mounted directory (not the directory containing
  the image), the file view must show the mounted contents, and switching away
  and back to the tab must return to that same mountpoint.
- Test a raw IMG with one partition (automatic choice) and another with two or
  more partitions (GTK partition chooser).
- Confirm **Open Mounted Image** and **Unmount Image** replace **Mount Image**
  while the image is mounted and that the loop device is detached on unmount.
- Test once as root (Puppy/direct mount) and once as a normal user with udisks2.

Drives
- Confirm plugging and unplugging a USB drive still updates the sidebar
  and desktop icons promptly. Polling is now 45 s, so this path depends on
  GVolumeMonitor rather than on the poll.

SMB
- Connect to a share and confirm it mounts, browses, copies and unmounts.
- Confirm no password is written to smb.ini and no credentials file is
  left in the temporary directory.

Interfaces
- Options > Interface still shows both preview cards and both are
  selectable.
- rox --classic and rox --modern remain temporary overrides.
- Classic remains visually and functionally unchanged.


Image mounter (new checks for 2.12.2-85)
- Mount a .sfs, a .iso and a .img from the file menu. Each opens its
  contents; the mount appears under /media/rox-filer2-images/.
- While a large image mounts, confirm the spinner window appears and the
  filer window behind it still redraws and scrolls. In -84 the interface
  froze for up to two seconds.
- Unmount each one. Confirm `losetup -a` is empty afterwards and no
  directory is left in /media/rox-filer2-images/.
- Write a state file by hand under the image-mounter cache directory with
  mountpoint pointing outside /media/rox-filer2-images, then choose Unmount
  Image. Rox-Filer2 must refuse and say so. In -84 it unmounted that path
  and deleted the directory.
- Mount a .img with more than nine partitions. Confirm the chooser lists
  them in numeric order and that the label of each entry matches the real
  partition number, not its position in the list.
- Confirm every image is mounted read-only: `mount` must show `ro` for it,
  and writing into the mount point must fail.
- As a normal user without udisksctl installed, confirm the mount is
  refused with a clear message rather than failing silently.
- Confirm Help > Manual now has a "Disk images" section in your language.


Image mounter (2.12.2-87 behaviour retained through 2.12.2-89)
- Start mounting an IMG whose partition scan takes noticeable time, then close
  the originating filer window before the worker finishes. Rox-Filer2 must not
  crash; if the mount succeeds it opens a normal filer window instead of using
  the destroyed Modern tab owner.
- Repeat while closing the spinner through destruction of its parent window; no
  invalid GtkWidget access or GLib/GTK critical is permitted.
- Test an IMG where p1 appears before later partitions. The chooser must wait
  for the partition set to stabilize and must show all partitions rather than
  auto-mounting p1.
- Run without XDG_RUNTIME_DIR. Image Mounter state must be created below
  /tmp/rox-filer2-<uid>/rox-filer2/image-mounter, not under ~/.cache.
- Force save_state() to fail (for example with an unwritable runtime state
  directory). The UI must stay responsive while rollback runs; success may only
  be reported after the mount is actually gone.
- Before publishing a source archive, confirm there is no ROX-Filer/ROX-Filer,
  ROX-Filer/ROX-Filer.dbg or rox-find/rox-find generated ELF in the tree.
