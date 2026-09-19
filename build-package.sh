#!/bin/sh
set -eu

PROJECT_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
APP_DIR="$PROJECT_ROOT/ROX-Filer"
PACKAGE_BASE="$PROJECT_ROOT/package-base"
OUTPUT_DIR="$PROJECT_ROOT/output"
PACKAGE_NAME="rox-filer2"

# Keep package/version naming in sync with Rox-Filer2 itself.
# AppInfo.xml is the canonical source. Current releases use Debian-style
# versions such as 2.13.0-1; legacy 2.12-rNN trees remain supported.
DISPLAY_VERSION=$(
    sed -n 's/^[[:space:]]*<Version>\([^<][^<]*\)<\/Version>[[:space:]]*$/\1/p' \
        "$APP_DIR/AppInfo.xml" | head -n 1
)

if [ -z "$DISPLAY_VERSION" ]; then
    echo "ERROR: unable to read Rox-Filer2 version from $APP_DIR/AppInfo.xml" >&2
    exit 1
fi

case "$DISPLAY_VERSION" in
    *-r*)
        BASE_VERSION=${DISPLAY_VERSION%-r*}
        REVISION=${DISPLAY_VERSION##*-r}
        case "$REVISION" in
            ''|*[!0-9]*)
                echo "ERROR: unsupported Rox-Filer2 revision in version: $DISPLAY_VERSION" >&2
                exit 1
                ;;
        esac
        DEB_VERSION="${BASE_VERSION}+gtk3.v-${REVISION}"
        ;;
    *)
        if printf '%s\n' "$DISPLAY_VERSION" |
            grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+-[0-9]+$'; then
            DEB_VERSION="$DISPLAY_VERSION"
        else
            echo "ERROR: unsupported Rox-Filer2 version format: $DISPLAY_VERSION" >&2
            exit 1
        fi
        ;;
esac
SKIP_COMPILE=0
ARCH_PACKAGE_MODE=auto
MESON_BUILD_DIR="${ROX_MESON_BUILD_DIR:-$PROJECT_ROOT/build}"
ROX_BINARY=""
ROX_FIND_BINARY=""
MOUNT_HELPER_BINARY=""

# 2.12.2-97: some Puppy/Essora bases provide file utilities through BusyBox
# without a standalone `cp` in PATH.  Resolve a copy backend once and use it
# throughout the packager so a successful Rox-Filer2 build is not discarded
# merely because the coreutils cp frontend is absent.
CP_BIN=""
BUSYBOX_BIN=""
resolve_copy_backend() {
    if command -v cp >/dev/null 2>&1; then
        CP_BIN=$(command -v cp)
    elif [ -x /bin/cp ]; then
        CP_BIN=/bin/cp
    elif [ -x /usr/bin/cp ]; then
        CP_BIN=/usr/bin/cp
    elif command -v busybox >/dev/null 2>&1; then
        BUSYBOX_BIN=$(command -v busybox)
    elif [ -x /bin/busybox ]; then
        BUSYBOX_BIN=/bin/busybox
    elif [ -x /usr/bin/busybox ]; then
        BUSYBOX_BIN=/usr/bin/busybox
    else
        echo "ERROR: neither cp nor BusyBox cp is available; cannot stage package files." >&2
        exit 1
    fi
}

copy_a() {
    if [ -n "$CP_BIN" ]; then
        "$CP_BIN" -a "$@"
    else
        "$BUSYBOX_BIN" cp -a "$@"
    fi
}

copy_f() {
    if [ -n "$CP_BIN" ]; then
        "$CP_BIN" -f "$@"
    else
        "$BUSYBOX_BIN" cp -f "$@"
    fi
}

