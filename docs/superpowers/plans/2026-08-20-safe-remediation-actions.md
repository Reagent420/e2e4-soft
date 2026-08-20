# Safe Remediation Actions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add seven explicit Windows remediation actions followed by five native macOS equivalents, with individual Fix/Revert controls and a confirmed transactional **Fix all** flow using durable backups, verification, and rollback.

**Architecture:** A platform-neutral remediation domain owns typed plans and transactions. Separate Windows and macOS privileged helpers perform closed sets of structured operations; the GUI never executes shell text. A durable JSON store records the plan and original values before mutation, and every action is re-observed before it is reported as applied or reverted.

**Tech Stack:** C++17, CMake 3.24+, Qt 6 Widgets, nlohmann/json 3.12.0, Win32 IP Helper/Power/Registry/Process APIs, doctest, CTest.

## Global Constraints

- Diagnostics never invoke remediation automatically on startup, scan completion, monitoring, profile import, timer, or game detection.
- The first Windows release includes DNS, MTU, allowlisted TCP parameters, power plan, Game DVR, per-executable fullscreen optimizations, and selected-process priority.
- The following macOS phase includes DNS, MTU, audited TCP parameters, energy mode, and selected-process priority under `src/remediation/macos/`; Game DVR and Windows fullscreen optimizations are omitted because they have no direct macOS equivalent.
- VPN and traffic tunnelling are absent from remediation and **Fix all**.
- **Fix all** requires full preflight, a persisted backup, a human-readable preview, and explicit user confirmation.
- Only one transaction executes at a time; operations are bounded, cancellable between mutations, verified after application, and rolled back in reverse order.
- The helper accepts a generated transaction UUID only. It never accepts arbitrary commands, registry paths, executable paths, or network values on its command line.
- Windows mutations use allowlisted Windows APIs. Do not add `system()`, `cmd.exe /c`, `ShellExecute` of a user-built command, detached threads, legacy `applyFix`, or auto-apply paths.
- Before the macOS phase lands, macOS exposes mutation actions as typed `Unsupported`. Afterward it exposes only the five verified native equivalents; Linux remains `Unsupported`.
- Durable state uses bounded, versioned JSON and sibling-temp atomic replacement. Unknown schema versions are preserved and not executed.
- Normal unit/UI tests use fakes and do not alter the developer machine. Real mutation tests run only in disposable Windows CI environments explicitly enabled for integration testing.
- All shell commands in this repository are prefixed with `rtk`.

---

## File Map

**Platform-neutral remediation domain**

- Create `src/remediation/remediation_types.h`: action IDs, statuses, targets, values, plans, outcomes, and typed errors.
- Create `src/remediation/fix_action.h`: read-only polymorphic action contract.
- Create `src/remediation/fix_transaction.h/.cpp`: preflight, apply, verify, failure stop, and reverse rollback orchestration.
- Create `src/remediation/backup_store.h`: persistence interface used by the transaction engine.
- Create `src/remediation/json_backup_store.h/.cpp`: bounded versioned transaction persistence using `json_persistence`.
- Create `src/remediation/platform_action_factory.h/.cpp`: constructs Windows actions or explicit unsupported actions.

**Windows boundary**

- Create `src/remediation/windows/windows_state_api.h`: narrow injectable interface for Win32 observation/mutation.
- Create `src/remediation/windows/windows_state_api.cpp`: production implementation using typed Win32 APIs.
- Create `src/remediation/windows/windows_fix_action.h/.cpp`: seven allowlisted `FixAction` implementations.
- Create `src/remediation/windows/privilege_runner.h/.cpp`: starts the signed project helper with `runas` and a UUID only, then reads its bounded result.
- Create `src/remediation/windows/helper_main.cpp`: validates and executes one persisted prepared transaction.

**macOS boundary (implemented after the Windows feature is complete)**

- Create `src/remediation/macos/macos_state_api.h/.mm`: narrow injectable interface and production SystemConfiguration, interface, sysctl, power, and process operations.
- Create `src/remediation/macos/macos_fix_action.h/.cpp`: five macOS `FixAction` implementations.
- Create `src/remediation/macos/privileged_helper.h/.mm`: Service Management registration, authorization state, and UUID-only IPC.
- Create `src/remediation/macos/helper_main.mm`: validates and executes one persisted prepared transaction.
- Create `tests/remediation_macos_tests.cpp`: fake-adapter and helper-boundary tests that do not mutate the host.

**UI and wiring**

- Create `src/ui/remediation_widget.h/.cpp`: action rows, status, individual Fix/Revert, Fix all, Revert changes, preview/confirmation, owned worker.
- Modify `src/ui/sidebar.h/.cpp`: add `NavPage::Remediation` and the `Исправления` navigation button.
- Modify `src/ui/main_window.cpp`: add the remediation page.
- Modify `CMakeLists.txt`: remediation library, GUI sources, Windows helper, libraries, tests, and gates.

