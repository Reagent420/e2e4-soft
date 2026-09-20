#!/usr/bin/env bash
# Сборка и упаковка GNO: windeployqt + проверка полноты набора.
#
# Зачем: main_gui.cpp при старте проверяет наличие platforms/qwindows.dll и
# Qt6*.dll рядом с исполняемым файлом и отказывается запускаться, если
# чего-то нет. Раньше набор собирали руками — и он тихо разъезжался с
# реальными зависимостями: приложение падало у пользователя, а не у нас.
#
# Использование:
#   scripts/deploy.sh                  # разложить DLL рядом с exe в build/bin
#   scripts/deploy.sh --package DIR    # дополнительно собрать чистый пакет
#   BUILD_DIR=/c/other-build scripts/deploy.sh
#
# Скрипт идемпотентен: повторный запуск просто обновляет набор.

set -euo pipefail

BUILD_DIR="${BUILD_DIR:-/c/gno-build}"
BIN_DIR="$BUILD_DIR/bin"
PACKAGE_DIR=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --package)
            [[ $# -ge 2 ]] || { echo "--package требует каталог" >&2; exit 2; }
            PACKAGE_DIR="$2"
            shift 2
            ;;
        -h|--help)
            sed -n '2,16p' "$0"
            exit 0
            ;;
        *)
            echo "Неизвестный аргумент: $1" >&2
            exit 2
            ;;
    esac
done

export PATH="/c/msys64/ucrt64/bin:$PATH"

if [[ ! -f "$BIN_DIR/GNO.exe" ]]; then
    echo "Нет $BIN_DIR/GNO.exe — сначала соберите: ninja -C $BUILD_DIR" >&2
    exit 1
fi

# windeployqt — нативный (не MSYS) exe, поэтому аргументы-пути нужно отдать
# ему в Windows-виде. Голый POSIX-путь "/c/gno-build/bin/GNO.exe" MSYS
# превращает в "\c\gno-build\bin\GNO.exe", и инструмент молча ничего не делает.
EXE_WIN="$(cygpath -w "$BIN_DIR/GNO.exe")"
DIR_WIN="$(cygpath -w "$BIN_DIR")"

echo "==> windeployqt -> $BIN_DIR"
# Замечание: предупреждение "Cannot open .../translations/catalogs.json" в
# MSYS2-сборке Qt безвредно — файла нет в дистрибутиве, а переводы нам и не
# нужны (--no-translations). Реальную ошибку ловим и по коду возврата, и по
# наличию обязательных файлов ниже.
if ! windeployqt --release --no-translations --no-system-d3d-compiler \
                 --no-opengl-sw --compiler-runtime \
                 --dir "$DIR_WIN" "$EXE_WIN"; then
    echo "windeployqt завершился с ошибкой." >&2
    exit 1
fi

# Обязательный минимум — ровно то, что проверяет main_gui.cpp.
REQUIRED=(
    "platforms/qwindows.dll"
    "Qt6Core.dll"
    "Qt6Gui.dll"
    "Qt6Widgets.dll"
)
missing=0
for f in "${REQUIRED[@]}"; do
    if [[ -f "$BIN_DIR/$f" ]]; then
        echo "  OK   $f"
    else
        echo "  НЕТ  $f" >&2
        missing=1
    fi
done
if [[ $missing -ne 0 ]]; then
    echo "Набор неполный — приложение не запустится." >&2
    exit 1
fi

if [[ -n "$PACKAGE_DIR" ]]; then
    echo "==> чистый пакет -> $PACKAGE_DIR"
    rm -rf "$PACKAGE_DIR"
    mkdir -p "$PACKAGE_DIR"

    cp "$BIN_DIR/GNO.exe" "$PACKAGE_DIR/"
    [[ -f "$BIN_DIR/GNO-console.exe" ]] && cp "$BIN_DIR/GNO-console.exe" "$PACKAGE_DIR/"

    # Всё, что разложил windeployqt: сами DLL и каталоги плагинов.
    find "$BIN_DIR" -maxdepth 1 -type f -name '*.dll' -exec cp {} "$PACKAGE_DIR/" \;
    for d in platforms styles imageformats iconengines networkinformation generic tls; do
        [[ -d "$BIN_DIR/$d" ]] && cp -r "$BIN_DIR/$d" "$PACKAGE_DIR/"
    done

    echo "  файлов: $(find "$PACKAGE_DIR" -type f | wc -l | tr -d ' '), размер: $(du -sh "$PACKAGE_DIR" | cut -f1)"
fi

echo "==> готово, $BIN_DIR: $(du -sh "$BIN_DIR" | cut -f1)"
