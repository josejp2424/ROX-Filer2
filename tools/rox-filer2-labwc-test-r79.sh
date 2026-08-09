#!/bin/sh
set -u

VERSION="1.4-r79"
STOP_AFTER=0
BINARY=""
LEVEL="debug"

usage() {
    cat <<USAGE
Uso: $0 [--binary RUTA] [--stop-after] [--log-level NIVEL]

Prueba Rox-Filer2 en Labwc/Wayland, incluyendo:
  - desktop Layer Shell
  - segunda ejecución/refresh
  - apertura normal con rox-wayland
  - apertura mediante /usr/local/bin/roxfiler (ruta del menú)
  - registro SOAP/RPC local de Wayland

Genera un log combinado y un tar.bz2 para compartir.
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --binary) BINARY=${2:-}; shift 2 ;;
        --stop-after) STOP_AFTER=1; shift ;;
        --log-level) LEVEL=${2:-debug}; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Opción desconocida: $1" >&2; usage >&2; exit 2 ;;
    esac
done

[ -n "$BINARY" ] || BINARY=$(command -v rox-wayland 2>/dev/null || true)
[ -n "$BINARY" ] || [ ! -x /usr/local/apps/Rox-Filer/ROX-Filer ] || BINARY=/usr/local/apps/Rox-Filer/ROX-Filer
if [ -z "$BINARY" ] || [ ! -x "$BINARY" ]; then
    echo "No se encontró rox-wayland. Use --binary RUTA." >&2
    exit 1
fi

STAMP=$(date +%Y%m%d-%H%M%S)
OUTDIR="$(pwd)/rox-filer2-labwc-test-r79-${STAMP}"
mkdir -p "$OUTDIR"
LOG="$OUTDIR/combined.log"
DESKTOP_LOG="$OUTDIR/desktop.log"
SECOND_LOG="$OUTDIR/second.log"
NORMAL_LOG="$OUTDIR/normal.log"
MENU_LOG="$OUTDIR/menu.log"
HELPLOG="$OUTDIR/help.txt"
SYSTEMLOG="$OUTDIR/system.txt"
RUNTIME=${XDG_RUNTIME_DIR:-/tmp}
SOCKET="$RUNTIME/rox-filer-desktop-wayland-$(id -u).sock"
SCRIPT_COPY="$OUTDIR/$(basename "$0")"
cp -f "$0" "$SCRIPT_COPY" 2>/dev/null || true

exec >"$LOG" 2>&1

errors=0; warns=0
ok() { echo "[OK] $*"; }
warn() { echo "[AVISO] $*"; warns=$((warns + 1)); }
fail() { echo "[ERROR] $*"; errors=$((errors + 1)); }

printf '%s\n' "============================================================" "Rox-Filer2 Wayland Labwc test $VERSION" "============================================================"
echo "Fecha: $(date)"
echo "Binario: $BINARY"
echo "XDG_SESSION_TYPE=${XDG_SESSION_TYPE:-<vacío>}"
echo "XDG_CURRENT_DESKTOP=${XDG_CURRENT_DESKTOP:-<vacío>}"
echo "WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-<vacío>}"
echo "DISPLAY=${DISPLAY:-<vacío>}"
echo "XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-<vacío>}"

{
    echo "uname: $(uname -a 2>/dev/null || true)"
    echo "id: $(id 2>/dev/null || true)"
    echo "--- processes ---"
    ps -ef 2>/dev/null | grep -E 'Rox-Filer|ROX-Filer|rox-wayland|labwc' | grep -v grep || true
    echo "--- launchers ---"
    for x in rox rox-x11 rox-wayland /usr/local/bin/roxfiler /usr/local/apps/Rox-Filer/ROX-Filer; do
        if command -v "$x" >/dev/null 2>&1; then
            command -v "$x"
        elif [ -e "$x" ]; then
            ls -l "$x"
        fi
    done
} >"$SYSTEMLOG" 2>&1

[ "${XDG_SESSION_TYPE:-}" = wayland ] && ok "Sesión Wayland" || fail "XDG_SESSION_TYPE no es wayland"
[ -n "${WAYLAND_DISPLAY:-}" ] && ok "WAYLAND_DISPLAY definido" || fail "WAYLAND_DISPLAY ausente"
if pgrep -x labwc >/dev/null 2>&1 || printf '%s' "${XDG_CURRENT_DESKTOP:-}" | grep -qi labwc; then
    ok "Labwc detectado"
else
    warn "Labwc no detectado"
fi
if command -v wayland-info >/dev/null 2>&1; then
    wayland-info 2>/dev/null | grep -q zwlr_layer_shell_v1 && ok "zwlr_layer_shell_v1 disponible" || fail "Falta zwlr_layer_shell_v1"
else
    warn "wayland-info no instalado"
fi
if command -v ldconfig >/dev/null 2>&1 && ldconfig -p 2>/dev/null | grep -q libgtk-layer-shell.so.0; then
    ok "GTK Layer Shell instalada"
else
    warn "No se confirmó libgtk-layer-shell.so.0"
fi

"$BINARY" --version | head -1
if command -v rox >/dev/null 2>&1; then
    rox --help >"$HELPLOG" 2>&1
else
    "$BINARY" --help >"$HELPLOG" 2>&1
fi
if grep -q 'Usage: rox ' "$HELPLOG" && grep -q 'Rox-Filer2' "$HELPLOG" && grep -q 'rox-x11' "$HELPLOG" && grep -q 'rox-wayland' "$HELPLOG"; then
    ok "rox --help describe Rox-Filer2, X11 y Wayland"