**Tests and documentation**

- Create `tests/remediation_tests.cpp`: domain/store tests with fake actions and fake state APIs.
- Create `tests/remediation_windows_tests.cpp`: Windows adapter contract tests using an injected fake API.
- Modify `tests/ui_tests.cpp`: seven rows, buttons, confirmation, unsupported state, and safe destruction.
- Create `cmake/remediation_release_gate.cmake`: reject generic command execution and legacy auto-fix symbols while allowing reviewed remediation symbols.
- Modify `README.md` and `docs/development/diagnostic-builds.md`: safety model, elevation, rollback, and test commands.

---

### Task 1: Finish the Foundation Safety Prerequisites

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/main_console.cpp`
- Modify: `src/core/dns_manager.h`
- Modify: `src/core/dns_manager.cpp`
- Modify: `src/core/speed_test.h`
- Modify: `src/core/speed_test.cpp`
- Modify: `src/core/process_monitor.h`
- Modify: `src/core/process_monitor.cpp`
- Modify: `src/core/game_detector.h`
- Modify: `src/core/game_detector.cpp`
- Modify: `src/core/game_watcher.h`
- Modify: `src/core/game_watcher.cpp`
- Modify: `src/monitoring/stats_collector.h`
- Modify: `src/monitoring/stats_collector.cpp`
- Modify: `src/monitoring/ping_monitor.h`
- Modify: `src/monitoring/ping_monitor.cpp`
- Modify: `src/monitoring/packet_loss_monitor.h`
- Modify: `src/monitoring/packet_loss_monitor.cpp`
- Modify: `src/ui/dashboard.cpp`
- Modify: `src/ui/network_tools.cpp`
- Modify: `tests/unit_tests.cpp`
- Modify: `tests/ui_tests.cpp`
- Create: `cmake/assert_cli_rejection.cmake`
- Delete declarations/definitions: `StatsCollector::loadSession`, `GameDetector::loadGameDatabase`, `ProcessMonitor::startMonitoring`, `ProcessMonitor::stopMonitoring`, `ProcessMonitor::setProcessCallback`, and unused asynchronous `PacketLossMonitor` APIs after confirming `rtk rg` finds no retained callers.

**Interfaces:**
- Consumes: `Ipv4Address::parse(std::string_view)`, `DiagnosticError::UnsupportedCapability`, `CancellationSource`.
- Produces: `static bool PingMonitor::isSupported() noexcept`, `static bool DNSManager::isSupported() noexcept`, `static bool SpeedTest::isSupported() noexcept`, prompt bounded shutdown, and exact rejected-option tests.

- [ ] **Step 1: Write failing lifecycle, validation, unsupported, and CLI tests**

Add tests that require invalid DNS to fail without measurement, a stats callback to call `getStats()` without deadlocking, `GameWatcher::stop()` to finish within 250 ms even after starting with a 60-second interval, and non-Windows UI labels to contain `Недоступно на этой платформе`. Register exact CLI rejection with:

```cmake
add_test(NAME GNO-RejectBoost
  COMMAND ${CMAKE_COMMAND}
    -DEXECUTABLE=$<TARGET_FILE:GNO-console>
    -DARGUMENT=--boost
    -DEXPECTED_EXIT=1
    -DEXPECTED_TEXT=Unknown option: --boost
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/assert_cli_rejection.cmake)
```

- [ ] **Step 2: Run the focused suite and verify RED**

Run:

```bash
rtk cmake -S . -B build-remediation -DGNO_CONSOLE=ON -DGNO_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
rtk cmake --build build-remediation -j 4
rtk ctest --test-dir build-remediation --output-on-failure
```

Expected: the new lifecycle/unsupported tests fail, and the old `WILL_FAIL` contract is insufficient.

- [ ] **Step 3: Implement bounded workers and explicit platform support**

Use an owned condition variable for interruptible waits and copy callbacks outside locks:

```cpp
void PingMonitor::stop() {
    running_ = false;
    wait_cv_.notify_all();
    if (monitor_thread_.joinable() && monitor_thread_.get_id() != std::this_thread::get_id()) {
        monitor_thread_.join();
    }
}

