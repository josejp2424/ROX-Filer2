#!/bin/sh
set -eu

PROJECT_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
APP_DIR="$PROJECT_ROOT/ROX-Filer"
PACKAGE_BASE="$PROJECT_ROOT/package-base"
OUTPUT_DIR="$PROJECT_ROOT/output"
PACKAGE_NAME="rox-filer2"

# Keep package/version naming in sync with Rox-Filer2 itself.
# AppInfo.xml is the canonical source. Current releases use Debian-style
# versions such as 2.12.2-1; legacy 2.12-rNN trees remain supported.
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

usage() {
    cat <<USAGE
Usage: $0 [--skip-compile] [--clean]

  --skip-compile  Package the existing Rox-Filer2-compatible ROX-Filer/ROX-Filer binary.
  --clean         Remove generated package output and ROX-Filer/build.

Without options, the script compiles Rox-Filer2 and then creates:
  - a Debian .deb package;
  - the complete Debian package directory;
  - a portable root filesystem directory and tar.gz archive.
USAGE
}

case "${1:-}" in
    --skip-compile) SKIP_COMPILE=1 ;;
    --clean)
        rm -rf "$OUTPUT_DIR" "$APP_DIR/build"
        rm -f "$PROJECT_ROOT/rox-find/rox-find"
        echo "Cleaned generated output."
        exit 0
        ;;
    -h|--help) usage; exit 0 ;;
    "") ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
esac

if [ "$SKIP_COMPILE" -eq 0 ]; then
    "$APP_DIR/AppRun" --compile-only
fi

if [ ! -x "$APP_DIR/ROX-Filer" ]; then
    echo "ERROR: compiled binary not found: $APP_DIR/ROX-Filer" >&2
    exit 1
fi

if [ ! -x "$PROJECT_ROOT/rox-find/rox-find" ]; then
    echo "ERROR: ROX File Search binary not found: $PROJECT_ROOT/rox-find/rox-find" >&2
    exit 1
fi

if [ ! -d "$PACKAGE_BASE/usr/local/apps/Rox-Filer/ROX" ]; then
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
cp -a "$PACKAGE_BASE/." "$PACKAGE_DIR/"
find "$PACKAGE_DIR" -type d -exec chmod u-s,g-s {} +

# Build the installed ROX application from the freshly compiled source tree.
# The user's supplied ROX directory is preserved exactly.
SUPPLIED_ROX_TMP="$OUTPUT_DIR/.supplied-ROX.$$"
rm -rf "$SUPPLIED_ROX_TMP"
cp -a "$PACKAGE_BASE/usr/local/apps/Rox-Filer/ROX" "$SUPPLIED_ROX_TMP"

rm -rf "$PACKAGE_DIR/usr/local/apps/Rox-Filer" "$PACKAGE_DIR/usr/local/apps/ROX-Filer"
mkdir -p "$PACKAGE_DIR/usr/local/apps/Rox-Filer"
cp -a "$APP_DIR/." "$PACKAGE_DIR/usr/local/apps/Rox-Filer/"
rm -rf "$PACKAGE_DIR/usr/local/apps/Rox-Filer/ROX"
cp -a "$SUPPLIED_ROX_TMP" "$PACKAGE_DIR/usr/local/apps/Rox-Filer/ROX"
rm -rf "$SUPPLIED_ROX_TMP"
ln -s Rox-Filer "$PACKAGE_DIR/usr/local/apps/ROX-Filer"

# Runtime packages must not contain compiler output or C source files.
rm -rf \
    "$PACKAGE_DIR/usr/local/apps/Rox-Filer/build" \
    "$PACKAGE_DIR/usr/local/apps/Rox-Filer/src"

# Install Rox-Filer2 application icons supplied with the source.  Desktop
# files use Icon=rox-filer2 so GTK/icon themes can choose the correct size.
ROX_ICON_ROOT="$PROJECT_ROOT/data/icons/hicolor"
for size in 16 22 24 32 48 64 96 128 192 256; do
    install -Dm0644 \
        "$ROX_ICON_ROOT/${size}x${size}/apps/rox-filer2.png" \
        "$PACKAGE_DIR/usr/share/icons/hicolor/${size}x${size}/apps/rox-filer2.png"
done
install -Dm0644 \
    "$ROX_ICON_ROOT/scalable/apps/rox-filer2.svg" \
    "$PACKAGE_DIR/usr/share/icons/hicolor/scalable/apps/rox-filer2.svg"
# Keep a pixmaps fallback for lightweight Puppy setups that do not consult
# the icon theme cache.
install -Dm0644 \
    "$ROX_ICON_ROOT/scalable/apps/rox-filer2.svg" \
    "$PACKAGE_DIR/usr/share/pixmaps/rox-filer2.svg"

# Install the native ROX File Search companion application.
install -Dm0755 "$PROJECT_ROOT/rox-find/rox-find" \
    "$PACKAGE_DIR/usr/bin/rox-find"
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
    cp -a "$PROJECT_ROOT/rox-find/locale/." "$PACKAGE_DIR/usr/share/locale/"
fi

# Restore package-base integration files and normalize permissions.
mkdir -p "$PACKAGE_DIR/DEBIAN"
chmod u-s,g-s "$PACKAGE_DIR/DEBIAN"
chmod 0755 "$PACKAGE_DIR/DEBIAN"
cp -a "$PROJECT_ROOT/DEBIAN/postinst" "$PACKAGE_DIR/DEBIAN/postinst"
cp -a "$PROJECT_ROOT/DEBIAN/postrm" "$PACKAGE_DIR/DEBIAN/postrm"
chmod 0755 "$PACKAGE_DIR/DEBIAN/postinst" "$PACKAGE_DIR/DEBIAN/postrm"
find "$PACKAGE_DIR/usr/share/applications" -type f -name '*.desktop' -exec chmod 0644 {} + 2>/dev/null || true
find "$PACKAGE_DIR/usr/share/pixmaps" -type f -exec chmod 0644 {} + 2>/dev/null || true

INSTALLED_SIZE=$(du -sk "$PACKAGE_DIR/usr" | awk '{print $1}')
sed \
    -e "s/@VERSION@/$DEB_VERSION/g" \
    -e "s/@ARCH@/$ARCH/g" \
    -e "s/@INSTALLED_SIZE@/$INSTALLED_SIZE/g" \
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
cp -a "$PACKAGE_DIR/usr" "$PORTABLE_DIR/usr"
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

# build/ is disposable. src/ remains only in the development source tree;
# both directories were removed from the generated runtime package trees.
rm -rf "$APP_DIR/build"

echo "Complete Debian package directory: $PACKAGE_DIR"
echo "Portable package directory: $PORTABLE_DIR"
echo "Portable archive: $PORTABLE_TAR"
