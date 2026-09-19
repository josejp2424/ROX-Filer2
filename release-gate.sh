#!/bin/sh
# Rox-Filer2 release gate.
#
# 2.12.2-82: hasta -81 esta puerta no podia terminar en PASS.  Invocaba
# lintian bajo "set -eu" sobre un paquete que instalaba enlaces en
# /usr/local y por tanto salia con codigo 2.  Ademas no comprobaba tres
# cosas que si fallaron en produccion:
#   - corrupcion de memoria en tiempo de ejecucion (habia un use-after-free
#     en cada apertura de ventana, invisible para msgfmt y lintian);
#   - que messages.pot siguiera sincronizado con las fuentes (po/tips.py
#     era Python 2 y la extraccion se abortaba en silencio);
#   - que el manual existiera realmente en ROX-Filer/Help.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

for tool in meson ninja msgfmt xgettext python3 lintian readelf dpkg-deb; do
  command -v "$tool" >/dev/null 2>&1 || { echo "ERROR: $tool missing" >&2; exit 1; }
done

echo "== 1/9 higiene del source =="
[ -s "$ROOT/ROX-Filer.svg" ] || { echo "ERROR: falta ROX-Filer.svg" >&2; exit 1; }
[ -s "$ROOT/ROX-Filer-root.svg" ] || { echo "ERROR: falta ROX-Filer-root.svg" >&2; exit 1; }
[ -s "$ROOT/ROX-Filer/ROX-Filer-root.svg" ] || { echo "ERROR: falta fallback AppDir ROX-Filer-root.svg" >&2; exit 1; }
[ -s "$ROOT/ROX-Filer/src/samba_share.c" ] || { echo "ERROR: falta samba_share.c" >&2; exit 1; }
[ -s "$ROOT/ROX-Filer/src/samba_share.h" ] || { echo "ERROR: falta samba_share.h" >&2; exit 1; }
for generated in \
  "$ROOT/ROX-Filer/ROX-Filer" \
  "$ROOT/ROX-Filer/ROX-Filer.dbg" \
  "$ROOT/rox-find/rox-find"; do
  if [ -e "$generated" ]; then
    echo "ERROR: el source contiene un binario generado: $generated" >&2
    echo "Ejecute ./build-package.sh --clean antes de crear el tar de source." >&2
    exit 1
  fi
done

echo "== 2/9 catalogos de traduccion =="
PO_FILES=""
for po in ar ca de es fr hu it ja pt ru zh; do
  po_file="$ROOT/ROX-Filer/src/po/$po.po"
  msgfmt -c "$po_file" -o /dev/null
  PO_FILES="$PO_FILES $po_file"
done
# shellcheck disable=SC2086
python3 "$ROOT/check-po-formats.py" $PO_FILES
# 2.12.2-83: msgfmt -c valida sintaxis, pero no falla por traducciones
# vacias ni fuzzy. La puerta de release ahora exige catalogos realmente
# completos en los once idiomas mantenidos.
# shellcheck disable=SC2086
python3 "$ROOT/tools/check-po-completeness.py" $PO_FILES

echo "== 3/9 messages.pot sincronizado con las fuentes =="
(
  cd "$ROOT/ROX-Filer/src"
  python3 po/tips.py >/dev/null
  python3 po/options_strings.py ../Options.xml > Options.xml.h
  xgettext --from-code=UTF-8 --keyword=_ --keyword=N_ --keyword=ngettext:1,2 \
      --output=/tmp/rox-gate.pot *.c tips Options.xml.h 2>/dev/null
  missing=$(python3 - <<'PY'
import re
# 2.12.2-85: comparar por CONTENIDO, no por el literal del archivo.  Un
# msguniq o un reajuste de linea cambia como se parte el mensaje en varios
# literales sin cambiar el mensaje en si, y la comparacion textual producia
# decenas de falsos positivos que habrian bloqueado la puerta sin motivo.
SEG = re.compile(r'"((?:[^"\\]|\\.)*)"')
def ids(path):
    out = set()
    for block in open(path, encoding='utf-8').read().split('\n\n'):
        m = re.search(r'^msgid((?:\s*"(?:[^"\\]|\\.)*")+)', block, re.M)
        if m:
            key = ''.join(SEG.findall(m.group(1)))
            if key:
                out.add(key)
    return out
gap = ids('/tmp/rox-gate.pot') - ids('messages.pot')
print(len(gap))
for g in sorted(gap)[:10]:
    print('  falta en messages.pot:', g[:70])
PY
)
  count=$(echo "$missing" | head -n1)
  if [ "$count" != "0" ]; then
    echo "ERROR: messages.pot desactualizado ($count cadenas sin extraer)." >&2
    echo "$missing" | tail -n +2 >&2
    echo "Ejecute ROX-Filer/src/po/update-po." >&2
    exit 1
  fi
)

echo "== 4/9 manual en los 11 idiomas =="
python3 "$ROOT/tools/make-manual.py" >/dev/null
LOGO="$ROOT/ROX-Filer/Help/rox-filer2.svg"
[ -s "$LOGO" ] || { echo "ERROR: falta el logo del manual: $LOGO" >&2; exit 1; }
for lang in "" -en -es -pt -fr -it -ca -de -hu -ru -ja -zh -ar; do
  case "$lang" in
    "") f="$ROOT/ROX-Filer/Help/Manual.html" ;;
    *)  f="$ROOT/ROX-Filer/Help/Manual$lang.html" ;;
  esac
  [ -s "$f" ] || { echo "ERROR: falta $f" >&2; exit 1; }
  grep -q 'src="rox-filer2.svg"' "$f" || {
    echo "ERROR: el manual no contiene el logo Rox-Filer2: $f" >&2
    exit 1
  }
