Rox-Filer2 2.13.0-8 release validation
========================================

## 2.13.0-8 focused checks

- Open **Options > Drives** and confirm both **Mount local drives automatically at startup** and **Include removable drives** are present and translated.
- Leave startup automount disabled, restart Rox-Filer2 and confirm no unmounted local partition is mounted as a side effect.
- Enable startup automount but leave removable drives disabled. Restart Rox-Filer2 and confirm eligible unmounted internal/local partitions mount one by one after the UI appears.
- Confirm already-mounted volumes are skipped and their existing mountpoints are not changed.
- Confirm network shares, MTP/PTP/iOS devices, mounted ISO/SFS/IMG images, optical media, swap, encrypted LUKS containers and hidden/system/technical partitions are never automounted.
- Enable **Include removable drives**, restart with an unmounted USB or SD filesystem attached, and confirm it is mounted through the same ROX drive backend and receives the normal mounted/eject indicator.
- In a normal-user session, confirm multiple targets are mounted serially rather than opening simultaneous authorization dialogs.
- Save the options and verify `drives_mount_all_startup` and `drives_mount_removable_startup` are stored in the standard Rox-Filer2 XDG `Options` file, with no PMADAS/Startup helper created.
- Confirm all 11 maintained catalogues report 1655/1655 translated strings, 0 fuzzy, and the format gate passes.
- Re-run the 2.13.0-7 quick unmount/eject indicator checks, 2.13.0-6 Samba sharing checks, and the complete 2.13.0-5 Options/tab-routing regression checks.

## 2.13.0-7 focused checks

- In Classic Partitions and Modern Devices, confirm an unmounted partition has no quick-action arrow.
- Mount the partition and confirm `media-eject-symbolic`/`media-eject` appears immediately as the mounted-state indicator.
- Click the arrow on a normal filesystem and confirm only that volume is unmounted; the arrow must disappear after refresh.
- On removable media that supports safe eject, confirm the same control performs Eject rather than only unmounting.
- Confirm foreign mounts owned by another application do not expose the quick unmount/eject action.

## 2.13.0-6 focused checks

- With Samba command-line tools absent, right-click an owned folder and confirm **Share Folder...** is not shown; Rox-Filer2 must otherwise behave normally.
- With Samba usershare available, right-click one owned folder and confirm **Share Folder...** appears with the themed public-share icon (or its fallback), and create a share as a normal user.
- Confirm a newly shared folder gets a small shared emblem when the active icon theme provides `emblem-shared`; themes without that emblem must keep the normal folder icon without an error placeholder.
- In Classic, open **Samba > Shared Folders...**. In Modern, open the same **Samba > Shared Folders...** menu. Both must show the same usershare list.
- In the Shared Folders window, verify **Open**, **Edit**, and **Stop Sharing**. Edit must reuse the existing Folder Sharing dialog; Stop Sharing must ask for confirmation and remove only the selected usershare.
- With no usershares, the manager must show **No folders are currently shared.** and keep action buttons insensitive.
- Verify the manager columns Name, Path, Write and Guest reflect `net usershare info`. Double-clicking a row should open its local folder.
- Reopen or refresh the parent folder after sharing/unsharing and confirm the shared emblem state updates without restarting Rox-Filer2.

## 2.13.0-4 focused checks

- Open Options on a normal desktop and confirm the initial window is about 960x600, giving the right-hand Interface/Modern content enough width to read without the severe wrapping seen at 640x400.
- Confirm the Options window is still freely resizable smaller/larger and that the left category list remains scrollable.
- Re-run the complete 2.13.0-2 focused checks below; no Samba, tab-routing or Close Window behavior should regress.

## 2.13.0-2 focused checks

- Build the full Meson target and confirm `samba_share.c` compiles with the GTK3 compatibility menu types available; the 2.13.0-1 `RoxItemFactoryEntry` include-order failure must not return.
- Start Classic and Modern with Samba completely absent; both interfaces must open normally. Right-clicking a local folder may offer **Share Folder...**, but activating it must only report the missing `net` helper and must never request root.
- With Samba usershares configured for a normal user, right-click one owned local directory, enable **Share this folder**, choose a name/comment and Apply. Confirm `net usershare info` reports the same path and settings.
- Reopen the dialog for the same folder and confirm the existing name, comment, writable and guest settings are loaded. Change them and verify the usershare is updated.
- Disable **Share this folder** and confirm only that usershare is removed.
- Enable writable and/or guest access on a directory that lacks the required Unix mode bits. Rox-Filer2 must ask before changing permissions; Cancel must leave the mode unchanged.
- Modern: press the `+` New Tab button, then click back and forth between two or more tabs. Every clicked tab must become active, restore its own path/view/history, and accept normal file navigation immediately.
- Modern: use Ctrl+T, Ctrl+Tab and Ctrl+Shift+Tab and confirm the active toggle, location entry and real file view always describe the same tab.
- Options > Interface > Modern: with **New Window**, ordinary new-window navigation must retain separate-window behavior. With **New Tab**, opening a folder/bookmark through a gesture that would normally create another window must create a tab in the source Modern window; a request forwarded to the running Modern process must create a tab in the primary Modern window. Explicit **New Window** from the Modern menu must always remain a real new window.
- Options > Toolbar: enable **Close Window**. Confirm the button appears in Classic and Modern, closes only the current filer window, and is usable in undecorated/tiling WMs such as BSPWM and SpectrWM. Disable it again and confirm it disappears.
- Confirm the new Close Window control does not terminate the desktop/pinboard service or unrelated Rox-Filer2 windows.
- Re-run the complete 2.12.2-99 Classic/Modern type-ahead tests and 2.12.2-95 portable-device/SMB regression checks.

