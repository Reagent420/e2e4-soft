# Safe Remediation Actions Design

**Date:** 2026-08-20  
**Status:** Proposed for implementation  
**Product:** E2E4 Soft

## Goal

Add explicit, user-controlled system fixes alongside the diagnostic-only foundation. The application may diagnose without elevated privileges, but it must never modify the system merely because it was opened, a scan ran, a game was detected, or monitoring started.

The first remediation release includes all currently requested actions:

- DNS configuration;
- interface MTU;
- approved TCP parameters;
- active Windows power plan;
- Windows Game DVR;
- Windows fullscreen optimizations;
- priority of a selected running game process.

VPN and traffic tunnelling are explicitly outside this feature. They must not be included in **Fix all** and remain a separate project stage.

## User Experience

The remediation page presents one row per action. Each row contains:

- action name and a short plain-language explanation;
- current detected state;
- proposed target state;
- status: `Not checked`, `Recommended`, `Already configured`, `Applied`, `Failed`, or `Unsupported`;
- an individual **Fix** button;
- an individual **Revert** button when this application has a valid backup for that action.

The page also provides:

- **Check again** to refresh all observed states without changing anything;
- **Fix all** to prepare every applicable recommended action;
- **Revert changes** to revert changes made by a selected application transaction.

Before either an individual fix or **Fix all**, the UI shows a confirmation dialog containing the exact actions, old values, proposed new values, privilege requirement, and possible side effects. Nothing is applied until the user confirms.

**Fix all** includes only actions whose preflight check succeeds and whose current state warrants a change. Unsupported, already configured, ambiguous, and unsafe actions are shown but skipped. Process priority requires an explicitly selected running process; it is never guessed and is skipped when no process is selected.

## Safety Model

Diagnostics and remediation use separate interfaces and targets. Diagnostic code cannot call remediation by accident. Every write-capable implementation is reachable only through the remediation controller and an explicit UI or CLI confirmation flow.

There is no automatic remediation on startup, game detection, monitoring, profile import, scheduled timers, or diagnostic completion. Legacy auto-fix and auto-apply flags remain disabled and are not consulted by the remediation engine.

Every action follows the same lifecycle:

1. Observe the current state using a read-only adapter.
2. Validate the requested target against a closed allowlist and bounded typed inputs.
3. Produce a human-readable and machine-readable preview.
4. Capture the exact original state required for rollback.
5. Persist the transaction and backup atomically before any system mutation.
6. Request elevation only when the confirmed action actually requires it.
7. Apply the action with a bounded timeout and cancellation support.
8. Read the state again and verify the expected result.
9. Record the verified outcome atomically.

An action is reported as applied only after post-apply verification. A successful process exit alone is insufficient.

## Components

### FixAction

`FixAction` is the platform-neutral contract for one remediation. It exposes observation, preview, backup capture, application, verification, and rollback. Results use a typed status and error contract rather than booleans or timeout-shaped failures.

Each action declares:

- stable action identifier and schema version;
- supported platforms and minimum platform version;
- whether elevation is required;
- bounded typed parameters;
- conflicts or ordering constraints;
- whether rollback is supported for the captured state.

### FixTransaction

`FixTransaction` owns an ordered collection of prepared actions. It performs complete preflight before the first mutation. Actions are then applied sequentially and each is verified immediately.

If an action fails, execution stops. The UI reports exactly what succeeded, failed, and was not attempted, then offers rollback of the successfully applied prefix. Rollback occurs in reverse order and is itself verified. A rollback failure is preserved as a visible, durable state; it is never reported as success.

The initial ordering is:

1. power plan;
2. Game DVR;
3. fullscreen optimizations;
4. TCP parameters;
5. DNS;
6. MTU;
7. selected-process priority.

This ordering keeps transient network changes together and leaves the process-scoped action last because the process may exit at any time.

### BackupStore

`BackupStore` writes versioned JSON transactions beneath the platform application-data directory. Records contain action identifiers, typed old and proposed values, timestamps, platform metadata, apply and verification results, and rollback state.

Writes use the foundation's sibling-temporary-file and atomic-replacement mechanism. Files are bounded in size and count. Unknown future schema versions are preserved but not executed. Backups never contain credentials, command-line secrets, packet contents, or unrelated registry/system data.

Only transactions created and successfully persisted by this application may be reverted. A new fix does not overwrite the backup needed by an unresolved transaction.

### PrivilegeRunner

`PrivilegeRunner` executes structured allowlisted operations. It does not accept an arbitrary shell command. User-controlled strings are parsed into types before reaching the runner:

- DNS servers are validated IPv4 addresses selected from an approved preset or entered explicitly;
- MTU is an integer inside a platform- and interface-bounded range;
- interface and process identities must come from a fresh system enumeration and match stable identifiers;
- registry paths, value names, power plans, and supported TCP keys are compile-time allowlisted.

The Windows adapter uses system APIs where practical. Where a command-line system utility is unavoidable, arguments are passed without shell interpretation, output and exit status are captured, execution is timed out, and the result is verified independently. No `cmd.exe /c`, string-concatenated `netsh`, detached worker, or generic `system()` entry point is allowed.