done

echo "== 5/9 compilacion release =="
rm -rf "$ROOT/build-gate"
meson setup --buildtype=release -Dsmb=enabled -Dportable_devices=enabled "$ROOT/build-gate" "$ROOT" >/dev/null
meson compile -C "$ROOT/build-gate"
# 2.12.2-93: libsmbclient is a runtime plugin, never an ELF DT_NEEDED entry.
if readelf -d "$ROOT/build-gate/ROX-Filer" | grep -qi 'libsmbclient'; then
  echo "ERROR: ROX-Filer has a hard ELF dependency on libsmbclient" >&2
  exit 1
fi
if readelf -d "$ROOT/build-gate/ROX-Filer" | grep -Eqi 'libmtp|libgphoto|libimobiledevice'; then
  echo "ERROR: ROX-Filer has a hard ELF dependency on an optional portable-device library" >&2
  exit 1
fi

echo "== 6/9 compilacion con sanitizers y arranque headless =="
# ASan/UBSan detectan corrupcion de memoria que ninguna otra comprobacion ve.
# Se omite solo si falta xvfb-run, pero entonces se avisa.
if command -v xvfb-run >/dev/null 2>&1; then
  rm -rf "$ROOT/build-asan"
  meson setup --buildtype=debug -Dsmb=enabled -Dportable_devices=enabled \
      -Db_sanitize=address,undefined -Db_lundef=false \
      "$ROOT/build-asan" "$ROOT" >/dev/null
  meson compile -C "$ROOT/build-asan" >/dev/null
  asan_log=$(mktemp -d)
  rc=0
  for mode in --classic --modern --desktop; do
    HOME=$(mktemp -d) \
    ASAN_OPTIONS="detect_leaks=0:halt_on_error=0:log_path=$asan_log/asan" \
    UBSAN_OPTIONS="print_stacktrace=1" \
    G_SLICE=always-malloc \
    timeout 40 xvfb-run -a --server-args="-screen 0 1280x1024x24" \
        "$ROOT/build-asan/ROX-Filer" "$mode" /tmp >"$asan_log/run$mode.log" 2>&1 || true
    if ls "$asan_log"/asan.* >/dev/null 2>&1; then
      echo "ERROR: AddressSanitizer/UBSan reporto errores en $mode:" >&2
      head -n 20 "$asan_log"/asan.* >&2
      rc=1
      break
    fi
    if grep -qE "runtime error|Gtk-CRITICAL|GLib-CRITICAL|assertion .* failed" \
        "$asan_log/run$mode.log"; then
      echo "ERROR: diagnosticos en tiempo de ejecucion en $mode:" >&2
      grep -E "runtime error|CRITICAL|assertion" "$asan_log/run$mode.log" | head >&2
      rc=1
      break
    fi
  done
  rm -rf "$asan_log"
  [ "$rc" -eq 0 ] || exit 1
else
  echo "AVISO: xvfb-run no esta instalado; se omite la prueba con sanitizers." >&2
fi

echo "== 7/9 ruta historica AppRun/autoconf =="
"$ROOT/ROX-Filer/AppRun" --compile-only
# The legacy path builds in-tree.  It is a validation target, not source
# payload; remove its generated ELF files immediately after the check.
rm -f "$ROOT/ROX-Filer/ROX-Filer" "$ROOT/ROX-Filer/ROX-Filer.dbg"

echo "== 8/9 construccion del paquete =="
"$ROOT/build-package.sh" --no-arch-package
DEB=$(find "$ROOT/output" -maxdepth 1 -name 'rox-filer2_*.deb' -print | head -n1)
[ -n "$DEB" ] || { echo "ERROR: .deb missing" >&2; exit 1; }
dpkg-deb --info "$DEB" >/dev/null
dpkg-deb --contents "$DEB" >/dev/null
dpkg-deb --contents "$DEB" | grep -q './usr/share/pixmaps/ROX-Filer.svg$' || {
  echo "ERROR: Debian package is missing ROX-Filer.svg" >&2
  exit 1
}
dpkg-deb --contents "$DEB" | grep -q './usr/share/pixmaps/ROX-Filer-root.svg$' || {
  echo "ERROR: Debian package is missing ROX-Filer-root.svg" >&2
  exit 1
}
DEB_DEPENDS=$(dpkg-deb -f "$DEB" Depends)
DEB_RECOMMENDS=$(dpkg-deb -f "$DEB" Recommends)
if echo "$DEB_DEPENDS" | grep -qi 'libsmbclient'; then
  echo "ERROR: libsmbclient must not be a hard Debian Depends" >&2
  exit 1
fi
echo "$DEB_RECOMMENDS" | grep -q 'libsmbclient0' || {
  echo "ERROR: Debian package must recommend libsmbclient0" >&2
  exit 1
}
for samba_pkg in samba-common-bin samba; do
  echo "$DEB_RECOMMENDS" | grep -q "${samba_pkg}" || {
    echo "ERROR: Debian package must recommend ${samba_pkg} for usershare integration" >&2
    exit 1
  }
done
for portable_pkg in fuse jmtpfs libmtp gphoto2 gphotofs libgphoto2 ifuse usbmuxd libimobiledevice-utils libimobiledevice; do
  echo "$DEB_RECOMMENDS" | grep -q "${portable_pkg}" || {
    echo "ERROR: Debian package must recommend ${portable_pkg}" >&2
    exit 1
  }
done

echo "== 9/9 lintian =="
lintian "$DEB"

if command -v xvfb-run >/dev/null 2>&1; then
  xvfb-run -a "$ROOT/build-gate/ROX-Filer" --version >/dev/null
fi
echo "Release gate: PASS"