usage() {
    cat <<USAGE
Usage: $0 [--skip-compile] [--arch-package] [--no-arch-package] [--clean]

  --skip-compile    Package an existing Meson or legacy Rox-Filer2 build.
  --arch-package    Force generation of an Arch Linux package with makepkg.
  --no-arch-package Disable automatic Arch Linux package generation.
  --clean           Remove generated package output and build directories.

Meson + Ninja are required for release builds. The historical AppRun/autoconf
build remains available for developers but is validated separately. It then creates:
  - a Debian .deb package when dpkg-deb is available;
  - the complete Debian package directory;
  - a portable root filesystem directory and tar.gz archive;
  - an Arch Linux .pkg.tar.zst when Arch/makepkg is detected.
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --skip-compile) SKIP_COMPILE=1 ;;
        --arch-package) ARCH_PACKAGE_MODE=force ;;
        --no-arch-package) ARCH_PACKAGE_MODE=off ;;
        --clean)
            rm -rf "$OUTPUT_DIR" "$APP_DIR/build" "$MESON_BUILD_DIR"
            rm -f \
                "$APP_DIR/ROX-Filer" \
                "$APP_DIR/ROX-Filer.dbg" \
                "$PROJECT_ROOT/rox-find/rox-find"
            echo "Cleaned generated output and in-tree binaries."
            exit 0
            ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
    shift
done

resolve_copy_backend

if [ "$SKIP_COMPILE" -eq 0 ]; then
    command -v msgfmt >/dev/null 2>&1 || { echo "ERROR: msgfmt/gettext is required." >&2; exit 1; }
    command -v python3 >/dev/null 2>&1 || { echo "ERROR: python3 is required for the PO format gate." >&2; exit 1; }
    PO_FILES=""
    for lang in ar ca de es fr hu it ja pt ru zh; do
        po_file="$APP_DIR/src/po/$lang.po"
        msgfmt -c "$po_file" -o /dev/null
        PO_FILES="$PO_FILES $po_file"
    done
    # Validate every catalogue before touching the bundled runtime Messages.
    # shellcheck disable=SC2086
    python3 "$PROJECT_ROOT/check-po-formats.py" $PO_FILES
    rm -rf "$APP_DIR/Messages"
    mkdir -p "$APP_DIR/Messages"
    for lang in ar ca de es fr hu it ja pt ru zh; do
        (cd "$APP_DIR/src/po" && ./make-mo "$lang")
    done
    command -v meson >/dev/null 2>&1 || { echo "ERROR: Meson is required." >&2; exit 1; }
    command -v ninja >/dev/null 2>&1 || { echo "ERROR: Ninja is required." >&2; exit 1; }
    echo "Building Rox-Filer2 with Meson..." >&2
    rm -rf "$MESON_BUILD_DIR"
    # 2.12.2-93: SMB is runtime-optional. The executable never links directly
    # against libsmbclient, so one package starts with or without libsmbclient0.
    # If libsmbclient.so.0 is present it is discovered lazily with dlopen().
    meson setup --buildtype=release -Dsmb=auto -Dportable_devices=auto \
        "$MESON_BUILD_DIR" "$PROJECT_ROOT"
    meson compile -C "$MESON_BUILD_DIR"
    ROX_BINARY="$MESON_BUILD_DIR/ROX-Filer"
    ROX_FIND_BINARY="$MESON_BUILD_DIR/rox-find"
    MOUNT_HELPER_BINARY="$MESON_BUILD_DIR/rox-mount-helper"
else
    ROX_BINARY="$MESON_BUILD_DIR/ROX-Filer"
    ROX_FIND_BINARY="$MESON_BUILD_DIR/rox-find"
    MOUNT_HELPER_BINARY="$MESON_BUILD_DIR/rox-mount-helper"
fi

if [ ! -x "$ROX_BINARY" ]; then
    echo "ERROR: compiled Rox-Filer2 binary not found: $ROX_BINARY" >&2
    exit 1
fi

if [ ! -x "$ROX_FIND_BINARY" ]; then
    echo "ERROR: ROX File Search binary not found: $ROX_FIND_BINARY" >&2
    exit 1
fi

if [ ! -x "$MOUNT_HELPER_BINARY" ]; then
    echo "ERROR: Rox-Filer2 mount helper not found: $MOUNT_HELPER_BINARY" >&2
    exit 1
fi

if [ ! -d "$PROJECT_ROOT/package-assets/ROX" ]; then
    echo "ERROR: supplied ROX package directory is missing." >&2
    exit 1
fi

if command -v dpkg >/dev/null 2>&1; then
    ARCH=$(dpkg --print-architecture 2>/dev/null || true)
else
    ARCH=""
fi

if [ -z "$ARCH" ]; then
    case "$(uname -m)" in
        x86_64) ARCH=amd64 ;;
        i?86) ARCH=i386 ;;
        aarch64) ARCH=arm64 ;;
        armv7*|armv6*) ARCH=armhf ;;
        *) ARCH=$(uname -m) ;;
    esac
