# v1.0.0rc04 Target Integration Checklist

Complete before formal target measurements.

## Source / Build Identity

- [ ] Exact rc04 source identity recorded.
- [ ] Application firmware build identity recorded.
- [ ] Compiler/version/optimization/LTO recorded.
- [ ] Development and Release profile configuration recorded.
- [ ] Recorder-OFF build available for baseline comparison.

## Retained RAM

- [ ] Dedicated retained section mapped to verified physical retained RAM.
- [ ] Section size fits the reserved region.
- [ ] Startup initialization does not clear the section for claimed retained reset classes.
- [ ] Reset-cause behavior documented by reset class.

## Platform Primitives

- [ ] Timestamp callback is bounded.
- [ ] Critical primitive is bounded and ISR-safe for supported call sites.
- [ ] Fatal claim primitive is non-blocking and valid in the supported exception model.
- [ ] Publication barrier is correct for compiler + CPU memory ordering.
- [ ] Target disassembly can be retained for protected publication paths.

## Persistence Adapter

- [ ] Physical NVM geometry documented.
- [ ] Erase/program alignment documented.
- [ ] Maximum operation times documented/measured.
- [ ] Commit/integrity encoding defined.
- [ ] Recovery scan bounded.
- [ ] Pending-generation retention/reclaim policy defined.
- [ ] Wear/endurance budget defined.

## Export Adapter

- [ ] Export work runs outside hot probe paths.
- [ ] Missing/slow destination cannot block product control.
- [ ] Abort/failure leaves source evidence intact.
- [ ] Successful export and source reclamation are separate.

## Development Trace

- [ ] Continuous trace enabled only in Development profile.
- [ ] Queue capacity/budget recorded.
- [ ] File/session/rollover policy defined by the project adapter.
- [ ] Destination failure produces bounded diagnostic loss, not product blocking.

## Measurement Readiness

- [ ] Product acceptance limits declared before measurement.
- [ ] Recorder OFF / Release ON / Development ON workloads are comparable.
- [ ] Timing instrumentation itself is accounted for.
- [ ] Stack/CPU/latency measurement method documented.
- [ ] Known-root-cause or controlled synthetic target case prepared.
