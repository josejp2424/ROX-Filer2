# Building Rox-Filer2 packages

Run from the project root:

```sh
./build-package.sh
```

The script compiles both **Rox-Filer2** and the companion **ROX File Search**
application, then creates everything under `output/`:

- `rox-filer2_<version>_<architecture>.deb`
- the complete Debian package directory;
- a portable filesystem directory containing `usr/`;
- a portable `.tar.gz` archive for other package formats.

The generated runtime trees include:

- `/usr/local/apps/Rox-Filer`
- `/usr/bin/Rox-Filer2`
- `/usr/bin/rox-find`
- the ROX File Search desktop entry, icon and translation catalogues
- the standard Puppy MIME icons installed through the Debian maintenance scripts

The runtime trees do not contain `Rox-Filer/src` or `Rox-Filer/build`. The
development source keeps `ROX-Filer/src`, because it is required for future
compilation. The temporary `ROX-Filer/build` directory is removed after
packaging.

The installed `Rox-Filer/ROX` directory comes from the package base supplied by
josejp2424 in `package-base/usr/local/apps/Rox-Filer/ROX`.

To package binaries that have already been compiled:

```sh
./build-package.sh --skip-compile
```

To compile without creating packages:

```sh
./ROX-Filer/AppRun --compile-only
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
mismo binario `/usr/local/apps/Rox-Filer/Rox-Filer2`. El soporte Wayland carga
`libgtk-layer-shell.so.0` solo cuando el display es Wayland; el paquete la
recomienda, pero X11 continúa funcionando sin ella.

## Automatic normal-window launcher (r76)

`/usr/local/bin/roxfiler` selects `/usr/bin/rox-wayland` in a native Wayland
session and `/usr/bin/rox-x11` in X11. `/usr/bin/rox` points to this selector.
Desktop menu entries use this wrapper. The AppDir can still be launched
directly because the binary now avoids the X11-only remote IPC path on Wayland.