void PingMonitor::updateStats(const ICMPResult& result) {
    StatsCallback callback;
    PingStats snapshot;
    {
        std::scoped_lock lock(stats_mutex_, callback_mutex_);
        // update stats_
        snapshot = stats_;
        callback = stats_callback_;
    }
    if (callback) callback(snapshot);
}
```

Clamp caller-controlled counts, intervals, and timeouts. Make `--dns` and every public DNS measurement parse `Ipv4Address` before Win32 use. On non-Windows, disable affected GUI controls and return/print `UnsupportedCapability`; remove the unconditional `ALL TESTS PASSED` message. Raise `cmake_minimum_required` to 3.24. Remove unused unbounded/asynchronous APIs listed in this task rather than hardening dead code.

- [ ] **Step 4: Run focused and GUI suites and verify GREEN**

Run:

```bash
rtk cmake --build build-remediation -j 4
rtk ctest --test-dir build-remediation --output-on-failure
rtk cmake -S . -B build-remediation-gui -DGNO_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
rtk cmake --build build-remediation-gui -j 4
QT_QPA_PLATFORM=offscreen rtk ctest --test-dir build-remediation-gui --output-on-failure
```

Expected: all tests pass and teardown cases finish under their asserted limits.

- [ ] **Step 5: Commit**

```bash
rtk git add CMakeLists.txt cmake/assert_cli_rejection.cmake src tests
rtk git commit -m "fix: complete diagnostic safety prerequisites"
```

---

### Task 2: Define the Remediation Domain and Transaction Engine

**Files:**
- Create: `src/remediation/remediation_types.h`
- Create: `src/remediation/fix_action.h`
- Create: `src/remediation/backup_store.h`
- Create: `src/remediation/fix_transaction.h`
- Create: `src/remediation/fix_transaction.cpp`
- Create: `tests/remediation_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `ActionId`, `ActionStatus`, `RemediationError`, `ActionTarget`, `ActionState`, `PreparedAction`, `ActionOutcome`, `TransactionRecord`, `FixAction`, `BackupStore`, and `FixTransaction`.
- `FixAction` exact contract:

```cpp
class FixAction {
public:
    virtual ~FixAction() = default;
    virtual ActionId id() const noexcept = 0;
    virtual Result<ActionState> observe(const ActionTarget&, const CancellationToken&) const = 0;
    virtual Result<PreparedAction> prepare(const ActionTarget&, const ActionState&) const = 0;
    virtual Result<ActionState> apply(const PreparedAction&, const CancellationToken&) = 0;
    virtual Result<ActionState> rollback(const PreparedAction&, const CancellationToken&) = 0;
};
```

- [ ] **Step 1: Write failing transaction tests**

Create fake actions with a call log and assert: full preflight precedes the first apply; backup persistence precedes mutation; apply stops at the first failure; outcomes distinguish not-attempted; rollback is reverse-order; verification mismatch is failure; cancellation stops before the next action; concurrent `execute()` returns `Busy`.

```cpp
CHECK(log == std::vector<std::string>{"observe:power", "prepare:power",
                                      "observe:dns", "prepare:dns",
                                      "save", "apply:power", "apply:dns"});
CHECK(transaction.rollback(token).value.action_order ==
      std::vector<ActionId>{ActionId::Dns, ActionId::PowerPlan});
```

- [ ] **Step 2: Run the target and verify RED**

```bash
rtk cmake --build build-remediation --target GNO-tests -j 4
rtk ctest --test-dir build-remediation -R GNO-UnitTests --output-on-failure
```

Expected: compilation fails because the remediation contracts do not exist.

- [ ] **Step 3: Implement typed contracts and orchestration**

Define the closed action set:

```cpp
enum class ActionId { PowerPlan, EnergyMode, GameDvr, FullscreenOptimizations,
                      TcpParameters, Dns, Mtu, ProcessPriority };
enum class ActionStatus { NotChecked, Recommended, AlreadyConfigured, Applied, Failed, Unsupported, Reverted };
enum class RemediationError { None, Unsupported, InvalidTarget, PermissionDenied, ElevationCancelled,
    PreflightFailed, BackupFailed, ApplyFailed, VerificationMismatch, Timeout, Cancelled,
    RollbackFailed, Busy, InternalFailure };

template <typename T>
struct Result {
    T value{};
    RemediationError error = RemediationError::InternalFailure;
    std::string detail;
    bool ok() const noexcept { return error == RemediationError::None; }
};
```

Use `std::variant` for typed values rather than free-form strings. The variant defines `DnsValue`, `MtuValue`, `TcpValue`, `PowerPlanValue`, `EnergyValue`, `RegistryValue`, `FullscreenValue`, `PriorityValue`, and `NiceValue`; unavailable values use `std::monostate`, never magic strings. `FixTransaction::prepare()` gathers every observation and plan without applying. `execute()` first persists `Prepared`, then applies in declared order and persists after every verified transition. `rollback()` traverses only successfully applied actions in reverse.

- [ ] **Step 4: Run tests and verify GREEN**

Run the Task 2 command. Expected: all transaction tests pass with no platform mutation.

- [ ] **Step 5: Commit**

```bash
rtk git add CMakeLists.txt src/remediation tests/remediation_tests.cpp
rtk git commit -m "feat: add transactional remediation domain"
```

---

### Task 3: Add Durable Bounded Transaction Backups

