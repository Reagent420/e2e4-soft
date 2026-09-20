# Промпты для большого обновления E2E4 Soft (gno-native)

Готовые самодостаточные промпты для новых сессий. Каждый можно вставить в новый
чат без пояснений — в нём уже зашиты пути, ритуал сборки/проверки и соглашения
проекта. Сначала идёт «мастер-промпт» (полный контекст), потом варианты A–F.

---

## Мастер-промпт (общий контекст для любого варианта)

```
Ты работаешь над проектом gno-native — настольным приложением «E2E4 Soft —
Оптимизатор игровой сети» (Qt 6.11.1, MinGW GCC ucrt64, CMake + Ninja,
статическая линковка). Версия продукта — 3.0.0 (см. src/ui/theme.h,
theme::APP_VERSION). Продукт НЕ строит релеи/VPN — сетевые оптимизации
локальные, поэтому метрики «клиент→релей» намеренно нулевые.

Исходники: C:\NEW\дистр\DISTR 2\pi-chat\gno-native (компилятор видит их как
C:/gno/src — точка монтирования MSYS). Сборка: C:\gno-build.

Ритуал проверки любой правки (ОБЯЗАТЕЛЕН, без него работа не засчитана):
  export PATH="/c/msys64/ucrt64/bin:$PATH" && cd /c/gno-build && ninja
  ./bin/GNO-tests.exe            # сейчас 117 кейсов / 647 утверждений
  # запуск GUI: python os.startfile('C:\\gno-build\\bin\\GNO.exe') +
  # tasklist + taskkill /IM GNO.exe /F (PowerShell-тула иногда не отдаёт stdout)

Таргеты CMake: GNO (GUI, src/main_gui.cpp), GNO-console (src/main_console.cpp),
GNO-tests (tests/unit_tests.cpp). src/main.cpp УДАЛЁН — он был мёртвым
дубликатом main_gui.cpp и не компилировался.

Известные ловушки проекта:
  - Два разных Result-типа: gno::remediation::Result<T> имеет МЕТОД .value(),
    а gno::DiagnosticResult<T> имеет ПОЛЕ .value. Перепутать = ошибка сборки.
  - MSYS искажает пути: tools получай Windows-стиль (C:\... или C:/...).
  - В песочнице ICMP-прокси отвечает даже на TEST-NET (192.0.2.0); для теста
    таймаутов используй 240.0.0.1 — он стабильно reply=0.
  - Пороги диагностики едины для всего продукта: 80 мс задержка, 2% потерь,
    15 мс джиттер (см. plain_language.cpp).
  - Паттерн тестирования: блокирующие операции (ICMP, реестр, powercfg)
    инжектируются через std::function-пробу (см. MultipathEngine::setPathProbe,
    DiagnosticsService::Dependencies) — тесты идут без сети и без админ-прав.

Прошлое аудитом уже исправлено: core/ (multipath_engine, network_utils,
game_detector, problem_db, sqlite_history_store), diagnostics/ (реализован
весь слой: EndpointObserver, NetworkSampler, ProbeClient, DiagnosticsService),
optimization/ (fps_optimizer), monitoring/ (packet_loss_monitor,
stats_collector, ping_monitor).

Отвечай на русском, если вопрос на русском. После завершения substantive-работы
дописывай запись в C:\NEW\дистр\DISTR 2\pi-chat\.workbuddy-ai\memory\2026-MM-DD.md
(append-only) и показывай результат через present_files.

Задача: <ВСТАВИТЬ ВАРИАНТ A–F ИЛИ СВОЙ ОБЪЁМ>
```

---

## Вариант A — Завершить баг-аудит (продолжение текущей работы)

```
Задача: продолжить систематический баг-аудит. Осталось:
  1. src/remediation/ (8 файлов): boost_gate, remediation_service, fix_transaction,
     backup_store, legacy_bridge, target_discovery, windows_fix_action,
     windows_state_api. Это критический путь — транзакционный движок отката
     системных настроек; баги здесь = испорченная ОС пользователя.
  2. src/ui/ (21 файл): dashboard, monitoring, overlay, system_tray,
     settings_page, theme_editor, geo_map, report_export, diagnostics_widget,
     knowledge_widget и др.
  3. Остаток src/core/: capability_matrix, connection_grader, plain_language,
     profile_engine, alert_thresholds, health_score, server_confidence,
     json_persistence, autopilot_plan, net_utils, network_stats,
     session_history, launch_diagnostics, system_audit, speed_test,
     dns_manager, process_monitor, game_watcher, game_profiles.

Правила аудита: читай файл целиком; классифицируй находки как
баг / мёртвый код / упущение; чини только реальные дефекты (небезопасная
память, гонки, проглатывание ошибок, мёртвые параметры, некорректная
математика, зависящие от платформы падения); НЕ меняй продуктовое поведение
без причины; на каждую серию правок добавляй регрессионные тесты;
собирай и гоняй тесты после каждой группы правок.
Финальный отчёт: таблица дефект → исправление → файл.
```

---

## Вариант B — Интеграция наработок в интерфейс (мост UI)

