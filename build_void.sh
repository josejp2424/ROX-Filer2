#!/usr/bin/env bash
# Rox-Filer2 native Void Linux / KLV package builder.
# Compile Rox-Filer2 with Meson and runtime-optional libsmbclient support.
#
# Special thanks to rockedge and Sofiya for their testing, feedback and help
# in finding a clean native XBPS packaging solution for Rox-Filer2 on Void
# Linux and KLV.
#
# Discussion and packaging work:
# https://forum.puppylinux.com/viewtopic.php?t=17225&start=90
set -euo pipefail

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$PROJECT_ROOT/ROX-Filer"
BUILD_DIR="${ROX_VOID_BUILD_DIR:-$PROJECT_ROOT/build-void}"
OUTPUT_DIR="${ROX_OUTPUT_DIR:-$PROJECT_ROOT/output}"
WORK_DIR="$OUTPUT_DIR/void-build"
STAGE_DIR="$WORK_DIR/rootfs"
PACKAGE_NAME="rox-filer2"
LANGS=(ar ca de es fr hu it ja pt ru zh)

usage() {
    cat <<'USAGE'
Usage: ./build_void.sh [--clean]

Builds Rox-Filer2 natively on Void Linux. libsmbclient is detected at runtime
and is not required for Rox-Filer2 to start. It also creates a native .xbps
package in output/ and indexes output/ as a local
XBPS repository so the package can be installed with xbps-install -R.

Requirements: install the Void build dependencies first.
USAGE
}

if [[ "${1:-}" == "--clean" ]]; then
    rm -rf "$BUILD_DIR" "$WORK_DIR"
    echo "Cleaned Void build directories."
    exit 0
