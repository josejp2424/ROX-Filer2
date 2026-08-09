#!/bin/bash
# Diagnóstico integral para ROX-Filer GTK3 (r72 y posteriores)
# Genera un log único para compartir al depurar compilación, escritorio y MIME.

set -u
set -o pipefail

SCRIPT_VERSION="1.5"
TARGET=""
BIN_OVERRIDE=""
DO_BUILD=1
DO_GUI=1
KEEP_TEMP=0
LOG_DIR="${ROX_TEST_LOG_DIR:-$PWD}"
EXTRA_SCRIPTS=()
GEANY_TEST_FILE=""
RENAME_TEST_FILE=""

usage() {
    cat <<'USAGE'
Uso:
  ./rox-filer-diagnostico.sh [FUENTE_O_TAR] [opciones]

FUENTE_O_TAR puede ser:
  - la carpeta raíz del paquete (contiene ROX-Filer/src/main.c)
  - la AppDir ROX-Filer (contiene src/main.c y AppRun)
  - el archivo .tar.gz del fuente
  - un ejecutable ROX-Filer

Opciones:
  --binary RUTA     probar este ejecutable de forma explícita
  --no-build        no intentar configurar ni compilar el fuente
  --no-gui          no ejecutar pruebas gráficas ni el sondeo MIME interno
  --keep-temp       conservar el directorio temporal de diagnóstico
  --log-dir DIR     guardar el log en DIR
  --test-script FILE ejecutar y registrar un script real que esté fallando
                     (se puede repetir; el script será ejecutado)
  --geany-file FILE probar un archivo real con el .desktop de Geany en Xvfb
  --rename-file FILE comprobar que el diálogo Renombrar se crea y se presenta
  -h, --help        mostrar esta ayuda

Ejemplo recomendado:
  chmod +x rox-filer-diagnostico.sh
  ./rox-filer-diagnostico.sh rox-filer-2.12-gtk3-r72-source.tar.gz

Para registrar casos concretos:
  ./rox-filer-diagnostico.sh FUENTE \
      --test-script "/ruta/al/script que falla.sh" \
      --geany-file "/ruta/al/archivo que no abre.txt" \
      --rename-file "/ruta/al/archivo que no permite renombrar"

El script no modifica tu fuente original. Las pruebas de escritorio se hacen
con Xvfb cuando está disponible. No reemplaza el escritorio real de la sesión.
USAGE
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --binary)
            [ "$#" -ge 2 ] || { echo "Falta la ruta después de --binary" >&2; exit 2; }
            BIN_OVERRIDE="$2"; shift 2 ;;
        --no-build) DO_BUILD=0; shift ;;
        --no-gui) DO_GUI=0; shift ;;
        --keep-temp) KEEP_TEMP=1; shift ;;
        --log-dir)
            [ "$#" -ge 2 ] || { echo "Falta el directorio después de --log-dir" >&2; exit 2; }
            LOG_DIR="$2"; shift 2 ;;
        --test-script)
            [ "$#" -ge 2 ] || { echo "Falta el archivo después de --test-script" >&2; exit 2; }
            EXTRA_SCRIPTS+=("$2"); shift 2 ;;
        --geany-file)
            [ "$#" -ge 2 ] || { echo "Falta el archivo después de --geany-file" >&2; exit 2; }
            GEANY_TEST_FILE="$2"; shift 2 ;;
        --rename-file)
            [ "$#" -ge 2 ] || { echo "Falta el archivo después de --rename-file" >&2; exit 2; }
            RENAME_TEST_FILE="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        --) shift; break ;;
        -*) echo "Opción desconocida: $1" >&2; usage >&2; exit 2 ;;
        *)
            if [ -z "$TARGET" ]; then TARGET="$1"; else echo "Argumento extra: $1" >&2; exit 2; fi
            shift ;;
    esac
done

mkdir -p "$LOG_DIR" || exit 1
STAMP="$(date +%Y%m%d-%H%M%S)"
LOG_FILE="$LOG_DIR/rox-filer-diagnostico-$STAMP.log"
SUMMARY_FILE="$LOG_DIR/rox-filer-diagnostico-$STAMP-resumen.txt"
WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/rox-diag.XXXXXX")" || exit 1

PASS_COUNT=0
WARN_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0

cleanup() {
    if [ "$KEEP_TEMP" -eq 0 ]; then
        rm -rf "$WORK_DIR"
    else
        echo "Directorio temporal conservado: $WORK_DIR"
    fi
}
trap cleanup EXIT HUP INT TERM

exec > >(tee -a "$LOG_FILE") 2>&1

section() { printf '\n============================================================\n%s\n============================================================\n' "$1"; }
pass() { PASS_COUNT=$((PASS_COUNT + 1)); printf '[OK]   %s\n' "$*"; }
warn() { WARN_COUNT=$((WARN_COUNT + 1)); printf '[AVISO] %s\n' "$*"; }
fail() { FAIL_COUNT=$((FAIL_COUNT + 1)); printf '[ERROR] %s\n' "$*"; }
skip() { SKIP_COUNT=$((SKIP_COUNT + 1)); printf '[OMITIDO] %s\n' "$*"; }
cmd_exists() { command -v "$1" >/dev/null 2>&1; }

show_cmd() {
    printf '\n$'
    printf ' %q' "$@"
    printf '\n'
    "$@"
}

contains_legacy_short_option() {
    # Detecta -p o -S como opciones independientes, no como partes de palabras.
    local option="$1" file="$2"
    grep -E "(^|[[:space:],])${option}([=[:space:],]|$)" "$file" >/dev/null 2>&1
}

