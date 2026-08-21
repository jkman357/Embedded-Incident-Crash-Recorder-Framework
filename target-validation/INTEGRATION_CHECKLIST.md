# v1.0.0rc06 Target Integration Checklist

Complete before formal target measurements.

## Source / Build Identity

- [ ] Exact rc06 source identity recorded.
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

## Probe Lifecycle / Feature Closure

- [ ] Project-added probe inventory is available.
- [ ] Each probe is classified as Permanent Monitor or Development Probe.
- [ ] Each active Development Probe has a stated engineering question and removal criterion.
- [ ] Completed feature/defect work records REMOVE or PROMOTE disposition for each Development Probe introduced by that work.
- [ ] Every Permanent Monitor has continuing diagnostic value documented.
- [ ] Permanent Monitor event/object semantics and interpretation are stable where persisted evidence depends on them.
- [ ] Permanent Monitor call-path cost, event rate/data volume, RAM/persistence/export contribution, and failure isolation are bounded for the target.
- [ ] Permanent Monitor observer effect/runtime cost is accepted for the target.
- [ ] Release candidate contains no stale Development Probes merely hidden behind a Development-only build switch.
- [ ] Any Development Probe transferred to future work is owned by a separately tracked active work item with a bounded engineering question and removal criterion; such transfer is not counted as closure of the originating work.
- [ ] Post-cleanup source has been rebuilt and receives the required regression/timing confirmation.

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