fi

PACKAGE_DIR="$OUTPUT_DIR/${PACKAGE_NAME}_${DEB_VERSION}_${ARCH}"
PORTABLE_DIR="$OUTPUT_DIR/${PACKAGE_NAME}-${DISPLAY_VERSION}-portable-${ARCH}"
DEB_FILE="$OUTPUT_DIR/${PACKAGE_NAME}_${DEB_VERSION}_${ARCH}.deb"
PORTABLE_TAR="$OUTPUT_DIR/${PACKAGE_NAME}-${DISPLAY_VERSION}-portable-${ARCH}.tar.gz"

rm -rf "$PACKAGE_DIR" "$PORTABLE_DIR" "$DEB_FILE" "$PORTABLE_TAR"
mkdir -p "$OUTPUT_DIR" "$PACKAGE_DIR"
# Do not allow setuid/setgid bits inherited from the source/build directory
# to leak into the Debian package tree. dpkg-deb requires DEBIAN itself to
# have ordinary directory permissions (0755..0775, without special bits).
chmod u-s,g-s "$OUTPUT_DIR" "$PACKAGE_DIR"
chmod 0755 "$OUTPUT_DIR" "$PACKAGE_DIR"
copy_a "$PACKAGE_BASE/." "$PACKAGE_DIR/"
find "$PACKAGE_DIR" -type d -exec chmod u-s,g-s {} +

# El AppDir empaquetado vive en /usr/lib/rox-filer2 y es propiedad de dpkg.
APP_INSTALL_DIR="$PACKAGE_DIR/usr/lib/rox-filer2"
rm -rf "$APP_INSTALL_DIR"
mkdir -p "$APP_INSTALL_DIR"
copy_a "$APP_DIR/." "$APP_INSTALL_DIR/"
install -m0755 "$ROX_BINARY" "$APP_INSTALL_DIR/ROX-Filer"
install -m0755 "$MOUNT_HELPER_BINARY" "$APP_INSTALL_DIR/rox-mount-helper"
rm -f "$APP_INSTALL_DIR/ROX-Filer.dbg"
rm -rf "$APP_INSTALL_DIR/ROX"
copy_a "$PROJECT_ROOT/package-assets/ROX" "$APP_INSTALL_DIR/ROX"

# This is a user file template, not a packaged executable. Preserve the useful
# Python shebang in newly-created files without creating a runtime dependency.
if [ -f "$APP_INSTALL_DIR/Templates/python3.py" ]; then
    chmod 0644 "$APP_INSTALL_DIR/Templates/python3.py"
fi

# Runtime packages must not contain compiler output or C source files.
rm -rf \
    "$APP_INSTALL_DIR/build" \
    "$APP_INSTALL_DIR/src"

# Install the official Rox-Filer2 application icon set supplied in
# data/icons/hicolor.  Desktop files use Icon=rox-filer2 so GTK/icon themes
# select the best size.  Also refresh every legacy/AppDir fallback here so a
# stale icon from package-base can never override the current artwork.
ROX_ICON_ROOT="$PROJECT_ROOT/data/icons/hicolor"
for size in 16 22 24 32 48 64 96 128 192 256; do
    icon_file="$ROX_ICON_ROOT/${size}x${size}/apps/rox-filer2.png"
    if [ ! -f "$icon_file" ]; then
        echo "ERROR: missing Rox-Filer2 icon: $icon_file" >&2
        exit 1
    fi
    install -Dm0644 "$icon_file" \
        "$PACKAGE_DIR/usr/share/icons/hicolor/${size}x${size}/apps/rox-filer2.png"
done

ROX_SCALABLE_ICON="$ROX_ICON_ROOT/scalable/apps/rox-filer2.svg"
if [ ! -f "$ROX_SCALABLE_ICON" ]; then
    echo "ERROR: missing Rox-Filer2 scalable icon: $ROX_SCALABLE_ICON" >&2
    exit 1
fi
install -Dm0644 "$ROX_SCALABLE_ICON" \
    "$PACKAGE_DIR/usr/share/icons/hicolor/scalable/apps/rox-filer2.svg"