find_desktop_file() {
    local name="$1" d
    [ -n "$name" ] || return 1
    case "$name" in
        /*) [ -f "$name" ] && { printf '%s\n' "$name"; return 0; } ;;
    esac
    for d in \
        "${XDG_DATA_HOME:-$HOME/.local/share}/applications" \
        /usr/local/share/applications \
        /usr/share/applications \
        /usr/local/lib/applications \
        /usr/lib/applications; do
        [ -f "$d/$name" ] && { printf '%s\n' "$d/$name"; return 0; }
    done
    return 1
}

extract_exec_command() {
    local desktop="$1" line cmd
    line="$(grep -m1 '^Exec=' "$desktop" 2>/dev/null | sed 's/^Exec=//')"
    [ -n "$line" ] || return 1
    # Primera palabra solamente; suficiente para detectar binarios ausentes.
    cmd="${line%% *}"
    cmd="${cmd#\"}"; cmd="${cmd%\"}"
    printf '%s\n' "$cmd"
}


canonical_path() {
    local path="$1"
    if cmd_exists readlink; then
        readlink -f -- "$path" 2>/dev/null && return 0
    fi
    if cmd_exists realpath; then
        realpath -- "$path" 2>/dev/null && return 0
    fi
    printf '%s
' "$path"
}

paths_equivalent() {
    local left="$1" right="$2"
    [ "$left" = "$right" ] && return 0
    [ "$(canonical_path "$left")" = "$(canonical_path "$right")" ]
}

log_has_equivalent_arg() {
    local log_file="$1" prefix="$2" expected="$3" actual
    while IFS= read -r actual; do
        actual="${actual#${prefix}}"
        if paths_equivalent "$actual" "$expected"; then
            return 0
        fi
    done < <(grep -F "$prefix" "$log_file" 2>/dev/null || true)
    return 1
}

section "ROX-Filer diagnóstico $SCRIPT_VERSION"
echo "Fecha: $(date -R 2>/dev/null || date)"
echo "Log: $LOG_FILE"
echo "Temporal: $WORK_DIR"
echo "Usuario: $(id 2>/dev/null || true)"
echo "Directorio actual: $PWD"
if [ "${#EXTRA_SCRIPTS[@]}" -gt 0 ]; then
    printf 'Scripts reales solicitados: %s\n' "${EXTRA_SCRIPTS[*]}"
fi
if [ -n "$GEANY_TEST_FILE" ]; then
    echo "Archivo real solicitado para Geany: $GEANY_TEST_FILE"
fi
if [ -n "$RENAME_TEST_FILE" ]; then
    echo "Archivo real solicitado para Renombrar: $RENAME_TEST_FILE"
fi

section "1. Entorno del sistema"
echo "Kernel: $(uname -a 2>/dev/null || echo desconocido)"
[ -r /etc/os-release ] && { echo "--- /etc/os-release ---"; cat /etc/os-release; }
echo "DISPLAY=${DISPLAY:-<vacío>}"
echo "WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-<vacío>}"
echo "XDG_SESSION_TYPE=${XDG_SESSION_TYPE:-<vacío>}"
echo "GDK_BACKEND=${GDK_BACKEND:-<automático>}"
echo "LANG=${LANG:-<vacío>}"
echo "DBUS_SESSION_BUS_ADDRESS=${DBUS_SESSION_BUS_ADDRESS:+definido}"

for c in gcc make pkg-config file xdg-mime gio timeout tar grep sed awk; do
    if cmd_exists "$c"; then
        pass "Herramienta disponible: $c ($(command -v "$c"))"
    else
        warn "Herramienta no disponible: $c"
    fi
done

if cmd_exists pkg-config; then
    for p in gtk+-3.0 gio-unix-2.0 libxml-2.0 sm ice; do
        if pkg-config --exists "$p"; then
            pass "Dependencia pkg-config: $p $(pkg-config --modversion "$p" 2>/dev/null || true)"
        elif [ "$DO_BUILD" -eq 1 ]; then
            fail "Falta dependencia de compilación: $p"
        else
            warn "Falta dependencia de compilación (no se solicitó compilar): $p"
        fi
    done
fi

SOURCE_ROOT=""
APPDIR=""
PROJECT_ROOT=""
BINARY=""

section "2. Localización del fuente y del binario"
if [ -z "$TARGET" ]; then
    TARGET="$PWD"
fi

if [ -e "$TARGET" ]; then
    TARGET="$(cd "$(dirname "$TARGET")" 2>/dev/null && printf '%s/%s\n' "$PWD" "$(basename "$TARGET")")"
fi

echo "Objetivo solicitado: $TARGET"

if [ -f "$TARGET" ] && [[ "$TARGET" == *.tar.gz || "$TARGET" == *.tgz || "$TARGET" == *.tar.xz || "$TARGET" == *.tar.bz2 ]]; then
    mkdir -p "$WORK_DIR/extracted"
    if tar -xf "$TARGET" -C "$WORK_DIR/extracted"; then
        SOURCE_ROOT="$(find "$WORK_DIR/extracted" -type f -path '*/ROX-Filer/src/main.c' -print -quit 2>/dev/null)"
        if [ -n "$SOURCE_ROOT" ]; then
            APPDIR="${SOURCE_ROOT%/src/main.c}"
            PROJECT_ROOT="$(dirname "$APPDIR")"
            pass "Fuente extraído correctamente: $PROJECT_ROOT"
        else
            fail "El archivo no contiene ROX-Filer/src/main.c"
        fi
    else
        fail "No se pudo extraer el archivo fuente"
    fi
elif [ -d "$TARGET/ROX-Filer/src" ] && [ -f "$TARGET/ROX-Filer/src/main.c" ]; then
    PROJECT_ROOT="$TARGET"
    APPDIR="$TARGET/ROX-Filer"
    SOURCE_ROOT="$APPDIR/src/main.c"
    pass "Raíz del proyecto localizada: $PROJECT_ROOT"
elif [ -d "$TARGET/src" ] && [ -f "$TARGET/src/main.c" ]; then
    APPDIR="$TARGET"
    PROJECT_ROOT="$(dirname "$APPDIR")"
    SOURCE_ROOT="$APPDIR/src/main.c"
    pass "AppDir fuente localizada: $APPDIR"
elif [ -x "$TARGET" ] && [ ! -d "$TARGET" ]; then
    BINARY="$TARGET"
    pass "Ejecutable indicado como objetivo: $BINARY"
elif [ -d "$TARGET" ]; then
    # Comodidad para pruebas desde una carpeta como /roxfiler: localizar el
    # paquete fuente más reciente en vez de omitir silenciosamente el código.
    AUTO_TAR="$(find "$TARGET" -maxdepth 1 -type f \( -name 'rox-filer-*-source.tar.gz' -o -name 'rox-filer-*.tar.gz' \) -printf '%T@ %p\n' 2>/dev/null | sort -nr | sed -n '1s/^[^ ]* //p')"
    if [ -n "$AUTO_TAR" ]; then
        mkdir -p "$WORK_DIR/extracted"
        if tar -xf "$AUTO_TAR" -C "$WORK_DIR/extracted"; then
            SOURCE_ROOT="$(find "$WORK_DIR/extracted" -type f -path '*/ROX-Filer/src/main.c' -print -quit 2>/dev/null)"
            if [ -n "$SOURCE_ROOT" ]; then
                APPDIR="${SOURCE_ROOT%/src/main.c}"
                PROJECT_ROOT="$(dirname "$APPDIR")"
                pass "Fuente detectado automáticamente en $AUTO_TAR"
            else
                warn "Se encontró $AUTO_TAR, pero no contiene ROX-Filer/src/main.c"
            fi
        else
            warn "No se pudo extraer automáticamente $AUTO_TAR"
        fi
    else
        warn "No se encontró un árbol fuente reconocible en el objetivo"
    fi
else
    warn "No se encontró un árbol fuente reconocible en el objetivo"
fi

if [ -n "$BIN_OVERRIDE" ]; then
    if [ -x "$BIN_OVERRIDE" ]; then
        BINARY="$(cd "$(dirname "$BIN_OVERRIDE")" && printf '%s/%s\n' "$PWD" "$(basename "$BIN_OVERRIDE")")"
        pass "Ejecutable explícito: $BINARY"
    else
        fail "El archivo de --binary no existe o no es ejecutable: $BIN_OVERRIDE"
    fi
fi

if [ -z "$BINARY" ] && [ -n "$APPDIR" ] && [ -x "$APPDIR/ROX-Filer" ]; then
    BINARY="$APPDIR/ROX-Filer"
    pass "Binario incluido en AppDir: $BINARY"
fi

section "3. Revisión estructural del fuente"
if [ -z "$APPDIR" ]; then
    skip "No hay fuente disponible para revisión estructural"
else
    SRC="$APPDIR/src"
    required_files="desktop.c desktop.h desktop-backend.c desktop-backend.h desktop-x11.c main.c Makefile.in xdg_apps.c type.c diritem.c filer.c"
    for f in $required_files; do
        if [ -f "$SRC/$f" ]; then pass "Existe src/$f"; else fail "Falta src/$f"; fi
    done

    for f in pinboard.c pinboard.h tasklist.c tasklist.h; do
        if [ -e "$SRC/$f" ]; then fail "Archivo heredado todavía presente: src/$f"; else pass "Archivo heredado ausente: src/$f"; fi
    done

    if grep -F -- '"desktop"' "$SRC/main.c" >/dev/null 2>&1 && grep -F -- '--desktop' "$SRC/main.c" >/dev/null 2>&1; then
        pass "main.c registra y documenta --desktop"
    else
        fail "main.c no registra correctamente --desktop"
    fi

    if grep -F -- '--pinboard' "$SRC/main.c" >/dev/null 2>&1 || grep -F -- '--rox-session' "$SRC/main.c" >/dev/null 2>&1; then
        fail "main.c todavía contiene opciones públicas antiguas"
    else
        pass "main.c no contiene --pinboard ni --rox-session"
    fi

    if contains_legacy_short_option '-p' "$SRC/main.c" || contains_legacy_short_option '-S' "$SRC/main.c"; then
        fail "main.c todavía parece documentar -p o -S"
    else
        pass "main.c no documenta -p ni -S"
    fi

    if grep -Eq 'desktop\.c.*desktop-backend\.c.*desktop-x11\.c' "$SRC/Makefile.in"; then
        pass "Makefile.in compila el módulo común y el backend X11"
    else
        fail "Makefile.in no incluye correctamente los módulos desktop"
    fi

    if grep -E '(^|[[:space:]\\])pinboard\.(c|o)|(^|[[:space:]\\])tasklist\.(c|o)' "$SRC/Makefile.in" >/dev/null 2>&1; then
        fail "Makefile.in todavía compila pinboard o tasklist"
    else
        pass "Makefile.in no compila pinboard ni tasklist"
    fi

    if grep -E 'gdkx|gdk_x11|XInternAtom|XChangeProperty|XSendEvent' "$SRC/desktop.c" >/dev/null 2>&1; then
        fail "desktop.c todavía contiene llamadas X11 directas"
    else
        pass "desktop.c no contiene llamadas X11 directas"
    fi

    if grep -F -- 'desktop_x11_backend_get' "$SRC/desktop-x11.c" >/dev/null 2>&1 && \
       grep -F -- 'desktop_backend_select' "$SRC/desktop-backend.c" >/dev/null 2>&1; then
        pass "Selector y backend X11 están conectados"
    else
        fail "La interfaz del backend desktop parece incompleta"
    fi

    if grep -F -- 'ROX_ICON_HOME' "$SRC/global.h" >/dev/null 2>&1 && \
       grep -F -- 'user-home' "$SRC/global.h" >/dev/null 2>&1 && \
       grep -F -- '.DirIcon' "$SRC/diritem.c" >/dev/null 2>&1; then
        pass "Existen las protecciones del icono home frente a .DirIcon"
    else
        warn "No pude confirmar estáticamente la protección user-home/.DirIcon"
    fi

    if grep -F -- 'inode/directory' "$SRC/xdg_apps.c" >/dev/null 2>&1; then
        pass "xdg_apps.c contiene tratamiento explícito de inode/directory"
    else
        fail "xdg_apps.c no contiene tratamiento de inode/directory"
    fi

    if grep -F -- '{N_("Cut"),' "$SRC/menu.c" >/dev/null 2>&1 && \
       grep -F -- '{N_("Copy"),' "$SRC/menu.c" >/dev/null 2>&1 && \
       grep -F -- '{N_("Paste"),' "$SRC/menu.c" >/dev/null 2>&1; then
        pass "Cortar, Copiar y Pegar están definidos en el menú principal"
    else
        warn "No pude confirmar Cortar, Copiar y Pegar en el menú principal"
    fi

    if [ -f "$SRC/po/es.po" ] && grep -A1 '^msgid "Cut"$' "$SRC/po/es.po" | grep -F 'msgstr "Cortar"' >/dev/null 2>&1; then
        pass "La traducción española de Cut es Cortar"
    else
        warn "La traducción española de Cut no parece ser Cortar"
    fi

    if grep -F 'N_("Smaller Icons"), ROX_ICON_ZOOM_OUT' "$SRC/toolbar.c" >/dev/null 2>&1 && \
       grep -F 'N_("Automatic"), ROX_ICON_ZOOM_FIT' "$SRC/toolbar.c" >/dev/null 2>&1 && \
       grep -F 'N_("Bigger Icons"), ROX_ICON_ZOOM_IN' "$SRC/toolbar.c" >/dev/null 2>&1; then
        pass "La barra contiene Zoom Out, tamaño automático y Zoom In con etiquetas distintas"
    else
        fail "La barra no contiene los tres controles explícitos de tamaño"
    fi

    if grep -F 'N_("Size"), ROX_ICON_ZOOM_' "$SRC/toolbar.c" >/dev/null 2>&1; then
        fail "La barra todavía contiene botones ambiguos llamados Size"
    else
        pass "La barra ya no usa la etiqueta duplicada Size para el zoom"
    fi

    if grep -F 'terminal_build_argv' "$SRC/menu.c" >/dev/null 2>&1 && \
       grep -F 'terminal_create_runner' "$SRC/menu.c" >/dev/null 2>&1 && \
       grep -F 'path_read_shebang' "$SRC/menu.c" >/dev/null 2>&1 && \
       grep -F 'ROX_DIAGNOSTIC_TERMINAL' "$SRC/menu.c" >/dev/null 2>&1; then
        pass "Ejecutar en terminal usa el lanzador adaptable y el lector robusto de shebang"
    else
        fail "La integración de Ejecutar en terminal r72 parece incompleta"
    fi
    if grep -F 'TERMINAL_RUN_BASH' "$SRC/menu.c" >/dev/null 2>&1 && \
       grep -F 'TERMINAL_RUN_ASH' "$SRC/menu.c" >/dev/null 2>&1 && \
       grep -F 'terminal_shebang_available' "$SRC/menu.c" >/dev/null 2>&1; then
        pass "Los intérpretes Bash/Ash y la validación de #! están presentes"
    else
        fail "Falta la selección de intérpretes de scripts de r72"
    fi
    if grep -F 'gtk_window_present(GTK_WINDOW(savebox))' "$SRC/menu.c" >/dev/null 2>&1 && \
       grep -F 'diagnose-rename' "$SRC/main.c" >/dev/null 2>&1; then
        pass "El diálogo Renombrar se presenta y tiene prueba interna"
    else
        fail "La corrección del diálogo Renombrar está incompleta"
    fi

    if grep -F 'diagnose-open-with' "$SRC/main.c" >/dev/null 2>&1 && \
       grep -F 'diagnose-terminal' "$SRC/main.c" >/dev/null 2>&1 && \
       grep -F 'diagnose-rename' "$SRC/main.c" >/dev/null 2>&1 && \
       grep -F 'xdg_apps_diagnose_launch' "$SRC/xdg_apps.c" >/dev/null 2>&1 && \
       grep -F 'menu_diagnose_run_in_terminal' "$SRC/menu.c" >/dev/null 2>&1 && \
       grep -F 'menu_diagnose_rename_dialog' "$SRC/menu.c" >/dev/null 2>&1; then
        pass "El binario incluye las pruebas internas de Open With, terminal y Renombrar"
    else
        fail "Faltan las entradas internas usadas por el diagnóstico r72"
    fi

    direct_line="$(grep -n 'run_desktop_entry(desktop_file' "$SRC/xdg_apps.c" | head -1 | cut -d: -f1)"
    gio_line="$(grep -n 'g_app_info_launch(app' "$SRC/xdg_apps.c" | head -1 | cut -d: -f1)"
    if [ -n "$direct_line" ] && [ -n "$gio_line" ] && [ "$gio_line" -lt "$direct_line" ]; then
        pass "Open With usa GIO con contexto gráfico y conserva Exec= como respaldo"
    else
        fail "Open With no parece usar el orden GIO → Exec="
    fi

    echo "--- Referencias residuales a pinboard en código ejecutable ---"
    residual="$(find "$SRC" -maxdepth 1 -type f \( -name '*.c' -o -name '*.h' \) -print0 | xargs -0 grep -nEi 'pinboard|tasklist' 2>/dev/null || true)"
    if [ -n "$residual" ]; then
        echo "$residual"
        warn "Quedan referencias textuales a pinboard/tasklist; revisar si son comentarios o lógica residual"
    else
        pass "No quedan referencias textuales a pinboard/tasklist en C/H"
    fi

    echo "La validación sintáctica real se realizará mediante configure/make; no se usan conteos de delimitadores porque producen falsos positivos con cadenas y comentarios C."
fi

section "4. Configuración y compilación aislada"
if [ "$DO_BUILD" -eq 0 ]; then
    skip "Compilación desactivada con --no-build"
elif [ -z "$PROJECT_ROOT" ] || [ -z "$APPDIR" ]; then
    skip "No hay fuente para compilar"
elif ! cmd_exists make || ! cmd_exists pkg-config; then
    fail "No se puede compilar: faltan make o pkg-config"
else
    BUILD_ROOT="$WORK_DIR/buildtree"
    BUILD_APP="$BUILD_ROOT/ROX-Filer"
    mkdir -p "$BUILD_APP"
    echo "Copiando solamente la AppDir fuente a: $BUILD_APP"
    if cp -a "$APPDIR/." "$BUILD_APP/"; then
        mkdir -p "$BUILD_APP/build"
        echo "--- configure ---"
        if (cd "$BUILD_APP/build" && ../src/configure); then
            pass "configure terminó correctamente"
            JOBS=1
            if cmd_exists getconf; then JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)"; fi
            case "$JOBS" in ''|*[!0-9]*) JOBS=1;; esac
            [ "$JOBS" -gt 8 ] && JOBS=8
            echo "--- make -j$JOBS ---"
            if (cd "$BUILD_APP/build" && make clean >/dev/null 2>&1 || true; make -j"$JOBS"); then
                if [ -x "$BUILD_APP/ROX-Filer" ]; then
                    pass "Compilación terminada: $BUILD_APP/ROX-Filer"
                    BINARY="$BUILD_APP/ROX-Filer"
                else
                    fail "make terminó sin crear ROX-Filer"
                fi
            else
                fail "La compilación falló; el error completo está arriba en el log"
            fi
        else
            fail "configure falló; revisar dependencias y mensajes anteriores"
        fi
    else
        fail "No se pudo copiar el árbol para la compilación aislada"
    fi
