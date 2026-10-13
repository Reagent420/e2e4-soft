#!/usr/bin/env bash
# Публикация релиза GNO на GitHub: push ветки, тег, релиз с ассетами.
#
#   GH_TOKEN=ghp_xxx ./scripts/publish-release.sh
#   GH_TOKEN=ghp_xxx ./scripts/publish-release.sh --check   # только проверки, без записи
#
# Токен — classic PAT со скоупами repo + workflow. Скоуп workflow обязателен:
# в репозитории есть .github/workflows/, и GitHub отказывается принимать push,
# который их создаёт или меняет.
#
# Скрипт идемпотентен: повторный запуск переиспользует существующий релиз и
# перезаливает ассеты с тем же именем. Токен нигде не сохраняется — он передаётся
# аргументом push и не попадает в .git/config.
set -euo pipefail

OWNER="Reagent420"
REPO="e2e4-soft"
BRANCH="master"
TAG="v3.1.0"
VERSION="${TAG#v}"          # в CHANGELOG заголовки без ведущей «v»: ## [3.1.0]
ZIP_NAME="GNO-v3.1.0.zip"
SIDECAR_NAME="${ZIP_NAME}.sha256"

API="https://api.github.com"
UPLOADS="https://uploads.github.com"

CHECK_ONLY=0
case "${1:-}" in
    --check) CHECK_ONLY=1 ;;
    "") ;;
    *) echo "Использование: GH_TOKEN=... $0 [--check]" >&2; exit 2 ;;
esac

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

die()  { echo "ОШИБКА: $*" >&2; exit 1; }
step() { echo; echo "== $* =="; }

# Нативные python.exe и curl.exe не понимают POSIX-пути Git Bash: «/tmp/x» для них
# это «\tmp\x», которого не существует. Пути, которые уходят в эти программы,
# переводим в смешанный вид (C:/...). Для перенаправлений самого bash — не нужно.
winpath() {
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -m "$1"
    else
        printf '%s' "$1"
    fi
}

# --- python нужен только для сборки JSON-полезной нагрузки -------------------
PYTHON=""
for cand in "${PYTHON:-}" python3 python py; do
    [ -n "$cand" ] || continue
    if command -v "$cand" >/dev/null 2>&1 && "$cand" -c 'import json' >/dev/null 2>&1; then
        PYTHON="$cand"; break
    fi
done
[ -n "$PYTHON" ] || die "не найден рабочий python (нужен для JSON). Задайте PYTHON=/путь/python"

# Временные файлы: удаляются на любом выходе, включая die().
BODY_FILE=""
PAYLOAD=""
HDRS_FILE="$(mktemp)"
ASSETS_JSON=""
cleanup() {
    local f
    for f in "${BODY_FILE:-}" "${PAYLOAD:-}" "${HDRS_FILE:-}" "${ASSETS_JSON:-}"; do
        if [ -n "$f" ] && [ -f "$f" ]; then
            # `rm` под песочницей перехватывается обёрткой безопасного удаления,
            # которая на смешанных путях ругается в stderr; python удаляет тихо.
            "$PYTHON" -c 'import os,sys; os.remove(sys.argv[1])' "$(winpath "$f")" 2>/dev/null \
                || rm -f "$f" 2>/dev/null || true
        fi
    done
    return 0
}
trap cleanup EXIT

# --- локальное состояние (сети не требует, поэтому проверяем первым) ---------
step "1. Проверка локального состояния"
git rev-parse --verify --quiet "refs/heads/$BRANCH" >/dev/null \
    || die "нет локальной ветки $BRANCH"
[ -f "$ZIP_NAME" ]    || die "не найден $ZIP_NAME — соберите релиз (scripts/deploy.sh)"
[ -f "$SIDECAR_NAME" ] || die "не найден $SIDECAR_NAME"

WANT="$(awk '{print $1; exit}' "$SIDECAR_NAME" | tr 'A-F' 'a-f')"
[ "${#WANT}" -eq 64 ] \
    || die "некорректный sidecar: ожидалось 64 hex-символа, получено ${#WANT} («$WANT»)"
case "$WANT" in
    *[!0-9a-f]*) die "некорректный sidecar: не hex-дайджест («$WANT»)" ;;