**Files:**
- Create: `src/remediation/json_backup_store.h`
- Create: `src/remediation/json_backup_store.cpp`
- Modify: `src/core/json_persistence.h`
- Modify: `src/core/json_persistence.cpp`
- Modify: `tests/remediation_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `BackupStore`, `TransactionRecord`, `persistence::atomicWriteText`.
- Produces:

```cpp
class JsonBackupStore final : public BackupStore {
public:
    explicit JsonBackupStore(std::filesystem::path storage_root = {});
    Result<std::monostate> save(const TransactionRecord&) override;
    Result<TransactionRecord> load(std::string_view transaction_id) const override;
    Result<std::vector<TransactionSummary>> list() const override;
};
```

- [ ] **Step 1: Write failing persistence tests**

Test round-trip of every typed value, maximum 256 KiB per transaction, maximum 100 retained resolved transactions, unresolved transaction preservation, invalid UUID rejection, malformed/future-version survival, failed temp write preserving the prior record, and refusal to rollback a transaction not created by this application.

```cpp
CHECK_FALSE(store.load("../escape").ok());
CHECK(store.load(id).value.status == TransactionStatus::Prepared);
CHECK(readFile(path) == valid_before_failed_save);
```

- [ ] **Step 2: Run and verify RED**

Run the Task 2 test command. Expected: missing `JsonBackupStore` compilation failures.

- [ ] **Step 3: Implement the store**

Store records at `applicationDataRoot()/GNO/remediation/transactions/<uuid>.json`. Require UUID format `[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}`. Serialize with `{"version":1,"producer":"E2E4 Soft","transaction":...}`. Read through `readBoundedFile(..., 256 * 1024)`, validate array/string bounds before constructing domain values, and write through `atomicWriteText`.

Do not delete unresolved records. When more than 100 resolved records exist, remove the oldest only after the new record is durably written.

- [ ] **Step 4: Run and verify GREEN**

Run the Task 2 test command. Expected: all persistence and transaction tests pass.

- [ ] **Step 5: Commit**

```bash
rtk git add CMakeLists.txt src/core/json_persistence.* src/remediation/json_backup_store.* tests/remediation_tests.cpp
rtk git commit -m "feat: persist remediation transactions safely"
```

---

### Task 4: Implement the Allowlisted Windows State API and Seven Actions

**Files:**
- Create: `src/remediation/windows/windows_state_api.h`
- Create: `src/remediation/windows/windows_state_api.cpp`
- Create: `src/remediation/windows/windows_fix_action.h`
- Create: `src/remediation/windows/windows_fix_action.cpp`
- Create: `src/remediation/platform_action_factory.h`
- Create: `src/remediation/platform_action_factory.cpp`
- Create: `tests/remediation_windows_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Task 2 `FixAction` and typed values.
- Produces `WindowsStateApi`, `WindowsFixAction`, and:

```cpp
std::vector<std::unique_ptr<FixAction>> createPlatformFixActions(
    std::shared_ptr<WindowsStateApi> api);
```

- [ ] **Step 1: Write failing adapter-contract tests against a fake WindowsStateApi**

For every action, assert exact observe/prepare/apply/verify/rollback behavior. Add negative cases for invalid IPv4, MTU outside `[576, 9000]`, unknown TCP key, unknown registry value, missing interface GUID, stale executable identity, exited process, PID reuse, and realtime priority. Assert non-Windows factory results are all `Unsupported`.

```cpp
CHECK_FALSE(makeMtuAction(api).prepare(target, MtuValue{575}).ok());
CHECK_FALSE(makePriorityAction(api).prepare(target, PriorityValue::Realtime).ok());
CHECK(actions.size() == 7);
```

- [ ] **Step 2: Run and verify RED**

```bash
rtk cmake --build build-remediation --target GNO-tests -j 4
rtk ctest --test-dir build-remediation -R GNO-UnitTests --output-on-failure
```

Expected: missing Windows action contracts or failed unsupported assertions.

- [ ] **Step 3: Implement the narrow state API**

Expose typed methods only:

```cpp
virtual Result<DnsValue> getDns(const InterfaceId&) const = 0;
virtual Result<std::monostate> setDns(const InterfaceId&, const DnsValue&) = 0;
virtual Result<MtuValue> getMtu(const InterfaceId&) const = 0;
virtual Result<std::monostate> setMtu(const InterfaceId&, MtuValue) = 0;
virtual Result<RegistryValue> getAllowedRegistry(AllowedRegistryKey) const = 0;
virtual Result<std::monostate> setAllowedRegistry(AllowedRegistryKey, const RegistryValue&) = 0;
virtual Result<PowerPlanValue> getPowerPlan() const = 0;
virtual Result<std::monostate> setPowerPlan(const PowerPlanValue&) = 0;
virtual Result<PriorityValue> getPriority(const ProcessIdentity&) const = 0;
virtual Result<std::monostate> setPriority(const ProcessIdentity&, PriorityValue) = 0;
```

