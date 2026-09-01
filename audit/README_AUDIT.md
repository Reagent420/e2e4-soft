# GNO (E2E4 Soft) — Полный аудит кодовой базы
# Дата: $(Get-Date -Format "yyyy-MM-dd")

## 1. ПОЛНЫЙ СПИСОК ИЗМЕНЕНИЙ РЕЕСТРА

GNO изменяет реестр через 3 механизма:
- `system_tweaks.cpp` — 11 настроек через RegSetValueExA
- `tweak_registry.h` — ~30 declarative tweaks
- `windows_state_api.cpp` — динамические настройки (DNS, MTU, Game DVR, etc.)

### 1.1 Сеть (Tcpip\Parameters)

| Параметр | Значение | Hive | Безопасность |
|----------|----------|------|-------------|
| TcpAckFrequency | 1 | HKLM | ⚠️ Wi-Fi может лагать |
| TcpNoDelay | 1 (Nagle off) | HKLM | ⚠️ Увеличивает трафик |
| TcpInitialRtt | 5000 | HKLM | ✅ Безопасно |
| DefaultTTL | 64 | HKLM | ✅ Стандарт |
| Tcp1323Opts | 1 | HKLM | ✅ TCP timestamps |
| TcpAutotuning | 1 | HKLM | ✅ Автонастройка окна |

### 1.2 MMCSS (Multimedia Scheduler)

| Параметр | Значение | Безопасность |
|----------|----------|-------------|
| NetworkThrottlingIndex | 0xFFFFFFFF | ✅ Отключает троттлинг |
| SystemResponsiveness | 0 | ⚠️ Все ядра игре |
| GPU Priority (Games) | 8 | ✅ Приоритет GPU |
| Priority (Games) | 6 | ✅ Приоритет CPU |

### 1.3 Game DVR / Game Mode

| Параметр | Значение | Безопасность |
|----------|----------|-------------|
| GameDVR_Enabled | 0 | ✅ Отключает запись |
| AppCaptureEnabled | 0 | ✅ Отключает захват |
| AllowAutoGameMode | 0 | ✅ Отключает авто-режим |
| GameDVR_HonorUserFSEBehaviorMode | 1 | ✅ Полноэкранный режим |

### 1.4 Приватность / Телеметрия

| Параметр | Значение | Безопасность |
|----------|----------|-------------|
| AllowTelemetry | 0 | ✅ Отключает телеметрию |
| AdvertisingInfo\Enabled | 0 | ✅ Рекламный ID выкл |
| RestrictImplicitInkCollection | 1 | ✅ Ограничивает ввод |
| DisableUAR | 1 | ✅ Steps Recorder off |
| UploadUserActivities | 0 | ✅ Timeline off |
| BingSearchEnabled | 0 | ✅ Bing off |
| CortanaConsent | 0 | ✅ Cortana off |
| SubscribedContent-338389Enabled | 0 | ✅ Tips off |
| SilentInstalledAppsEnabled | 0 | ✅ Нет рекламных apps |
| RotatingLockScreenOverlayEnabled | 0 | ✅ Реклама на lock screen off |
| TailoredExperiencesWithDiagnosticDataEnabled | 0 | ✅ Персонализация off |

### 1.5 Эффекты / Производительность

| Параметр | Значение | Безопасность |
|----------|----------|-------------|
| VisualFXSetting | 2 | ✅ Best Performance |
| ListviewAlphaSelect | 0 | ✅ Убирает прозрачность |
| ListviewShadow | 0 | ✅ Убирает тени |
| TaskbarAnimations | 0 | ✅ Убирает анимации |
| MenuShowDelay | "0" | ✅ Мгновенное меню |
| MinAnimate | "0" | ✅ Без анимации сворачивания |
| OverlayTestMode | 5 | ⚠️ Multi-Plane Overlay off |
| GlobalUserDisabled | 1 | ⚠️ Фоновые UWP apps off |

### 1.6 Питание

| Параметр | Значение | Безопасность |
|----------|----------|-------------|
| HibernateEnabled | 0 | ✅ Отключает гибернацию |
| DisableSelectiveSuspend | 1 | ✅ USB не засыпает |
| PowerThrottlingOff | 1 | ⚠️ Ядра не троттлятся |

### 1.7 Другое

| Параметр | Значение | Безопасность |
|----------|----------|-------------|
| Win32PrioritySeparation | 38 | ⚠️ Требует перезагрузки |
| HwSchMode | 2 | ⚠️ HAGS включён |
| StartupDelayInMSec | 0 | ✅ Без задержки старта |
| NoDriveTypeAutoRun | 255 | ✅ AutoPlay off |
| NoAutoUpdate | 1 | ⚠️ Auto Update off |
| LargeSystemCache | 0 | ✅ Оптимизация памяти |
| NoLockScreen | 1 | ✅ Без lock screen |
| MenuShowDelay | "0" | ✅ Мгновенное меню |

---

## 2. ТРАНЗАКЦИОННЫЙ ДВИЖОК

### Как работает (из кода):

