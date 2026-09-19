# Building Rox-Filer2 packages

Run from the project root:

```sh
./build-package.sh
```

### Puppy/Essora copy backend

Since 2.12.2-96 the packager no longer assumes a standalone coreutils `cp` command.
It uses `cp` when available and falls back automatically to the BusyBox `cp`
applet for Puppy/Essora-style bases. This affects package staging only; the
Rox-Filer2 runtime is unchanged.

### Optional libsmbclient at runtime

Rox-Filer2 no longer links the main executable directly against `libsmbclient`.
The same binary starts normally whether `libsmbclient.so.0` is installed or not.
When the library is present it is loaded lazily for SMB share listing and
diagnostics; `mount.cifs` remains an independent path. Debian packages therefore
recommend `libsmbclient0` instead of making it a hard `Depends`.

### Optional portable-device support at runtime

Meson probes `libmtp` and `libgphoto2` as optional development dependencies.
For Apple support it probes `libimobiledevice-1.0` pkg-config metadata with
`pkg-config --modversion` and reports the actual `idevice_id` and `ifuse` runtime
helpers separately. Rox-Filer2 deliberately does not link the main executable
to any of those optional mobile libraries. Portable devices are enabled at runtime through optional
helpers: `simple-mtpfs` or `jmtpfs` for Android/MTP, `gphoto2` + `gphotofs` for
PTP cameras, and `ifuse` + libimobiledevice tools/usbmuxd for iPhone/iPad.
Missing helpers never prevent Rox-Filer2 from compiling or starting.

The Debian package lists these stacks plus a FUSE userspace helper under `Recommends`, not `Depends`.
Already-mounted non-block filesystems below `/media` are discovered independently
of the helpers and are shown in the drive UIs so they can be opened/unmounted.


### Optional Samba usershare support

Rox-Filer2 2.13 manages local folder shares with the runtime `net usershare`
command and optional `testparm`. The quick **Share Folder...** action and the
central **Samba > Shared Folders...** manager use the same usershare backend; the
file manager does not link to Samba libraries and it does not run as root. Debian/Devuan packages therefore keep
`samba-common-bin` and `samba` under `Recommends`. Arch exposes `samba` as an
optional dependency and Void includes it only when present on the build host.
Missing Samba tools never prevent Rox-Filer2 from compiling or starting.

## Dedicated native builders

The normal builder remains independent. For a package compiled natively on
the target distribution, the source tree also provides:

```sh
./build_arch.sh
```

On Arch/Arch-based systems this compiles with Meson + `-Dsmb=auto -Dportable_devices=auto` and
creates a native `output/rox-filer2-*.pkg.tar.zst` with `makepkg`. Run it as a
normal user.

```sh
./build_void.sh
```

On Void/KLV this compiles with Meson + `-Dsmb=auto -Dportable_devices=auto` and creates a native
`output/rox-filer2-*.xbps`, then indexes `output/` as a local XBPS repository.
The upstream release `2.13.0-8` is represented as `2.13.0_8` for XBPS,
and runtime dependencies are emitted as validated versioned XBPS patterns.
Both scripts have `--clean` and intentionally do not call or modify the normal
`build-package.sh` path.

The script compiles **Rox-Filer2**, the companion **ROX File Search**
application and the package-owned **rox-mount-helper** used through pkexec for
normal-user drive operations. Meson/Ninja is the preferred build path; if Meson is unavailable,
the script falls back to the historical AppRun/autoconf build. It then creates
everything under `output/`:

- `rox-filer2_<version>_<architecture>.deb`
- the complete Debian package directory;
- a portable filesystem directory containing `usr/`;
- a portable `.tar.gz` archive for other package formats.

The generated runtime trees include:

- `/usr/lib/rox-filer2` (runtime real) + `/usr/local/apps/Rox-Filer` y `/usr/local/apps/ROX-Filer` (enlaces de compatibilidad)
- `/usr/bin/Rox-Filer2`
- `/usr/bin/rox-find`
- the ROX File Search desktop entry, icon and translation catalogues
- the standard Puppy MIME icons installed through the Debian maintenance scripts