# ROX AppDir fallback used when the icon theme is unavailable.
install -Dm0644 \
    "$ROX_ICON_ROOT/256x256/apps/rox-filer2.png" \
    "$APP_INSTALL_DIR/ROX-Filer.png"
ln -sfn ROX-Filer.png "$APP_INSTALL_DIR/.DirIcon"

# 2.12.2-82: hasta -81 el paquete instalaba enlaces en /usr/local/apps y el
# lanzador en /usr/local/bin.  /usr/local esta reservado al administrador
# local: lintian lo marcaba con cinco errores y el preinst llegaba a hacer
# "rm -rf" sobre AppDirs reales que el usuario pudiera tener alli.  El
# paquete ya no toca /usr/local en absoluto.

# Legacy pixmaps fallbacks used by lightweight Puppy setups and old launchers.
for legacy_name in rox-filer2.svg ROX-Filer.svg Rox-Filer2.svg; do
    install -Dm0644 "$ROX_SCALABLE_ICON" \
        "$PACKAGE_DIR/usr/share/pixmaps/$legacy_name"
done
install -Dm0644 "$PROJECT_ROOT/ROX-Filer-root.svg" \
    "$PACKAGE_DIR/usr/share/pixmaps/ROX-Filer-root.svg"

# Install Puppy/ROX MIME SVG icons as ordinary dpkg-owned payload. SVG files
# belong only in hicolor/scalable, never in fixed-size directories.
for mime_icon in application-pet.svg application-x-sfs.svg application-x-squashfs-image.svg; do
    install -Dm0644 "$PROJECT_ROOT/package-assets/ROX/MIME/$mime_icon" \
        "$PACKAGE_DIR/usr/share/icons/hicolor/scalable/mimetypes/$mime_icon"
done

# 2.12.2-84: extend the shared MIME database so older Puppy and lightweight
# distributions recognise .sfs/.squashfs/.sqfs as SquashFS images. ISO and
# raw IMG globs are repeated intentionally for old shared-mime-info releases.
install -Dm0644 "$PROJECT_ROOT/data/mime/rox-filer2-image-mounter.xml" \
    "$PACKAGE_DIR/usr/share/mime/packages/rox-filer2-image-mounter.xml"

# Install the native ROX File Search companion application.
install -Dm0755 "$ROX_FIND_BINARY" \
    "$PACKAGE_DIR/usr/bin/rox-find"

# Debian binary packages ship stripped runtime binaries. Keep this explicit so
# Meson and legacy-built inputs are normalized to the same package policy.
command -v strip >/dev/null 2>&1 || { echo "ERROR: strip/binutils is required." >&2; exit 1; }
strip --strip-unneeded "$APP_INSTALL_DIR/ROX-Filer" "$APP_INSTALL_DIR/rox-mount-helper" "$PACKAGE_DIR/usr/bin/rox-find"
install -Dm0644 "$PROJECT_ROOT/rox-find/data/rox-find.desktop" \
    "$PACKAGE_DIR/usr/share/applications/rox-find.desktop"
install -Dm0644 "$PROJECT_ROOT/rox-find/data/rox-find.svg" \
    "$PACKAGE_DIR/usr/share/pixmaps/rox-find.svg"
for size in 48 64 128; do
    install -Dm0644 \
        "$PROJECT_ROOT/rox-find/data/icons/${size}x${size}/apps/rox-find.png" \
        "$PACKAGE_DIR/usr/share/icons/hicolor/${size}x${size}/apps/rox-find.png"
done
if [ -d "$PROJECT_ROOT/rox-find/locale" ]; then
    mkdir -p "$PACKAGE_DIR/usr/share/locale"
    copy_a "$PROJECT_ROOT/rox-find/locale/." "$PACKAGE_DIR/usr/share/locale/"
fi

# Restore package-base integration files and normalize permissions.
mkdir -p "$PACKAGE_DIR/DEBIAN"
chmod u-s,g-s "$PACKAGE_DIR/DEBIAN"
chmod 0755 "$PACKAGE_DIR/DEBIAN"
copy_a "$PROJECT_ROOT/DEBIAN/preinst" "$PACKAGE_DIR/DEBIAN/preinst"
copy_a "$PROJECT_ROOT/DEBIAN/postinst" "$PACKAGE_DIR/DEBIAN/postinst"
copy_a "$PROJECT_ROOT/DEBIAN/postrm" "$PACKAGE_DIR/DEBIAN/postrm"
chmod 0755 "$PACKAGE_DIR/DEBIAN/preinst" "$PACKAGE_DIR/DEBIAN/postinst" "$PACKAGE_DIR/DEBIAN/postrm"
find "$PACKAGE_DIR/usr/share/applications" -type f -name '*.desktop' -exec chmod 0644 {} + 2>/dev/null || true
find "$PACKAGE_DIR/usr/share/pixmaps" -type f -exec chmod 0644 {} + 2>/dev/null || true