fi

if [ -z "$BINARY" ] && cmd_exists ROX-Filer; then
    BINARY="$(command -v ROX-Filer)"
    warn "Se usará el ROX-Filer instalado porque no se obtuvo un binario del fuente: $BINARY"
fi

section "5. Pruebas del ejecutable"
if [ -z "$BINARY" ] || [ ! -x "$BINARY" ]; then
    skip "No hay un ejecutable disponible; se omiten pruebas de ejecución"
else
    echo "Ejecutable probado: $BINARY"
    if cmd_exists file; then show_cmd file "$BINARY" || true; fi
    if cmd_exists ldd; then
        echo "--- ldd ---"
        LDD_OUT="$(ldd "$BINARY" 2>&1 || true)"
        echo "$LDD_OUT"
        if echo "$LDD_OUT" | grep -F 'not found' >/dev/null 2>&1; then fail "El binario tiene bibliotecas no encontradas"; else pass "ldd no informa bibliotecas faltantes"; fi
    fi

    VERSION_OUT="$WORK_DIR/version.txt"
    HELP_OUT="$WORK_DIR/help.txt"
    if "$BINARY" --version >"$VERSION_OUT" 2>&1; then
        pass "ROX-Filer --version finalizó correctamente"
    else
        fail "ROX-Filer --version devolvió error"
    fi
    cat "$VERSION_OUT"

    if "$BINARY" --help >"$HELP_OUT" 2>&1; then
        pass "ROX-Filer --help finalizó correctamente"
    else
        fail "ROX-Filer --help devolvió error"
    fi
    echo "--- salida de --help ---"
    cat "$HELP_OUT"

    if grep -F -- 'Usage: ROX-Filer' "$HELP_OUT" >/dev/null 2>&1; then pass "La ayuda usa el nombre canónico ROX-Filer"; else fail "La ayuda no usa 'Usage: ROX-Filer'"; fi
    if grep -F -- '--desktop' "$HELP_OUT" >/dev/null 2>&1; then pass "La ayuda muestra --desktop"; else fail "La ayuda no muestra --desktop"; fi
    if grep -F -- '--pinboard' "$HELP_OUT" >/dev/null 2>&1 || grep -F -- '--rox-session' "$HELP_OUT" >/dev/null 2>&1 || contains_legacy_short_option '-p' "$HELP_OUT" || contains_legacy_short_option '-S' "$HELP_OUT"; then
        fail "La ayuda todavía muestra opciones antiguas de pinboard/sesión"
    else
        pass "La ayuda no muestra -p, --pinboard, -S ni --rox-session"
    fi

    for oldopt in '--pinboard=Default' '--rox-session'; do
        OLD_OUT="$WORK_DIR/legacy-${oldopt//[^A-Za-z0-9]/_}.txt"
        if cmd_exists timeout; then
            timeout 5 "$BINARY" "$oldopt" >"$OLD_OUT" 2>&1
            rc=$?
        else
            "$BINARY" "$oldopt" >"$OLD_OUT" 2>&1
            rc=$?
        fi
        echo "--- prueba opción antigua $oldopt (rc=$rc) ---"
        cat "$OLD_OUT"
        if [ "$rc" -ne 0 ]; then pass "La opción antigua $oldopt es rechazada"; else fail "La opción antigua $oldopt todavía fue aceptada"; fi
    done
