#!/usr/bin/env bash
# Smoke-тест GUI: поднимаем GNO.exe по-настоящему и убеждаемся, что он
# стартует и живёт, а не падает на отсутствующей DLL или битом плагине.
#
# Зачем: unit-тесты не линкуют Qt и потому физически не могут поймать
# «приложение не запускается». Такой отказ ловится только реальным стартом.
# GNO.exe с ключом --smoke-test проходит обычный путь инициализации
# (проверка DLL -> stylesheet -> MainWindow -> SystemTray -> MonitoringService
# -> событийный цикл) и сам выходит через 1.5 с с кодом 0.
#
# Использование:
#   scripts/smoke-test.sh                 # build/bin/GNO.exe
#   scripts/smoke-test.sh --exe path.exe
#   BUILD_DIR=/c/other-build scripts/smoke-test.sh
#
# Коды возврата: 0 — успех, 1 — отказ, 2 — неверные аргументы/не Windows.

set -euo pipefail

BUILD_DIR="${BUILD_DIR:-/c/gno-build}"
EXE="${BUILD_DIR}/bin/GNO.exe"
TIMEOUT_SEC=60

while [[ $# -gt 0 ]]; do
    case "$1" in
        --exe)
            [[ $# -ge 2 ]] || { echo "--exe требует путь" >&2; exit 2; }
            EXE="$2"
            shift 2
            ;;
        --timeout)
            [[ $# -ge 2 ]] || { echo "--timeout требует секунды" >&2; exit 2; }
            TIMEOUT_SEC="$2"
            shift 2
            ;;
        -h|--help)
            sed -n '2,18p' "$0"
            exit 0
            ;;
        *)
            echo "Неизвестный аргумент: $1" >&2
            exit 2
            ;;
    esac
done

export PATH="/c/msys64/ucrt64/bin:$PATH"

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) ;;
    *) echo "smoke-test рассчитан на Windows (проверяет qwindows.dll)." >&2; exit 2 ;;
esac

fail() { echo "ОТКАЗ: $*" >&2; exit 1; }

[[ -f "$EXE" ]] || fail "нет $EXE — сначала соберите: ninja -C $BUILD_DIR"
command -v timeout >/dev/null 2>&1 || fail "нет утилиты timeout"

BIN_DIR="$(dirname "$EXE")"

# 1. Ровно тот минимум, который проверяет сам main_gui.cpp.
REQUIRED=("platforms/qwindows.dll" "Qt6Core.dll" "Qt6Gui.dll" "Qt6Widgets.dll")
for f in "${REQUIRED[@]}"; do
    [[ -f "$BIN_DIR/$f" ]] || fail "нет $BIN_DIR/$f — запустите scripts/deploy.sh"
    echo "  OK   $f"
done

# 2. Куда приложение пишет лог (см. core/logger.cpp): <APPDATA>/GNO/logs, а
#    если APPDATA не проброшен (служба, урезанное окружение, песочница) —
#    фолбэк в <TEMP>/GNO/logs. Ищем в обоих, берём самый свежий файл.
APPDATA_WIN="${APPDATA:-${USERPROFILE:-}/AppData/Roaming}"
APPDATA_DIR="$(cygpath -u "$APPDATA_WIN" 2>/dev/null || echo "$HOME/AppData/Roaming")/GNO/logs"
TEMP_DIR="$(cygpath -u "${TEMP:-${TMP:-/tmp}}" 2>/dev/null || echo /tmp)/GNO/logs"
LOG_DIRS=("$APPDATA_DIR" "$TEMP_DIR")

echo "==> запуск: $EXE --smoke-test (лимит ${TIMEOUT_SEC}s)"
RUN_STARTED=$(date +%s)

# 3. Собственно запуск. Модальных окон в этом режиме нет, поэтому зависание
#    возможно только по-настоящему — его и ловим по коду 124 от timeout.
set +e
timeout "$TIMEOUT_SEC" "$EXE" --smoke-test >/dev/null 2>&1
rc=$?
set -e

if [[ $rc -eq 124 ]]; then
    fail "процесс не завершился за ${TIMEOUT_SEC}s (зависание на старте)"
elif [[ $rc -ne 0 ]]; then
    fail "GNO.exe завершился с кодом $rc"
fi
echo "  OK   процесс стартовал и вышел с кодом 0"

# 4. Лог — доказательство, что мы дошли до событийного цикла, а не вышли раньше.
#    Берём файл, обновлённый не раньше старта прогона: иначе можно случайно
#    проверить лог от предыдущего запуска.
LATEST_LOG=""
for d in "${LOG_DIRS[@]}"; do
    [[ -d "$d" ]] || continue
    while IFS= read -r f; do
        [[ -n "$f" ]] || continue
        if [[ -z "$LATEST_LOG" || "$f" -nt "$LATEST_LOG" ]]; then
            LATEST_LOG="$f"
        fi
    done < <(find "$d" -maxdepth 1 -name 'gno-*.log' -newermt "@$((RUN_STARTED - 1))" 2>/dev/null)
done
if [[ -n "$LATEST_LOG" && -f "$LATEST_LOG" ]]; then
    echo "==> лог: $LATEST_LOG"
    grep -q "Entering event loop" "$LATEST_LOG" \
        || fail "в логе нет 'Entering event loop' — старт не дошёл до цикла"
    grep -q "Event loop finished" "$LATEST_LOG" \
        || fail "в логе нет 'Event loop finished' — выход не штатный"
    if grep -qE "MISSING!|FATAL" "$LATEST_LOG"; then
        grep -nE "MISSING!|FATAL" "$LATEST_LOG" >&2
        fail "в логе есть MISSING!/FATAL"
    fi
    echo "  OK   лог подтверждает полный цикл старта и выхода"
else
    echo "  ПРЕДУПРЕЖДЕНИЕ: свежий лог не найден (${LOG_DIRS[*]}) — проверка лога пропущена"
fi

echo "==> smoke-тест пройден"