# Debian Policy documentation: license/copyright and changelog are part of the
# package payload and therefore tracked by dpkg.
install -Dm0644 "$PROJECT_ROOT/data/debian/copyright" \
    "$PACKAGE_DIR/usr/share/doc/$PACKAGE_NAME/copyright"
install -Dm0644 "$PROJECT_ROOT/data/debian/changelog.Debian" \
    "$PACKAGE_DIR/usr/share/doc/$PACKAGE_NAME/changelog.Debian"
gzip -n -9 "$PACKAGE_DIR/usr/share/doc/$PACKAGE_NAME/changelog.Debian"

# Manual pages for every installed command name. Wrapper aliases share the
# main Rox-Filer2 page; rox-find has its own page.
install -Dm0644 "$PROJECT_ROOT/data/man/rox.1" \
    "$PACKAGE_DIR/usr/share/man/man1/rox.1"
install -Dm0644 "$PROJECT_ROOT/data/man/rox-find.1" \
    "$PACKAGE_DIR/usr/share/man/man1/rox-find.1"
gzip -n -9 "$PACKAGE_DIR/usr/share/man/man1/rox.1" \
    "$PACKAGE_DIR/usr/share/man/man1/rox-find.1"
for alias in roxfiler Rox-Filer2 ROX-Filer rox-wayland rox-x11; do
    ln -sfn rox.1.gz "$PACKAGE_DIR/usr/share/man/man1/$alias.1.gz"
done

INSTALLED_SIZE=$(du -sk "$PACKAGE_DIR/usr" | awk '{print $1}')

# Prefer Debian's exact shlib resolver when it exists, but do not make it a
# prerequisite for building Rox-Filer2 on Essora/Puppy.  Those systems can
# intentionally provide dpkg-compatible package handling without dpkg-dev and
# therefore without dpkg-shlibdeps.  The fallback covers Rox-Filer2's direct
# core stack; optional SMB/MTP/PTP/iOS components remain Recommends.
SHLIBS_DEPENDS=${ROX_DEB_SHLIBS_DEPENDS:-}
if [ -z "$SHLIBS_DEPENDS" ] && command -v dpkg-shlibdeps >/dev/null 2>&1; then
    TMP_DEBIAN_DIR="$PROJECT_ROOT/debian"
    mkdir -p "$TMP_DEBIAN_DIR"
    cat > "$TMP_DEBIAN_DIR/control" <<EOF
Source: rox-filer2
Section: utils
Priority: optional
Maintainer: josejp2424 <puppylinuxjosejp2424@gmail.com>
Standards-Version: 4.6.2

Package: rox-filer2
Architecture: any
Description: Rox-Filer2
EOF
    SHLIBS_DEPENDS=$(cd "$PROJECT_ROOT" && dpkg-shlibdeps -O "$APP_INSTALL_DIR/ROX-Filer" "$APP_INSTALL_DIR/rox-mount-helper" "$PACKAGE_DIR/usr/bin/rox-find" 2>/dev/null | sed -n 's/^shlibs:Depends=//p' || true)
    rm -rf "$TMP_DEBIAN_DIR"
fi

if [ -z "$SHLIBS_DEPENDS" ]; then
    SHLIBS_DEPENDS='libc6, libgtk-3-0t64 | libgtk-3-0, libglib2.0-0t64 | libglib2.0-0, libxml2, libsm6, libice6, libx11-6'
    echo "WARNING: dpkg-shlibdeps unavailable or unable to resolve dependencies; using the portable Debian/Devuan core dependency fallback." >&2