fi

section "5b. Procesos ROX ya ejecutándose"
if [ -z "$BINARY" ] || [ ! -x "$BINARY" ]; then
    skip "No hay binario para comparar con procesos existentes"
elif ! cmd_exists pgrep; then
    skip "pgrep no está disponible"
else
    running_found=0
    binary_mtime="$(stat -Lc %Y "$BINARY" 2>/dev/null || echo 0)"
    while IFS= read -r pid; do
        [ -n "$pid" ] || continue
        [ "$pid" = "$$" ] && continue
        [ -r "/proc/$pid/cmdline" ] || continue
        cmdline="$(tr '\0' ' ' < "/proc/$pid/cmdline" 2>/dev/null || true)"
        case "$cmdline" in
            *ROX-Filer*) ;;
            *) continue ;;
        esac
        running_found=1
        exe="$(readlink "/proc/$pid/exe" 2>/dev/null || true)"
        proc_started="$(stat -Lc %Y "/proc/$pid" 2>/dev/null || echo 0)"
        echo "PID=$pid"
        echo "  cmdline=$cmdline"
        echo "  exe=${exe:-<desconocido>}"
        echo "  inicio_epoch=$proc_started binario_mtime=$binary_mtime"
        if [ -n "$exe" ] && [ -e "/proc/$pid/exe" ] && [ "/proc/$pid/exe" -ef "$BINARY" ]; then
            if [ "$proc_started" -gt 0 ] && [ "$binary_mtime" -gt 0 ] && [ "$proc_started" -lt "$binary_mtime" ]; then
                warn "El proceso ROX PID $pid empezó antes de instalar el binario probado; reinícielo antes de validar cambios"
            else
                pass "El proceso ROX PID $pid usa el mismo ejecutable que se está probando"
            fi
        else
            warn "El proceso ROX PID $pid usa otro ejecutable o una copia antigua: ${exe:-desconocido}"
        fi
    done < <(pgrep -f '(^|/)(ROX-Filer)([[:space:]]|$)' 2>/dev/null || true)
    if [ "$running_found" -eq 0 ]; then
        pass "No hay procesos ROX-Filer antiguos interfiriendo con la prueba"
    fi

    if cmd_exists xprop && [ -n "${DISPLAY:-}" ]; then
        desktop_xid="$(xprop -root _ROX_DESKTOP_WINDOW 2>/dev/null | sed -n 's/.*# 0x/0x/p' | awk '{print $1}' | head -1)"
        if [ -n "$desktop_xid" ]; then
            echo "Escritorio ROX X11 registrado: $desktop_xid"
            xprop -id "$desktop_xid" _NET_WM_PID WM_NAME 2>/dev/null || true
        fi
    fi
fi

section "6. Diagnóstico MIME del sistema"
SAMPLES="$WORK_DIR/mime-samples"
mkdir -p "$SAMPLES/carpeta"
printf 'Texto de prueba ROX-Filer\n' > "$SAMPLES/prueba.txt"
printf '<!doctype html><html><body>ROX</body></html>\n' > "$SAMPLES/prueba.html"
printf '%%PDF-1.1\n1 0 obj<<>>endobj\ntrailer<<>>\n%%%%EOF\n' > "$SAMPLES/prueba.pdf"
if cmd_exists base64; then
    printf '%s' 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAusB9Y9Zl1sAAAAASUVORK5CYII=' | base64 -d > "$SAMPLES/prueba.png" 2>/dev/null || true
fi

for sample in "$SAMPLES/carpeta" "$SAMPLES/prueba.txt" "$SAMPLES/prueba.html" "$SAMPLES/prueba.pdf" "$SAMPLES/prueba.png"; do
    [ -e "$sample" ] || continue
    echo "--- $sample ---"
    mime_file=""
    mime_xdg=""
    mime_gio=""
    mime_rox=""
    if cmd_exists file; then mime_file="$(file -b --mime-type "$sample" 2>&1 || true)"; echo "file:     $mime_file"; fi
    if cmd_exists xdg-mime; then mime_xdg="$(xdg-mime query filetype "$sample" 2>&1 || true)"; echo "xdg-mime: $mime_xdg"; fi
    if cmd_exists gio; then mime_gio="$(gio info -a standard::content-type "$sample" 2>/dev/null | sed -n 's/^[[:space:]]*standard::content-type:[[:space:]]*//p' | head -1)"; echo "GIO:      $mime_gio"; fi
    if [ -n "$BINARY" ] && [ -x "$BINARY" ]; then
        mime_rox_err="$WORK_DIR/mime-rox-$(basename "$sample").err"
        mime_rox="$("$BINARY" -m "$sample" 2>"$mime_rox_err" || true)"
        echo "ROX:      $mime_rox"
        if [ -s "$mime_rox_err" ]; then
            echo "ROX stderr:"
            sed 's/^/  /' "$mime_rox_err"
            if grep -E 'GLib-(CRITICAL|ERROR)|Gtk-(CRITICAL|ERROR)' "$mime_rox_err" >/dev/null 2>&1; then
                fail "ROX emitió una advertencia crítica al consultar el MIME de $(basename "$sample")"
            else
                warn "ROX escribió mensajes en stderr al consultar el MIME de $(basename "$sample")"
            fi
        fi
    fi

    if [ -d "$sample" ]; then
        if [ "$mime_rox" = "inode/directory" ] || { [ -z "$mime_rox" ] && [ "$mime_xdg" = "inode/directory" ]; }; then
            pass "La carpeta se identifica como inode/directory"
        else
            fail "La carpeta no se identificó como inode/directory"
        fi
    fi

done