else
    fail "La ayuda no refleja completamente Rox-Filer2/X11/Wayland"
fi

started_desktop=0
desktop_pid=""
if [ -S "$SOCKET" ]; then
    warn "Ya existe un desktop Wayland; se conservará y se probará su refresh"
    "$BINARY" --desktop --debug --log-file="$SECOND_LOG" >/dev/null 2>&1 || fail "No se pudo refrescar el desktop existente"
else
    "$BINARY" --desktop --debug --log-level="$LEVEL" --log-file="$DESKTOP_LOG" &
    desktop_pid=$!
    started_desktop=1
    sleep 4
    if kill -0 "$desktop_pid" 2>/dev/null; then ok "Desktop activo PID $desktop_pid"; else fail "El desktop terminó durante el inicio"; wait "$desktop_pid" 2>/dev/null || true; fi
    [ -S "$SOCKET" ] && ok "Socket de instancia creado" || fail "No se creó $SOCKET"
    "$BINARY" --desktop --debug --log-file="$SECOND_LOG" >/dev/null 2>&1
    [ "$?" -eq 0 ] && ok "Segunda ejecución refrescó el desktop" || fail "La segunda ejecución --desktop falló"
fi

# Normal launch: exact path which failed in r78.
"$BINARY" --debug --log-file="$NORMAL_LOG" "$HOME" >/dev/null 2>&1 &
normal_pid=$!
sleep 4
if grep -q "RPC registry ready; running request locally" "$NORMAL_LOG" 2>/dev/null && \
   grep -q "SOAP/RPC method registry initialized" "$NORMAL_LOG" 2>/dev/null && \
   grep -q "invoking RPC method=Run\|invoking RPC method=OpenDir" "$NORMAL_LOG" 2>/dev/null && \
   grep -q "entering GTK main loop; windows=" "$NORMAL_LOG" 2>/dev/null && \
   ! grep -q "g_hash_table_lookup: assertion 'hash_table != NULL' failed" "$NORMAL_LOG" 2>/dev/null; then
    ok "rox-wayland abrió una ventana normal mediante RPC local"
else
    fail "La apertura normal sigue fallando; revisar normal.log"
fi
if kill -0 "$normal_pid" 2>/dev/null; then
    kill "$normal_pid" 2>/dev/null || true
    wait "$normal_pid" 2>/dev/null || true
fi

# Menu/automatic selector.
if [ -x /usr/local/bin/roxfiler ]; then
    /usr/local/bin/roxfiler --debug --log-file="$MENU_LOG" "$HOME" >/dev/null 2>&1 &
    menu_pid=$!
    sleep 4
    if grep -q "GDK_BACKEND=wayland forced_backend=wayland" "$MENU_LOG" 2>/dev/null && \
       grep -q "RPC registry ready; running request locally" "$MENU_LOG" 2>/dev/null && \
       grep -q "entering GTK main loop; windows=" "$MENU_LOG" 2>/dev/null && \
       ! grep -q "g_hash_table_lookup: assertion 'hash_table != NULL' failed" "$MENU_LOG" 2>/dev/null; then
        ok "El selector del menú eligió Wayland y abrió Rox-Filer2"
    else
        fail "El lanzamiento del menú sigue fallando; revisar menu.log"
    fi
    if kill -0 "$menu_pid" 2>/dev/null; then
        kill "$menu_pid" 2>/dev/null || true
        wait "$menu_pid" 2>/dev/null || true
    fi
else
    warn "/usr/local/bin/roxfiler no está instalado"
fi

if [ -f /usr/share/applications/rox-filer.desktop ]; then
    echo "--- /usr/share/applications/rox-filer.desktop ---"
    grep -E '^(Name|Exec|Icon)=' /usr/share/applications/rox-filer.desktop || true
    grep -q '^Name=Rox-Filer2$' /usr/share/applications/rox-filer.desktop && ok "El menú muestra Rox-Filer2" || warn "El .desktop instalado todavía usa otro Name"
    grep -q '^Exec=/usr/local/bin/roxfiler' /usr/share/applications/rox-filer.desktop && ok "El menú usa el selector automático" || fail "El .desktop no usa /usr/local/bin/roxfiler"
fi

for file in "$SYSTEMLOG" "$HELPLOG" "$DESKTOP_LOG" "$SECOND_LOG" "$NORMAL_LOG" "$MENU_LOG"; do
    if [ -f "$file" ]; then
        echo; echo "================ $file ================"; cat "$file"
    fi
done

if [ "$STOP_AFTER" -eq 1 ] && [ "$started_desktop" -eq 1 ] && [ -n "$desktop_pid" ] && kill -0 "$desktop_pid" 2>/dev/null; then
    kill "$desktop_pid" 2>/dev/null || true
    wait "$desktop_pid" 2>/dev/null || true
    ok "Desktop iniciado por la prueba detenido"
elif [ "$started_desktop" -eq 1 ] && [ -n "$desktop_pid" ]; then
    echo "[INFO] El desktop queda activo PID $desktop_pid"
fi

echo; echo "Resumen: avisos=$warns errores=$errors"
echo "Directorio: $OUTDIR"

# Close combined log before archiving by using a child shell? The current file
# descriptor remains open but tar can still read the flushed content.
ARCHIVE="${OUTDIR}.tar.bz2"
tar -cjf "$ARCHIVE" -C "$(dirname "$OUTDIR")" "$(basename "$OUTDIR")" 2>/dev/null || warn "No se pudo crear el tar.bz2"
echo "Paquete para compartir: $ARCHIVE"
[ "$errors" -eq 0 ]