Production Windows implementation uses `GetInterfaceDnsSettings`/`SetInterfaceDnsSettings` when available, `GetIpInterfaceEntry`/`SetIpInterfaceEntry`, allowlisted `RegGetValueW`/`RegSetValueExW`/`RegDeleteValueW`, `PowerGetActiveScheme`/`PowerSetActiveScheme`, and `GetPriorityClass`/`SetPriorityClass`. Resolve newer DNS APIs defensively and return `Unsupported` when absent. Re-check interface LUID/GUID and process creation time immediately before mutation.

- [ ] **Step 4: Implement the seven FixAction objects**

Each action stores the observed old typed value and proposed typed value in `PreparedAction`. `apply()` calls exactly one matching state method and then observes again. Equality with the proposed state is required. `rollback()` restores the old value and verifies equality. The TCP and Game DVR actions contain only named enum keys declared in code; fullscreen optimization takes only a canonical executable identity.

- [ ] **Step 5: Run and verify GREEN**

Run the Task 4 command. Expected: all fake-adapter tests pass on every platform; Windows production source compiles in Windows CI.

- [ ] **Step 6: Commit**

```bash
rtk git add CMakeLists.txt src/remediation tests/remediation_windows_tests.cpp
rtk git commit -m "feat: add allowlisted Windows remediation actions"
```

---

### Task 5: Add the Elevated UUID-Only Helper Boundary

**Files:**
- Create: `src/remediation/windows/privilege_runner.h`
- Create: `src/remediation/windows/privilege_runner.cpp`
- Create: `src/remediation/windows/helper_main.cpp`
- Modify: `src/remediation/fix_transaction.h`
- Modify: `src/remediation/fix_transaction.cpp`
- Modify: `tests/remediation_tests.cpp`
- Modify: `tests/remediation_windows_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: prepared `TransactionRecord`, `JsonBackupStore`, `WindowsStateApi`.
- Produces:

```cpp
class PrivilegeRunner {
public:
    virtual ~PrivilegeRunner() = default;
    virtual Result<TransactionRecord> executePrepared(
        std::string_view transaction_id,
        std::chrono::milliseconds timeout,
        const CancellationToken&) = 0;
};
```

- [ ] **Step 1: Write failing boundary tests**

Assert the runner rejects malformed IDs before launch, launches only the fixed helper path plus one UUID, maps UAC cancellation to `ElevationCancelled`, times out and terminates its owned helper, reads a bounded result, and rejects a result whose ID/schema/plan digest differs. Test helper rejection of non-Prepared, unknown action, changed target identity, and tampered plan.

- [ ] **Step 2: Run and verify RED**

Run the Task 4 command. Expected: missing runner/helper contracts.

- [ ] **Step 3: Implement runner and helper**

Build `E2E4-remediation-helper` on Windows only. Use `ShellExecuteExW` with verb `runas`, executable fixed to the installed sibling helper, and parameters containing only the validated UUID. Wait on the returned process handle with a bounded timeout; never detach. The helper loads the prepared record from the fixed application-data store, validates producer/version/digest/status/action IDs/typed values, re-observes targets, executes through `WindowsStateApi`, verifies, and atomically records results.

On non-Windows, `PrivilegeRunner` returns `Unsupported` without launching anything.

- [ ] **Step 4: Run and verify GREEN**

Run all unit tests. On Windows CI also execute helper parser/validation tests with fake actions; do not enable real mutations.

- [ ] **Step 5: Commit**

```bash
rtk git add CMakeLists.txt src/remediation tests
rtk git commit -m "feat: isolate elevated remediation helper"
```

---

### Task 6: Build the Remediation Page and Confirmation Flow

**Files:**
- Create: `src/ui/remediation_widget.h`
- Create: `src/ui/remediation_widget.cpp`
- Modify: `src/ui/sidebar.h`
- Modify: `src/ui/sidebar.cpp`
- Modify: `src/ui/main_window.cpp`
- Modify: `tests/ui_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `FixTransaction`, `JsonBackupStore`, `PrivilegeRunner`, platform action factory.
- Produces: `gno::RemediationWidget` with object names `remediationPage`, `checkAllButton`, `fixAllButton`, `revertTransactionButton`, and `actionRow-<stable-id>`.

- [ ] **Step 1: Write failing UI tests**

Update navigation expectations to seven pages with `Исправления` between diagnostics and history. Assert exactly seven action rows and individual Fix buttons; `Fix all` disabled before check; preview dialog lists old/new values; cancelling confirmation makes zero runner calls; unsupported rows are disabled on non-Windows; process priority cannot be selected without an explicit live process; destruction joins an active fake worker within 250 ms and drops queued callbacks.

