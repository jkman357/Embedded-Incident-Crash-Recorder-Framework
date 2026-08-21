# Target Validation Package

This directory defines the evidence contract for `v1.0.0rc05` target validation.

The source package is a **Target Validation Candidate**. It does not claim that any MCU, RTOS, retained-memory region, NVM device, filesystem, SD card, or product firmware has already passed these checks.

Use the exact rc05 source revision for target execution and record the source ZIP or commit identity in the result report.

Files:

- `PLAN.md` — required target-validation cases and evidence.
- `RESULTS_TEMPLATE.md` — result format for one concrete target integration.
- `INTEGRATION_CHECKLIST.md` — pre-test integration checklist.

A target result is valid only when it identifies the concrete target, compiler/toolchain, build profile, source identity, measurement method, acceptance criterion, and retained evidence artifacts.