The macOS adapter exposes only actions with a safe, supported implementation. Other rows remain visible as `Unsupported on macOS` and cannot be selected. This release does not add a Network Extension or VPN entitlement.

## Action Semantics

### DNS

Capture the complete DNS configuration for the selected interface, including automatic/DHCP state and ordered server list. Apply only validated IPv4 servers. Rollback restores the captured mode and server order. Verify by reading interface configuration; an optional resolution probe is diagnostic evidence, not the source of truth.

### MTU

Capture the selected interface's MTU and stable identity. Validate the proposed MTU against the adapter's safe range. Apply and verify by reading the same interface again. If the interface disappears or identity changes, abort without targeting another interface.

### TCP Parameters

Support a small documented allowlist of parameters, with the exact proposed value visible in preview. Capture both value and prior existence so rollback can restore a value or remove one that did not previously exist. Parameters that cannot be independently verified are excluded.

### Power Plan

Capture the active plan identifier. Apply only a known plan identifier discovered from the system or a bundled allowlist. Rollback reactivates the captured plan. The feature does not delete or rewrite power plans.

### Game DVR

Capture the exact supported registry values and whether each existed. Apply only the documented disable values. Rollback restores original values or removes values created by the transaction. Changes outside the allowlist are untouched.

### Fullscreen Optimizations

Operate only on an explicitly selected executable. Canonicalize and validate the executable path before use. Capture the existing compatibility entry and restore it exactly on rollback. Global changes are not permitted.

### Process Priority

Operate only on a currently running process explicitly selected by the user. Bind the preview to both PID and process creation identity to prevent PID-reuse errors. Capture its current priority class, apply an allowlisted non-realtime target, and verify immediately. Rollback is available only while the same process instance remains alive. Realtime priority is never offered.

## Concurrency and Cancellation

Only one remediation transaction may execute at a time. Diagnostics may continue only when they do not inspect state being mutated; affected checks are paused and refreshed after the transaction.

Workers are owned and joinable. Cancellation is checked between operations and during bounded waits. Cancellation stops before the next mutation; it does not interrupt an indivisible system API call. The transaction then reports the verified state and offers rollback where applicable.

Callbacks are synchronized, copied under lock, and invoked after releasing locks. UI delivery uses queued ownership-guarded calls. No worker may detach or join itself.

## Error Handling

Errors distinguish at least:

- unsupported capability;
- invalid or stale target;
- permission denied or elevation cancelled;
- preflight failure;
- backup persistence failure;
- apply failure;
- verification mismatch;
- timeout;
- cancellation;
- rollback failure;
- internal failure.

The UI must show which action failed and a safe next step. Logs include action IDs and structured outcomes but redact user-sensitive paths where they are not necessary. Failed and partially applied transactions remain visible until resolved or explicitly archived.

## Testing and Release Gates

Unit tests use fake observers and runners; normal tests never modify the host system. Coverage includes:

- typed input boundaries and allowlist rejection;
- preview accuracy;
- no mutation before durable backup;
- individual apply and rollback;
- **Fix all** ordering and skipped-action behavior;
- failure before the first action;
- failure in the middle of a transaction;
- reverse-order rollback and rollback failure;
- verification mismatch after a nominally successful command;
- permission denial, timeout, cancellation, stale interface, exited process, and PID reuse;
- backup size, schema-version, atomicity, corruption, and retention limits;
- callback reentrancy and bounded shutdown;
- explicit unsupported behavior on macOS.

Platform integration tests run only in isolated disposable Windows test environments with known fixtures. They must restore the observed initial state during cleanup and cannot be required on a developer's normal workstation.

Release gates continue to reject legacy optimizer artifacts and generic mutation entry points. The remediation build may contain only the new allowlisted interfaces; prohibited generic shell execution, detached threads, legacy `applyFix`, auto-apply paths, and unreviewed mutator symbols remain build failures.

## Migration from Upstream v1.4.0

Upstream `v1.4.0` is not merged wholesale. Its UI ideas and read-only detection logic may be ported selectively. Direct mutation implementations are rewritten behind `FixAction` and `PrivilegeRunner` because the upstream code contains string-built system commands, incomplete result checking, weak rollback, legacy auto-fix paths, and detached workers.

The reusable candidates are the capability presentation, action descriptions, report concepts, overlay concepts, and portions of the centralized monitoring model. VPN/tunnel work remains independent.

## Acceptance Criteria

- Every requested fix has an individual button and visible status.
- **Fix all** previews the full applicable plan and requires confirmation.
- No action runs automatically or before an atomic backup exists.
- Every applied action is independently verified.
- All reversible actions restore their captured original state; limitations are shown before confirmation.
- Unsupported macOS actions are disabled and explicitly labeled.
- No arbitrary command execution or user-controlled shell interpolation exists.
- Partial failure produces a durable, accurate transaction state and supports verified reverse rollback.
- VPN/tunnel behavior is absent from the remediation transaction.
- Unit, UI, lifecycle, negative-input, transaction, and release-gate tests pass on supported CI platforms.