for mime in inode/directory text/plain text/html application/pdf image/png; do
    echo "--- Asociación: $mime ---"
    default_app=""
    if cmd_exists xdg-mime; then
        default_app="$(xdg-mime query default "$mime" 2>/dev/null || true)"
        echo "xdg-mime default: ${default_app:-<ninguna>}"
    fi
    if cmd_exists gio; then gio mime "$mime" 2>&1 || true; fi

    if [ -n "$default_app" ]; then
        desktop_path="$(find_desktop_file "$default_app" || true)"
        if [ -n "$desktop_path" ]; then
            pass "Existe el .desktop predeterminado para $mime: $desktop_path"
            echo "Name: $(grep -m1 '^Name=' "$desktop_path" 2>/dev/null | sed 's/^Name=//' || true)"
            echo "Exec: $(grep -m1 '^Exec=' "$desktop_path" 2>/dev/null | sed 's/^Exec=//' || true)"
            echo "MimeType: $(grep -m1 '^MimeType=' "$desktop_path" 2>/dev/null | sed 's/^MimeType=//' || true)"
            exec_cmd="$(extract_exec_command "$desktop_path" || true)"
            if [ -n "$exec_cmd" ]; then
                if [[ "$exec_cmd" = /* ]]; then
                    [ -x "$exec_cmd" ] && pass "El comando Exec existe: $exec_cmd" || warn "El comando Exec no es ejecutable: $exec_cmd"
                elif command -v "$exec_cmd" >/dev/null 2>&1; then
                    pass "El comando Exec está en PATH: $exec_cmd"
                else
                    warn "El comando Exec no está en PATH: $exec_cmd"
                fi
            else
                warn "El .desktop no tiene una línea Exec= utilizable"
            fi
        else
            warn "La asociación apunta a un .desktop no localizado: $default_app"
        fi
    else
        warn "No hay aplicación predeterminada para $mime"
    fi

done

# Comprobación específica del problema reportado con Geany y carpetas.
geany_dir_declared=0
while IFS= read -r geany_desktop; do
    [ -f "$geany_desktop" ] || continue
    echo "--- Geany desktop: $geany_desktop ---"
    mime_line="$(grep -m1 '^MimeType=' "$geany_desktop" 2>/dev/null || true)"
    echo "${mime_line:-MimeType=<ausente>}"
    if echo "$mime_line" | grep -F 'inode/directory' >/dev/null 2>&1; then
        geany_dir_declared=1
        warn "Este .desktop de Geany declara inode/directory"
    fi
done < <(find "${XDG_DATA_HOME:-$HOME/.local/share}/applications" /usr/local/share/applications /usr/share/applications -maxdepth 1 -type f -iname '*geany*.desktop' 2>/dev/null | sort -u)

if [ "$geany_dir_declared" -eq 0 ]; then pass "No encontré un Geany .desktop que declare inode/directory"; fi
if cmd_exists xdg-mime; then
    dir_default="$(xdg-mime query default inode/directory 2>/dev/null || true)"
    if echo "$dir_default" | grep -qi geany; then fail "El sistema tiene Geany como predeterminado de inode/directory: $dir_default"; else pass "Geany no es el predeterminado del sistema para inode/directory"; fi
fi

section "7. Sondeo interno de apertura MIME"
if [ "$DO_GUI" -eq 0 ]; then
    skip "Sondeo MIME interno desactivado con --no-gui"
elif [ -z "$BINARY" ] || [ ! -x "$BINARY" ]; then
    skip "No hay binario para el sondeo MIME interno"
elif [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ]; then
    skip "No hay display gráfico en la sesión"
else
    PROBE="$WORK_DIR/mime-probe"
    mkdir -p "$PROBE/home" "$PROBE/config" "$PROBE/data/applications" "$PROBE/Choices"
    PROBE_LOG="$PROBE/handler.log"
    HANDLER="$PROBE/rox-mime-probe-handler"
    cat > "$HANDLER" <<EOF_HANDLER
#!/bin/sh
{
  echo "HANDLER_INVOKED"
  echo "PWD=\$PWD"
  printf 'ARG=%s\\n' "\$@"
} >> "$PROBE_LOG"
EOF_HANDLER
    chmod +x "$HANDLER"
    cat > "$PROBE/data/applications/rox-mime-probe.desktop" <<EOF_DESKTOP
[Desktop Entry]
Type=Application
Name=ROX MIME Probe
NoDisplay=true
Exec=$HANDLER %f
MimeType=text/plain;
Terminal=false
EOF_DESKTOP
    cat > "$PROBE/config/mimeapps.list" <<'EOF_MIMEAPPS'
[Default Applications]
text/plain=rox-mime-probe.desktop;
[Added Associations]
text/plain=rox-mime-probe.desktop;
EOF_MIMEAPPS
    cp "$PROBE/config/mimeapps.list" "$PROBE/data/applications/mimeapps.list"
    if cmd_exists update-desktop-database; then update-desktop-database "$PROBE/data/applications" >/dev/null 2>&1 || true; fi

    PROBE_STDOUT="$PROBE/rox-output.txt"
    env HOME="$PROBE/home" \
        XDG_CONFIG_HOME="$PROBE/config" \
        XDG_DATA_HOME="$PROBE/data" \
        XDG_DATA_DIRS="/usr/local/share:/usr/share" \
        CHOICESPATH="$PROBE/Choices" \
        "$BINARY" -n "$SAMPLES/prueba.txt" >"$PROBE_STDOUT" 2>&1 &
    probe_pid=$!
    for _i in $(seq 1 50 2>/dev/null || echo 1 2 3 4 5 6 7 8 9 10); do
        [ -s "$PROBE_LOG" ] && break
        kill -0 "$probe_pid" 2>/dev/null || break
        sleep 0.1
    done
    kill "$probe_pid" 2>/dev/null || true
    wait "$probe_pid" 2>/dev/null || true
    echo "--- salida ROX del sondeo MIME ---"
    cat "$PROBE_STDOUT" 2>/dev/null || true
    echo "--- salida del handler ---"
    cat "$PROBE_LOG" 2>/dev/null || true
    if grep -F 'HANDLER_INVOKED' "$PROBE_LOG" >/dev/null 2>&1 && grep -F "$SAMPLES/prueba.txt" "$PROBE_LOG" >/dev/null 2>&1; then
        pass "ROX lanzó correctamente el manejador MIME temporal para text/plain"
    else
        fail "ROX no lanzó el manejador MIME temporal; revisar salida anterior"
    fi

    # Una carpeta debe abrirse como carpeta y no ejecutarse con el handler de texto.
    : > "$PROBE_LOG"
    env HOME="$PROBE/home" \
        XDG_CONFIG_HOME="$PROBE/config" \
        XDG_DATA_HOME="$PROBE/data" \
        XDG_DATA_DIRS="/usr/local/share:/usr/share" \
        CHOICESPATH="$PROBE/Choices" \
        "$BINARY" -n "$SAMPLES/carpeta" >"$PROBE/dir-output.txt" 2>&1 &
    dir_pid=$!
    sleep 1
    kill "$dir_pid" 2>/dev/null || true
    wait "$dir_pid" 2>/dev/null || true
    if [ -s "$PROBE_LOG" ]; then
        cat "$PROBE_LOG"
        fail "Al abrir una carpeta se ejecutó el manejador MIME temporal"
    else
        pass "Al abrir una carpeta no se ejecutó un manejador MIME de archivo"
    fi
fi

section "8. Prueba exacta de Open With"
if [ "$DO_GUI" -eq 0 ]; then
    skip "Prueba Open With desactivada con --no-gui"
elif [ -z "$BINARY" ] || [ ! -x "$BINARY" ]; then
    skip "No hay binario para probar Open With"
elif [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ] && ! cmd_exists xvfb-run; then
    skip "No hay display ni xvfb-run para ejecutar las pruebas internas"
else
    OPEN_TEST="$WORK_DIR/open-with-test"
    mkdir -p "$OPEN_TEST/home" "$OPEN_TEST/config" "$OPEN_TEST/data/applications" "$OPEN_TEST/bin" "$OPEN_TEST/Choices"
    OPEN_LOG="$OPEN_TEST/handler.log"
    OPEN_HANDLER="$OPEN_TEST/bin/geany-probe"
    cat > "$OPEN_HANDLER" <<'EOF_OPEN_HANDLER'
#!/bin/sh
{
  echo "OPEN_HANDLER_INVOKED"
  echo "OPEN_HANDLER_PWD=$PWD"
  printf 'OPEN_HANDLER_ARG=%s\n' "$@"
} >> "$ROX_OPEN_TEST_LOG"
EOF_OPEN_HANDLER
    chmod +x "$OPEN_HANDLER"
    OPEN_FILE="$OPEN_TEST/archivo con espacio y ñ.txt"
    printf 'ROX Open With test\n' > "$OPEN_FILE"
    GEANY_OPEN_FILE="$OPEN_FILE"
    if [ -n "$GEANY_TEST_FILE" ]; then
        if [ -f "$GEANY_TEST_FILE" ]; then
            GEANY_OPEN_FILE="$(readlink -f "$GEANY_TEST_FILE" 2>/dev/null || printf '%s' "$GEANY_TEST_FILE")"
            echo "--- archivo real solicitado para Geany ---"
            ls -l -- "$GEANY_OPEN_FILE" 2>&1 || true
            file -- "$GEANY_OPEN_FILE" 2>&1 || true
            if [ -n "$BINARY" ]; then
                "$BINARY" -m "$GEANY_OPEN_FILE" 2>&1 | sed 's/^/ROX MIME: /' || true
            fi
        else
            warn "El archivo indicado con --geany-file no existe: $GEANY_TEST_FILE"
        fi
    fi
    OPEN_DESKTOP="$OPEN_TEST/data/applications/geany-probe.desktop"
    cat > "$OPEN_DESKTOP" <<EOF_OPEN_DESKTOP
[Desktop Entry]
Type=Application
Name=Geany Probe
Exec="$OPEN_HANDLER" %F
MimeType=text/plain;
Terminal=false
NoDisplay=true
EOF_OPEN_DESKTOP

    OPEN_OUT="$OPEN_TEST/open-with.out"
    run_open_probe() {
        env ROX_DIAGNOSTIC=1 ROX_OPEN_TEST_LOG="$OPEN_LOG" \
            HOME="$OPEN_TEST/home" XDG_CONFIG_HOME="$OPEN_TEST/config" \
            XDG_DATA_HOME="$OPEN_TEST/data" XDG_DATA_DIRS="/usr/local/share:/usr/share" \
            CHOICESPATH="$OPEN_TEST/Choices" \
            "$BINARY" --diagnose-open-with="$OPEN_DESKTOP" "$OPEN_FILE"
    }
    if [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ]; then
        run_open_probe >"$OPEN_OUT" 2>&1
        open_rc=$?
    else
        xvfb-run -a -s '-screen 0 1024x768x24' bash -c \
            'env ROX_DIAGNOSTIC=1 ROX_OPEN_TEST_LOG="$1" HOME="$2" XDG_CONFIG_HOME="$3" XDG_DATA_HOME="$4" XDG_DATA_DIRS="/usr/local/share:/usr/share" CHOICESPATH="$5" "$6" --diagnose-open-with="$7" "$8"' \
            _ "$OPEN_LOG" "$OPEN_TEST/home" "$OPEN_TEST/config" "$OPEN_TEST/data" "$OPEN_TEST/Choices" "$BINARY" "$OPEN_DESKTOP" "$OPEN_FILE" >"$OPEN_OUT" 2>&1
        open_rc=$?
    fi
    for _i in $(seq 1 40 2>/dev/null || echo 1 2 3 4 5 6 7 8 9 10); do
        grep -F 'OPEN_HANDLER_INVOKED' "$OPEN_LOG" >/dev/null 2>&1 && break
        sleep 0.1
    done
    echo "--- salida diagnóstico Open With (rc=$open_rc) ---"
    cat "$OPEN_OUT" 2>/dev/null || true
    echo "--- salida del manejador tipo Geany ---"
    cat "$OPEN_LOG" 2>/dev/null || true
    if [ "$open_rc" -eq 0 ] && grep -F 'OPEN_HANDLER_INVOKED' "$OPEN_LOG" >/dev/null 2>&1 && \
       log_has_equivalent_arg "$OPEN_LOG" "OPEN_HANDLER_ARG=" "$OPEN_FILE"; then
        pass "Open With ejecutó un .desktop tipo Geany y conservó correctamente la ruta con espacios"
    else
        fail "Open With no ejecutó correctamente el .desktop de prueba"
    fi

    geany_desktop="$(find_desktop_file geany.desktop || true)"
    if [ -n "$geany_desktop" ]; then
        echo "--- inspección del Geany real: $geany_desktop ---"
        grep -E '^(Name|Exec|TryExec|Terminal|DBusActivatable|MimeType)=' "$geany_desktop" 2>/dev/null || true
        geany_exec="$(extract_exec_command "$geany_desktop" || true)"
        if [ -n "$geany_exec" ]; then
            if [[ "$geany_exec" = /* ]]; then
                geany_program="$geany_exec"
            else
                geany_program="$(command -v "$geany_exec" 2>/dev/null || true)"
            fi
            if [ -n "$geany_program" ] && [ -x "$geany_program" ]; then
                pass "El ejecutable real de Geany existe: $geany_program"
                timeout 5 "$geany_program" --version 2>&1 | sed 's/^/  /' || warn "Geany --version devolvió error o expiró"

                # Ejecutar el .desktop REAL de Geany, pero sombrear el comando
                # geany con un registrador. Así se comprueban sus códigos %F/%f
                # sin abrir una ventana ni alterar la configuración del usuario.
                if [[ "$geany_exec" != /* ]]; then
                    GEANY_SHADOW_LOG="$OPEN_TEST/geany-real-desktop.log"
                    GEANY_SHADOW="$OPEN_TEST/bin/$geany_exec"
                    cat > "$GEANY_SHADOW" <<'EOF_GEANY_SHADOW'
#!/bin/sh
{
  echo "GEANY_REAL_DESKTOP_INVOKED"
  echo "GEANY_REAL_DESKTOP_PWD=$PWD"
  printf 'GEANY_REAL_DESKTOP_ARG=%s\n' "$@"
} >> "$ROX_GEANY_TEST_LOG"
EOF_GEANY_SHADOW
                    chmod +x "$GEANY_SHADOW"
                    GEANY_REAL_OUT="$OPEN_TEST/geany-real-desktop.out"
                    if [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ]; then
                        env PATH="$OPEN_TEST/bin:$PATH" ROX_DIAGNOSTIC=1 \
                            ROX_GEANY_TEST_LOG="$GEANY_SHADOW_LOG" \
                            HOME="$OPEN_TEST/home" XDG_CONFIG_HOME="$OPEN_TEST/config" \
                            XDG_DATA_HOME="$OPEN_TEST/data" CHOICESPATH="$OPEN_TEST/Choices" \
                            "$BINARY" --diagnose-open-with="$geany_desktop" "$GEANY_OPEN_FILE" \
                            >"$GEANY_REAL_OUT" 2>&1
                        geany_real_rc=$?
                    else
                        xvfb-run -a -s '-screen 0 1024x768x24' bash -c \
                            'env PATH="$1:$PATH" ROX_DIAGNOSTIC=1 ROX_GEANY_TEST_LOG="$2" HOME="$3" XDG_CONFIG_HOME="$4" XDG_DATA_HOME="$5" CHOICESPATH="$6" "$7" --diagnose-open-with="$8" "$9"' \
                            _ "$OPEN_TEST/bin" "$GEANY_SHADOW_LOG" "$OPEN_TEST/home" "$OPEN_TEST/config" "$OPEN_TEST/data" "$OPEN_TEST/Choices" "$BINARY" "$geany_desktop" "$GEANY_OPEN_FILE" \
                            >"$GEANY_REAL_OUT" 2>&1
                        geany_real_rc=$?
                    fi
                    for _i in $(seq 1 40 2>/dev/null || echo 1 2 3 4 5 6 7 8 9 10); do
                        grep -F 'GEANY_REAL_DESKTOP_INVOKED' "$GEANY_SHADOW_LOG" >/dev/null 2>&1 && break
                        sleep 0.1
                    done
                    echo "--- ejecución controlada del .desktop real de Geany (rc=$geany_real_rc) ---"
                    cat "$GEANY_REAL_OUT" 2>/dev/null || true
                    cat "$GEANY_SHADOW_LOG" 2>/dev/null || true
                    if [ "$geany_real_rc" -eq 0 ] && \
                       grep -F 'GEANY_REAL_DESKTOP_INVOKED' "$GEANY_SHADOW_LOG" >/dev/null 2>&1 && \
                       log_has_equivalent_arg "$GEANY_SHADOW_LOG" "GEANY_REAL_DESKTOP_ARG=" "$GEANY_OPEN_FILE"; then
                        pass "ROX interpretó correctamente el Exec= del .desktop real de Geany"
                    else
                        fail "ROX no interpretó correctamente el .desktop real de Geany"
                    fi
                else
                    warn "El Exec= de Geany usa una ruta absoluta; no se puede sombrear sin ejecutar la aplicación real"
                fi

                # Prueba gráfica real aislada. Se hace únicamente dentro de Xvfb
                # y con HOME temporal para no tocar la sesión/configuración real.
                if cmd_exists xvfb-run; then
                    GEANY_SMOKE_OUT="$OPEN_TEST/geany-smoke.out"
                    xvfb-run -a -s '-screen 0 1024x768x24' bash -c \
                        'export HOME="$1" XDG_CONFIG_HOME="$2" XDG_DATA_HOME="$3"; "$4" --new-instance --no-session "$5" >"$6" 2>&1 & pid=$!; sleep 2; if kill -0 "$pid" 2>/dev/null; then echo GEANY_SMOKE_ALIVE; kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; exit 0; fi; wait "$pid"; rc=$?; echo GEANY_SMOKE_EXIT=$rc; exit 1' \
                        _ "$OPEN_TEST/home" "$OPEN_TEST/config" "$OPEN_TEST/data" \
                        "$geany_program" "$GEANY_OPEN_FILE" "$GEANY_SMOKE_OUT"
                    geany_smoke_rc=$?
                    echo "--- prueba gráfica aislada de Geany (rc=$geany_smoke_rc) ---"
                    cat "$GEANY_SMOKE_OUT" 2>/dev/null || true
                    if [ "$geany_smoke_rc" -eq 0 ]; then
                        pass "Geany pudo iniciar en una pantalla X aislada"
                    else
                        warn "Geany no permaneció activo en la prueba Xvfb; revisar su salida anterior"
                    fi
                else
                    skip "No hay xvfb-run para probar el inicio gráfico real de Geany"
                fi
            else
                fail "El Exec= del Geany real no resuelve a un ejecutable"
            fi
        else
            fail "No se pudo extraer Exec= del Geany real"
        fi
    else
        warn "No se encontró geany.desktop para inspección real"
    fi
fi

section "9. Prueba exacta de Ejecutar en terminal"
if [ "$DO_GUI" -eq 0 ]; then
    skip "Prueba de terminal desactivada con --no-gui"
elif [ -z "$BINARY" ] || [ ! -x "$BINARY" ]; then
    skip "No hay binario para probar Ejecutar en terminal"
elif [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ] && ! cmd_exists xvfb-run; then
    skip "No hay display ni xvfb-run para la prueba de terminal"
else
    TERM_TEST="$WORK_DIR/terminal-test"
    mkdir -p "$TERM_TEST/home" "$TERM_TEST/config" "$TERM_TEST/data" "$TERM_TEST/Choices" "$TERM_TEST/bin" "$TERM_TEST/scripts"
    TERM_WRAPPER="$TERM_TEST/bin/xterm"
    TERM_WRAPPER_LOG="$TERM_TEST/terminal-wrapper.log"
    TERM_RESULT_LOG="$TERM_TEST/script-results.log"
    cat > "$TERM_WRAPPER" <<'EOF_TERM_WRAPPER'
#!/bin/sh
{
  echo "TERMINAL_WRAPPER_INVOKED"
  printf 'TERMINAL_WRAPPER_ARG=%s\n' "$@"
} >> "$ROX_TERMINAL_WRAPPER_LOG"
case "${1:-}" in
  -e|-x|--execute|--) shift ;;
esac
"$@" </dev/null >> "$ROX_TERMINAL_WRAPPER_LOG" 2>&1
status=$?
echo "TERMINAL_WRAPPER_STATUS=$status" >> "$ROX_TERMINAL_WRAPPER_LOG"
exit "$status"
EOF_TERM_WRAPPER
    chmod +x "$TERM_WRAPPER"

    SCRIPT_EXEC="$TERM_TEST/scripts/shell ejecutable.sh"
    cat > "$SCRIPT_EXEC" <<'EOF_SCRIPT_EXEC'
#!/bin/sh
printf 'shell-executable-ok\n' >> "$ROX_TERMINAL_TEST_LOG"
EOF_SCRIPT_EXEC
    chmod +x "$SCRIPT_EXEC"

    SCRIPT_NOEXEC="$TERM_TEST/scripts/shell sin permiso.sh"
    cat > "$SCRIPT_NOEXEC" <<'EOF_SCRIPT_NOEXEC'
printf 'shell-noexec-ok\n' >> "$ROX_TERMINAL_TEST_LOG"
EOF_SCRIPT_NOEXEC
    chmod 0644 "$SCRIPT_NOEXEC"

    SCRIPT_BASH="$TERM_TEST/scripts/bash con shebang"
    cat > "$SCRIPT_BASH" <<'EOF_SCRIPT_BASH'
#!/usr/bin/env bash
value=(bash funciona)
printf '%s-%s-ok\n' "${value[0]}" "${value[1]}" >> "$ROX_TERMINAL_TEST_LOG"
EOF_SCRIPT_BASH
    chmod +x "$SCRIPT_BASH"

    SCRIPT_BASH_EXT="$TERM_TEST/scripts/bash sin shebang.bash"
    cat > "$SCRIPT_BASH_EXT" <<'EOF_SCRIPT_BASH_EXT'
value=(bash extension)
printf '%s-%s-ok\n' "${value[0]}" "${value[1]}" >> "$ROX_TERMINAL_TEST_LOG"
EOF_SCRIPT_BASH_EXT
    chmod 0644 "$SCRIPT_BASH_EXT"

    SCRIPT_ASH="$TERM_TEST/scripts/ash sin shebang.ash"
    cat > "$SCRIPT_ASH" <<'EOF_SCRIPT_ASH'
printf 'ash-extension-ok\n' >> "$ROX_TERMINAL_TEST_LOG"
EOF_SCRIPT_ASH
    chmod 0644 "$SCRIPT_ASH"

    SCRIPT_CRLF="$TERM_TEST/scripts/shebang-crlf"
    printf '#!/bin/sh\r\nprintf '\''shebang-crlf-ok\\n'\'' >> "$ROX_TERMINAL_TEST_LOG"\n' > "$SCRIPT_CRLF"
    chmod +x "$SCRIPT_CRLF"

    SCRIPT_PY="$TERM_TEST/scripts/python sin permiso.py"
    cat > "$SCRIPT_PY" <<'EOF_SCRIPT_PY'
import os
with open(os.environ["ROX_TERMINAL_TEST_LOG"], "a", encoding="utf-8") as out:
    out.write("python-noexec-ok\n")
EOF_SCRIPT_PY
    chmod 0644 "$SCRIPT_PY"


    SCRIPT_REL_DIR="$TERM_TEST/scripts/relativo"
    mkdir -p "$SCRIPT_REL_DIR"
    printf 'relative-data-ok\n' > "$SCRIPT_REL_DIR/dato-relativo.txt"
    SCRIPT_REL="$SCRIPT_REL_DIR/usa ruta relativa.sh"
    cat > "$SCRIPT_REL" <<'EOF_SCRIPT_REL'
#!/bin/sh
cat ./dato-relativo.txt >> "$ROX_TERMINAL_TEST_LOG"
EOF_SCRIPT_REL
    chmod +x "$SCRIPT_REL"

    SCRIPT_TEXT_EXEC="$TERM_TEST/scripts/shell ejecutable sin shebang"
    cat > "$SCRIPT_TEXT_EXEC" <<'EOF_SCRIPT_TEXT_EXEC'
value="text-executable"
printf '%s-ok\n' "$value" >> "$ROX_TERMINAL_TEST_LOG"
EOF_SCRIPT_TEXT_EXEC
    chmod +x "$SCRIPT_TEXT_EXEC"

    run_terminal_probe() {
        local script_path="$1" out_file="$2"
        if [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ]; then
            env ROX_DIAGNOSTIC=1 ROX_DIAGNOSTIC_TERMINAL="$TERM_WRAPPER" \
                ROX_TERMINAL_WRAPPER_LOG="$TERM_WRAPPER_LOG" ROX_TERMINAL_TEST_LOG="$TERM_RESULT_LOG" \
                HOME="$TERM_TEST/home" XDG_CONFIG_HOME="$TERM_TEST/config" XDG_DATA_HOME="$TERM_TEST/data" \
                CHOICESPATH="$TERM_TEST/Choices" \
                "$BINARY" --diagnose-terminal="$script_path" >"$out_file" 2>&1
        else
            xvfb-run -a -s '-screen 0 1024x768x24' bash -c \
                'env ROX_DIAGNOSTIC=1 ROX_DIAGNOSTIC_TERMINAL="$1" ROX_TERMINAL_WRAPPER_LOG="$2" ROX_TERMINAL_TEST_LOG="$3" HOME="$4" XDG_CONFIG_HOME="$5" XDG_DATA_HOME="$6" CHOICESPATH="$7" "$8" --diagnose-terminal="$9"' \
                _ "$TERM_WRAPPER" "$TERM_WRAPPER_LOG" "$TERM_RESULT_LOG" "$TERM_TEST/home" "$TERM_TEST/config" "$TERM_TEST/data" "$TERM_TEST/Choices" "$BINARY" "$script_path" >"$out_file" 2>&1
        fi
    }

    terminal_cases=0
    terminal_failures=0
    terminal_case_list=(
        "$SCRIPT_EXEC|shell-executable-ok"
        "$SCRIPT_NOEXEC|shell-noexec-ok"
        "$SCRIPT_BASH|bash-funciona-ok"
        "$SCRIPT_BASH_EXT|bash-extension-ok"
        "$SCRIPT_CRLF|shebang-crlf-ok"
        "$SCRIPT_PY|python-noexec-ok"
        "$SCRIPT_REL|relative-data-ok"
        "$SCRIPT_TEXT_EXEC|text-executable-ok"
    )
    if command -v ash >/dev/null 2>&1; then
        terminal_case_list+=("$SCRIPT_ASH|ash-extension-ok")
    else
        warn "Ash no está instalado; se omite la prueba .ash sin shebang"
    fi
    for case_data in "${terminal_case_list[@]}"; do
        script_path="${case_data%%|*}"
        marker="${case_data#*|}"
        terminal_cases=$((terminal_cases + 1))
        case_out="$TERM_TEST/case-$terminal_cases.out"
        : > "$TERM_WRAPPER_LOG"
        run_terminal_probe "$script_path" "$case_out"
        case_rc=$?
        for _i in $(seq 1 40 2>/dev/null || echo 1 2 3 4 5 6 7 8 9 10); do
            grep -F "$marker" "$TERM_RESULT_LOG" >/dev/null 2>&1 && break
            sleep 0.1
        done
        echo "--- terminal caso $terminal_cases: $script_path (rc=$case_rc) ---"
        cat "$case_out" 2>/dev/null || true
        cat "$TERM_WRAPPER_LOG" 2>/dev/null || true
        if [ "$case_rc" -eq 0 ] && grep -F "$marker" "$TERM_RESULT_LOG" >/dev/null 2>&1; then
            pass "Ejecutar en terminal funcionó: $(basename "$script_path")"
        else
            fail "Ejecutar en terminal falló: $(basename "$script_path")"
            terminal_failures=$((terminal_failures + 1))
        fi
    done
    echo "--- resultados acumulados de scripts ---"
    cat "$TERM_RESULT_LOG" 2>/dev/null || true
    if [ "$terminal_failures" -eq 0 ]; then
        pass "Todos los tipos de script probados se ejecutaron en terminal"
    fi

    echo "--- prueba con el terminal REAL configurado en ROX ---"
    REAL_TERM_SCRIPT="$TERM_TEST/scripts/prueba terminal real.sh"
    REAL_TERM_MARKER="$TERM_TEST/terminal-real.ok"
    cat > "$REAL_TERM_SCRIPT" <<'EOF_REAL_TERM_SCRIPT'
#!/bin/sh
pwd > "$ROX_REAL_TERMINAL_MARKER"
printf 'real-terminal-ok\n' >> "$ROX_REAL_TERMINAL_MARKER"
EOF_REAL_TERM_SCRIPT
    chmod +x "$REAL_TERM_SCRIPT"
    REAL_TERM_OUT="$TERM_TEST/real-terminal.out"
    rm -f "$REAL_TERM_MARKER"
    if cmd_exists xvfb-run; then
        xvfb-run -a -s '-screen 0 1024x768x24' bash -c '
            unset ROX_DIAGNOSTIC_TERMINAL
            export ROX_DIAGNOSTIC=1 ROX_REAL_TERMINAL_MARKER="$1"
            export HOME="$2" XDG_CONFIG_HOME="$3" XDG_DATA_HOME="$4" CHOICESPATH="$5"
            "$6" --diagnose-terminal="$7" >"$8" 2>&1
            for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do
                [ -s "$1" ] && exit 0
                sleep 0.2
            done
            exit 1
        ' _ "$REAL_TERM_MARKER" "$TERM_TEST/home" "$TERM_TEST/config" "$TERM_TEST/data" "$TERM_TEST/Choices" "$BINARY" "$REAL_TERM_SCRIPT" "$REAL_TERM_OUT"
        real_term_rc=$?
        cat "$REAL_TERM_OUT" 2>/dev/null || true
        cat "$REAL_TERM_MARKER" 2>/dev/null || true
        if [ "$real_term_rc" -eq 0 ] && grep -F 'real-terminal-ok' "$REAL_TERM_MARKER" >/dev/null 2>&1; then
            pass "El terminal real configurado por ROX pudo ejecutar un script"
        else
            fail "El terminal real configurado por ROX no ejecutó el script; revisar DIAG_TERMINAL_COMMAND arriba"
        fi
    else
        skip "No hay xvfb-run para probar el terminal real sin afectar la sesión"
    fi

    if [ "${#EXTRA_SCRIPTS[@]}" -gt 0 ]; then
        echo "--- pruebas solicitadas sobre scripts reales ---"
        real_index=0
        for requested_script in "${EXTRA_SCRIPTS[@]}"; do
            real_index=$((real_index + 1))
            if [ ! -f "$requested_script" ]; then
                fail "El script solicitado no existe: $requested_script"
                continue
            fi
            requested_script="$(readlink -f "$requested_script" 2>/dev/null || printf '%s' "$requested_script")"
            echo "--- script real $real_index: $requested_script ---"
            ls -l -- "$requested_script" 2>&1 || true
            file -- "$requested_script" 2>&1 || true
            echo "MIME xdg: $(xdg-mime query filetype "$requested_script" 2>/dev/null || true)"
            echo "Primeros bytes:"
            od -An -tx1 -N96 -- "$requested_script" 2>/dev/null | sed 's/^/  /' || true
            echo "Primera línea visible:"
            first_line="$(sed -n '1p' "$requested_script" 2>/dev/null | tr -d '\r')"
            printf '%s\n' "$first_line" | cat -v | sed 's/^/  /' || true
            case "$first_line" in
                '#!'*)
                    interpreter="${first_line#\#!}"
                    interpreter="${interpreter#${interpreter%%[![:space:]]*}}"
                    echo "Shebang interpretado: $interpreter"
                    interpreter_program="${interpreter%%[[:space:]]*}"
                    if [ "$(basename "$interpreter_program")" = env ]; then
                        rest="${interpreter#*[[:space:]]}"
                        rest="${rest#-S }"
                        interpreter_program="${rest%%[[:space:]]*}"
                    fi
                    if [[ "$interpreter_program" = /* ]]; then
                        [ -x "$interpreter_program" ] && pass "El intérprete existe: $interpreter_program" || fail "El intérprete del script no existe o no es ejecutable: $interpreter_program"
                    elif command -v "$interpreter_program" >/dev/null 2>&1; then
                        pass "El intérprete está en PATH: $interpreter_program"
                    else
                        fail "El intérprete del script no está en PATH: $interpreter_program"
                    fi
                    ;;
                *) warn "El script real no tiene #!; ROX dependerá de una extensión conocida" ;;
            esac

            real_out="$TERM_TEST/real-$real_index.out"
            : > "$TERM_WRAPPER_LOG"
            run_terminal_probe "$requested_script" "$real_out"
            real_rc=$?
            for _i in $(seq 1 50 2>/dev/null || echo 1 2 3 4 5 6 7 8 9 10); do
                grep -F 'TERMINAL_WRAPPER_STATUS=' "$TERM_WRAPPER_LOG" >/dev/null 2>&1 && break
                sleep 0.1
            done
            cat "$real_out" 2>/dev/null || true
            cat "$TERM_WRAPPER_LOG" 2>/dev/null || true
            child_status="$(sed -n 's/^TERMINAL_WRAPPER_STATUS=//p' "$TERM_WRAPPER_LOG" | tail -1)"
            if [ "$real_rc" -eq 0 ] && [ "$child_status" = "0" ]; then
                pass "El script real terminó correctamente en la prueba: $(basename "$requested_script")"
            elif [ "$real_rc" -eq 0 ] && [ -n "$child_status" ]; then
                warn "ROX y el terminal lo iniciaron, pero el script real terminó con código $child_status: $(basename "$requested_script")"
            else
                fail "No se pudo iniciar el script real mediante ROX: $(basename "$requested_script")"
            fi
        done
    fi
fi

section "10. Prueba del diálogo Renombrar"
if [ "$DO_GUI" -eq 0 ]; then
    skip "Prueba de Renombrar desactivada con --no-gui"
elif [ -z "$BINARY" ] || [ ! -x "$BINARY" ]; then
    skip "No hay binario para probar Renombrar"
elif [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ] && ! cmd_exists xvfb-run; then
    skip "No hay display ni xvfb-run para probar Renombrar"
else
    RENAME_TEST="$WORK_DIR/rename-test"
    mkdir -p "$RENAME_TEST"
    RENAME_FILE="$RENAME_TEST/archivo para renombrar.txt"
    printf 'rename test\n' > "$RENAME_FILE"
    if [ -n "$RENAME_TEST_FILE" ]; then
        if [ -e "$RENAME_TEST_FILE" ]; then
            RENAME_FILE="$(readlink -f "$RENAME_TEST_FILE" 2>/dev/null || printf '%s' "$RENAME_TEST_FILE")"
        else
            fail "El archivo indicado con --rename-file no existe: $RENAME_TEST_FILE"
        fi
    fi
    RENAME_OUT="$RENAME_TEST/rename.out"
    if [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ]; then
        env ROX_DIAGNOSTIC=1 "$BINARY" --diagnose-rename="$RENAME_FILE" >"$RENAME_OUT" 2>&1
        rename_rc=$?
    else
        xvfb-run -a -s '-screen 0 1024x768x24' env ROX_DIAGNOSTIC=1 \
            "$BINARY" --diagnose-rename="$RENAME_FILE" >"$RENAME_OUT" 2>&1
        rename_rc=$?
    fi
    echo "--- salida Renombrar (rc=$rename_rc) ---"
    cat "$RENAME_OUT" 2>/dev/null || true
    if [ "$rename_rc" -eq 0 ] && grep -F 'DIAG_RENAME_VISIBLE=1' "$RENAME_OUT" >/dev/null 2>&1 && \
       grep -F 'DIAG_RENAME_REALIZED=1' "$RENAME_OUT" >/dev/null 2>&1; then
        pass "El diálogo Renombrar se creó, realizó y presentó correctamente"
    else
        fail "El diálogo Renombrar no llegó a mostrarse correctamente"
    fi
fi

section "11. Prueba aislada de ROX-Filer --desktop"
if [ "$DO_GUI" -eq 0 ]; then
    skip "Prueba de escritorio desactivada con --no-gui"
elif [ -z "$BINARY" ] || [ ! -x "$BINARY" ]; then
    skip "No hay binario para probar el escritorio"
elif ! cmd_exists xvfb-run; then
    skip "xvfb-run no está instalado; no se toca el escritorio real"
else
    DESKTEST="$WORK_DIR/desktop-test.sh"
    cat > "$DESKTEST" <<'EOF_DESKTEST'
#!/bin/bash
set -u
BIN="$1"
OUTDIR="$2"
mkdir -p "$OUTDIR/home" "$OUTDIR/config" "$OUTDIR/data" "$OUTDIR/Choices"
export HOME="$OUTDIR/home"
export XDG_CONFIG_HOME="$OUTDIR/config"
export XDG_DATA_HOME="$OUTDIR/data"
export CHOICESPATH="$OUTDIR/Choices"

"$BIN" --desktop >"$OUTDIR/first.log" 2>&1 &
pid1=$!
sleep 3
if ! kill -0 "$pid1" 2>/dev/null; then
    echo "FIRST_DIED"
    cat "$OUTDIR/first.log"
    exit 21
fi
echo "FIRST_ALIVE pid=$pid1"
if command -v xprop >/dev/null 2>&1; then
    xprop -root _ROX_DESKTOP_WINDOW >"$OUTDIR/xprop.log" 2>&1 || true
    cat "$OUTDIR/xprop.log"
fi

start=$(date +%s)
"$BIN" --desktop >"$OUTDIR/second.log" 2>&1
second_rc=$?
end=$(date +%s)
elapsed=$((end-start))
echo "SECOND_RC=$second_rc ELAPSED=$elapsed"
cat "$OUTDIR/second.log"

if ! kill -0 "$pid1" 2>/dev/null; then
    echo "FIRST_DIED_AFTER_SECOND"
    exit 22
fi
if [ "$elapsed" -gt 5 ]; then
    echo "SECOND_DID_NOT_EXIT_QUICKLY"
    exit 23
fi

kill "$pid1" 2>/dev/null || true
wait "$pid1" 2>/dev/null || true
exit "$second_rc"
EOF_DESKTEST
    chmod +x "$DESKTEST"
    DESKOUT="$WORK_DIR/desktop-test"
    mkdir -p "$DESKOUT"
    echo "Ejecutando en Xvfb; no afecta el escritorio real..."
    if xvfb-run -a -s '-screen 0 1280x800x24' "$DESKTEST" "$BINARY" "$DESKOUT"; then
        pass "La primera instancia --desktop sobrevivió y la segunda terminó rápidamente"
    else
        rc=$?
        fail "La prueba aislada de --desktop falló (rc=$rc)"
    fi
    echo "--- primer escritorio ---"; cat "$DESKOUT/first.log" 2>/dev/null || true
    echo "--- segunda invocación ---"; cat "$DESKOUT/second.log" 2>/dev/null || true
    echo "--- propiedad X11 ---"; cat "$DESKOUT/xprop.log" 2>/dev/null || true
fi

section "12. Valgrind básico"
if [ -z "$BINARY" ] || [ ! -x "$BINARY" ]; then
    skip "No hay binario para Valgrind"
elif ! cmd_exists valgrind; then
    skip "Valgrind no está instalado"
else
    VGLOG="$WORK_DIR/valgrind.log"
    valgrind --quiet --error-exitcode=97 --leak-check=summary --log-file="$VGLOG" "$BINARY" --help >/dev/null 2>&1
    rc=$?
    cat "$VGLOG" 2>/dev/null || true
    if [ "$rc" -eq 0 ]; then pass "Valgrind no detectó errores en --help"; else warn "Valgrind devolvió rc=$rc en --help"; fi
fi

section "RESUMEN"
echo "OK:      $PASS_COUNT"
echo "AVISOS:  $WARN_COUNT"
echo "ERRORES: $FAIL_COUNT"
echo "OMITIDO: $SKIP_COUNT"
echo "LOG:     $LOG_FILE"

{
    echo "ROX-Filer diagnóstico $SCRIPT_VERSION"
    echo "Fecha: $(date -R 2>/dev/null || date)"
    echo "Binario: ${BINARY:-<ninguno>}"
    echo "Fuente: ${PROJECT_ROOT:-<ninguno>}"
    echo "OK: $PASS_COUNT"
    echo "Avisos: $WARN_COUNT"
    echo "Errores: $FAIL_COUNT"
    echo "Omitidos: $SKIP_COUNT"
    echo "Log: $LOG_FILE"
} > "$SUMMARY_FILE"

echo "RESUMEN: $SUMMARY_FILE"
if [ "$FAIL_COUNT" -gt 0 ]; then
    echo "Resultado general: SE ENCONTRARON ERRORES"
    exit 1
fi
echo "Resultado general: SIN ERRORES BLOQUEANTES"
exit 0