```cpp
CHECK(group->button(static_cast<int>(gno::NavPage::Remediation))->text() ==
      QString::fromUtf8("Исправления"));
CHECK(widget.findChildren<QWidget*>(QRegularExpression("actionRow-.*")).size() == 7);
```

- [ ] **Step 2: Run GUI tests and verify RED**

```bash
rtk cmake --build build-remediation-gui --target GNO-ui-tests -j 4
QT_QPA_PLATFORM=offscreen rtk ctest --test-dir build-remediation-gui -R GNO-UITests --output-on-failure
```

Expected: navigation count/page/action-row assertions fail.

- [ ] **Step 3: Implement static layout and status model**

Create one row per stable action with name, explanation, current value, proposed value, status badge, Fix, and Revert. Add Check again, Fix all, and Revert changes. The widget receives controller dependencies through a constructor overload for fake UI tests; production construction uses the platform factory.

- [ ] **Step 4: Implement preview, confirmation, execution, and rollback UI**

Checking runs read-only observations. Individual Fix and Fix all first call `prepare()`, then display a modal table with exact old/new values, skipped actions, privilege requirements, and side effects. Only `QDialog::Accepted` calls the runner. Run work on one owned joinable worker with cancellation and queued `QPointer`-guarded delivery. Refresh affected observations after completion. Keep failed/partial transaction status visible and enable reverse rollback when eligible.

- [ ] **Step 5: Run GUI and unit suites and verify GREEN**

Run Task 1 GUI commands plus the Task 4 unit command. Expected: all tests pass offscreen without elevation or host mutation.

- [ ] **Step 6: Commit**

```bash
rtk git add CMakeLists.txt src/ui src/remediation tests/ui_tests.cpp
rtk git commit -m "feat: add confirmed remediation controls"
```

---

### Task 7: Harden Release Gates, CI, and User Documentation

**Files:**
- Create: `cmake/remediation_release_gate.cmake`
- Modify: `cmake/release_gate.cmake`
- Modify: `CMakeLists.txt`
- Modify: `.github/workflows/ci.yml`
- Modify: `README.md`
- Modify: `docs/development/diagnostic-builds.md`
- Modify: `tests/remediation_tests.cpp`

**Interfaces:**
- Consumes: all remediation targets.
- Produces: source and post-link gates that distinguish reviewed remediation from prohibited generic mutation paths.

- [ ] **Step 1: Write failing gate fixtures**

Add CTest fixtures containing `system(`, `cmd.exe /c`, `.detach()`, `applyFix`, `auto_apply_profiles = true`, a helper command containing a non-UUID value, and a fake legacy optimizer symbol. Assert each fixture is rejected. Add an allowed fixture containing `SetInterfaceDnsSettings`, `SetIpInterfaceEntry`, `PowerSetActiveScheme`, allowlisted registry calls, and `SetPriorityClass` only under `src/remediation/windows/`.

- [ ] **Step 2: Run gates and verify RED**

```bash
rtk ctest --test-dir build-remediation -R "ReleaseGate|RemediationGate" --output-on-failure
```

Expected: at least one prohibited fixture is not rejected by the existing gate.

- [ ] **Step 3: Implement scoped source and artifact gates**

Reject generic shell execution, detached threads, legacy auto-fix/optimizer symbols, checked-in executable archives, and Win32 mutator imports outside the reviewed remediation directory/helper. Scan the console, GUI, and helper artifacts. Require the helper invocation format to be fixed executable plus validated UUID. Keep existing diagnostic artifact checks and allow only the exact reviewed remediation symbols in the helper/GUI artifacts.

- [ ] **Step 4: Update CI and documentation**

Windows CI builds the helper and runs parser/domain/UI/gate tests without real mutation. macOS ARM, macOS Intel, and Linux sanitizer jobs verify explicit Unsupported behavior and absence of a helper binary. Document each action, required elevation, preview, backups, partial failure, rollback limitations, application-data location, and the fact that VPN is separate.

- [ ] **Step 5: Run the complete local completion gate**

```bash
rtk cmake -S . -B build-remediation-final -DGNO_CONSOLE=ON -DGNO_TESTS=ON -DGNO_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
rtk cmake --build build-remediation-final -j 4
rtk ctest --test-dir build-remediation-final --output-on-failure
rtk cmake -S . -B build-remediation-gui-final -DGNO_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
rtk cmake --build build-remediation-gui-final -j 4
QT_QPA_PLATFORM=offscreen rtk ctest --test-dir build-remediation-gui-final --output-on-failure
rtk git diff --check
rtk git status --short
```

Expected: all console, sanitizer, GUI, transaction, lifecycle, rejection, and artifact gates pass; `git diff --check` is empty; status contains only intended plan/progress changes before the final commit.

- [ ] **Step 6: Commit**

