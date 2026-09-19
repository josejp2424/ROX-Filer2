#!/usr/bin/env bash
# Rox-Filer2 native Arch Linux package builder.
# Compile Rox-Filer2 with Meson and runtime-optional libsmbclient support.
set -euo pipefail

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
APP_DIR="$PROJECT_ROOT/ROX-Filer"
BUILD_DIR="${ROX_ARCH_BUILD_DIR:-$PROJECT_ROOT/build-arch}"
OUTPUT_DIR="${ROX_OUTPUT_DIR:-$PROJECT_ROOT/output}"
WORK_DIR="$OUTPUT_DIR/arch-build"
STAGE_DIR="$WORK_DIR/rootfs"
PACKAGE_NAME="rox-filer2"
LANGS=(ar ca de es fr hu it ja pt ru zh)

usage() {
    cat <<'USAGE'
Usage: ./build_arch.sh [--clean]

Builds Rox-Filer2 natively on Arch Linux. libsmbclient is detected at runtime
and is not required for the file manager to start.

Requirements: install the Arch build dependencies first. makepkg must be
run as a normal user.
USAGE
}

if [[ "${1:-}" == "--clean" ]]; then
    rm -rf "$BUILD_DIR" "$WORK_DIR"
    echo "Cleaned Arch build directories."
    exit 0
elif [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
elif [[ $# -gt 0 ]]; then
    echo "ERROR: unknown option: $1" >&2
    usage >&2
    exit 2
fi

if [[ ! -f /etc/arch-release ]] && ! command -v pacman >/dev/null 2>&1; then
    echo "ERROR: build_arch.sh must be run on Arch Linux or an Arch-based system." >&2
    exit 1
fi

if [[ ${EUID:-$(id -u)} -eq 0 ]]; then
    echo "ERROR: do not run build_arch.sh as root; makepkg refuses to build as root." >&2
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
PKGVER="${BASH_REMATCH[1]}"
PKGREL="${BASH_REMATCH[2]}"

case "$(uname -m)" in
    x86_64) ARCH=x86_64 ;;
    i?86) ARCH=i686 ;;
    aarch64) ARCH=aarch64 ;;
    armv7*) ARCH=armv7h ;;
    *) ARCH=$(uname -m) ;;
esac

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "ERROR: required command not found: $1" >&2
        exit 1
    }
}

for cmd in cc pkg-config meson ninja python3 msgfmt makepkg install gzip; do
    need_cmd "$cmd"
done

required_pc=(gtk+-3.0 gio-unix-2.0 libxml-2.0 sm ice x11 shared-mime-info pango)
missing=0
for mod in "${required_pc[@]}"; do
    if ! pkg-config --exists "$mod"; then
        echo "ERROR: missing pkg-config dependency: $mod" >&2
        missing=1
    fi
done
if [[ $missing -ne 0 ]]; then
    echo "Install the Rox-Filer2 Arch build dependencies first." >&2
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
    echo "==> Building Rox-Filer2 $DISPLAY_VERSION for Arch Linux"
    rm -rf "$BUILD_DIR"
    meson setup --buildtype=release -Dsmb=auto -Dportable_devices=auto "$BUILD_DIR" "$PROJECT_ROOT"
    meson compile -C "$BUILD_DIR"
    [[ -x "$BUILD_DIR/ROX-Filer" ]] || { echo "ERROR: ROX-Filer binary was not produced." >&2; exit 1; }
    [[ -x "$BUILD_DIR/rox-find" ]] || { echo "ERROR: rox-find binary was not produced." >&2; exit 1; }
    [[ -x "$BUILD_DIR/rox-mount-helper" ]] || { echo "ERROR: rox-mount-helper binary was not produced." >&2; exit 1; }
}

stage_runtime() {
    echo "==> Preparing Arch package filesystem"
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

    find "$STAGE_DIR/usr/share/applications" -type f -name '*.desktop' -exec chmod 0644 {} + 2>/dev/null || true
    find "$STAGE_DIR/usr/share/pixmaps" -type f -exec chmod 0644 {} + 2>/dev/null || true
    find "$STAGE_DIR" -type d -exec chmod u-s,g-s {} +
}

make_arch_package() {
    cat > "$WORK_DIR/PKGBUILD" <<PKGBUILD
pkgname=rox-filer2
pkgver=$PKGVER
pkgrel=$PKGREL
pkgdesc='Rox-Filer2 lightweight GTK3 file manager and desktop'
arch=('$ARCH')
url='https://github.com/josejp2424/ROX-Filer2'
license=('GPL-3.0-or-later')
depends=(
  'glibc' 'glib2' 'gtk3' 'libxml2' 'libsm' 'libice' 'libx11'
  'shared-mime-info' 'hicolor-icon-theme' 'desktop-file-utils'
  'util-linux' 'file'
)
optdepends=(
  'gtk-layer-shell: Wayland desktop layer support'
  'polkit: pkexec authorization for drive mount/unmount as a regular user'
  'udisks2: Image Mounter support as a regular user'
  'smbclient: SMB share listing/diagnostics loaded automatically when available'
  'cifs-utils: kernel CIFS mounting support'
  'samba: rootless folder sharing through net usershare'
  'fuse3: FUSE mount/unmount helpers for portable devices'
  'jmtpfs: Android/MTP portable-device mounting'
  'gphoto2: PTP camera detection'
  'gphotofs: PTP camera FUSE mounting'
  'ifuse: iPhone/iPad FUSE mounting'
  'libimobiledevice: iPhone/iPad detection utilities'
  'usbmuxd: USB transport for Apple mobile devices'
)
provides=('rox-filer')
conflicts=('rox-filer')
options=('!debug')

package() {
    cp -a "\$startdir/rootfs/usr" "\$pkgdir/"
}
PKGBUILD

    echo "==> Creating native Arch package with makepkg"
    mkdir -p "$OUTPUT_DIR"
    (
        cd "$WORK_DIR"
        PKGDEST="$OUTPUT_DIR" makepkg --force --cleanbuild
    )

    local pkg
    pkg=$(find "$OUTPUT_DIR" -maxdepth 1 -type f \
        -name "${PACKAGE_NAME}-${PKGVER}-${PKGREL}-*.pkg.tar.*" ! -name '*.sig' | sort | tail -n1)
    [[ -n "$pkg" ]] || { echo "ERROR: makepkg finished but the package was not found." >&2; exit 1; }

    echo
    echo "============================================================"
    echo "Rox-Filer2 Arch package created successfully"
    echo "Version : $DISPLAY_VERSION"
    echo "Package : $pkg"
    echo "Install : sudo pacman -U '$pkg'"
    echo "============================================================"
}

prepare_messages
build_rox
stage_runtime
make_arch_package