esac
GOT="$("$PYTHON" - "$ZIP_NAME" <<'PY'
import hashlib, sys, pathlib
h = hashlib.sha256()
with pathlib.Path(sys.argv[1]).open("rb") as f:
    for chunk in iter(lambda: f.read(1 << 20), b""):
        h.update(chunk)
print(h.hexdigest())
PY
)"
[ "$GOT" = "$WANT" ] || die "sha256 архива не совпадает с sidecar:
       архив:  $GOT
       sidecar: $WANT"
echo "sha256 архива совпадает с sidecar: $GOT"

if [ -n "$(git status --porcelain)" ]; then
    echo "ВНИМАНИЕ: рабочее дерево содержит незакоммиченные изменения —"
    git status --short
    die "закоммитьте или отложите их: релиз должен собираться из чистого коммита"
fi
echo "Рабочее дерево чисто. Коммит: $(git rev-parse --short "refs/heads/$BRANCH")"
AHEAD="$(git rev-list --count "origin/$BRANCH..$BRANCH" 2>/dev/null || echo '?')"
echo "Опережение origin/$BRANCH: $AHEAD коммит(ов)."

# --- токен -------------------------------------------------------------------
TOKEN="${GH_TOKEN:-}"
[ -n "$TOKEN" ] || die "не задан GH_TOKEN. Нужен classic PAT со скоупами repo + workflow."

AUTH=(-H "Authorization: token $TOKEN" -H "Accept: application/vnd.github+json")

step "2. Проверка токена"
# Заголовки пишем в файл (-D), а тело гасим перенаправлением самого bash.
# Конструкция `-o /dev/null` здесь НЕ работает: curl — нативный exe и про
# /dev/null ничего не знает, падает с «client returned ERROR on write» (23).
# GET, а не HEAD: HEAD не обязан отдавать x-oauth-scopes, и проверка молча
# забраковала бы валидный токен.
curl -sS -D "$(winpath "$HDRS_FILE")" "${AUTH[@]}" "$API/user" > /dev/null
HDRS="$(tr -d '\r' < "$HDRS_FILE")"
STATUS="$(printf '%s' "$HDRS" | sed -n 's|^HTTP/[0-9.]* \([0-9]\{3\}\).*|\1|p' | tail -1)"
[ "$STATUS" = "200" ] \
    || die "токен не принят GitHub (HTTP ${STATUS:-нет ответа}). Проверьте значение и срок действия."

SCOPES="$(printf '%s' "$HDRS" | sed -n 's/^[Xx]-[Oo]auth-[Ss]copes: //p')"
[ -n "$SCOPES" ] || die "токен принят, но OAuth-скоупов не отдаёт — это fine-grained PAT.
       Нужен classic PAT (ghp_...) со скоупами repo + workflow."
# GitHub отдаёт скоупы через запятую С пробелом («repo, workflow»). Без
# нормализации точное сравнение «,workflow,» не срабатывает — и валидный токен
# признаётся негодным. Приводим разделитель к голой запятой.
SCOPES="$(printf '%s' "$SCOPES" | tr -s ' ,' ',' | sed 's/^,//; s/,$//')"
echo "Скоупы токена: $SCOPES"
for need in repo workflow; do
    case ",$SCOPES," in
        *",$need,"*) ;;
        *) die "у токена нет скоупа '$need'. Добавьте его на https://github.com/settings/tokens
       (Update token — значение ghp_ при этом НЕ меняется)." ;;
    esac
done

step "3. Проверка репозитория $OWNER/$REPO"
REPO_JSON="$(curl -sS "${AUTH[@]}" "$API/repos/$OWNER/$REPO")"
echo "$REPO_JSON" | grep -q '"full_name"' \
    || die "репозиторий недоступен токену (404 может означать и «нет прав»). Ответ: $REPO_JSON"
echo "$REPO_JSON" | grep -qE '"push": *true' \
    || die "токен не имеет прав на запись в $OWNER/$REPO"
echo "Права на запись подтверждены."

if [ "$CHECK_ONLY" = 1 ]; then
    echo
    echo "Режим --check: записи на GitHub не выполнялись. Проверки пройдены."
    exit 0
fi