```bash
rtk git add .github/workflows/ci.yml CMakeLists.txt cmake README.md docs/development tests/remediation_tests.cpp
rtk git commit -m "ci: gate safe remediation releases"
```

---

### Task 8: Implement Five Native macOS Remediation Actions

**Files:**
- Create: `src/remediation/macos/macos_state_api.h`
- Create: `src/remediation/macos/macos_state_api.mm`
- Create: `src/remediation/macos/macos_fix_action.h`
- Create: `src/remediation/macos/macos_fix_action.cpp`
- Create: `src/remediation/macos/privileged_helper.h`
- Create: `src/remediation/macos/privileged_helper.mm`
- Create: `src/remediation/macos/helper_main.mm`
- Create: `tests/remediation_macos_tests.cpp`
- Modify: `src/remediation/platform_action_factory.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: platform-neutral `FixAction`, `FixTransaction`, `JsonBackupStore`, and transaction UUID boundary.
- Produces `MacStateApi`, five macOS actions, and `MacPrivilegeRunner`:

```cpp
class MacStateApi {
public:
    virtual ~MacStateApi() = default;
    virtual Result<DnsValue> getDns(const NetworkServiceId&) const = 0;
    virtual Result<std::monostate> setDns(const NetworkServiceId&, const DnsValue&) = 0;
    virtual Result<MtuValue> getMtu(const InterfaceId&) const = 0;
    virtual Result<std::monostate> setMtu(const InterfaceId&, MtuValue) = 0;
    virtual Result<TcpValue> getAllowedTcp(MacTcpKey) const = 0;
    virtual Result<std::monostate> setAllowedTcp(MacTcpKey, const TcpValue&) = 0;
    virtual Result<EnergyValue> getEnergy(EnergySource) const = 0;
    virtual Result<std::monostate> setEnergy(EnergySource, const EnergyValue&) = 0;
    virtual Result<NiceValue> getNice(const ProcessIdentity&) const = 0;
    virtual Result<std::monostate> setNice(const ProcessIdentity&, NiceValue) = 0;
};
```

- [ ] **Step 1: Write failing macOS adapter tests**

Using a fake `MacStateApi`, assert exactly five actions in this order: energy, TCP, DNS, MTU, process priority. Cover automatic/manual DNS restoration, ordered servers, MTU range, unknown/unwritable sysctl rejection, runtime-only TCP disclosure, battery/charger energy scope, stale service/interface, exited process, PID reuse, non-realtime nice bounds, apply verification, and reverse rollback. Assert no Game DVR or fullscreen action IDs are created.

```cpp
const auto actions = createMacFixActions(fake);
CHECK(actionIds(actions) == std::vector<ActionId>{ActionId::EnergyMode,
    ActionId::TcpParameters, ActionId::Dns, ActionId::Mtu, ActionId::ProcessPriority});
CHECK_FALSE(contains(actions, ActionId::GameDvr));
CHECK_FALSE(contains(actions, ActionId::FullscreenOptimizations));
```

- [ ] **Step 2: Run on macOS and verify RED**

```bash
rtk cmake -S . -B build-remediation-macos -DGNO_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
rtk cmake --build build-remediation-macos -j 4
rtk ctest --test-dir build-remediation-macos -R "GNO-UnitTests|GNO-MacRemediationTests" --output-on-failure
```

Expected: missing macOS contracts or factory assertions fail.

- [ ] **Step 3: Implement observation and typed mutation boundaries**

Use SystemConfiguration for network-service discovery and DNS state, `getifaddrs` plus interface ioctls for identity/MTU, `sysctlbyname` for the compiled TCP allowlist, public IOKit power-management APIs where available, and `getpriority`/`setpriority` for the selected process. Re-read the same stable identity immediately before mutation and after it.

When a public mutation API is unavailable, permit only a fixed Apple executable selected by an enum, an absolute compiled path, and an argument vector produced entirely from validated typed values. Execute with `posix_spawn`, captured stdout/stderr, a bounded wait, and no shell:

```cpp
enum class AllowedMacTool { NetworkSetup, Pmset };
Result<ToolOutput> runAllowedTool(AllowedMacTool tool,
                                  const std::vector<TypedArgument>& arguments,
                                  std::chrono::milliseconds timeout,
                                  const CancellationToken& cancellation);