## 2.12.2-99 focused checks

- In Classic, click a toolbar button such as **Partitions** so it visibly owns keyboard focus, then type `p`, `k`, `e` in `/usr/bin`; type-ahead must leave the toolbar button, select the first matching filename and refine to `pkexec`.
- Repeat with other non-text Classic toolbar controls: ordinary printable typing must start type-ahead and move focus to the file view on the first match.
- Confirm Modern retains the working 2.12.2-98 behavior.
- Confirm typing in the Modern path/location entry, the historical minibuffer, a spin button or any other editable text widget is never intercepted by type-ahead.
- Re-check the complete 2.12.2-98 prefix, timeout, Backspace, UTF-8 and shortcut behavior below.

## 2.12.2-98 focused checks

- In both Classic and Modern file views, type `p`, then `k`, then `e` in a directory containing `pkexec`; the highlighted/cursor item must refine to the first visible filename beginning with the accumulated prefix.
- Confirm matching is case-insensitive and UTF-8 aware, the active prefix resets after about 1.5 seconds, and Backspace edits the prefix while active.
- Confirm Backspace still opens the parent directory when no type-ahead prefix is active, digits still use ROX selection groups, Space still toggles the cursor item, and Ctrl/Alt shortcuts are unchanged.
- Confirm typing in the Modern path entry or the minibuffer is not intercepted by type-ahead selection.

## 2.12.2-97 focused checks

- With `pkg-config --modversion libimobiledevice-1.0` returning a version, configure a clean Meson build and confirm the summary reports `libimobiledevice pkg-config : YES <version>`.
- Confirm Meson reports `idevice_id runtime helper` and `ifuse runtime helper` independently; missing helpers must never stop configuration or compilation.
- Confirm `readelf -d build/ROX-Filer` still has no `DT_NEEDED` entry for libimobiledevice, libmtp or libgphoto2.
- Run `./build-package.sh` on Debian/Devuan with `dpkg-shlibdeps`; exact shlib dependency generation should still be preferred.
- Run the packager on Essora/Puppy without `dpkg-shlibdeps`; it must warn and continue using the core Debian/Devuan dependency fallback instead of aborting.
- Confirm the resulting control keeps SMB/MTP/PTP/iOS stacks under `Recommends`, never hard `Depends`.
- Re-check the 2.12.2-96 BusyBox `cp` fallback and all 2.12.2-95 portable-device behavior below.

## 2.12.2-95 portable-device regression checks

- Build with all optional mobile development libraries absent; Meson must still configure and Rox-Filer2 must compile. The summary may report libmtp/libgphoto2 as NO, libimobiledevice pkg-config as NO, and the Apple runtime helpers as NO.
- Build with those development libraries installed and confirm the Meson summary detects libmtp/libgphoto2, libimobiledevice pkg-config metadata and the installed Apple helpers as appropriate, while `readelf -d ROX-Filer` still has no `DT_NEEDED` entry for libmtp, libgphoto2 or libimobiledevice.
- Start Classic and Modern with `simple-mtpfs`, `jmtpfs`, `gphoto2`, `gphotofs`, `ifuse` and libimobiledevice tools all absent; both interfaces must open normally.
- Connect an Android/MTP device. With simple-mtpfs installed it should be listed automatically; with simple-mtpfs absent and jmtpfs installed, the jmtpfs fallback should expose it under **Portable Devices**. Mount, open and unmount it.
- Connect a PTP camera with gphoto2/gphotofs installed and confirm it appears under **Portable Devices**, mounts as FUSE and can be unmounted from both device UIs.
- Connect an iPhone/iPad with usbmuxd, libimobiledevice-utils and ifuse installed; confirm its name is discovered when available, then mount/open/unmount it.
- Mount an unrelated non-block filesystem below `/media/...` and confirm it appears in both the Classic Partitions popover and Modern Devices sidebar with an unmount action. Ordinary block partitions must not be duplicated.
- Confirm Rox-Filer2-managed ISO/SFS/IMG mounts remain under **Mounted Images**, separate from **Portable Devices** and ordinary partitions.
- Plug/unplug a portable USB device and confirm the `/sys/bus/usb/devices` monitor refreshes the drive UI promptly without waiting for the 45-second safety poll.
- Confirm the generated Debian package keeps the mobile stack out of `Depends` and recommends jmtpfs/libmtp, gphoto2/gphotofs/libgphoto2, ifuse/usbmuxd and the libimobiledevice runtime/tools.
- Re-check 2.12.2-94 privilege-aware blue/orange frame icons and 2.12.2-93 optional libsmbclient startup behavior.
- All eleven runtime PO catalogues must be complete at 1617/1617 with no fuzzy entries.


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
- The release binary has no hard `DT_NEEDED` entry for libsmbclient, libmtp,
  libgphoto2 or libimobiledevice; the Debian package keeps those optional
  capabilities out of hard `Depends` and exposes the helper stacks through
  `Recommends`.
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
  2.13.0-8.
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


Image mounter (2.12.2-87 behaviour retained through 2.12.2-93)
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