```
FixTransaction::prepare()
  → observe() каждого action (читает текущее состояние)
  → prepare() создаёт PreparedAction (before + proposed)
  → persist() → JsonBackupStore::save() → JSON файл

FixTransaction::execute()
  → freshness check (состояние не изменилось с момента prepare)
  → apply() каждого action
  → observe() после apply (verify)
  → comparison: verified == proposed
  → persist(record)

FixTransaction::rollback()
  → freshness check (applied state не изменился)
  → rollback() в обратном порядке
  → verify: rolled_back == before
  → persist(record)
```

### Где хранятся бэкапы:
- `JsonBackupStore` — JSON файлы в директории (передаётся в конструктор)
- Содержит: transaction_id, prepared_actions, outcomes, action_order
- Формат: `{transaction_id}.json`

### Типы значений ActionValue:
- DnsValue (primary, secondary servers)
- MtuValue (bytes)
- TcpValue (InitialRetransmissionTimeout)
- PowerPlanValue (GUID)
- GameDvrValue (GameDVR_Enabled, AppCaptureEnabled)
- FullscreenValue (flags, compatibility)
- PriorityLevel (Normal, AboveNormal, High)
- Cs2MaxPingValue (max_ping)
- MouseAccelValue (speed, thresholds)

---

## 3. DNS / СЕТЕВАЯ ДИАГНОСТИКА

### Как пингует:
- ICMP через IcmpSendEcho (WinAPI)
- 3 пробы на сервер, timeout 2000мс
- Интервал 200мс между пингами

### Какие серверы пингует:
- Cloudflare: 1.1.1.1
- Google: 8.8.8.8
- Quad9: 9.9.9.9
- OpenDNS: 208.67.222.222
- Yandex: 77.88.8.8

### Quality Score (connection_grader.cpp):
- Ping: 40% веса
- Jitter: 30% веса
- Loss: 30% веса
- Оценки: A+ (≥95), A (≥85), B (≥70), C (≥55), D (≥40), F (<40)

### Шкала:
- Ping ≤20ms → 100, ≤40 → 90, ≤60 → 80, ≤80 → 70, ≤100 → 60, ≤150 → 40, ≤200 → 20, >200 → 0
- Jitter ≤5ms → 100, ≤10 → 90, ≤20 → 75, ≤30 → 60, ≤50 → 40, ≤100 → 20, >100 → 0
- Loss ≤0.1% → 100, ≤0.5 → 90, ≤1 → 80, ≤2 → 60, ≤5 → 40, ≤10 → 20, >10 → 0

---

## 4. ТЕЛЕМЕТРИЯ / СЕТЕВАЯ АКТИВНОСТЬ

### Что отправляет GNO:
- **Ничего.** Приложение работает полностью offline-first.

### Какие сетевые соединения использует:
1. **ICMP пинг** — IcmpSendEcho к DNS серверам (1.1.1.1, 8.8.8.8 и др.)
2. **TCP подключения** — только speed test (по запросу пользователя, к cachefly.cachefly.net)
3. **DNS resolve** — getaddrinfo() для разрешения имён

### Что НЕ отправляет:
- ❌ HTTP/HTTPS запросы на серверы разработчика
- ❌ Телеметрию / аналитику
- ❌ Crash reports
- ❌ Данные пользователей
- ❌ WebSocket соединения
- ❌ Апдейт-чекер

---

## 5. СОВМЕСТИМОСТЬ С АНТИ-ЧИТАМИ

### Что изменяет GNO:
- Реестр (HKLM/HKCU) — только разрешённые ключи
- Power plan (PowerSetActiveScheme)
- DNS (netsh)
- MTU (netsh)
- CS2 конфиг (файл в userdata)

### Что НЕ изменяет:
- ❌ Драйверы
- ❌ Системные службы
- ❌ Файлы игр (кроме CS2 cfg)
- ❌ Процессы других программ
- ❌ Kernel-mode компоненты

### Упоминания анти-читов в коде:
- `capability_matrix.cpp:36` — "BattlEye/Vanguard/EAC защищены производителем"
- `problem_db.cpp:78-80` — описание проблемы "Vanguard не запущен"

### Потенциально рискованные оптимизации:
1. Win32PrioritySeparation = 38 (может триггерить Vanguard)
2. HAGS (Hardware-Accelerated GPU Scheduling)
3. SystemResponsiveness = 0
4. ПриоритетAboveNormal/High

### Безопасные оптимизации:
1. DNS серверы
2. MTU
3. Power Plan
4. Game DVR off
5. Анимации Windows off
6. Telemetry off
7. Visual Effects → Best Performance

---

## 6. СКРИПТЫ ДЛЯ АУДИТА

Все скрипты в папке `audit/`:
1. `01_registry_audit.ps1` — экспорт ключей реестра ДО/ПОСЛЕ
2. `02_transaction_validation.ps1` — проверка BACKUP→APPLY→VERIFY→ROLLBACK
3. `03_dns_latency_audit.ps1` — сравнение DNS/пинга с эталоном
4. `04_telemetry_audit.ps1` — проверка сетевой активности
5. `05_anticheat_check.ps1` — совместимость с анти-читами
6. `06_comparative_test.ps1` — план на 2 дня + таблица результатов

Запускать все скрипты от имени Администратора!
