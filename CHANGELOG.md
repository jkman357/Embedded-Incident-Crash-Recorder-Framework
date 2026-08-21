# Changelog

All notable public release-candidate changes for the Embedded Incident & Crash Recorder Framework are recorded here.

## v1.0.0rc07 — 2026-08-21

### Evidence / Documentation Closure

- Regenerated `validation/rc05_to_rc06_scope.diff` as a standards-compliant unified diff that can be replayed with normal patch tooling.
- Corrected the README public RC history ordering so rc05 precedes rc06 and rc07 follows rc06.
- Clarified that prior-RC source manifests are the authoritative content-level provenance record; archive hashes are supplemental packaging evidence and may differ after repackaging.
- Added rc06 content manifest and rc06→rc07 review-evidence metadata for independent delta review.
- Advanced default framework build identity to `0x01000007`.
- Updated target-validation identity from rc06 to rc07 while retaining all target results as `PENDING TARGET EVIDENCE`.
- No recorder runtime API, runtime record schema, retained layout, persistence/export model, hot-path event flow, reference runtime, or linker behavior changed.

## v1.0.0rc06 — 2026-08-21

### Fixed

- Closed rc05 review F-01 by aligning the base Permanent Monitor contract with promotion requirements for stable event/object semantics and bounded RAM/persistence/export contribution.
- Closed rc05 review F-02 by clarifying that intentional Development Probe retention for future work does not satisfy feature/change closure; ownership must remain open or transfer to a separately tracked active work item, and final closure still requires REMOVE or PROMOTE.
- Closed rc05 review F-03 wording by narrowing the host lifecycle gate to documentation/checklist presence evidence rather than implying project-level lifecycle compliance.

### Review evidence

- Added rc05→rc06 review-evidence metadata and a reproducible scope diff to support independent RC delta review.
- The prior package/container hash and source-tree diff evidence are recorded under `validation/`.

### Changed

- Advanced default framework build identity to `0x01000006`.
- Formal target-validation package now identifies the immutable rc06 source baseline.
- No recorder-core API, runtime record schema, retained-store layout, persistence transaction model, or hot-path I/O boundary was intentionally changed.

### Validation boundary

- Host/source pre-target gates must still pass.
- Real-target probe cost, observer effect, retention, NVM, export, and SD/filesystem evidence remain pending until measured on a concrete integration.

## v1.0.0rc05 — 2026-08-21

### Added

- Normative `Permanent Monitor` and `Development Probe` lifecycle classifications.
- Explicit Development Probe feature/defect closure gate: every temporary probe must be `REMOVE` or `PROMOTE TO PERMANENT MONITOR`.
- Promotion criteria covering continuing diagnostic value, stable interpretation, bounded execution/event rate, bounded storage contribution, failure isolation, and target observer-effect/runtime-cost acceptance.
- Project-level guidance that `IR_MONITOR_*` / `IR_DEV_PROBE_*` wrappers may be used for readability without changing recorder-core semantics or the persisted record schema.
- Target integration checklist items for probe inventory, temporary-probe exit criteria, Remove-or-Promote disposition, and Release configuration cleanup.

### Clarified

- Development/Release build profile is a storage/export policy and is not the same thing as Development Probe lifecycle.
- Hiding temporary instrumentation behind a Development-only compile switch does not satisfy feature-closure cleanup.
- Permanent Monitor does not imply continuous SD/filesystem logging; Release storage/export rules remain unchanged.
- Evidence collected with temporary probes belongs to the tested build; after probe cleanup, the cleaned build requires appropriate rebuild/regression confirmation.

### Changed

- Advanced default framework build identity to `0x01000005`.
- Formal target-validation package now identifies the immutable rc05 source baseline.
- No recorder-core API, runtime record schema, retained-store layout, persistence transaction model, or hot-path I/O boundary was intentionally changed.

### Validation boundary

- Host/source pre-target gates must still pass.
- Real-target probe cost, observer effect, retention, NVM, export, and SD/filesystem evidence remain pending until measured on a concrete integration.

## v1.0.0rc04 — 2026-08-21

### Fixed

- Removed weak non-GNU/Clang atomic/publication fallbacks from the host reference; unsupported host toolchains now fail closed instead of silently weakening the required semantics.
- Widened host reference journal generation ordering to 64-bit and removed implementation-defined unsigned-to-signed generation comparison.
- Added bounded live-record-ID collision avoidance for the 32-bit public export identifier.
- Reconstructs volatile journal allocation state from the newest committed slot during reference recovery/restart simulation.
- Split persistence/export retry interval configuration and widened runtime retry countdowns to 32-bit so values above 255 are not truncated.
- Persistence source formation now copies First-Abnormal and Fatal payloads only after their authoritative validity state is true.

### Added

- Wide retry configuration host build gate using 1000 service calls.
- Hardening checks for restart allocator reconstruction and suppression of unpublished protected-snapshot payload bytes.
- `target-validation/PLAN.md` with retained-memory, timing, observer-effect, NVM, export, Development trace, and known-root-cause validation cases.
- `target-validation/INTEGRATION_CHECKLIST.md` for pre-measurement target integration.
- `target-validation/RESULTS_TEMPLATE.md` with explicit `PENDING TARGET EVIDENCE` status.
- `target-validation/README.md` describing the immutable source/evidence relationship.