The runtime trees do not contain `Rox-Filer/src` or legacy `Rox-Filer/build`.
The development source keeps `ROX-Filer/src`, because it is required for future
compilation. Meson output lives in the top-level `build/` directory and is
never copied into runtime packages.

The installed `Rox-Filer/ROX` directory comes from the package base supplied by
josejp2424 in `package-assets/ROX`.

To package binaries that have already been compiled:

```sh
./build-package.sh --skip-compile
```

To compile without creating packages (recommended):

```sh
meson setup build
meson compile -C build
```

The legacy compiler remains available:

```sh
./ROX-Filer/AppRun --compile-only
```

To force the package builder to use that historical build path:

```sh
./build-package.sh --legacy-build
```

To remove generated package output and temporary binaries:

```sh
./build-package.sh --clean
```


### r70 runtime notes

No new mandatory library is required. The diagnostic-only command-line hooks
are hidden from `rox --help` and require `ROX_DIAGNOSTIC=1`. They are used
by `tools/rox-filer-diagnostico.sh` to test the exact Open With and Run in
Terminal launch paths. Optional `--test-script FILE` and `--geany-file FILE`
arguments add concrete user files to the report.

### r69 runtime notes

No new mandatory library is required. Run in Terminal uses standard GLib and
POSIX temporary-file APIs. A terminal emulator is detected at runtime, with
`defaultterminal`, `x-terminal-emulator`, and `xterm` as the first choices.

### r71 runtime notes

Use `tools/rox-filer-diagnostico.sh` 1.4 to test Bash/Ash scripts, real Open With
entries and the Rename dialog. The hidden diagnostic options remain excluded
from `Rox-Filer2 --help`.

### r72 runtime notes

No new library is required. `ROX_DEBUG_LOG` is disabled unless the environment
variable names a writable log file. After replacing an installed Rox-Filer2
binary, restart existing filer and desktop processes before validating the new
build. Use diagnostic 1.5 to identify stale processes and test the real terminal.


## Enlaces de backend r73

El paquete instala `/usr/bin/rox-x11` y `/usr/bin/rox-wayland` como enlaces al
mismo binario `/usr/lib/rox-filer2/ROX-Filer`. El soporte Wayland carga
`libgtk-layer-shell.so.0` solo cuando el display es Wayland; el paquete la
recomienda, pero X11 continúa funcionando sin ella.

## Automatic normal-window launcher (r76)

`/usr/local/bin/roxfiler` selects `/usr/bin/rox-wayland` in a native Wayland
session and `/usr/bin/rox-x11` in X11. `/usr/bin/rox` points to this selector.
Desktop menu entries use this wrapper. The AppDir can still be launched
directly because the binary now avoids the X11-only remote IPC path on Wayland.

## Native Arch Linux package (2.12.2-33+)

`build-package.sh` now detects Arch Linux (`/etc/arch-release` / `ID_LIKE=arch`)
or an available `makepkg`. When detected, the normal build also creates a native
Arch package after the portable tree has been produced:

```sh
./build-package.sh
```

Typical output:

```text
output/rox-filer2-2.12.2-33-x86_64.pkg.tar.zst
```

The Arch package is made from the same already-compiled portable filesystem tree
used by the Debian/Puppy package path; Rox-Filer2 is not compiled a second time.
The generated `PKGBUILD` is kept under `output/arch-build/` for inspection.
Pacman hooks handle icon and desktop cache updates, so no obsolete `.install`
hook is generated.

To force this path on another distribution that has `makepkg` installed:

```sh
./build-package.sh --arch-package
```

To disable it even on Arch:

```sh
./build-package.sh --no-arch-package
```

`makepkg` must be run as a normal user. If the build script itself is run as
root, Debian/portable outputs are still generated but the Arch package step is
skipped with a warning.
