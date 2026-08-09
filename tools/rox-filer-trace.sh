#!/bin/sh
# Inicia una copia independiente de ROX-Filer con trazas de MIME y terminal.
set -eu

BIN=${ROX_FILER_BINARY:-$(command -v ROX-Filer 2>/dev/null || true)}
if [ -z "$BIN" ] || [ ! -x "$BIN" ]; then
    echo "No se encontró un ejecutable ROX-Filer. Use ROX_FILER_BINARY=/ruta/ROX-Filer" >&2
    exit 1
fi

STAMP=$(date +%Y%m%d-%H%M%S)
LOG=${ROX_TRACE_LOG:-"$PWD/rox-filer-trace-$STAMP.log"}
START=${1:-$PWD}

: > "$LOG"
printf '%s\n' "ROX-Filer: $BIN" "Ruta inicial: $START" "Log: $LOG"
printf '%s\n' "Reproduzca en esta ventana el fallo de Abrir con o Ejecutar en terminal y luego cierre la ventana."

ROX_DEBUG_LOG="$LOG" "$BIN" -n "$START"

printf '\nLog generado: %s\n' "$LOG"
