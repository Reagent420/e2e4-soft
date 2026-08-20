# GNO Windows Remediation — статус и TODO

Последнее обновление: 21 августа 2026 года.

## Где находится работа

- Репозиторий: `https://github.com/Reagent420/e2e4-soft`
- Ветка: `feature/diagnostic-foundation`
- Локальный worktree автора: `/Users/d.fedorenko/orca/e2e4-soft/.worktrees/diagnostic-foundation`
- Последний функциональный commit: `b501ce694a201fcf52e30634215e6e9848bcf14e`
- `origin/master` v1.4.0 целиком не вливался; старые direct mutators не переносились.

Подробные требования и история выполнения:

- `docs/superpowers/plans/2026-08-20-safe-remediation-actions.md`
- `docs/superpowers/specs/2026-08-20-safe-remediation-actions-design.md`
- `.superpowers/sdd/progress.md`

## Что уже сделано

### Диагностическая основа

- Переносимые сборки и регистрация тестов.
- Ограниченная валидация входных данных и structured persistence.
- Управляемые фоновые workers без detached-потоков и зависаний при остановке.
- Диагностические contracts и truthful `Unsupported` на неподдерживаемых платформах.
- Базовые CI/build gates против старых auto-fix и generic mutation paths.

### Safe remediation — Tasks 1–4

- Закрыты safety prerequisites диагностического слоя.
- Добавлены typed remediation contracts и `FixTransaction`.
- Добавлен durable `JsonBackupStore`: bounded/versioned JSON, atomic replacement,
  строгая валидация состояния и сохранение rollback-информации.
- Реализованы ровно семь allowlisted Windows actions:
  1. активный power plan;
  2. Game DVR;
  3. fullscreen optimizations выбранного executable;
  4. allowlisted TCP parameter `InitialRttData`;
  5. DNS выбранного интерфейса;
  6. MTU выбранного интерфейса;
  7. priority class выбранного живого процесса.
- Для actions есть typed target/value, validate, observe, stale preflight, apply,
  независимый verify и exact rollback.
- Интерфейс повторно идентифицируется по GUID + LUID.
- Процесс повторно идентифицируется по PID + creation time + executable path;
  Realtime priority запрещён.
- Game DVR сохраняет оба allowlisted DWORD и исходное наличие registry keys.
- Ошибка записи или post-write verification запускает компенсацию к exact before-state.
- На macOS/Linux фабрика возвращает семь typed `Unsupported`; macOS mutation-кода нет.
- Ни один обычный тест не изменяет host system.

Последняя проверка после Task 4:

- focused `GNO-UnitTests`: `1/1 passed`;
- полный CTest: `11/11 passed`;
- `git diff --check`: без ошибок;
- Windows CI и реальные Win32 mutation tests пока не запускались.

## Что осталось сделать

### FED-360 — Elevated UUID-only Windows helper

- Helper принимает только канонический UUID подготовленной транзакции.
- Загружает запись из доверенного `JsonBackupStore` и строго проверяет schema,
  state и allowlisted actions.
- Повторяет fresh observe/staleness check непосредственно перед mutation.
- Добавляет interprocess single-flight, durable transitions, cancellation,
  partial-failure и rollback handling.
- Не принимает command/action/target/value, JSON через stdin или произвольный path.

### FED-362 — Remediation UI

- Семь строк действий с current/proposed state, риском и отдельной кнопкой Fix.
- Детерминированный Fix all только из доступных allowlisted actions.
- Полный preview и явное подтверждение до запуска helper.
- Progress/cancel без ложного success, restart recovery и явный rollback.
- Защита от duplicate click/reentrancy.
- Явный выбор executable/process для соответствующих действий.

### FED-363 — Windows CI, release gates и безопасный пилот

- MSVC x64 Debug/Release для console, GUI и helper.
- Windows unit/integration/offscreen tests, включая Win32 durability/locking branches.
- Проверка UAC manifest: GUI не elevated, elevation только у helper.
- Artifact gate против legacy mutators, auto-fix, kill-process paths и tracked binaries.
- Test-only package, checksum, инструкции удаления и аварийного rollback.
- Контролируемый пилот: baseline, действия по одному, verify/замеры, rollback,
  затем Fix all только после успешных одиночных действий.

### FED-364 — Финальный security review

- Проверить весь путь UI input → transaction → durable store → UUID helper →
  Win32 mutation → verify → rollback.
- Проверить privilege boundary, injection, registry scope, TOCTOU, reparse points,
  interprocess races, crash recovery и backup tampering.
- Сверить ровно семь actions со spec и проверить отсутствие direct-mutator обходов.
- Разрешить test-only release только при отсутствии Critical/Important findings.

## Отложено и не входит в текущий Windows scope

- macOS remediation actions, helper, UI и packaging;
- публичный release, installer, signing и notarization;
- VPN/traffic tunnelling;
- массовый merge `origin/master` v1.4.0.

## Риски, которые ещё должен закрыть Windows-host

- Компиляция и линковка с реальным Windows SDK.
- Доступность новых per-interface DNS APIs на поддерживаемых версиях Windows.
- UAC/ACL для HKLM, power, network и process APIs.
- GUID/LUID, PID reuse, executable replacement и другие реальные race conditions.
- Registry rollback и crash recovery при частичных Win32-сбоях.
- Фактический системный эффект и обратимость TCP, Game DVR и fullscreen settings.

## Как забрать ветку

Новый clone:

```bash
git clone https://github.com/Reagent420/e2e4-soft.git
cd e2e4-soft
git fetch origin
git switch --track origin/feature/diagnostic-foundation
```

Если репозиторий уже клонирован, но локальной ветки ещё нет:

```bash
git fetch origin
git switch --track origin/feature/diagnostic-foundation
```

Если локальная ветка уже существует:

```bash
git switch feature/diagnostic-foundation
git pull --ff-only
```

Для отдельного worktree из существующего clone:

```bash
git fetch origin
git worktree add -b feature/diagnostic-foundation \
  .worktrees/diagnostic-foundation origin/feature/diagnostic-foundation
```

## С чего продолжать

Следующая задача — `FED-360`. Перед изменениями полностью прочитать plan, spec,
ledger, описание FED-360 и функциональный commit `b501ce6`. Использовать готовые
`FixTransaction`, `JsonBackupStore`, `WindowsStateApi` и семь actions; не начинать UI,
installer, CI/pilot или macOS раньше соответствующих отдельных задач.
