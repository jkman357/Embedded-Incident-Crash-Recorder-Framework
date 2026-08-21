# v1.0.0rc06 Target Validation Plan

## 1. Objective

Demonstrate on a real embedded target that the Incident & Crash Recorder:

- captures useful evidence;
- preserves the first known abnormal condition and fatal context as designed;
- survives the reset classes claimed by the integration;
- persists and exports evidence with bounded failure behavior;
- does not introduce an unacceptable observer effect;
- preserves normal product behavior when diagnostic storage or export is unavailable.

This plan does not define product-specific acceptance limits. The integrating project must declare those limits before measurement.

## 2. Required Identity

Record:

- target / board revision;
- MCU / SoC;
- RTOS or scheduler model;
- compiler and version;
- optimization and LTO settings;
- linker script identity;
- recorder source version and source hash / commit;
- application firmware build identity;
- Development or Release build profile;
- retained-memory region address and size;
- persistent-storage technology and geometry;
- export medium and filesystem, if used.

## 3. Validation Matrix

### TV-BUILD-01 — Build-profile separation

Verify from target MAP/symbol evidence:

- Release contains retained Incident Evidence support;
- Release does not contain the Development continuous-trace queue/writer path;
- Development contains the intended bounded continuous-trace path;
- Recorder-OFF contains no retained recorder store or reference/target recorder storage object that should have compiled out.

### TV-LIFE-01 — Probe lifecycle and feature-closure audit

Review the project-added diagnostic probe inventory for the measured candidate.

Verify:

- every project-added probe is classified as Permanent Monitor or Development Probe;
- every active Development Probe has a documented engineering question and removal criterion;
- completed feature/defect work has an explicit REMOVE or PROMOTE disposition for each Development Probe introduced by that work;
- every Permanent Monitor has continuing diagnostic value, stable event/object semantics and interpretation where persisted evidence depends on them, bounded execution/event rate, bounded RAM/persistence/export contribution, failure isolation, and target runtime-cost/observer-effect acceptance;
- the measured Release candidate contains no stale Development Probes merely hidden by a Development-only compile switch;
- any Development Probe intentionally transferred to future work is owned by a separately tracked active work item with a bounded engineering question and removal criterion, and is not counted as closure of the originating work;
- post-cleanup source was rebuilt and the required regression/timing confirmation was repeated where cleanup could materially alter timing or behavior.

Retain the probe inventory and disposition record as target/project evidence.

### TV-RET-01 — Retained section placement

Verify `.incident_ram` or the project equivalent:

- maps entirely inside the intended retained physical RAM range;
- does not overlap application memory;
- is not initialized/cleared by startup code for reset classes claimed to preserve evidence.

Provide MAP/linker/startup evidence.

### TV-RET-02 — Reset-class survival

For each claimed reset class, record whether retained evidence is expected to survive and verify the result experimentally.

Minimum categories where supported by the target:

- software reset;
- watchdog reset;
- external reset;
- brownout/power-on reset;
- fault-triggered reset.

Do not claim retention for a reset class that physically clears the memory.

### TV-TIME-01 — Task probe execution time

Measure representative and worst observed Task-context probe execution time with the target compiler and optimization settings.

Record:

- measurement method;
- sample count;
- minimum / typical / maximum;
- application acceptance limit.

### TV-TIME-02 — ISR probe and critical-section timing

Measure:

- ISR probe execution time;
- maximum recorder critical-section duration;
- interrupt priorities affected by the critical primitive;
- any interrupt-latency change caused by recorder activity.

The critical primitive must remain bounded and must not rely on an RTOS mutex or unbounded wait.

### TV-TIME-03 — Fatal claim/publication path

Validate the target implementation of:

- `try_claim_u32`;
- publication barrier;
- Fatal exception/nesting assumptions;
- First-Abnormal publication ordering;
- Fatal publication ordering.

Inspect optimized target disassembly where practical.

### TV-OBS-01 — Recorder OFF vs ON observer effect

