#!/bin/sh
set -u

VERSION="1.2-r75"
STOP_AFTER=0
BINARY=""
LEVEL="trace"

usage() {
    cat <<USAGE
Uso: $0 [--binary RUTA] [--stop-after] [--log-level NIVEL]

Prueba ROX Desktop Wayland dentro de Labwc. Activa el registro interno r75 y
crea un log combinado en el directorio actual.
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --binary) BINARY=${2:-}; shift 2 ;;
        --stop-after) STOP_AFTER=1; shift ;;
        --log-level) LEVEL=${2:-trace}; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Opción desconocida: $1" >&2; usage >&2; exit 2 ;;
    esac
done

[ -n "$BINARY" ] || BINARY=$(command -v rox-wayland 2>/dev/null || true)
[ -n "$BINARY" ] || [ ! -x /usr/local/apps/ROX-Filer/rox-wayland ] || BINARY=/usr/local/apps/ROX-Filer/rox-wayland
if [ -z "$BINARY" ] || [ ! -x "$BINARY" ]; then
    echo "No se encontró rox-wayland. Use --binary RUTA." >&2
    exit 1
fi

STAMP=$(date +%Y%m%d-%H%M%S)
BASE="$(pwd)/rox-wayland-labwc-${STAMP}"
LOG="${BASE}.log"
INTERNAL="${BASE}-internal.log"
SECOND="${BASE}-second.log"
RUNTIME=${XDG_RUNTIME_DIR:-/tmp}
SOCKET="$RUNTIME/rox-filer-desktop-wayland-$(id -u).sock"
exec >"$LOG" 2>&1

errors=0; warns=0
ok() { echo "[OK] $*"; }
warn() { echo "[AVISO] $*"; warns=$((warns + 1)); }
fail() { echo "[ERROR] $*"; errors=$((errors + 1)); }

printf '%s\n' "============================================================" "ROX Wayland Labwc test $VERSION" "============================================================"
echo "Fecha: $(date)"; echo "Binario: $BINARY"; echo "Log interno: $INTERNAL"
echo "XDG_SESSION_TYPE=${XDG_SESSION_TYPE:-<vacío>}"
echo "XDG_CURRENT_DESKTOP=${XDG_CURRENT_DESKTOP:-<vacío>}"
echo "WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-<vacío>}"; echo "DISPLAY=${DISPLAY:-<vacío>}"

[ "${XDG_SESSION_TYPE:-}" = wayland ] && ok "Sesión Wayland" || fail "XDG_SESSION_TYPE no es wayland"
[ -n "${WAYLAND_DISPLAY:-}" ] && ok "WAYLAND_DISPLAY definido" || fail "WAYLAND_DISPLAY ausente"
if pgrep -x labwc >/dev/null 2>&1 || printf '%s' "${XDG_CURRENT_DESKTOP:-}" | grep -qi labwc; then ok "Labwc detectado"; else warn "Labwc no detectado"; fi
if command -v wayland-info >/dev/null 2>&1; then
    wayland-info 2>/dev/null | grep -q zwlr_layer_shell_v1 && ok "zwlr_layer_shell_v1 disponible" || fail "Falta zwlr_layer_shell_v1"
else warn "wayland-info no instalado"; fi
if command -v ldconfig >/dev/null 2>&1 && ldconfig -p 2>/dev/null | grep -q libgtk-layer-shell.so.0; then ok "GTK Layer Shell instalada"; else warn "No se confirmó libgtk-layer-shell.so.0"; fi
"$BINARY" --version | head -1

if [ -S "$SOCKET" ]; then
    fail "Ya existe un escritorio ROX Wayland. Ciérrelo antes para probar el arranque con log."
    pid=""
else
    "$BINARY" --desktop --debug --log-level="$LEVEL" --log-file="$INTERNAL" &
    pid=$!
    sleep 4
    if kill -0 "$pid" 2>/dev/null; then ok "Escritorio activo PID $pid"; else fail "El escritorio terminó durante el inicio"; wait "$pid" 2>/dev/null || true; fi
    [ -S "$SOCKET" ] && ok "Socket de instancia creado" || fail "No se creó $SOCKET"
fi

if [ -n "${pid:-}" ]; then
    begin=$(date +%s)
    "$BINARY" --desktop --debug --log-file="$SECOND"
    rc=$?; elapsed=$(( $(date +%s) - begin ))
    [ "$rc" -eq 0 ] && ok "Segunda ejecución terminó rc=0 en ${elapsed}s" || fail "Segunda ejecución rc=$rc"
fi

for file in "$INTERNAL" "$SECOND"; do
    if [ -f "$file" ]; then
        echo; echo "================ $file ================"; cat "$file"
    fi
done

if [ "$STOP_AFTER" -eq 1 ] && [ -n "${pid:-}" ] && kill -0 "$pid" 2>/dev/null; then
    kill "$pid"; wait "$pid" 2>/dev/null || true; ok "Instancia detenida"
elif [ -n "${pid:-}" ]; then echo "[INFO] El escritorio queda activo PID $pid"; fi

echo; echo "Resumen: avisos=$warns errores=$errors"; echo "Log combinado: $LOG"
[ "$errors" -eq 0 ]
