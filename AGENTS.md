# GNO — Engineering rules for AI coding agents

Этот файл обязателен к прочтению любой AI-моделью (Claude/Codex/Cursor) ПЕРЕД изменением кода в этом репозитории (`gno-native`).

## Продуктовые запреты (нарушение = отказывать в изменении)
1. Никаких обещаний FPS/ping в UI-текстах и комментах («boost +N FPS», «гарантированно снизит пинг»). Формулировки — качественные + confidence.
2. Никаких новых «магических» кнопок: всё, что меняет систему, проходит через remediation-движок (см. ниже).
3. Не показывать Health Score числом при confidence < 0.4 (`core/health_score.h` делает это сам — UI обязан уважать `hidden_by_confidence`).
4. «Лучший сервер» — только через `core/server_confidence.h` (`confidentBetter`), никогда по одному пингу.

## Обязательный протокол перед КАЖДЫМ изменением кода
1. Найти существующую реализацию (grep по домену: `rg -i "ping|dns|registry|profile"` и т.п.).
2. Определить зависимости (кто вызывает: grep по символам + `CMakeLists.txt`).
3. Проверить тесты (`tests/unit_tests.cpp`, doctest, 50+ кейсов).
4. Определить API/интерфейсы модуля (заголовки).
5. Убедиться, что код не используется другими модулями в обход intended path.
6. Только после этого изменять.

## Архитектурные инварианты
- UI НЕ меняет реестр/сеть/систему напрямую. Путь: `UI → BoostGate/RemediationService → FixTransaction → WindowsStateApi → OS`.
- Все системные мутации — только `src/remediation/` (allowlist `ActionId`, backup→apply→verify→auto-rollback).
  Новый тип изменения = новый `ActionId` + ветки в `windows_fix_action.cpp` + `windows_state_api.cpp` + тест.
  Прямые `RegSetValueExA`/`system("reg add")`/`netsh` вне `src/remediation/` запрещены.
- Не дублировать: diagnostic-логика (пинги/DNS/traceroute — `src/monitoring/`, `src/core/dns_manager|route_analyzer`), game detection (`game_detector`), оптимизации (`remediation` + `tweak_registry`).
- Pure-логика (скоры, модели, оценки) — заголовки без Qt в `src/core/` (образцы: `health_score.h`, `server_map_model.h`, `server_confidence.h`) + doctest-кейсы. UI — только `src/ui/`.
- Тексты интерфейса — русские литералы (`QString::fromUtf8`), единого словаря нет.
  Селектор языка сохраняет `SettingsKeys::Language()`, но **никем не читается** — локализация
  не реализована. Прежний `core/i18n.h` удалён как мёртвый: не вызывался нигде и писал в
  рассинхронизированный ключ `app/language`. Появится реальная локализация — заводить движок
  с ключами через `SettingsKeys`, а не восстанавливать этот заголовок.
- Каждая фича-кнопка обязана иметь объяснение (plain_language) и откат, если что-то меняет.

## Стиль
- C++20, existing style (4 spaces, `camel_case_` members with trailing underscore — см. соседние файлы).
- Коммит-сообщения: conventional (`feat:`, `fix:`, `refactor:`, `test:`).
- После изменений: прогнать тесты (`GNO-tests.exe` или изолированный doctest-прогон для pure-заголовков).

## Что делать с найденным в обход правил
Если встречаешь legacy-код, пишущий в систему в обход движка (например `optimization/fps_optimizer*.cpp` всё ещё содержит прямые RegSetValue, но вызовы из UI вычищены) — НЕ расширять этот путь; мигрировать на ActionId и помечать в PR.