fi
sed \
    -e "s/@VERSION@/$DEB_VERSION/g" \
    -e "s/@ARCH@/$ARCH/g" \
    -e "s/@INSTALLED_SIZE@/$INSTALLED_SIZE/g" \
    -e "s/@SHLIBS_DEPENDS@/$SHLIBS_DEPENDS/g" \
    "$PACKAGE_BASE/DEBIAN/control.in" > "$PACKAGE_DIR/DEBIAN/control"
chmod 0644 "$PACKAGE_DIR/DEBIAN/control"
rm -f "$PACKAGE_DIR/DEBIAN/control.in"

(
    cd "$PACKAGE_DIR"
    find usr -type f -print0 | LC_ALL=C sort -z | xargs -0 md5sum > DEBIAN/md5sums
)
chmod 0644 "$PACKAGE_DIR/DEBIAN/md5sums"

# Leave a portable filesystem tree for PET, Slackware, Arch or custom packages.
mkdir -p "$PORTABLE_DIR"
copy_a "$PACKAGE_DIR/usr" "$PORTABLE_DIR/usr"
cat > "$PORTABLE_DIR/PACKAGE-INFO.txt" <<INFO
Rox-Filer2 $DISPLAY_VERSION
Architecture: $ARCH

This directory is a portable filesystem tree. Copy or package its usr/
directory using the native package tools of the target distribution.
The Debian maintainer scripts are intentionally not included here.
INFO

(
    cd "$OUTPUT_DIR"
    tar -czf "$(basename "$PORTABLE_TAR")" "$(basename "$PORTABLE_DIR")"
)

if command -v dpkg-deb >/dev/null 2>&1; then
    dpkg-deb --build --root-owner-group "$PACKAGE_DIR" "$DEB_FILE"
    echo "Debian package: $DEB_FILE"
else
    echo "WARNING: dpkg-deb is unavailable; the .deb was not created." >&2
    echo "The complete package directory is still available at: $PACKAGE_DIR" >&2
fi

# Arch Linux packaging is optional and automatic.  The package is made from
# the exact portable tree produced above, so Debian/Puppy and Arch packages
# contain the same freshly compiled Rox-Filer2 and rox-find binaries.
IS_ARCH_LINUX=0
if [ -f /etc/arch-release ]; then
    IS_ARCH_LINUX=1
elif [ -r /etc/os-release ]; then
    OS_ID=
    OS_ID_LIKE=
    # os-release is shell syntax by specification.  Read only ID fields here.
    OS_ID=$(sed -n 's/^ID=//p' /etc/os-release | head -n1 | tr -d '\"')
    OS_ID_LIKE=$(sed -n 's/^ID_LIKE=//p' /etc/os-release | head -n1 | tr -d '\"')
    case " $OS_ID $OS_ID_LIKE " in
        *" arch "*) IS_ARCH_LINUX=1 ;;
    esac
fi

MAKEPKG_AVAILABLE=0
if command -v makepkg >/dev/null 2>&1; then
    MAKEPKG_AVAILABLE=1
fi

BUILD_ARCH_PACKAGE=0
case "$ARCH_PACKAGE_MODE" in
    force) BUILD_ARCH_PACKAGE=1 ;;
    off) BUILD_ARCH_PACKAGE=0 ;;
    auto)
        if [ "$IS_ARCH_LINUX" -eq 1 ] || [ "$MAKEPKG_AVAILABLE" -eq 1 ]; then
            BUILD_ARCH_PACKAGE=1
        fi
        ;;
esac

