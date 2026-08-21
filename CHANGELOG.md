# Changelog

All notable public release-candidate changes for the Embedded Incident & Crash Recorder Framework are recorded here.

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