```

Never invoke `/bin/sh`, `system`, `popen`, AppleScript, or a caller-supplied path. Return `Unsupported` when the running OS cannot safely apply and verify an action.

- [ ] **Step 4: Implement the five macOS FixAction objects**

Reuse the same prepare/backup/apply/verify/rollback lifecycle as Windows. DNS preserves automatic/manual mode and ordering. MTU binds to interface identity. TCP declares runtime-only behavior in its preview and never edits protected files. Energy preserves every affected power-source value. Priority binds PID plus start identity and forbids realtime/negative unsafe targets.

- [ ] **Step 5: Implement the Service Management helper boundary**

Register a bundled launch daemon with `SMAppService` on macOS 13+. The GUI sends only a validated transaction UUID over an authenticated local IPC channel. The helper resolves the fixed app-data transaction path, validates producer/version/digest/status/action IDs and code-signing identity, re-observes all targets, requests authorization immediately before mutation, and atomically records outcomes. No operation falls back to running the Qt GUI as root.

Provide a development status that clearly distinguishes `Helper unavailable`, `User approval required`, and `Release signing required`. Fake/helper-parser tests work without a paid account; real helper registration is an explicit local integration step and is skipped when the host lacks a usable signing identity.

- [ ] **Step 6: Run and verify GREEN**

Run the Task 8 command. Expected: domain, adapter, parser, and unsupported/helper-state tests pass without mutating the host.

- [ ] **Step 7: Commit**

```bash
rtk git add CMakeLists.txt src/remediation/macos src/remediation/platform_action_factory.cpp tests/remediation_macos_tests.cpp
rtk git commit -m "feat: add native macOS remediation actions"
```

---

### Task 9: Integrate macOS UI, Packaging, and Platform Gates

**Files:**
- Modify: `src/ui/remediation_widget.h`
- Modify: `src/ui/remediation_widget.cpp`
- Modify: `tests/ui_tests.cpp`
- Modify: `cmake/remediation_release_gate.cmake`
- Modify: `CMakeLists.txt`
- Modify: `.github/workflows/ci.yml`
- Modify: `README.md`
- Modify: `docs/development/diagnostic-builds.md`

**Interfaces:**
- Consumes: Task 8 macOS factory and helper states.
- Produces: a five-row native macOS remediation page and a signed-helper-ready app bundle layout.

- [ ] **Step 1: Write failing macOS UI and gate tests**

Assert macOS shows exactly DNS, MTU, TCP, energy mode, and process priority; shows no Windows-only rows; disables Fix/Fix all until the helper is approved; previews runtime-only TCP and per-power-source energy effects; cancellation causes zero helper calls; active work cancels and joins within 250 ms. Gate fixtures reject `/bin/sh`, `system(`, `popen(`, `osascript`, caller-controlled executable paths, unbounded `waitpid`, and helper messages containing anything other than a UUID.

- [ ] **Step 2: Run and verify RED**

```bash
QT_QPA_PLATFORM=offscreen rtk ctest --test-dir build-remediation-macos -R "GNO-UITests|RemediationGate" --output-on-failure
```

Expected: row selection/helper-state/gate assertions fail before integration.

- [ ] **Step 3: Wire platform-specific rows and helper state**

Select rows from the platform action factory rather than a hard-coded seven-row UI. Display `Требуется разрешить helper в настройках macOS`, `Helper недоступен`, or `Требуется релизная подпись` as distinct non-success states. Open the relevant System Settings pane only from an explicit user button. Keep diagnostics available when remediation is unavailable.

- [ ] **Step 4: Add bundle and CI configuration**

Place the helper and launch-daemon property list in the app bundle paths required by Service Management. Keep identifiers configurable through CMake cache variables without embedding credentials. macOS ARM and Intel CI compile the Objective-C++ bridge, validate plist/bundle layout, run fake/parser/UI/gate tests, and assert no actual helper registration or host mutation occurred.

Document local core/UI testing without a paid account, the optional locally signed helper integration step, explicit user approval, and the Developer ID/hardened-runtime/notarization requirement for public distribution.

- [ ] **Step 5: Run the complete cross-platform completion gate**

Run the Task 7 console/GUI gate and the Task 8/9 macOS commands, then:

```bash
rtk git diff --check
rtk git status --short
```

Expected: all supported-platform tests and gates pass; no host mutation test ran; status contains only intended documentation/progress changes before commit.

- [ ] **Step 6: Commit**

```bash
rtk git add .github/workflows/ci.yml CMakeLists.txt cmake/remediation_release_gate.cmake src/ui tests/ui_tests.cpp README.md docs/development/diagnostic-builds.md
rtk git commit -m "feat: integrate macOS remediation experience"
```

---

## Final Review Checklist

- Confirm all seven individual actions and **Fix all** exist and require confirmation.
- Confirm backup persistence completes before the first mutation.
- Confirm every apply and rollback is independently re-observed and verified.
- Confirm partial failure stops the transaction and reverse rollback is offered.
- Confirm no arbitrary command, shell interpolation, detached worker, auto-fix path, or VPN behavior is linked.
- Confirm macOS provides only the five verified equivalents and shows accurate helper/signing/authorization states; Linux remains typed Unsupported.
- Confirm ordinary tests never mutate the host and Windows integration mutation remains explicitly opt-in in a disposable environment.
- Run a whole-branch security review against the accepted specification before merge.