Using the same application workload and target configuration, compare at minimum:

```text
Recorder OFF
Recorder ON / Release
Recorder ON / Development   (when Development trace is in scope)
```

Measure project-relevant quantities such as:

- control/task period;
- deadline misses;
- CPU load;
- ISR latency;
- stack high-water mark;
- application throughput;
- event loss counters;
- persistent-service duration;
- Development trace queue high-water / loss;
- SD/filesystem service duration.

Acceptance limits must be declared by the integrating project before concluding PASS.

### TV-PERSIST-01 — Real NVM transaction

Verify the target persistence adapter against the actual NVM technology:

- erase/program alignment;
- write/erase timing;
- read-during-write restrictions;
- payload integrity check;
- commit publication;
- bounded slot/journal selection;
- pending evidence retention;
- wear/endurance policy.

### TV-PERSIST-02 — Interrupted persistence

Interrupt the target persistence transaction at defined phases, including where feasible:

```text
before target selection
transaction begun
metadata written
partial payload written
payload complete
integrity written
before commit publication
after commit publication
```

After reboot/recovery, verify that incomplete generations are rejected and the previous committed generation remains usable.

### TV-PERSIST-03 — Reboot reconstruction

Verify that generation/record identity and reclaim state are reconstructed from persistent media after reboot without relying on lost volatile counters.

### TV-CAP-01 — Capacity pressure

Fill the bounded persistent store according to the target policy.

Verify:

- pending evidence is not silently reclaimed contrary to policy;
- capacity exhaustion degrades diagnostics rather than product function;
- reclaim priority is explicit and reproducible.

### TV-EXPORT-01 — Explicit export success

In Release profile, trigger explicit persistent-evidence export and verify:

- source persistent evidence is readable before export;
- destination output is complete and verified;
- successful export does not immediately erase the only internal evidence copy;
- export status and retention/reclaim state remain separate decisions.

### TV-EXPORT-02 — Export failure isolation

Exercise, where applicable:

- media absent;
- media removed during export;
- destination full;
- mount/open failure;
- write failure;
- finalize/verify failure;
- reset during export.

Verify the original persistent evidence remains valid/retryable and normal application execution continues.

### TV-DEV-01 — Development continuous trace

In Development profile, exercise sustained logging and slow/unavailable destination behavior.

Verify:

- producer work stays bounded;
- the application does not wait for the destination;
- bounded queue overflow drops diagnostic records and updates loss/health evidence;
- rollover/session handling is bounded;
- storage stalls do not become control-path stalls.

### TV-WRAP-01 — Long-duration ordering

If a target timestamp can wrap during a supported logging session, verify or document how Task/ISR Development trace ordering is interpreted across rollover.

### TV-KRC-01 — Known-root-cause evidence test

Use a controlled known defect or synthetic target defect with an independently known first abnormal condition.

Run:

```text
A. defect + Recorder OFF
B. same defect + Recorder ON
C. fixed firmware + Recorder ON
```

Verify:

- A and B reproduce the same product-level defect behavior within declared tolerance;
- B captures evidence that identifies or materially narrows the known origin/propagation path;
- the Recorder does not move the apparent first abnormal condition merely by changing timing;
- C no longer produces the defect-specific abnormal evidence under the same workload.

Do not publish proprietary defect details in the public framework repository. A target project may retain private evidence separately.

## 4. Minimum Evidence Set

A complete target-validation result should retain at minimum:

- target MAP file;
- linker/startup retention evidence;
- compiler/build log;
- Recorder OFF/ON timing measurements;
- persistence interruption results;
- export failure results;
- Development trace stress results when applicable;
- known-root-cause comparison summary;
- target result report using `RESULTS_TEMPLATE.md`.

## 5. Gate Rule

Target validation is PASS only when the project-defined acceptance criteria are stated and all mandatory in-scope tests either:

- pass with retained evidence; or
- are explicitly marked not applicable with a technically valid reason.

Missing target evidence is `PENDING`, not `PASS`.