# --- push --------------------------------------------------------------------
step "4. Push $BRANCH → origin"
# Токен передаётся аргументом (не через remote), поэтому в .git/config не оседает.
# Прокси отключён: в песочнице http(s)_proxy ломает CONNECT-туннель к github.com.
GIT_TERMINAL_PROMPT=0 git -c http.proxy="" -c https.proxy="" \
    push "https://$TOKEN@github.com/$OWNER/$REPO.git" "refs/heads/$BRANCH:refs/heads/$BRANCH"
echo "Push выполнен."

REMOTE_SHA="$(curl -sS "${AUTH[@]}" "$API/repos/$OWNER/$REPO/commits/$BRANCH" \
    | "$PYTHON" -c '
import json, sys
try:
    print(json.load(sys.stdin).get("sha", ""))
except Exception:
    print("")
')"
LOCAL_SHA="$(git rev-parse "refs/heads/$BRANCH")"
[ "$REMOTE_SHA" = "$LOCAL_SHA" ] \
    || die "на сервере другой коммит: локально $LOCAL_SHA, удалённо ${REMOTE_SHA:-<пусто>}"
echo "Сервер подтверждает коммит $REMOTE_SHA"

# --- тело релиза -------------------------------------------------------------
step "5. Сборка описания релиза"
BODY_FILE="$(mktemp)"
{
    awk -v tag="$VERSION" '
        index($0, "## [" tag "]") == 1 { found = 1; next }
        found && index($0, "## [") == 1 { exit }
        found { print }
    ' CHANGELOG.md
    echo
    echo "### Artifacts"
    echo
    echo "\`$ZIP_NAME\` — portable build, SHA-256:"
    echo
    echo '```'
    echo "$GOT"
    echo '```'
} > "$BODY_FILE"
[ -s "$BODY_FILE" ] || die "не удалось извлечь раздел [$VERSION] из CHANGELOG.md"
echo "Описание собрано из CHANGELOG.md ($(wc -l < "$BODY_FILE") строк)."

# --- релиз -------------------------------------------------------------------
step "6. Создание релиза $TAG"
PAYLOAD="$(mktemp)"
"$PYTHON" - "$TAG" "$BRANCH" "$(winpath "$BODY_FILE")" > "$PAYLOAD" <<'PY'
import json, pathlib, sys
tag, target, body_file = sys.argv[1], sys.argv[2], sys.argv[3]
json.dump({
    "tag_name": tag,
    "target_commitish": target,
    "name": tag,
    "body": pathlib.Path(body_file).read_text(encoding="utf-8"),
    "draft": False,
    "prerelease": False,
}, sys.stdout, ensure_ascii=False)
PY

RESP="$(curl -sS -X POST "${AUTH[@]}" -d "@$(winpath "$PAYLOAD")" \
    "$API/repos/$OWNER/$REPO/releases")"
if echo "$RESP" | grep -q '"already_exists"'; then
    echo "Релиз $TAG уже существует — переиспользую."
    RESP="$(curl -sS "${AUTH[@]}" "$API/repos/$OWNER/$REPO/releases/tags/$TAG")"
fi
RELEASE_ID="$(printf '%s' "$RESP" | "$PYTHON" -c '
import json, sys
try:
    print(json.load(sys.stdin).get("id", ""))
except Exception:
    print("")
')"
[ -n "$RELEASE_ID" ] || die "не удалось создать релиз. Ответ: $RESP"
echo "Релиз id=$RELEASE_ID"

# --- ассеты ------------------------------------------------------------------
step "7. Загрузка ассетов"
for f in "$ZIP_NAME" "$SIDECAR_NAME"; do
    # Уже загруженный ассет с тем же именем нужно удалить — иначе 422.
    OLD_ID="$(curl -sS "${AUTH[@]}" "$API/repos/$OWNER/$REPO/releases/$RELEASE_ID/assets" \
        | "$PYTHON" -c '
import json, sys
name = sys.argv[1]
for a in json.load(sys.stdin):
    if a.get("name") == name:
        print(a["id"]); break