### Review basis

- rc03 independent review: `PASS WITH FINDINGS`, 0 Critical, 0 Major, 4 Minor, `RC03 → TARGET VALIDATION GATE: YES`.
- rc04 closes the four Minor source-level findings before target execution.

### Validation boundary

- Host/source pre-target gates pass on the supplied reference environment.
- No real-target retention, timing, NVM, SD/filesystem, or observer-effect PASS is claimed in the source package.
- Target results must identify the exact immutable rc04 source identity and remain separate evidence artifacts.

## v1.0.0rc03 — 2026-08-21

### Fixed

- Added bounds validation for Development trace `read_index`, `write_index`, and `count` before queue memory access.
- Invalid Development queue metadata now drops evidence and raises recorder health flags instead of indexing the queue.
- Replaced the single-buffer host persistence fixture with a bounded two-slot generation journal that never reclaims pending evidence.
- Added payload length, CRC-32, generation, record ID, transaction state, export state, and commit-marker semantics to the host persistence fixture.
- Added recovery handling for incomplete persistence interrupted after transaction begin or payload write.
- Added Task/ISR lost-record accounting for events omitted while persistence temporarily pauses ordinary timeline capture.
- Separated Fatal publication from normal shared `state_flags` updates using a dedicated fatal state, fatal publish sequence, and atomic claim callback.
- Made First-Abnormal and Fatal `VALID` states the authoritative publication markers and added a project publication-barrier contract.
- Renamed the First-Abnormal `checksum_or_commit` field to `integrity_sentinel` to remove ambiguous commit semantics.
- Added a complete Recorder-OFF compile/link/run gate for the full reference source set.
- Extended Recorder-OFF macro validation to Task, ISR, First-Abnormal, and Fatal public probe macros.
- Normalized the README license footer to the included MIT `LICENSE`.

### Added

- `reference/hardening_check.c` regression fixture covering queue-metadata corruption, pending-evidence retention, persistence interruption recovery, fatal one-shot publication, and persistence-pause loss accounting.
- AddressSanitizer/UndefinedBehaviorSanitizer hardening validation.
- Host reference atomic-claim and publication-barrier callbacks.
- Bounded service-call retry/backoff countdown for persistence/export retries.
- Validation MAP for the full Recorder-OFF build and hardening build, plus hardening disassembly evidence.

### Changed

- Retained header now carries dedicated Fatal publication/persistence metadata.
- Default 10 KiB retained layout now provides 309 Task records + 104 ISR records (413 total 24-byte records) with a compiled retained-store size of 10,228 bytes.
- Formal known-root-cause / observer-effect target validation moves to `v1.0.0rc04`.

### Validation boundary

- Host validation closes the rc02 implementation-level findings and proves the reference hardening fixtures on the supplied GCC host environment.
- MCU reset-retention behavior, interrupt/atomic/publication timing, real NVM timing/endurance, SD/filesystem behavior, and observer effect remain target-specific `v1.0.0rc04` work.

## v1.0.0rc02 — 2026-08-21

### Added

- Minimal portable embedded-C reference implementation.
- Separate Task and ISR runtime rings with a dedicated Fatal Snapshot.
- One-shot first-abnormal ownership using a project-provided bounded critical primitive.
- Frozen 24-byte `IR_RuntimeRecord` reference format.
- 10 KiB retained-store implementation with compile-time size guards.
- `.incident_ram` linker-section example and host MAP/ELF section validation.
- Early-boot retained-header containment without external I/O.
- Generic persistence-source and persistent-export adapter boundaries.
- Bounded low-priority export state machine with verification and retry-preserving abort behavior.
- Compile-time Development/Release profiles.
- Development-only bounded continuous-trace RAM queues and service writer path.
- Release on-demand export request path for persistent evidence retrieval.
- Host reference adapters and runtime validation fixture.
- Release, Development, Recorder-OFF, C99 compatibility, size, and MAP validation artifacts.

### Changed

- Reconciled the retained container with the already-selected Task-ring + ISR-ring writer architecture.
- Selected the 24-byte runtime record instead of the earlier 16-byte candidate so both event values remain available.
- Froze the minimal adapter/configuration interfaces used by the reference implementation.
- Moved the next milestone to target known-root-cause and observer-effect validation (`v1.0.0rc03`).

### Validation boundary

- Host validation demonstrates compile/link/interface/control-flow correctness only.
- MCU retention behavior, RTOS timing, critical-section WCET, NVM timing/endurance, real SD/filesystem behavior, and observer effect remain target-specific validation work.

## v1.0.0rc01 — 2026-08-21

- Established the initial public specification baseline for low-coupling incident evidence capture, first-abnormal-state localization, survivability, persistent evidence, compile-time Development/Release storage profiles, and transactional export behavior.