elif [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
elif [[ $# -gt 0 ]]; then
    echo "ERROR: unknown option: $1" >&2
    usage >&2
    exit 2
fi

if [[ ! -f /etc/void-release ]] && ! command -v xbps-install >/dev/null 2>&1; then
    echo "ERROR: build_void.sh must be run on Void Linux or a Void-based system." >&2
    exit 1
fi

[[ -f "$APP_DIR/AppInfo.xml" ]] || {
    echo "ERROR: run this script from the Rox-Filer2 source tree." >&2
    exit 1
}

DISPLAY_VERSION=$(sed -n 's/^[[:space:]]*<Version>\([^<][^<]*\)<\/Version>[[:space:]]*$/\1/p' "$APP_DIR/AppInfo.xml" | head -n1)
if [[ ! "$DISPLAY_VERSION" =~ ^([0-9]+\.[0-9]+\.[0-9]+)-([0-9]+)$ ]]; then
    echo "ERROR: unsupported Rox-Filer2 version: $DISPLAY_VERSION" >&2
    echo "Expected a version like 2.13.0-1." >&2
    exit 1
fi
# XBPS uses the suffix after an underscore as the package revision.  Rox-Filer2
# releases already use Debian-style X.Y.Z-N versions, so map 2.13.0-8 to
# 2.13.0_8 without inventing an extra numeric component.
VERSION="${BASH_REMATCH[1]}"
REVISION="${BASH_REMATCH[2]}"
PKGVER="${PACKAGE_NAME}-${VERSION}_${REVISION}"

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "ERROR: required command not found: $1" >&2
        exit 1
    }
}

for cmd in cc pkg-config meson ninja python3 msgfmt xbps-create xbps-rindex xbps-uhelper xbps-query install gzip strip readelf fakeroot; do
    need_cmd "$cmd"
done

XBPS_ARCH=$(xbps-uhelper arch)

required_pc=(gtk+-3.0 gio-unix-2.0 libxml-2.0 sm ice x11 shared-mime-info pango)
missing=0
for mod in "${required_pc[@]}"; do
    if ! pkg-config --exists "$mod"; then
        echo "ERROR: missing pkg-config dependency: $mod" >&2
        missing=1
    fi
done
if [[ $missing -ne 0 ]]; then
    echo "Install the Rox-Filer2 Void build dependencies first." >&2
    exit 1
fi

prepare_messages() {
    local lang po
    echo "==> Validating and building translations"
    for lang in "${LANGS[@]}"; do
        po="$APP_DIR/src/po/$lang.po"
        msgfmt -c "$po" -o /dev/null
    done
    local po_files=()
    for lang in "${LANGS[@]}"; do po_files+=("$APP_DIR/src/po/$lang.po"); done
    python3 "$PROJECT_ROOT/check-po-formats.py" "${po_files[@]}"
    rm -rf "$APP_DIR/Messages"
    mkdir -p "$APP_DIR/Messages"
    for lang in "${LANGS[@]}"; do
        (cd "$APP_DIR/src/po" && ./make-mo "$lang")
    done
}

build_rox() {
    echo "==> Building Rox-Filer2 $DISPLAY_VERSION for Void Linux ($XBPS_ARCH)"
    rm -rf "$BUILD_DIR"
    meson setup --buildtype=release -Dsmb=auto -Dportable_devices=auto "$BUILD_DIR" "$PROJECT_ROOT"
    meson compile -C "$BUILD_DIR"
    [[ -x "$BUILD_DIR/ROX-Filer" ]] || { echo "ERROR: ROX-Filer binary was not produced." >&2; exit 1; }
    [[ -x "$BUILD_DIR/rox-find" ]] || { echo "ERROR: rox-find binary was not produced." >&2; exit 1; }
    [[ -x "$BUILD_DIR/rox-mount-helper" ]] || { echo "ERROR: rox-mount-helper binary was not produced." >&2; exit 1; }
}

stage_runtime() {
    echo "==> Preparing Void package filesystem"
    rm -rf "$WORK_DIR"
    mkdir -p "$STAGE_DIR"

    cp -a "$PROJECT_ROOT/package-base/usr" "$STAGE_DIR/"

    local app_install="$STAGE_DIR/usr/lib/rox-filer2"
    rm -rf "$app_install"
    mkdir -p "$app_install"
    cp -a "$APP_DIR/." "$app_install/"
    install -m0755 "$BUILD_DIR/ROX-Filer" "$app_install/ROX-Filer"
    install -m0755 "$BUILD_DIR/rox-mount-helper" "$app_install/rox-mount-helper"
    rm -rf "$app_install/build" "$app_install/src" "$app_install/ROX-Filer.dbg" "$app_install/ROX"
    cp -a "$PROJECT_ROOT/package-assets/ROX" "$app_install/ROX"
    install -m0644 "$PROJECT_ROOT/data/icons/hicolor/256x256/apps/rox-filer2.png" "$app_install/ROX-Filer.png"
    ln -sfn ROX-Filer.png "$app_install/.DirIcon"

    local size icon
    for size in 16 22 24 32 48 64 96 128 192 256; do
        icon="$PROJECT_ROOT/data/icons/hicolor/${size}x${size}/apps/rox-filer2.png"
        install -Dm0644 "$icon" "$STAGE_DIR/usr/share/icons/hicolor/${size}x${size}/apps/rox-filer2.png"
    done
    install -Dm0644 "$PROJECT_ROOT/data/icons/hicolor/scalable/apps/rox-filer2.svg" \
        "$STAGE_DIR/usr/share/icons/hicolor/scalable/apps/rox-filer2.svg"
    for icon in rox-filer2.svg ROX-Filer.svg Rox-Filer2.svg; do
        install -Dm0644 "$PROJECT_ROOT/data/icons/hicolor/scalable/apps/rox-filer2.svg" \
            "$STAGE_DIR/usr/share/pixmaps/$icon"
    done
    install -Dm0644 "$PROJECT_ROOT/ROX-Filer-root.svg" \
        "$STAGE_DIR/usr/share/pixmaps/ROX-Filer-root.svg"

    for icon in application-pet.svg application-x-sfs.svg application-x-squashfs-image.svg; do
        install -Dm0644 "$PROJECT_ROOT/package-assets/ROX/MIME/$icon" \
            "$STAGE_DIR/usr/share/icons/hicolor/scalable/mimetypes/$icon"
    done
    install -Dm0644 "$PROJECT_ROOT/data/mime/rox-filer2-image-mounter.xml" \
        "$STAGE_DIR/usr/share/mime/packages/rox-filer2-image-mounter.xml"

    install -Dm0755 "$BUILD_DIR/rox-find" "$STAGE_DIR/usr/bin/rox-find"
    install -Dm0644 "$PROJECT_ROOT/rox-find/data/rox-find.desktop" \
        "$STAGE_DIR/usr/share/applications/rox-find.desktop"
    install -Dm0644 "$PROJECT_ROOT/rox-find/data/rox-find.svg" \
        "$STAGE_DIR/usr/share/pixmaps/rox-find.svg"
    for size in 48 64 128 256; do
        install -Dm0644 "$PROJECT_ROOT/rox-find/data/icons/${size}x${size}/apps/rox-find.png" \
            "$STAGE_DIR/usr/share/icons/hicolor/${size}x${size}/apps/rox-find.png"
    done
    if [[ -d "$PROJECT_ROOT/rox-find/locale" ]]; then
        mkdir -p "$STAGE_DIR/usr/share/locale"
        cp -a "$PROJECT_ROOT/rox-find/locale/." "$STAGE_DIR/usr/share/locale/"
    fi

    install -Dm0644 "$PROJECT_ROOT/LICENSE" "$STAGE_DIR/usr/share/licenses/$PACKAGE_NAME/LICENSE"
    install -Dm0644 "$PROJECT_ROOT/CHANGELOG" "$STAGE_DIR/usr/share/doc/$PACKAGE_NAME/CHANGELOG"
    install -Dm0644 "$PROJECT_ROOT/README.md" "$STAGE_DIR/usr/share/doc/$PACKAGE_NAME/README.md"

    install -Dm0644 "$PROJECT_ROOT/data/man/rox.1" "$STAGE_DIR/usr/share/man/man1/rox.1"
    install -Dm0644 "$PROJECT_ROOT/data/man/rox-find.1" "$STAGE_DIR/usr/share/man/man1/rox-find.1"
    gzip -n -9 "$STAGE_DIR/usr/share/man/man1/rox.1" "$STAGE_DIR/usr/share/man/man1/rox-find.1"
    for alias in roxfiler Rox-Filer2 ROX-Filer rox-wayland rox-x11; do
        ln -sfn rox.1.gz "$STAGE_DIR/usr/share/man/man1/$alias.1.gz"
    done

    # xbps-create does not perform the stripping stage that xbps-src normally
    # applies, so normalize the two native binaries here.
    strip --strip-unneeded "$app_install/ROX-Filer" "$app_install/rox-mount-helper" "$STAGE_DIR/usr/bin/rox-find"

    find "$STAGE_DIR/usr/share/applications" -type f -name '*.desktop' -exec chmod 0644 {} + 2>/dev/null || true
    find "$STAGE_DIR/usr/share/pixmaps" -type f -exec chmod 0644 {} + 2>/dev/null || true
    find "$STAGE_DIR" -type d -exec chmod u-s,g-s {} +
}

collect_shlib_requires() {
    {
        readelf -d "$STAGE_DIR/usr/lib/rox-filer2/ROX-Filer"
        readelf -d "$STAGE_DIR/usr/lib/rox-filer2/rox-mount-helper"
        readelf -d "$STAGE_DIR/usr/bin/rox-find"
    } | awk -F'[][]' '/Shared library:/{print $2}' | LC_ALL=C sort -u | tr '\n' ' '
}

package_is_installed() {
    xbps-query "$1" >/dev/null 2>&1
}

void_dependency_pattern() {
    local package="$1" pkgver pkgname version pattern checked

    pkgver=$(xbps-query -p pkgver "$package" 2>/dev/null) || return 1
    pkgname=$(xbps-uhelper getpkgname "$pkgver" 2>/dev/null) || return 1
    version=${pkgver#"$pkgname-"}
    [[ -n "$pkgname" && -n "$version" && "$version" != "$pkgver" ]] || return 1
    pattern="${pkgname}>=${version}"
    checked=$(xbps-uhelper getpkgdepname "$pattern" 2>/dev/null) || return 1
    [[ "$checked" == "$pkgname" ]] || return 1
    printf '%s\n' "$pattern"
}

make_void_package() {
    mkdir -p "$OUTPUT_DIR"

    # Core GUI/network dependencies plus helpers required for the complete
    # Image Mounter/Wayland integration.  On a normal Void/KLV build system
    # these were installed by install-roxfiler2-build-deps.sh.
    local deps=(gtk+3 glib libxml2 libSM libICE libX11 shared-mime-info hicolor-icon-theme desktop-file-utils util-linux file)
    local full_feature=(gtk-layer-shell polkit cifs-utils samba fuse3 jmtpfs gphoto2 gphotofs ifuse usbmuxd libimobiledevice-utils)
    local p
    for p in "${full_feature[@]}"; do
        if package_is_installed "$p"; then
            deps+=("$p")
        else
            echo "WARNING: optional full-integration package is not installed: $p" >&2
        fi
    done

    # XBPS -D requires package patterns, not bare package names. Resolve every
    # installed runtime package to a valid versioned pattern such as
    # gtk+3>=3.24.49_1, then validate it with xbps-uhelper before packaging.
    local dep_patterns=() dep pattern dep_string
    while IFS= read -r dep; do
        [[ -n "$dep" ]] || continue
        if ! pattern=$(void_dependency_pattern "$dep"); then
            echo "ERROR: unable to create a valid XBPS dependency pattern for: $dep" >&2
            exit 1
        fi
        dep_patterns+=("$pattern")
    done < <(printf '%s\n' "${deps[@]}" | awk '!seen[$0]++')
    dep_string=$(printf '%s ' "${dep_patterns[@]}")
    dep_string=${dep_string% }

    local shlibs
    shlibs=$(collect_shlib_requires)
    shlibs=${shlibs% }

    echo "==> Creating native XBPS package"
    echo "    architecture: $XBPS_ARCH"
    echo "    dependencies: $dep_string"

    local before after pkg
    before=$(find "$OUTPUT_DIR" -maxdepth 1 -type f -name "${PACKAGE_NAME}-${VERSION}_${REVISION}.*.xbps" -print | sort || true)

    (
        cd "$OUTPUT_DIR"
        fakeroot xbps-create \
            -A "$XBPS_ARCH" \
            -n "$PKGVER" \
            -s 'Rox-Filer2 lightweight GTK3 file manager and desktop' \
            -S 'Rox-Filer2 with Classic/Modern interfaces, SMB and Image Mounter.' \
            -m 'josejp2424' \
            -l 'GPL-3.0-or-later' \
            -H 'https://github.com/josejp2424/ROX-Filer2' \
            -B "Rox-Filer2 build_void.sh on $XBPS_ARCH" \
            -D "$dep_string" \
            -P "rox-filer-${VERSION}_${REVISION}" \
            -C 'rox-filer>=0' \
            --shlib-requires "$shlibs" \
            --compression zstd \
            "$STAGE_DIR"
    )

    after=$(find "$OUTPUT_DIR" -maxdepth 1 -type f -name "${PACKAGE_NAME}-${VERSION}_${REVISION}.*.xbps" -print | sort || true)
    pkg=$(comm -13 <(printf '%s\n' "$before" | sed '/^$/d') <(printf '%s\n' "$after" | sed '/^$/d') | tail -n1)
    if [[ -z "$pkg" ]]; then
        pkg=$(printf '%s\n' "$after" | tail -n1)
    fi
    [[ -n "$pkg" && -f "$pkg" ]] || { echo "ERROR: xbps-create finished but the package was not found." >&2; exit 1; }

    xbps-rindex -fa "$pkg" >/dev/null

    echo
    echo "============================================================"
    echo "Rox-Filer2 Void package created successfully"
    echo "Version : $DISPLAY_VERSION"
    echo "Package : $pkg"
    echo "Install : sudo xbps-install -R '$OUTPUT_DIR' rox-filer2"
    echo "============================================================"
}

prepare_messages
build_rox
stage_runtime
make_void_package