```
Задача: диностики и мультипуть уже реализованы и протестированы, но GUI их не
использует. Нужно провести их в интерфейс:
  1. src/ui/diagnostics_widget.h — запустить gno::DiagnosticsService по кнопке
     с отменой (CancellationToken) и отобразить DiagnosticReport
     (outcome, confidence, evidence) на русском через plain_language.
  2. MultipathEngine — добавить в monitoring/diagnostics вкладку: список путей
     (PathMetrics), подсветка активного, переключатель auto_switch,
     уведомление о переключении пути. Запуск/останов из консоли уже есть
     (--multipath) — перенести ту же модель в GUI.
  3. Интегрировать рекомендации диагностики в MonitoringService::
     recommendationAvailable (сейчас источник только history_).
Каждый шаг покрыть unit-тестами с инжектированными пробами; сборка + тесты
обязательны. Не нарушать существующие сигнальные связи в main_gui.cpp.
```

---

## Вариант C — Релиз 3.1: упаковка и стабильность

```
Задача: подготовить релиз 3.1.0:
  1. windeployqt + пакет в C:\gno-build\bin (плагин platforms/qwindows.dll,
     Qt6*.dll — main_gui.cpp уже проверяет их наличие, нужно убедиться, что
     полный набор попадает в дистрибутив).
  2. Единый лог-файл сессии (сейчас fprintf в stderr) — ротация, путь в
     QStandardPaths::AppDataLocation.
  3. Авто-обновление: проверка версии с GitHub-релиза, докачка, контроль
     подписи/хеша.
  4. Интеграционный smoke-тест запуска GNO.exe в CI-подобном скрипте.
  5. Обновить theme::APP_VERSION 3.0.0 → 3.1.0 и тексты в problem_db/
     plain_language.
Без изменения архитектуры. Любая правка — с пересборкой и полным прогоном
тестов.
```

---

## Вариант D — Развитие функционала

```
Задача: новые возможности для следующего крупного обновления:
  1. geo_map — реальная карта задержек до игровых серверов (источники:
     server_map_model.cpp, server_confidence.cpp); нарисовать SVG/Qt-карту
     с регионами и цветами по health_score.
  2. report_export — экспорт DiagnosticReport + SessionHistory в
     PDF/HTML/Markdown (использовать существующий report_exporter).
  3. theme_editor — визуальный редактор палитры (theme.h Colors/LightColors)
     с предпросмотром и сохранением в QSettings.
  4. База знаний knowledge_widget — связать с problem_db и выдавать советы
     по текущей диагностике.
Каждую фичу делать за отдельным проходом: дизайн → реализация → тесты →
ручной запуск GUI. Учитывать, что продукт не строит релеи — только локальные
оптимизации и диагностика.
```

---

## Вариант E — Архитектурный рефакторинг

```
Задача: убрать дублирование и привести архитектуру в порядок:
  1. Две параллельные системы оптимизации: legacy FPSOptimizer
     (optimization/fps_optimizer) и транзакционный remediation::BoostGate.
     GUI уже на BoostGate; console --game использует оба. Унифицировать:
     FPSOptimizer должен стать тонкой обёрткой над BoostGate или быть удалён.
  2. Унифицировать Result-типы (remediation::Result<T> vs DiagnosticResult<T>)
     — либо один шаблон, либо чёткая документация границы применения.
  3. Единый конфиг приложения (QSettings + settings_keys.cpp) для всех
     порогов и путей; убрать разбросанные хардкоды.
  4. Вынести blocking-вызовы (ICMP, реестр, powercfg) за интерфейсы с
     инжекцией, как уже сделано в diagnostics и MultipathEngine.
Рефакторинг безопасный: поведение сохраняется, тесты не ломаются, каждый
шаг — отдельный коммит-подход с пересборкой.
```

---

## Вариант F — Тестовое покрытие и качество

```
Задача: поднять качество тестов с текущего уровня (117 кейсов / 647 assertions):
  1. Покрытие: измерить, какие .cpp из src/ вообще не тестируются
     (особенно remediation/ и ui/), и закрыть базовыми кейсами.
  2. Интеграционные тесты: BoostGate apply/revert на тестовом каталоге
     backup_store, DiagnosticsService полный прогон, MultipathEngine
     со сменой путей.
  3. Фаззинг-тесты парсеров: Ipv4Address::parse, endpoint_observer
     splitEndpoint, json_persistence, sqlite_history_store.
  4. Тесты на гонки: ping_monitor/packet_loss_monitor start/stop под
     нагрузкой, ThreadSanitizer-сборка в отдельном build-каталоге.
  5. Отчёт: что покрыто, что нет, что намеренно не тестируется
     (платформозависимые вызовы).
```

---

## Сравнение вариантов

| Вариант | Что даёт | Риск | Объём | Зависимости |
|---|---|---|---|---|
| **A** Аудит | ноль известных багов в оставшихся модулях; база для всего остального | низкий (привычный ритм) | средний | нет |
| **B** UI-интеграция | наработки diagnostics + multipath доходят до пользователя | средний (Qt-сигналы, main_gui) | средний | желательно A для ui/ |
| **C** Релиз 3.1 | распространяемый продукт, логи, обновление | низкий-средний | средний | желательно завершить A |
| **D** Функционал | новые «продающие» экраны (карта, экспорт, темы) | средний (новый код + UI) | большой | B (виджеты есть) |
| **E** Рефакторинг | меньше дублирования, проще поддержка | средний (легко сломать поведение) | большой | желательно A |
| **F** Тесты | уверенность в регрессиях, CI-готовность | низкий | средний | нет |

Рекомендованный порядок: **A → C → B → D**, с F параллельно каждому этапу.
E — когда станет ясно, что legacy-слой (FPSOptimizer) больше никому не нужен.
