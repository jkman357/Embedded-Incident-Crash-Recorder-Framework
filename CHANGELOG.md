# Changelog

All notable public release-candidate changes for the Embedded Incident & Crash Recorder Framework are recorded here.

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