ARCH_PACKAGE_FILE=
if [ "$BUILD_ARCH_PACKAGE" -eq 1 ]; then
    if [ "$MAKEPKG_AVAILABLE" -ne 1 ]; then
        if [ "$ARCH_PACKAGE_MODE" = force ]; then
            echo "ERROR: --arch-package requested, but makepkg is not installed." >&2
            exit 1
        fi
        echo "WARNING: Arch Linux detected but makepkg is unavailable; skipping native Arch package." >&2
        echo "Install Arch base-devel (which provides makepkg) and run the script again." >&2
    elif [ "$(id -u)" -eq 0 ]; then
        echo "WARNING: makepkg refuses to run as root; skipping native Arch package." >&2
        echo "Run build-package.sh as a normal user to create the Arch package." >&2
    else
        case "$DISPLAY_VERSION" in
            *-r*)
                ARCH_PKGVER="${BASE_VERSION}.r${REVISION}"
                ARCH_PKGREL=1
                ;;
            *-*)
                ARCH_PKGVER=${DISPLAY_VERSION%-*}
                ARCH_PKGREL=${DISPLAY_VERSION##*-}
                ;;
            *)
                ARCH_PKGVER=$DISPLAY_VERSION
                ARCH_PKGREL=1
                ;;
        esac

        case "$(uname -m)" in
            x86_64) ARCH_NATIVE_ARCH=x86_64 ;;
            i?86) ARCH_NATIVE_ARCH=i686 ;;
            aarch64) ARCH_NATIVE_ARCH=aarch64 ;;
            armv7*|armv6*) ARCH_NATIVE_ARCH=armv7h ;;
            *) ARCH_NATIVE_ARCH=$(uname -m) ;;
        esac

        ARCH_BUILD_DIR="$OUTPUT_DIR/arch-build"
        ARCH_SOURCE_BASENAME=$(basename "$PORTABLE_TAR")
        ARCH_PORTABLE_BASENAME=$(basename "$PORTABLE_DIR")
        ARCH_SOURCE_SHA256=$(sha256sum "$PORTABLE_TAR" | awk '{print $1}')

        rm -rf "$ARCH_BUILD_DIR"
        mkdir -p "$ARCH_BUILD_DIR"
        copy_a "$PORTABLE_TAR" "$ARCH_BUILD_DIR/$ARCH_SOURCE_BASENAME"

        cat > "$ARCH_BUILD_DIR/PKGBUILD" <<ARCHPKG
pkgname=rox-filer2
pkgver=$ARCH_PKGVER
pkgrel=$ARCH_PKGREL
pkgdesc='Rox-Filer2 lightweight GTK3 file manager and desktop'
arch=('$ARCH_NATIVE_ARCH')
url='https://github.com/josejp2424/ROX-Filer-gtk3'
license=('GPL-3.0-or-later')
depends=('glibc' 'glib2' 'gtk3' 'libxml2' 'libsm' 'libice' 'libx11' 'shared-mime-info' 'hicolor-icon-theme')
optdepends=('gtk-layer-shell: Wayland desktop layer support')
provides=('rox-filer=$ARCH_PKGVER' 'file-manager')
conflicts=('rox-filer')
options=('!strip' '!debug')
source=('$ARCH_SOURCE_BASENAME')
sha256sums=('$ARCH_SOURCE_SHA256')

package() {
    cp -a "\$srcdir/$ARCH_PORTABLE_BASENAME/usr" "\$pkgdir/"

}
ARCHPKG

        echo "Building Arch Linux package with makepkg..." >&2
        (
            cd "$ARCH_BUILD_DIR"
            PKGDEST="$ARCH_BUILD_DIR" \
            SRCDEST="$ARCH_BUILD_DIR" \
            SRCPKGDEST="$ARCH_BUILD_DIR" \
            LOGDEST="$ARCH_BUILD_DIR" \
                makepkg --force --cleanbuild --clean
        )

        ARCH_PACKAGE_FILE=$(
            find "$ARCH_BUILD_DIR" -maxdepth 1 -type f \
                -name "${PACKAGE_NAME}-${ARCH_PKGVER}-${ARCH_PKGREL}-*.pkg.tar.*" \
                ! -name '*.sig' -print | head -n1
        )
        if [ -z "$ARCH_PACKAGE_FILE" ]; then
            echo "ERROR: makepkg completed but no Arch package was found." >&2
            exit 1
        fi
        ARCH_FINAL_FILE="$OUTPUT_DIR/$(basename "$ARCH_PACKAGE_FILE")"
        copy_f "$ARCH_PACKAGE_FILE" "$ARCH_FINAL_FILE"
        ARCH_PACKAGE_FILE=$ARCH_FINAL_FILE
        echo "Arch Linux package: $ARCH_PACKAGE_FILE"
        echo "Generated PKGBUILD: $ARCH_BUILD_DIR/PKGBUILD"
    fi
fi

# build/ is disposable. src/ remains only in the development source tree;
# both directories were removed from the generated runtime package trees.
rm -rf "$APP_DIR/build"

echo "Complete Debian package directory: $PACKAGE_DIR"
echo "Portable package directory: $PORTABLE_DIR"
echo "Portable archive: $PORTABLE_TAR"