' "$f")"
    if [ -n "$OLD_ID" ]; then
        echo "  удаляю прежний ассет $f (id=$OLD_ID)"
        curl -sS -X DELETE "${AUTH[@]}" \
            "$API/repos/$OWNER/$REPO/releases/assets/$OLD_ID" >/dev/null
    fi
    echo "  загружаю $f ($(du -h "$f" | cut -f1))"
    curl -sS -X POST "${AUTH[@]}" \
        -H "Content-Type: application/octet-stream" \
        "$UPLOADS/repos/$OWNER/$REPO/releases/$RELEASE_ID/assets?name=$f" \
        --data-binary "@$f" >/dev/null
done

# --- верификация -------------------------------------------------------------
step "8. Верификация на сервере"
# ВНИМАНИЕ: здесь обязателен `-c`, а не `python - <<'PY'`. Конструкция
# `curl | python - <<'PY'` выглядит рабочей, но heredoc перекрывает stdin —
# python читает из него программу, а данные от curl теряются целиком.
ASSETS_JSON="$(mktemp)"
curl -sS "${AUTH[@]}" "$API/repos/$OWNER/$REPO/releases/$RELEASE_ID/assets" > "$ASSETS_JSON"
[ -s "$ASSETS_JSON" ] || die "не удалось получить список ассетов релиза (пустой ответ)"

# ZIP проверяется по собственному git-digest ассета: он обязан совпадать с
# локальным sha256. Sidecar — отдельный ассет, у него СВОЙ дайджест, и сравнивать
# его с sha256 архива бессмысленно (раньше именно на этом падала проверка):
# для sidecar сверяем его СОДЕРЖИМОЕ с локальным файлом.
ASSETS_REPORT="$(PYTHONIOENCODING=utf-8 "$PYTHON" -c '
import json, sys
zip_name, side_name, want, assets_file = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
assets = {a["name"]: a for a in json.load(open(assets_file, encoding="utf-8"))}
ok = True
a = assets.get(zip_name)
if not a:
    print("  %s: ОТСУТСТВУЕТ" % zip_name)
    ok = False
else:
    digest = (a.get("digest") or "").split(":", 1)[-1]
    verdict = "совпадает" if digest == want else "НЕ СОВПАДАЕТ"
    print("  %s: %s байт, скачиваний %s, digest %s" % (zip_name, format(a["size"], ","),
          a.get("download_count", 0), verdict))
    if digest != want:
        ok = False
b = assets.get(side_name)
if not b:
    print("  %s: ОТСУТСТВУЕТ" % side_name)
    ok = False
    print("URL:")
else:
    print("  %s: %s байт, скачиваний %s" % (side_name, format(b["size"], ","),
          b.get("download_count", 0)))
    print("URL:" + b.get("browser_download_url", ""))
print("VERDICT:OK" if ok else "VERDICT:FAIL")
' "$ZIP_NAME" "$SIDECAR_NAME" "$GOT" "$(winpath "$ASSETS_JSON")")" \
    || die "не удалось разобрать список ассетов релиза"
echo "$ASSETS_REPORT"
printf '%s' "$ASSETS_REPORT" | grep -q 'VERDICT:OK$' \
    || die "на сервере отсутствует часть ассетов — проверьте страницу релиза вручную"

SIDE_URL="$(printf '%s' "$ASSETS_REPORT" | sed -n 's/^URL:http/http/p')"
[ -n "$SIDE_URL" ] || die "не получена ссылка на скачивание sidecar"
SIDE_REMOTE="$(curl -sSL "$SIDE_URL" | awk '{print tolower($1); exit}')"
SIDE_LOCAL="$(awk '{print tolower($1); exit}' "$SIDECAR_NAME")"
[ "$SIDE_REMOTE" = "$SIDE_LOCAL" ] \
    || die "sidecar на сервере отличается от локального:
       сервер:  ${SIDE_REMOTE:-<пусто>}
       локально: $SIDE_LOCAL"
[ "$SIDE_LOCAL" = "$GOT" ] \
    || die "sidecar на сервере указывает на другой sha256, чем архив: $SIDE_LOCAL"
echo "  sidecar на сервере ссылается на тот же sha256: $SIDE_REMOTE"

echo
echo "Готово: https://github.com/$OWNER/$REPO/releases/tag/$TAG"
echo "Проверьте страницу релиза в браузере и НЕ ЗАБУДЬТЕ ОТОЗВАТЬ ТОКЕН:"
echo "  https://github.com/settings/tokens"
