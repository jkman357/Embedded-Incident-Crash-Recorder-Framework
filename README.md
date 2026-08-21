# Embedded Incident & Crash Recorder Framework
## Low-Coupling Evidence, First-Abnormal-State, Survivability & Recovery Architecture

**Version:** v1.0.0rc02
**Status:** Reference Implementation Release Candidate — Minimal Frozen Baseline
**Date:** 2026-08-21  
**Scope:** Generic embedded systems; independent of specific MCU, RTOS, storage medium, communication bus, motor controller, sensor, company, or product.

**Release type:** Specification + minimal reference-implementation RC. This RC preserves the public architecture baseline and adds a portable embedded-C reference implementation, compile-time Development/Release profiles, retained-store sizing, host build validation, and generic persistence/export adapter skeletons.

---

## 0. Versioning Policy

The public release-candidate sequence starts at:

```text
v1.0.0rc01
v1.0.0rc02
v1.0.0rc03
...
v1.0.0
```

Rules:

- RC numbering is always two digits: `rc01`, `rc02`, ... `rc10`.
- A released RC is immutable; changes create the next RC.
- Specification RCs and implementation-validation RCs are distinguished by their stated scope.
- `v1.0.0` is used only after specification, reference implementation, and target validation converge.

### Normative Authority

This RC has **one canonical definition per topic**.

- Normative requirements are located in the main numbered Parts/Sections.
- Non-normative appendices may provide planning or explanatory material, but they do not override normative sections.
- Deprecated API signatures or duplicate normative definitions are not retained.
- If a future review changes a rule, the canonical section must be edited directly in the next RC.

### Public RC History

| Version | Date | Summary |
|---|---|---|
| v1.0.0rc01 | 2026-08-21 | Initial public specification baseline for low-coupling evidence capture, first-abnormal-state localization, survivability, persistence, compile-time Development/Release storage profiles, and transactional evidence export. |
| v1.0.0rc02 | 2026-08-21 | Minimal frozen reference implementation: Task/ISR rings, first-abnormal latch, fatal snapshot, retained 10 KiB store, early-boot containment, persistence/export service boundaries, Development trace queue, Release on-demand export path, and compile/link/MAP/size validation. |

### Roadmap

```text
v1.0.0rc01  Initial public specification baseline
v1.0.0rc02  Minimal frozen reference implementation + compile/link/MAP validation
v1.0.0rc03  Known-root-cause + observer-effect target validation
v1.0.0      Stabilized baseline
```

**Current milestone:** `v1.0.0rc02` is the frozen minimal reference-implementation baseline. Target-specific timing, linker/startup retention behavior, NVM stall behavior, and observer-effect measurements remain `v1.0.0rc03` work.

---

## 1. Unified Purpose

The framework exists to answer four questions after an embedded incident:

```text
1. What happened?
2. What was the first known abnormal condition?
3. Did the evidence survive the failure/reset/power-cycle?
4. Can the evidence be retrieved without requiring the product to remain usable?
```

The unified architecture is:

```text
Observe
   ↓
Detect
   ↓
Preserve
   ↓
Persist
   ↓
Recover
   ↓
Export
   ↓
Correlate
```

The MCU should perform lightweight evidence acquisition and preservation.

The PC/service-side tooling should perform expensive symbolization, correlation, timeline reconstruction, and root-cause candidate ranking.

---

## 2. Core Architectural Principle

The framework shall remain independent of physical implementation details.

Recorder-core concepts include:

```text
Evidence
Observation
Operation
Transition
Invariant
First Abnormal Evidence
Incident
Retained Evidence
Persistent Evidence
Pending Export
Boot Epoch
Recovery Status
```

Project/platform-specific concepts include:

```text
FRAM
EEPROM
Internal Flash
External Flash
SD
I2C
SPI
UART
USB
CAN
BLE
specific ring buffers
specific motor controllers
specific sensors
specific application states
```

These belong to adapters, probe sources, project dictionaries, and validation cases.

---

# Part A — Evidence Acquisition & First-Abnormal-State Localization

## A1. Background

The existing Incident Recorder concept already covers retained RAM, internal Flash persistence, reset/fault metadata, and selected runtime signals such as control activity, communication activity, task state, sensor/actuator state, and other system evidence.

A key diagnostic gap remains:

> The final crash location is often not the origin of the failure.

A defect may first corrupt a shared object, violate an internal invariant, or leave a subsystem in an impossible state. The system can continue running for milliseconds, seconds, or much longer before a visible hang, reset, or HardFault occurs.

This is a general **delayed-failure / first-abnormal-state** problem and is not specific to any physical device, bus, storage medium, or subsystem.

Examples include:

- a buffer boundary error corrupting an adjacent control object;
- an invalid queue/ring index causing a later out-of-range access;
- a stale or invalid pointer being consumed by another task;
- a protocol transaction leaving shared state inconsistent;
- a driver callback modifying state unexpectedly;
- a storage or communication operation overwriting caller-owned memory;
- an ISR/task race creating a state that is locally valid but globally impossible.

A controlled boundary-overwrite defect is useful as a **known-root-cause validation case**, but the framework must describe that case generically rather than preserving project-specific symbols or storage-driver names.

The next recorder increment should therefore prioritize:

> **First abnormal-state localization and first-corruption localization**

rather than simply adding more peripheral-specific probes.

---

## A2. Design Goal

The recorder shall improve postmortem diagnosability while minimizing disturbance to the original firmware.

The target behavior is:

- no modification of application control decisions;
- no modification of application return values;
- no blocking in probe paths;
- no dynamic memory allocation;
- no peripheral dependency in the hot path;
- bounded execution time;
- minimal stack usage;
- dedicated recorder memory;
- fail-silent recorder behavior;
- compile-time removal when disabled;
- high information density instead of continuous bulk logging.

The recorder is an **observer**, not a controller.

---

## A3. Definition of Low Coupling

Low coupling shall be evaluated in five dimensions.

### 3.1 Functional coupling

The recorder shall not:

- change application state intentionally;
- change return values;
- add recovery decisions to normal application logic;
- alter task sequencing;
- suppress or force peripheral transactions;
- inject delays to obtain evidence.

A probe may read application state and record observations.

---

### 3.2 Timing coupling

Hot-path probes shall:

- execute in bounded time;
- avoid loops proportional to buffer length;
- avoid `printf`, `sprintf`, file formatting, or string construction;
- avoid `blocking_semaphore_wait()`, mutex waits, sleeps, and polling loops;
- avoid Flash, SD, UART, USB, I2C, SPI, or other physical I/O;
- perform only short scalar copies to retained RAM.

Preferred operation:

```c
IR_EVT(IR_EVT_STORAGE_WRITE_BEGIN, value0, value1);
```

Avoid:

```c
IR_DumpBuffer(data, len);
```

---

### 3.3 Resource coupling

The recorder shall:

- use a fixed RAM budget;
- avoid `malloc()` / `free()`;
- avoid large probe-local arrays;
- avoid recursion;
- minimize additional task stack usage;
- use compact numeric event IDs instead of strings.

The current **10 KiB retained RAM budget remains the default** until measurements demonstrate otherwise.

---

### 3.4 Memory-layout coupling

A diagnostic mechanism shall not accidentally hide or move an existing bug.

Therefore the production recorder should avoid inserting arbitrary guard variables between existing globals because doing so changes linker placement.

Preferred:

- dedicated `.incident_ram` section;
- read-only invariant checks against existing application objects;
- independent recorder metadata;
- no intentional rearrangement of normal `.bss` / `.data` objects.

Canaries/red-zones may still be useful in a special diagnostic build, but they should not be the primary production mechanism.

---

### 3.5 Failure coupling

Recorder failure shall not become application failure.

If recorder capacity, metadata, or internal validation fails:

- drop the record if necessary;
- increment a lost-record counter;
- never block waiting for space;
- never assert/reset the system solely because recorder logging failed;
- never retry indefinitely.

Principle:

> **Lose evidence before losing product functionality.**

---

## A4. Proposed Probe Layers

### 4.1 Level 1 — System Behavior

Existing or planned probes may include:

- control-loop / actuator activity;
- communication TX/RX activity;
- sensor / encoder / home activity;
- rate / counter / cycle progress;
- watchdog or peer-controller observable state;
- transaction begin/end/result;
- task/activity breadcrumbs;
- reset and fault status.

The framework should identify these by logical event IDs rather than by hard-coding a specific peripheral or physical medium.

Purpose:

> Determine what the system was doing.

---

### 4.2 Level 2 — State Integrity

Add lightweight validity checks for critical objects.

Examples:

- ring index `< ring size`;
- fixed size field equals expected configuration;
- pointer lies inside legal SRAM;
- enum/state value is within valid range;
- counter relationships remain valid;
- queue head/tail are within bounds;
- stack watermark remains above configured minimum.

Purpose:

> Detect the first state that becomes impossible.

---

### 4.3 Level 3 — Memory Integrity / First-Corruption Localization

When a critical object first violates an invariant:

- latch the event;
- record the object ID;
- record the object address;
- record observed value;
- record expected range/value;
- record current task/context;
- record the most recent subsystem operation context;
- preserve the earliest corruption evidence.

Purpose:

> Determine where corruption first became externally observable.

This level is a high-priority capability because delayed memory corruption can separate the first observable abnormal state from the eventual crash.

---

## A5. First-Fault Latch

Repeated invalid-state logging can quickly destroy the most useful evidence.

Use a one-shot latch:

```c
if (!valid)
{
    if (IR_FIRST_ABNORMAL_TASK(object_id, rule_id, observed0, observed1))
    {
        /* This context atomically claimed the first-abnormal slot. */
    }
}
```

The public macro/function above must use the selected bounded atomic-latch primitive. A check-then-act sequence on a shared boolean is forbidden.

After latching:

- preserve the first-fault snapshot;
- continue the normal timeline ring if safe;
- later repeated failures may only increment counters.

The first invalid state is usually more valuable than the hundredth consequence.

---

## A6. Separate First-Fault Snapshot from Timeline Ring

Recommended conceptual layout:

```text
10 KiB Retained RAM
+----------------------------------+
| Header / build / reset metadata  |
+----------------------------------+
| First-Fault Snapshot             |
| Locked after first corruption    |
+----------------------------------+
| Runtime Timeline Ring            |
| Continuously overwritten         |
+----------------------------------+
```

No fixed percentage split is normative.

The retained store has a hard configured ceiling, defaulting to **10 KiB**. Timeline capacity is derived from actual compiled structure sizes:

```text
timeline_bytes
= retained_budget
- sizeof(header)
- sizeof(first_abnormal_snapshot)
- sizeof(fatal_snapshot)
- sizeof(operation_context)
- alignment/reserved bytes

timeline_record_count
= floor(timeline_bytes / sizeof(runtime_record))
```

The compiled `sizeof()` result and linker MAP file are authoritative. Event-density measurements on target hardware determine whether the resulting history window is sufficient.

---

## A7. Breadcrumb Strategy

Do not log every normal execution.

Prefer:

- state transition;
- transaction begin/end;
- timeout;
- failure;
- unexpected duration;
- integrity violation.

Example:

```text
PERSISTENCE_OPERATION_ENTER
PERSISTENCE_READ_FAIL
PERSISTENCE_WRITE_BEGIN
MEMORY_INTEGRITY_FAIL
CRITICAL_RING_INVALID
HARDFAULT
```

Avoid:

```text
CONTROL_LOOP_OK
CONTROL_LOOP_OK
CONTROL_LOOP_OK
CONTROL_LOOP_OK
...
```

The recorder should be primarily:

> **transition-driven + anomaly-driven**

rather than a bulk periodic data logger.

---

## A8. Generic Operation Context

Each important subsystem may maintain a compact **last operation context**.

The abstraction should describe *what the software was doing*, not the physical implementation.

Example:

```c
typedef struct
{
    uint16_t domain_id;      /* storage, comm, control, sensor, UI, etc. */
    uint16_t op_id;          /* read, write, enqueue, dequeue, update, tx... */
    uint32_t object_or_ptr;  /* logical object ID or relevant address */
    uint32_t arg0;
    uint32_t arg1;
    uint16_t result;
    uint16_t flags;
} IR_OPERATION_CONTEXT_t;
```

Possible domains:

```text
STORAGE
COMMUNICATION
CONTROL
SENSOR
ACTUATOR
QUEUE
RING_BUFFER
CONFIGURATION
PERSISTENCE
PROTOCOL
TASK
ISR
APPLICATION
```

Possible operations:

```text
READ
WRITE
COPY
UPDATE
ENQUEUE
DEQUEUE
TX
RX
START
STOP
COMMIT
VALIDATE
CALLBACK
STATE_TRANSITION
```

The context is overwritten on each relevant operation and does not need to consume timeline entries.

A backend or subsystem may map its physical implementation into this generic model. For example:

```text
domain = PERSISTENCE
op     = WRITE
object = PAYLOAD_BUFFER
arg0   = FRAME_TOTAL_SIZE
result = FAILED
```

If the first corrupted object is later observed adjacent to that payload region, the PC tool can correlate object IDs, relative addresses/offsets, timestamps, and operation ordering without needing to know whether the underlying implementation used FRAM, EEPROM, internal Flash, RAM copy, UART, SPI, I2C, DMA, or another mechanism.

This provides strong evidence even without a live debugger.

---

## A9. Critical Object Invariants

Initial candidates should be selected because corruption of these objects can cause delayed catastrophic failure.

Suggested classes:

- ring buffers;
- communication queue descriptors;
- callback/function-pointer tables;
- task/control state structures;
- retained RAM metadata;
- critical shared protocol state;
- stack boundary/watermark metadata.

Example ring validation:

```c
bool IR_IsCriticalRingValid(void)
{
    if (g_critical_ring.write_index >= g_critical_ring.size)
        return false;

    if (g_critical_ring.size != EXPECTED_CRITICAL_RING_SIZE)
        return false;

    if (!IR_IsValidRamAddress(g_critical_ring.buffer))
        return false;

    return true;
}
```

The validator must be:

- read-only;
- bounded;
- non-blocking;
- safe against already-corrupted values.

---

## A10. Pointer Safety

The recorder itself must not crash while examining bad data.

Avoid blindly dereferencing application pointers.

Preferred:

```c
IR_RecordValue(IR_FIELD_PTR, (uint32_t)data);
IR_RecordValue(IR_FIELD_LEN, len);
```

If dereference is necessary:

1. validate address range first;
2. limit the amount read;
3. never iterate over an untrusted length;
4. stop on uncertainty.

Principle:

> **The closer execution is to a crash, the less application memory the recorder should touch.**

---

## A11. ISR and Task Concurrency

Multiple writers to one recorder ring can create a new race condition.

Options:

### Option A — Very short critical section

```c
key = IR_PlatformEnterCritical();

idx = g_ir.write_index;
g_ir.record[idx] = rec;
g_ir.write_index = NextIndex(idx);

IR_PlatformExitCritical(key);
```

The critical section must remain extremely short.

### Option B — Separate rings

Example:

```text
Task ring : 500 records
ISR ring  : 100 records
```

The PC tool merges records by timestamp.

Separate rings may reduce contention and make timing behavior easier to audit.

---

## A12. Stack Impact Rules

Probe code should not significantly alter task stack requirements.

Avoid inside hot-path recorder functions:

- local arrays;
- formatted strings;
- deep call chains;
- recursive helpers;
- large temporary structures.

Prefer scalar parameters:

```c
IR_EVT3(IR_EVT_STORAGE_WRITE,
        (uint32_t)data,
        len,
        addr);
```

Stack watermark should be measured with recorder OFF and ON.

---

## A13. Dedicated Recorder Memory Section

Recommended linker strategy:

```text
Application RAM
    normal .data/.bss/heap/stacks

Dedicated Incident Recorder RAM
    .incident_ram
```

Benefits:

- recorder storage is isolated;
- application global placement is disturbed less;
- retained RAM behavior is explicit;
- dump tooling can locate recorder data consistently;
- recorder ON/OFF comparisons are easier.

The exact retained-memory linker placement must be chosen from the target platform memory map and startup/retention behavior.

---

## A14. Compile-Time Removal

All probes should support complete compile-time disable.

Example:

```c
#define INCIDENT_RECORDER_ENABLE       1
#define INCIDENT_MEMORY_PROBE_ENABLE   1
#define INCIDENT_STORAGE_PROBE_ENABLE  1
```

```c
#if INCIDENT_MEMORY_PROBE_ENABLE
#define IR_MEM_CHECK_CRITICAL_RING()  IncidentMem_CheckCriticalRing()
#else
#define IR_MEM_CHECK_CRITICAL_RING()  ((void)0)
#endif
```

When disabled, the probe statement should compile away.

This enables clean A/B testing:

```text
Build A: Recorder OFF
Build B: Recorder ON
```

---

## A15. Build Identity and Postmortem Correlation

Every persisted incident should identify the exact firmware image.

Recommended fields:

- firmware version;
- build timestamp or build ID;
- Git commit hash when available;
- recorder schema version;
- map/ELF identity or checksum.

This allows the PC tool to use the correct ELF/MAP file for:

- PC/LR symbol resolution;
- object address lookup;
- section mapping;
- adjacency analysis.

Without build identity, address-based postmortem evidence can become ambiguous after code changes.

---

## A16. Timestamp and Ordering

The timeline must provide deterministic event ordering.

Record:

- RTOS tick;
- optional high-resolution timer where justified;
- event sequence number.

The sequence number helps resolve events that share the same tick.

Timestamp wrap must be handled by the PC decoder.

---

## A17. Crash-Safe Persistence

Hot path:

```text
Probe
  ↓
Retained RAM
```

Do not perform:

```text
Probe
  ↓
Flash / SD / USB
```

Persistence should happen:

- periodically in a low-priority context;
- after an incident is latched when safe;
- at controlled checkpoints;
- after reboot from retained RAM.

Internal Flash writes must not be introduced into real-time control paths.

---

## A18. Recorder Self-Health

Suggested header fields:

```text
magic
schema_version
build_id
write_index
record_count
lost_count
first_fault_latched
reset_count
```

Do not calculate a CRC over the entire 10 KiB on every record.

Possible strategies:

- per-record lightweight checksum;
- block CRC only when committing to Flash;
- header consistency checks during export.

---

## A19. Why Production Canaries Are Not the First Choice

A guard such as:

```c
uint32_t guard_before;
CriticalObject object;
uint32_t guard_after;
```

can detect overwrite, but it changes layout.

For layout-sensitive defects, this may:

- move the victim;
- change the symptom;
- delay the crash;
- make a bug appear fixed.

Therefore:

- use invariant checks first in production recorder builds;
- reserve canary/red-zone builds for controlled diagnostics or fault injection.

---

## A20. Abstraction Boundary

The framework shall separate **diagnostic semantics** from **physical implementation**.

### 20.1 Diagnostic layer

The recorder should reason in terms of:

```text
operation
object
state transition
invariant
timestamp
result
first abnormal state
first corrupted object
```

### 20.2 Adapter layer

Subsystem-specific code translates implementation details into generic recorder events.

Examples:

```text
storage_frame_write()
        ↓
PERSISTENCE / WRITE

bus_transaction()
        ↓
COMMUNICATION / TRANSACTION

UART TX callback
        ↓
COMMUNICATION / TX_COMPLETE

ring buffer push
        ↓
QUEUE / ENQUEUE

motor command update
        ↓
CONTROL / COMMAND_UPDATE

configuration commit
        ↓
CONFIGURATION / COMMIT
```

The core recorder should not require knowledge of:

- FRAM;
- EEPROM;
- internal Flash;
- SD card;
- I2C;
- SPI;
- UART;
- USB;
- CAN;
- BLE;
- a particular motor controller;
- a particular sensor;
- a particular application object.

These are **adapters/probe sources**, not recorder-core concepts.

### 20.3 Why this matters

This separation allows the same recorder core to be reused across:

```text
MCU ↔ MCU
MCU ↔ PC
MCU ↔ peripheral
SoC ↔ MCU
single-controller products
multi-controller products
storage-heavy systems
communication-heavy systems
control-heavy systems
```

The framework therefore remains portable while individual projects define their own event IDs, invariants, and adapters.

---

## A21. Generic Boundary-Overwrite Case as a Known-Root-Cause Validation

A controlled boundary-overwrite defect provides a useful recorder validation case without binding the specification to a real product, symbol set, storage device, or driver.

### Defective behavior

```text
payload pointer exposes N valid bytes
        ↓
storage/helper operation is called with N + metadata bytes
        ↓
copy/write logic accesses beyond the caller-owned payload boundary
        ↓
adjacent critical object is corrupted
        ↓
later abnormal execution / hang / fault
```

### Corrected behavior

Use an explicitly sized staging frame that owns both payload and metadata:

```c
uint8_t staging_frame[FRAME_TOTAL_SIZE];

memcpy(staging_frame, payload, PAYLOAD_SIZE);
AppendFrameMetadata(staging_frame, FRAME_TOTAL_SIZE);

storage_frame_write(
    STORAGE_RECORD_ADDRESS,
    staging_frame,
    FRAME_TOTAL_SIZE);
```

The metadata and integrity fields remain inside the staging frame rather than extending a caller-owned payload buffer.

---

## A22. Known-Bug Validation Plan

Keep the intentionally defective implementation only in a controlled validation branch or test fixture.

### Test A — Baseline defect, recorder OFF

Goal:

- reproduce the original boundary overwrite and delayed failure behavior.

Expected:

- the defect remains reproducible without recorder instrumentation.

### Test B — Defect present, recorder ON

Goal:

- verify the recorder does not prevent or materially alter the defect;
- verify the recorder captures the first observable corruption evidence.

Desired evidence:

```text
PERSISTENCE_WRITE_BEGIN
buffer = X
length = N + metadata

MEMORY_INTEGRITY_FAIL
object = CRITICAL_RING
address = Y

CRITICAL_RING_INVALID

... later ...

HARDFAULT / HANG evidence
```

The recorder should allow an engineer to correlate operation context, object address, timing, and the suspicious overwrite span without requiring a live debugger.

### Test C — Fixed implementation, recorder ON

Goal:

- verify the memory corruption disappears;
- verify normal operation remains stable;
- verify recorder instrumentation does not introduce a timing regression.

Expected:

- no related integrity fault;
- no related hang/fault;
- acceptable measured recorder overhead.

---

## A23. Observer-Effect Audit

Recorder ON/OFF must be compared quantitatively.

Measure:

- task execution time;
- control-loop timing;
- communication timing;
- ISR latency where relevant;
- CPU load;
- stack watermark;
- RAM usage;
- lost-record count;
- application global addresses from MAP file.

Critical functions should be inspected at assembly/MAP level where necessary.

Acceptance should be based on measured impact, not assumption.

---

## A24. Suggested Acceptance Criteria

A candidate recorder build should satisfy all of the following:

### Functional

- no change to application decision logic;
- no probe-driven reset or recovery;
- no change to normal return values.

### Timing

- no blocking probe operations;
- bounded probe execution;
- no unacceptable control-loop or real-time timing regression.

### Memory

- fixed recorder RAM budget;
- no dynamic allocation;
- no significant new stack pressure;
- recorder memory isolated in a dedicated section.

### Reliability

- recorder overflow only drops evidence;
- corrupted application pointers cannot easily crash the recorder;
- first-fault evidence remains protected.

### Build

- all probes can be compiled out;
- recorder OFF is close to baseline;
- build identity is stored with incidents.

### Diagnostic Value

- a controlled boundary-overwrite defect can be localized without a live debugger;
- the first corrupted object can be identified;
- the preceding subsystem operation can be identified;
- PC/ELF/MAP correlation can narrow the responsible source area.

---

## A25. Recommended Next Implementation Order

1. Keep retained RAM at the configured fixed budget; the current default remains **10 KiB**.
2. Add dedicated first-fault snapshot.
3. Add compact event breadcrumbs.
4. Add generic `last operation` context for selected logical domains/subsystems.
5. Add read-only invariants for the most critical ring/shared objects.
6. Add first-corruption latch.
7. Isolate recorder storage in `.incident_ram`.
8. Add build ID / schema version.
9. Perform execution-time and stack-impact audit.
10. Validate against a controlled generic boundary-overwrite test case.
11. Only after this validation, decide whether more probes are justified.
12. Increase retained RAM only if measured evidence shows the configured budget is insufficient.

---

## A26. Key Design Principle

The recorder should not attempt to preserve everything.

Its primary mission is:

> **Preserve enough evidence to identify the first abnormal transition and connect it to the operation immediately preceding it.**

For delayed memory corruption, the most valuable sequence is:

```text
Last valid state
        ↓
Last subsystem operation
        ↓
First impossible state
        ↓
Secondary corruption
        ↓
Hang / fault
```

If the recorder can preserve this sequence reliably, a compact retained store can provide more diagnostic value than a much larger indiscriminate raw log.

---

## A27. Current Working Conclusion

A generic boundary-overwrite validation case demonstrates why a crash recorder focused only on final fault registers and peripheral activity is incomplete.

The high-value capability is:

> **Memory Integrity Probe + First-Corruption Localization**

This can remain low-coupling when implemented as:

- read-only invariant checks;
- compact breadcrumbs;
- one-shot first-fault snapshots;
- dedicated recorder RAM;
- no hot-path I/O;
- no application layout manipulation;
- bounded execution;
- compile-time removal.

Known-root-cause validation cases should be expressed using generic test fixtures or synthetic identifiers in the public framework. Product-specific symbols, addresses, device names, and defect narratives belong in private project validation material, not in the public specification.

---

---

# Part B — Evidence Survivability, Persistence, Recovery & Accessibility

## B1. Purpose

This document defines a low-coupling architecture for ensuring that incident evidence remains:

1. **survivable** after hang/reset/power-cycle,
2. **recoverable** without requiring a user to reach a debug/tech page before the system fails again,
3. **exportable** through one or more transports,
4. **independent** of the original application control flow as much as practical.

The key problem is:

> A system may hang before a user can manually export evidence, and after restart it may quickly fail again before the evidence can be retrieved.

Therefore, manual UI export must be treated as a convenience path, not the primary evidence-survival mechanism.

---

## B2. Core Design Principle

The architecture should separate:

```text
Evidence Capture
      ↓
Evidence Retention
      ↓
Evidence Persistence
      ↓
Evidence Recovery
      ↓
Evidence Export
```

These are distinct concerns.

A user interface such as a Tech/Service page is only one possible export interface.

It must not be the only way to preserve or retrieve incident evidence.

---

## B3. Abstraction Boundary

The core framework shall not depend on a physical storage or transport implementation.

The framework should reason in terms of:

```text
retained evidence
persistent evidence
pending export
exported evidence
recovery mode
boot/session identity
```

Physical implementations may include:

```text
Internal Flash
External Flash
EEPROM
FRAM
SD card
USB
UART
CAN
BLE
Network
Service tool
```

These belong to platform or project adapters.

---

## B4. Recommended Layering

```text
Application / Control Logic
          │
          │ observations / incident requests
          ▼
+-----------------------------+
| Incident Recorder Core      |
| - retained timeline         |
| - first abnormal evidence   |
| - fault metadata            |
| - incident state            |
+-----------------------------+
          │
          ▼
+-----------------------------+
| Persistence Service         |
| - persistent slots          |
| - commit state              |
| - CRC / integrity           |
+-----------------------------+
          │
          ▼
+-----------------------------+
| Recovery / Export Service   |
| - pending detection         |
| - automatic/on-demand export |
| - retry / status            |
+-----------------------------+
          │
      +---+---+
      │       │
      ▼       ▼
     SD      USB
   adapter   adapter
```

The recorder core should not know whether the final destination is SD, USB, BLE, or another transport.

---

## B5. Low-Coupling Requirement

The normal application shall not depend on the recorder in order to function.

Preferred dependency:

```text
Application
    ↓
Recorder
```

Avoid:

```text
Application
    ↓
Recorder
    ↓
Application
```

Recorder failure should degrade diagnostic capability, not normal product functionality.

Examples:

```text
SD unavailable
→ keep incident pending
→ normal application may continue

persistent slot invalid
→ mark recorder degraded
→ normal application may continue

export fails
→ retry later
→ do not block control task
```

---

## B6. Minimal Integration Points

A low-coupling implementation should require only a small application-facing surface.

Canonical lifecycle hooks:

```c
IR_Result IR_EarlyInit(void);
IR_Result IR_Init(void);
void      IR_SystemStable(void);
```

Canonical runtime evidence paths:

```c
IR_EVENT_TASK(...);
IR_EVENT_ISR(...);

IR_FIRST_ABNORMAL_TASK(...);
IR_FIRST_ABNORMAL_ISR(...);

IR_FATAL_CAPTURE(...);
```

Read-only aggregate status:

```c
IR_Result IR_GetStatus(IR_Status *status);
```

Persistence/export requests are recorder/service-internal by default. Ordinary application code should not need to know:

- persistent storage address,
- filesystem path,
- export transport,
- Flash sector/page details,
- journal slot encoding,
- service retry state.

`IR_EarlyInit()` and `IR_Init()` obtain project/platform configuration through static project adapters or compile/link-time configuration rather than caller-owned configuration objects.

---

## B7. Early Boot Hook

The most important integration point is a small early-boot hook.

Recommended boot sequence:

```text
Reset / Power On
       ↓
Minimal platform initialization
       ↓
IR_EarlyInit()
       ↓
Normal RTOS / Application initialization
```

`IR_EarlyInit()` may inspect:

- retained RAM validity,
- reset cause,
- previous boot state,
- pending persistent incident,
- recorder schema/build identity.

It should not perform expensive I/O in the critical boot path unless explicitly designed and bounded.

---

## B8. Evidence State Machine

Persistent incident data should have an explicit lifecycle.

Example:

```text
EMPTY
  ↓
WRITING
  ↓
COMMITTED
  ↓
PENDING_EXPORT
  ↓
EXPORTED
```

Important rule:

> Reboot must not automatically erase a committed or pending incident.

A pending incident remains valid until:

- explicitly cleared,
- safely superseded by policy,
- overwritten by controlled journal rotation.

---

## B9. Persistent Slot Model

A single persistent area is fragile.

Prefer at least two slots:

```text
Slot A
Slot B
```

or a small journal:

```text
Incident #101
Incident #102
Incident #103
Incident #104
```

Each slot may contain:

```text
magic
schema_version
generation
boot_id / epoch_id
incident_id
data_length
CRC
state
commit marker
```

---

## B10. Torn-Write Protection

A reset or power loss may occur while evidence is being persisted.

The write sequence should therefore be transactional:

```text
1. select target slot
2. mark slot WRITING
3. write body
4. write metadata / CRC
5. write COMMITTED marker last
```

After reboot:

```text
WRITING without valid commit
→ incomplete slot
→ ignore or recover according to policy
```

Previously committed evidence must remain usable.

---

## B11. Retained RAM and Persistent Storage Have Different Roles

Retained RAM is optimized for:

```text
high-frequency
low-latency
low-overhead
runtime evidence
```

Persistent storage is optimized for:

```text
survival across reset/power loss
later export
incident history
```

Recommended conceptual flow:

```text
Runtime Timeline
    ↓
Retained RAM
    ↓
Controlled Persistence
    ↓
Persistent Journal
```

Do not use persistent storage as the normal hot-path logger.

---

## B12. Persistence Triggers

A periodic checkpoint alone is insufficient.

A robust design may support multiple trigger classes:

```text
Periodic checkpoint
First abnormal evidence
Incident latch
Controlled shutdown
Fault/reset recovery
Manual request
```

Important rule:

> A probe should request persistence; it should not perform slow persistent I/O directly.

Preferred:

```text
Probe
  ↓
set/latch request
  ↓
Persistence Service
  ↓
persistent write
```

Avoid:

```c
IR_CheckSomething()
{
    Flash_Write(...);   /* not recommended */
}
```

---

## B13. Automatic and On-Demand Export

Export policy is build-profile and project-policy dependent.

Automatic export may be enabled where it is safe and useful, but it is not a universal requirement. In the Release build profile defined by Part AK, the reference retrieval path is explicit on-demand export from persistent storage after a service user inserts or connects an export destination.

Possible flow when automatic export is enabled:

```text
Boot / Service Start
      ↓
Pending incident exists?
      ↓
Yes
      ↓
Export destination available?
      ↓
Yes
      ↓
Automatic export
```

Reference Release-profile on-demand flow:

```text
Incident already preserved in Internal Flash
      ↓
Service / LCD export request
      ↓
Export destination available?
      ↓
Yes
      ↓
Transactional export
```

Example destinations:

```text
SD card
USB service tool
network service
serial service port
```

Neither automatic nor on-demand export may imply continuous SD logging in a Release build.

The persistent copy may remain after successful export according to the retention policy.

---

## B14. Manual UI Role

The UI should provide management functions such as:

```text
View incident status
Export pending
Re-export previous
Export all
Clear exported records
Show recorder health
```

The UI should not be required for:

```text
initial evidence preservation
reset survivability
first persistence
automatic recovery
```

Therefore:

> UI is an evidence-management interface, not the evidence-survival mechanism.

In a Release build, a Tech/Service LCD page may be the primary **on-demand retrieval interface** for copying Internal Flash evidence to an inserted SD card. This does not make the UI part of the evidence-preservation path and does not permit runtime switching into a Development logging profile.

---

## B15. Export Failure Behavior

Export failure must be fail-safe.

Examples:

```text
SD absent
→ incident remains PENDING_EXPORT

filesystem mount failed
→ incident remains PENDING_EXPORT

USB unavailable
→ incident remains PENDING_EXPORT

export interrupted
→ incident remains valid
```

No export failure should erase the only persistent copy.

---

## B16. Export Completion Policy

Do not erase evidence immediately after successful export.

Preferred:

```text
PENDING_EXPORT
      ↓
EXPORTED
```

The record remains in persistent storage.

It may be overwritten later according to a journal policy, for example:

```text
overwrite oldest EXPORTED record first
preserve newest PENDING record
preserve latest terminal fault
```

---

## B17. Crash-Loop Detection

A system may restart and fail again before export finishes.

Optional crash-loop detection can use persistent boot metadata.

Example:

```text
BOOT_START
```

then, after stable operation:

```text
BOOT_STABLE
```

If the next boot finds:

```text
previous BOOT_START
without BOOT_STABLE
```

the previous execution ended abnormally or too early.

A counter may identify repeated unstable boots.

---

## B18. Recovery Mode Should Not Belong to Recorder Core

The recorder may detect:

```text
pending incident
recent repeated unstable boots
previous watchdog reset
```

But the recorder core should not directly decide:

```text
do not start motor
do not start control task
```

That decision belongs to a separate **Boot Policy / Recovery Manager**.

Recommended dependency:

```text
Incident Recorder
      ↓
reports evidence/status
      ↓
Boot Policy
      ↓
NORMAL / RECOVERY decision
```

This preserves architectural separation.

---

## B19. Optional Recovery Boot

A future recovery mode may use one boot decision point.

Example:

```c
IR_BOOT_STATUS_t ir_status;

ir_status = IR_EarlyInit();

if (BootPolicy_SelectMode(ir_status) == BOOT_MODE_NORMAL)
{
    App_Start();
}
else
{
    Recovery_Start();
}
```

Avoid distributing checks everywhere:

```c
if (!recovery_mode) Motor_Start();
if (!recovery_mode) Control_Start();
if (!recovery_mode) Comm_Start();
...
```

A single boot policy boundary is much lower coupling.

---

## B20. Recovery Mode Content

A minimal recovery mode may enable only what is needed to retrieve evidence.

Possible components:

```text
minimal clocks
watchdog policy
persistent storage
SD/USB service
debug UI
incident exporter
```

Product-specific actuation/control subsystems may remain disabled according to system safety requirements.

This is optional and should be introduced only when justified by repeated crash-loop behavior.

---

## B21. Phased Adoption

### Phase A — Minimal Intrusion

Implement:

```text
retained RAM
persistent pending marker
persistent slots/journal
export service (automatic and/or on-demand)
manual re-export
```

Integration impact:

```text
one early hook
one low-priority service
existing probes
```

No change to normal application startup policy.

---

### Phase B — Reset-Aware Recovery

Add:

```text
reset cause
epoch/boot ID
previous stable marker
pending incident detection
automatic recovery/export status
```

Normal application can still start as before.

---

### Phase C — Crash-Loop Recovery

Add only if needed:

```text
repeated unstable boot detection
boot policy
minimal recovery mode
```

This is the first phase that intentionally changes startup behavior.

---

## B22. Evidence Survivability Model

Recommended overall model:

```text
                   Runtime
                      │
               Retained RAM
                      │
          +-----------+-----------+
          │                       │
   Periodic checkpoint      Incident trigger
          │                       │
          +-----------+-----------+
                      ↓
              Persistent Journal
                      ↓
                PENDING_EXPORT
                      ↓
             Reset / Power Cycle
                      ↓
               Early Boot Check
                      ↓
          +-----------+-----------+
          │                       │
       Auto Export             Manual Export
          │                       │
          +-----------+-----------+
                      ↓
                  EXPORTED
```

---

## B23. Separation of Responsibilities

### Recorder Core

Responsible for:

```text
incident identity
evidence state
retained timeline
first abnormal snapshot
fault snapshot
persistence request
```

### Persistence Adapter

Responsible for:

```text
persistent device operations
slot/journal write
CRC
commit marker
readback
```

### Export Adapter

Responsible for:

```text
SD
USB
UART
network
other transport
```

### Boot Policy

Responsible for:

```text
normal boot
recovery boot
crash-loop policy
```

### UI

Responsible for:

```text
status
manual export
re-export
clear/archive commands
```

No single module should own all of these concerns.

---

## B24. Suggested Module Layout

Conceptual structure:

```text
incident_recorder/
├─ core/
│  ├─ ir_core.c
│  ├─ ir_state.c
│  ├─ ir_snapshot.c
│  └─ ir_retained.c
│
├─ persistence/
│  ├─ ir_persistence.c
│  └─ ir_journal.c
│
├─ recovery/
│  ├─ ir_recovery_status.c
│  └─ ir_export_service.c
│
├─ adapters/
│  ├─ ir_persistent_storage_adapter.c
│  ├─ ir_sd_export_adapter.c
│  └─ ir_usb_export_adapter.c
│
└─ project/
   ├─ ir_project_config.h
   └─ ir_project_hooks.c

boot_policy/
└─ boot_policy.c
```

The exact file names are not normative.

---

## B25. Service Task Design

A low-priority service task may process non-real-time work:

```text
persistence request
export request
retry
journal housekeeping
status update
```

Conceptual example:

```c
void IncidentServiceTask(void)
{
    for (;;)
    {
        IR_ProcessPersistence();
        IR_ProcessExport();
        IR_ProcessHousekeeping();

        IR_PlatformServiceDelay(IR_SERVICE_PERIOD);
    }
}
```

Requirements:

```text
low priority
bounded work per iteration
no impact on control deadlines
no indefinite retry loop
```

---

## B26. Failure Isolation

The recorder/recovery subsystem must fail silently where possible.

Examples:

```text
Recorder initialization fails
→ recorder disabled/degraded
→ application continues

Export target absent
→ keep pending record
→ application continues

Persistent CRC invalid
→ reject that slot
→ preserve other valid slots
→ application continues

Service queue full
→ increment lost/request counter
→ do not block critical task
```

---

## B27. Boot Dependency Rule

Never use:

```c
if (!IR_Init())
{
    while (1);
}
```

Prefer:

```c
(void)IR_Init();
```

or:

```c
if (!IR_Init())
{
    IR_DisableRecorder();
}
```

The product must not require the diagnostic recorder to function normally.

---

## B28. Flash / Persistent Wear Policy

Frequent persistence must consider endurance.

Therefore:

```text
high-frequency evidence → RAM
incident snapshot       → RAM first
persistent write        → controlled event
periodic checkpoint     → configurable
```

Use:

```text
journal rotation
generation counters
wear distribution
event-triggered persistence
```

Do not solve evidence survivability by continuously writing every runtime record to persistent storage.

---

## B29. Power-Loss Boundary

No software-only architecture can guarantee preservation of evidence that exists only in volatile RAM at the exact moment of abrupt power removal.

Therefore evidence survivability has levels:

```text
Level 0  Runtime RAM only
Level 1  Reset-survivable retained RAM
Level 2  Periodically persisted evidence
Level 3  Incident-triggered persisted evidence
Level 4  Previously committed incident journal
```

The framework should report which level is available on a specific platform.

---

## B30. Watchdog Integration

Where a hardware watchdog exists, it can improve evidence recovery.

Conceptual flow:

```text
Application hangs
      ↓
Watchdog reset
      ↓
Early boot
      ↓
read reset cause
      ↓
recover retained evidence
      ↓
persist/export
```

This is usually more recoverable than waiting for a human to remove power.

However, SRAM retention behavior across watchdog/system reset must be validated on the target MCU.

---

## B31. Reset Cause as Evidence

The boot record should capture platform reset causes when available:

```text
power-on reset
watchdog reset
software reset
brownout
external reset
fault-induced reset
unknown
```

The recorder should preserve the raw platform cause bits where practical.

Interpretation belongs to the decoder/project layer.

---

## B32. Boot Epoch

Each boot should have a unique or monotonic execution epoch.

Example:

```text
epoch_id = 0x00001234
```

Evidence then becomes:

```text
epoch
sequence
timestamp
incident_id
```

This prevents confusion between:

```text
previous crash
current boot
old exported incident
new pending incident
```

---

## B33. Interaction with First-Abnormal-State Localization

Evidence survivability and evidence quality are separate dimensions.

The complete framework should support both:

```text
A. Evidence Quality
   - first abnormal state
   - memory/state/temporal/sequence invariant
   - operation context

B. Evidence Survivability
   - retained RAM
   - persistent journal
   - transactional commit

C. Evidence Accessibility
   - automatic export where enabled
   - on-demand/manual export
   - recovery mode
```

A strong detector is useless if the evidence disappears.

A strong persistent logger is also insufficient if it records only the final crash with no causal context.

---

## B34. Generic Incident Lifecycle

A generic incident may move through:

```text
OBSERVING
    ↓
ABNORMAL_LATCHED
    ↓
PERSIST_REQUESTED
    ↓
PERSISTED
    ↓
PENDING_EXPORT
    ↓
EXPORTED
    ↓
ARCHIVED / OVERWRITABLE
```

This lifecycle is independent of:

```text
fault type
physical storage
transport
product
```

---

## B35. Recommended Priority

For an existing embedded product where major application refactoring is undesirable:

1. preserve current application architecture;
2. keep recorder core modular;
3. add one early boot hook;
4. add a low-priority persistence/export service;
5. make persistent incidents survive reboot;
6. select automatic and/or on-demand export according to the compile-time build profile and project policy;
7. for Release builds using SD retrieval, keep explicit Tech/Service export available without requiring continuous SD logging;
8. add crash-loop recovery only after real-world validation shows it is needed.

This provides high diagnostic value with limited architectural impact.

---

## B36. Validation Matrix

Before integration into the main framework, validate:

| Scenario | Expected Result |
|---|---|
| Normal boot, no incident | Normal application behavior unchanged |
| Pending incident, no SD | Incident remains pending |
| Development profile, SD present | Continuous development logging may proceed through the isolated SD writer |
| Development profile, SD removed/full/error | Product operation continues; development trace is bounded/dropped and loss is observable |
| Release profile, normal runtime | No continuous SD logging is active |
| Release profile, pending incident, no SD | Incident remains in persistent storage |
| Release profile, LCD/service export with SD present | Internal Flash evidence exports transactionally to SD |
| Export interrupted | Original persistent incident remains valid |
| Power loss during persistent write | Previous committed slot remains usable |
| Recorder disabled | Application still functions |
| Recorder internal error | Application continues |
| Reboot after incident | Incident survives |
| Repeated quick crashes | Boot metadata identifies unstable sequence |
| Recovery mode disabled | Normal boot remains unchanged |
| Recovery mode enabled | Single boot-policy decision selects recovery |
| Tech page unavailable | Evidence can still survive/export automatically |

---

## B37. Observer-Effect Audit

Measure recorder/recovery ON versus OFF:

```text
boot time
task scheduling
control-loop latency
CPU load
stack watermark
RAM consumption
Flash write duration
SD export duration
lost recorder requests
```

Persistent writes and export must not occur in timing-critical paths.

---

## B38. Acceptance Criteria for Low Coupling

The architecture can be considered low-coupling when:

### Normal runtime

- application logic does not depend on recorder success;
- recorder does not alter application return values;
- recorder does not block control paths;
- recorder uses bounded RAM;
- recorder performs no hot-path external I/O.

### Boot

- one small early hook is sufficient;
- recorder failure does not prevent normal boot;
- recovery policy is isolated from recorder core.

### Persistence

- incident records survive normal reboot;
- torn writes are detectable;
- previous committed evidence is protected.

### Export

- manual UI is optional;
- absence of export transport does not lose evidence;
- successful export does not immediately destroy the persistent copy.

### Modularity

- storage and transport are adapters;
- application-specific logic remains outside core;
- recovery policy is separately configurable.

---

## B39. Architectural Summary

The recommended abstraction is:

```text
Application
    │
    ├── observations
    ├── first-abnormal indication
    └── fault/reset context
           ↓
    Incident Recorder Core
           ↓
      Retained Evidence
           ↓
   Persistence Service
           ↓
    Persistent Journal
           ↓
   Recovery / Export Service
           ↓
      Export Adapters
           ↓
      PC / Service Tool
```

Optional crash-loop handling:

```text
Incident Recorder Status
          ↓
      Boot Policy
          ↓
 NORMAL / RECOVERY
```

The recorder observes and preserves.

The boot policy decides startup behavior.

The application remains independent of recorder success.

---

## B40. Key Principle

> **Do not require the system to remain usable long enough for a human to preserve the evidence.**

The framework should preserve evidence into retained/persistent storage without depending on immediate human action and make it recoverable after restart.

A Tech/Service page may be the primary Release-profile retrieval interface, but it remains secondary to the automatic evidence-preservation path.

---

---

# Part C — Unified Module Boundary

## C1. Recommended High-Level Structure

```text
Application / Control Logic
        │
        ├── observations
        ├── transitions
        ├── operations
        ├── invariant checks
        └── incident/fault context
        │
        ▼
+----------------------------------+
| Incident Recorder Core           |
| - evidence records               |
| - timeline                       |
| - first abnormal evidence        |
| - incident state                 |
| - boot/epoch metadata            |
+----------------------------------+
        │
        ▼
+----------------------------------+
| Retained Evidence Store          |
+----------------------------------+
        │
        ▼
+----------------------------------+
| Persistence Service              |
| - journal / slots                |
| - CRC / commit semantics         |
| - pending/export state           |
+----------------------------------+
        │
        ▼
+----------------------------------+
| Recovery / Export Service        |
+----------------------------------+
        │
   +----+----+---------+
   │         │         │
   ▼         ▼         ▼
  SD        USB      other
adapter   adapter   adapter
        │
        ▼
PC / Service Tool
- decode
- symbolization
- timeline merge
- correlation
- candidate ranking
```

Optional startup policy remains separate:

```text
Recorder Status
      ↓
Boot Policy
      ↓
NORMAL / RECOVERY
```

The recorder reports evidence and status.

The boot policy decides startup behavior.

---

## C2. Low-Coupling Dependency Rule

Preferred:

```text
Application
    ↓
Recorder
```

Avoid:

```text
Application
    ↓
Recorder
    ↓
Application behavior
```

Normal product functionality shall not depend on recorder success.

Exceptions such as recovery boot must be isolated behind a boot-policy boundary and must not be embedded throughout application logic.

---

## C3. Minimal Integration Surface

The canonical application-facing surface is intentionally small:

```c
IR_EarlyInit();
IR_Init();
IR_SystemStable();

IR_EVENT_TASK(...);
IR_EVENT_ISR(...);

IR_FIRST_ABNORMAL_TASK(...);
IR_FIRST_ABNORMAL_ISR(...);

IR_FATAL_CAPTURE(...);

IR_GetStatus(...);
```

Semantic record classes such as Observation, Operation, and Transition remain part of the evidence model, but are encoded through the context-specific event API rather than separate public functions.

Requirements:

- compile-time removable where applicable;
- safe no-op/drop behavior when disabled or not initialized;
- non-blocking hot-path behavior;
- no dynamic allocation;
- no strings in hot-path records;
- fixed/bounded execution time;
- no direct persistence/export dependency from probe calls;
- no service lock may block Task/ISR/Fatal evidence paths.

---

# Part D — Unified Evidence Lifecycle

## D1. Runtime Evidence Lifecycle

```text
Observation
    ↓
Transition / Operation
    ↓
Invariant Check
    ↓
First Abnormal Evidence
    ↓
Incident Latch
    ↓
Persistence Request
```

## D2. Persistence Lifecycle

```text
EMPTY
  ↓
WRITING
  ↓
COMMITTED
  ↓
PENDING_EXPORT
  ↓
EXPORTED
  ↓
ARCHIVED / OVERWRITABLE
```

## D3. Boot/Recovery Lifecycle

```text
Reset / Power On
      ↓
Early Recorder Check
      ↓
Pending Evidence?
      ↓
Recovery/Export Service
      ↓
Normal Boot
or
Boot Policy → Recovery Mode
```

---

# Part E — Evidence Quality vs Survivability

These are independent requirements.

## E1. Evidence Quality

The recorder must capture enough context to identify:

```text
last valid state
last meaningful operation
first abnormal evidence
propagation
terminal fault/recovery
```

## E2. Evidence Survivability

The recorder must ensure that evidence can survive:

```text
software hang
watchdog reset
software reset
unexpected reboot
repeated quick failures
```

Power-loss survivability depends on the platform and the persistence level already achieved before power removal.

## E3. Evidence Accessibility

Evidence retrieval should not depend on:

```text
user reaction time
ability to reach a Tech/Service page
continued availability of the normal UI
continued availability of the failed subsystem
```

Automatic recovery/export is preferred when safe and practical.

---

# Part F — Validation Strategy

## F1. Known-Root-Cause Validation

For each known defect:

```text
Known defect
    ↓
Recorder OFF reproduces defect
    ↓
Recorder ON still reproduces defect
    ↓
Recorder identifies first abnormal evidence
    ↓
Fixed firmware removes abnormal evidence
```

This validates both diagnostic value and observer effect.

A physical device-specific defect may be used as a test case, but shall not define the framework abstraction.

---

## F2. Core Validation Matrix

| Area | Validation |
|---|---|
| Functional coupling | Application behavior unchanged when recorder succeeds/fails |
| Timing | Hot-path probes bounded; no control-loop regression |
| Stack | Recorder ON/OFF stack watermark comparison |
| RAM | Fixed budget; retained section isolated |
| Persistent write | Torn-write/power-loss test |
| Reset | Previous committed evidence survives |
| Export | Missing/failed transport does not erase incident |
| Flooding | Duplicate/coalescing behavior prevents timeline destruction |
| Schema | Old/new record versions decoded correctly |
| Boot epoch | Evidence from different boots remains separable |
| Crash loop | Repeated unstable boots detected without recorder owning application policy |
| Known bug | First abnormal evidence is localizable without JTAG |
| Recorder failure | Diagnostic failure does not become product failure |

---

# Part G — Current RAM Direction

The default retained RAM budget remains:

```text
10 KiB
```

The current design priority is:

> Improve evidence information density before increasing memory capacity.

A conceptual partition:

```text
10 KiB Retained RAM
├─ Timeline Ring
├─ First-Abnormal Snapshot
├─ Terminal Fault Snapshot
└─ Header / Epoch / Counters / Health
```

Exact byte allocation remains implementation-specific until measured on target hardware.

---

# Part H — GitHub Integration Direction

After validation, this unified document may become the architecture basis for the main repository.

Possible future split:

```text
docs/
├─ architecture.md
├─ evidence-model.md
├─ retained-persistence.md
├─ recovery-export.md
├─ observer-effect-validation.md
├─ probe-api.md
└─ known-root-cause-validation.md
```

However, during RC convergence, maintaining **one unified document** is preferred to avoid inconsistent definitions across multiple drafts.

---

# Part I — Current Working Principles

1. **Observe facts before drawing conclusions.**
2. **Preserve the first abnormal evidence.**
3. **Crash location is not assumed to be failure origin.**
4. **Do not let repetitive consequences overwrite origin evidence.**
5. **MCU-side work must remain lightweight and bounded.**
6. **Heavy correlation belongs on the PC/service side.**
7. **Recorder core must remain independent of physical media and buses.**
8. **Storage, persistence, export, and UI are separate concerns.**
9. **Tech/Service UI is not the evidence-survival mechanism; in a Release build it may be the primary on-demand retrieval interface.**
10. **Recorder failure must not become product failure.**
11. **Boot/recovery policy is separate from recorder core.**
12. **Use explicit build/schema/epoch identity.**
13. **Validate with real known-root-cause defects.**
14. **Measure observer effect instead of assuming it is negligible.**
15. **Increase evidence quality before increasing RAM size.**
16. **Development/Release storage profiles are compile-time policies, not runtime UI modes.**
17. **Continuous Development Trace is separate from persistent Incident Evidence.**

---

# Part J — RC Exit Criteria

Before promoting `v1.0.0rc02` to target-validation work in `v1.0.0rc03`:

```text
- Release reference build compiles and links with warnings treated as errors
- Development reference build compiles and links with warnings treated as errors
- Recorder-OFF probe macros compile away without evaluating arguments
- C99 compatibility build passes; C11 build uses _Static_assert size guards
- selected Task ring + ISR ring + dedicated Fatal Snapshot topology is implemented
- first-abnormal ownership uses the project bounded critical primitive and publishes validity last
- retained recorder storage fits the configured 10 KiB ceiling
- linked MAP/section report contains the dedicated .incident_ram section
- early-boot containment performs no persistence/export/filesystem I/O
- Development continuous trace uses a bounded RAM queue and low-priority service path
- Release build compiles out the continuous trace queue/writer path
- persistence adapter receives an explicit logical evidence view rather than a raw on-flash struct dump requirement
- on-demand export advances in bounded service steps and verifies before marking evidence exported
- successful export does not delete the persistent evidence bytes
- public-release hygiene audit finds no company/product/project-specific identifiers or private implementation fingerprints
```

The host reference build validates structure and control-flow contracts only. MCU-specific linker/startup retention, atomic critical-section timing, RTOS interaction, storage timing/endurance, and observer effect remain target-validation responsibilities.

# Part K — Document Status

**Current:** `v1.0.0rc02`

This is the **Minimal Frozen Reference Implementation Release Candidate**.

The public specification remains generic. The repository now also contains a portable embedded-C reference implementation and host validation harness that demonstrate the selected writer model, retained-store budget, compile-time storage profiles, persistence/export boundaries, and fail-isolated service behavior.

Future revisions:

```text
v1.0.0rc03  Known-root-cause + observer-effect target validation
...
v1.0.0      Stable baseline
```

Do not edit an archived RC in place.

# Part L — Failure Boundary & Survivability Contract

## L1. Why This Contract Exists

The framework must state clearly what evidence can and cannot survive each class of failure.

A recorder must not imply guarantees that the hardware/platform cannot provide.

The architecture therefore distinguishes:

```text
detection capability
retention capability
persistence capability
recovery capability
export capability
```

These are independent.

---

## L2. Survivability Guarantee Matrix

Each project/platform shall complete a matrix equivalent to the following.

| Failure Class | Retained RAM | Last Committed Persistent Record | New Incident Snapshot | Recovery Path |
|---|---|---|---|---|
| Task stall / deadlock | Usually available until reset/power loss | Yes | If detector executes before reset | Watchdog / manual reset |
| HardFault / fatal exception | Platform/reset dependent | Yes | Minimal fault capture may be possible | Early-boot salvage |
| Watchdog reset | Platform dependent; must be verified | Yes | Depends on prior latch/fault path | Early-boot salvage |
| Software reset | Platform dependent; must be verified | Yes | Depends on reset path | Early-boot salvage |
| External reset | Platform dependent | Yes | Usually no new snapshot at reset instant | Early-boot salvage |
| Brownout | Not guaranteed | Only fully committed data | Not guaranteed | Next boot |
| Sudden power removal | No volatile guarantee | Only fully committed data | No guarantee | Next boot |
| Repeated quick crash | Previous committed evidence should survive | Yes | New evidence depends on time before next crash | Crash-loop handling |

This matrix is a **platform contract**, not a universal promise.

---

## L3. Early-Boot Salvage / Containment

The boot path should protect evidence from the previous execution before normal application activity can overwrite it.

Recommended sequence:

```text
Reset / Power On
      ↓
Minimal hardware initialization
      ↓
Read raw reset cause
      ↓
Read fixed retained header only
      ↓
Validate magic / schema / fixed bounds / header integrity
      ↓
Valid?
 ┌────┴────┐
 No        Yes
 ↓          ↓
mark       identify unfinished previous epoch
RETENTION  preserve/mark fixed evidence metadata
INVALID    request deferred persistence if needed
 ↓          ↓
continue normal startup
```

`IR_EarlyInit()` is a **containment path, not a forensic-analysis path**.

Before retained metadata is validated, it shall not:

- follow retained/application pointers;
- use retained length/count/index values as unchecked loop bounds;
- access arbitrary application memory;
- mount a filesystem;
- perform I2C/SPI/UART/USB/network transactions;
- wait for RTOS objects;
- perform unbounded retry;
- erase/program persistent storage as part of ordinary early salvage.

If the retained header is invalid:

```text
set RETENTION_INVALID
do not trust variable-length retained content
continue boot through a deterministic escape path
```

The platform integration must also document watchdog behavior during `IR_EarlyInit()`:

```text
watchdog disabled/not yet enabled
or
early-init WCET safely below watchdog margin
or
a separately validated bounded watchdog-service action is allowed
```

Early boot must remain fixed, bounded, and independent of services that may not yet exist.

---

## L4. Fatal-Context Restrictions

HardFault/NMI/fatal-context capture is independent from First Abnormal Evidence.

**First Abnormal Evidence**

```text
earliest invariant/detector abnormality successfully latched
```

**Fatal Snapshot**

```text
terminal CPU/fault evidence captured by the fatal handler
```

A fatal handler never overwrites or competes for the first-abnormal snapshot.

Allowed fatal-context operations are limited to fixed, bounded capture such as:

```text
PC
LR
SP
PSR
raw fault status registers
fault addresses
current context/task identity if safely readable
epoch identity
local sequence/timestamp if safely readable
recorder health summary
first-abnormal-valid status
```

Fatal context shall not:

- claim the first-abnormal slot;
- touch Task/ISR timeline writer indices unless explicitly proven safe;
- mount filesystems;
- perform SD/USB/network export;
- use formatted printing;
- allocate/free memory;
- wait on semaphores or mutexes;
- follow application pointers;
- use untrusted lengths;
- perform large memory scans;
- depend on the normal recorder service task.

Recommended flow:

```text
Fatal Context
     ↓
minimal fixed snapshot
     ↓
publish fatal-snapshot valid marker
     ↓
reset / watchdog path
     ↓
Early-Boot Containment
     ↓
Deferred Persistence / Export
```

If no first-abnormal detector fired before the fatal event:

```text
first_abnormal_valid = false
fatal_snapshot_valid = true
```

The analysis tool must not reinterpret the fatal snapshot as proof of the original root cause.

---

## L5. Reset-Retention Capability Model

The framework must not assume that `.noinit` or retained RAM survives every reset.

Each platform adapter should declare verified capabilities such as:

```c
IR_CAP_RETENTION_WATCHDOG_RESET
IR_CAP_RETENTION_SOFTWARE_RESET
IR_CAP_RETENTION_EXTERNAL_RESET
IR_CAP_RETENTION_FAULT_RESET
IR_CAP_RETENTION_BROWNOUT
IR_CAP_EARLY_BOOT_SALVAGE
```

These names are conceptual.

Each capability must be based on:

```text
MCU reference manual
linker/startup behavior
startup code review
target hardware test
```

The platform adapter should document whether startup code clears the selected RAM region.

---

## L6. Reset Taxonomy

At minimum, the framework should distinguish:

```text
POWER_ON
WATCHDOG
SOFTWARE
EXTERNAL
BROWNOUT
FAULT_RELATED
UNKNOWN
```

If the MCU provides raw reset-cause bits, preserve the raw value as evidence.

PC-side interpretation may map raw values to framework categories.

---

## L7. Persistence Timing Contract

Persistence adapters shall publish timing characteristics.

Required measurements should include:

```text
erase time
program time
commit-marker time
worst-case write duration
interrupt masking duration
scheduler impact
readback/CRC duration
```

The persistence layer must not be assumed safe for a real-time path merely because the API returns successfully.

---

## L8. Persistence Wear Budget

Persistent storage strategy must include a wear/endurance estimate.

Platform/project parameters should include:

```text
erase/program endurance
slot size
journal depth
checkpoint frequency
incident persistence frequency
expected writes per day
expected operating years
wear distribution policy
```

A simple budget review should answer:

> Can the selected policy meet expected product lifetime with margin?

The framework should favor:

```text
RAM for high-frequency evidence
persistent storage for controlled checkpoints/incidents
journal rotation for wear distribution
```

---

## L9. Atomic First-Abnormal Latch

"First abnormal evidence" is meaningful only if ownership is deterministic.

The canonical writer topology uses separate Task and ISR runtime paths, but both may compete for the single first-abnormal snapshot.

Required behavior:

```text
first eligible Task/ISR detector atomically claims ownership
winner writes the fixed snapshot
snapshot-valid marker is published last
later detections do not overwrite it
later detections may be recorded as propagation evidence
fatal context does not participate in this ownership race
```

A shared-boolean check-then-act sequence is prohibited.

The platform adapter must provide a bounded primitive suitable for the selected target, for example:

```text
very short interrupt-mask critical section
atomic compare/exchange where valid and verified
equivalent bounded ownership primitive
```

The implementation must document:

- Task and ISR nesting assumptions;
- interrupt priorities affected by the primitive;
- maximum critical-section / interrupt-off duration;
- behavior when ownership was claimed but snapshot publication was interrupted.

No RTOS mutex, service lock, persistence lock, or blocking primitive may be used for this latch.

---

## L10. First-Abnormal Ordering

A first-abnormal snapshot should contain enough ordering metadata to compare near-simultaneous evidence:

```text
epoch_id
context_type (Task / ISR)
context-local sequence
timestamp/tick
source/object ID
rule/event ID
valid/commit state
```

Task and ISR timeline rings may use independent local sequence counters.

A global cross-context sequence counter is optional and shall not be introduced if it creates excessive contention.

PC-side reconstruction should use:

1. epoch identity;
2. timestamp;
3. context-local sequence;
4. explicit synchronization/operation correlation evidence when available.

If total ordering cannot be proven, the analysis tool must preserve ordering uncertainty rather than invent a precise order.

---

## L11. Recorder Degraded Mode

Recorder health must be observable.

Suggested states:

```text
RECORDER_OK
RECORDER_DEGRADED
RETENTION_INVALID
PERSISTENCE_FAILED
EXPORT_FAILED
SCHEMA_MISMATCH
RECORDS_DROPPED
```

The exact encoding may be bit flags rather than an enum.

A degraded recorder should not normally stop the application.

---

## L12. Evidence Completeness Metadata

An exported incident should state whether the evidence is complete.

Example metadata:

```text
recorder_health = DEGRADED
lost_records = 17
timeline_valid = true
first_abnormal_valid = true
fault_snapshot_valid = true
persistent_crc_valid = true
export_complete = true
```

This prevents missing evidence from being misinterpreted as "nothing happened".

---

## L13. Recorder Self-Health Counters

Recommended counters:

```text
lost_record_count
dropped_low_priority_count
persistence_failure_count
export_failure_count
invalid_slot_count
torn_record_count
early_salvage_count
reset_recovery_count
```

Counters should be saturating or have defined wrap behavior.

They should not themselves create high-frequency logging.

---

# Part M — Unified Capability Matrix

## M1. Detection Capability

```text
Observation
Memory Invariant
State Invariant
Temporal Invariant
Sequence Invariant
Transition Evidence
First Abnormal Evidence
Terminal Fault Capture
```

## M2. Survival Capability

```text
Runtime RAM
Reset-Retained RAM
Early-Boot Salvage
Periodic Persistent Checkpoint
Incident-Triggered Persistence
Persistent Journal
Torn-Write Protection
```

## M3. Recovery Capability

```text
Pending Incident Detection
Boot Epoch Recovery
Reset Cause Capture
Repeated-Unstable-Boot Detection
Optional Crash-Loop Recovery
```

## M4. Access Capability

```text
Automatic Export
Manual Export
Re-export
Service/PC Retrieval
Multiple Export Adapters
```

## M5. Analysis Capability

```text
Build ID / Schema ID
ELF/MAP Symbolization
Address/Object Correlation
Timeline Reconstruction
Operation Correlation
Cross-Node Correlation
Root-Cause Candidate Ranking
Evidence Confidence / Provenance
```

A project does not need to implement every capability at once.

The framework should make implemented/unsupported capabilities explicit.

---

# Part N — Reference Implementation Guidance

## N1. Minimum Reference Prototype Path

For an existing firmware where architectural intrusion must stay low:

```text
1. keep 10 KiB retained evidence budget
2. add verified retained-RAM linker/startup handling
3. add atomic first-abnormal latch
4. add minimal fatal-context snapshot
5. add early-boot salvage hook
6. add persistent A/B slot or small journal
7. add recorder health/status metadata
8. keep export in low-priority service context
```

Do not require crash-loop recovery mode yet.

---

## N2. Prototype Non-Goals

This RC does not require:

```text
major application state-machine refactor
mandatory safe/recovery boot
continuous Flash logging
large RAM expansion
complex MCU-side root-cause inference
physical-device-specific recorder core logic
```

The emphasis remains:

> Low coupling, high evidence value, explicit survivability boundaries.

---

# Part O — Version History

| Version | Date | Purpose |
|---|---|---|
| v1.0.0rc01 | 2026-08-21 | Initial public specification baseline |
| v1.0.0rc02 | 2026-08-21 | Minimal frozen reference implementation plus compile/link/MAP/size validation |

Planned direction:

```text
v1.0.0rc03 → known-root-cause + observer-effect target validation
v1.0.0     → stabilized framework baseline
```

# Part P — Implementation Contract

## P1. Implementation Objective

`v1.0.0rc02` freezes the minimal public reference implementation without binding the framework to a specific MCU, RTOS, storage technology, peripheral, company, or product. It implements the selected Task/ISR/Fatal writer topology, bounded retained store, compile-time storage profiles, persistence/export service boundaries, and failure-isolated Development/Release paths while keeping platform-specific mechanisms behind adapters.

The implementation model is:

```text
Project Probe / Adapter
        ↓
Recorder Public API
        ↓
Recorder Core
        ↓
Retained Store
        ↓
Persistence Service
        ↓
Export Service
```

The API must preserve these properties:

```text
low coupling
bounded hot-path execution
compile-time removability
no dynamic allocation
no blocking in probe/fatal paths
no direct physical I/O from probe calls
schema-aware persistence
platform-independent core
```

---

## P2. Implementation Language Assumptions

The reference contract targets embedded C.

Recommended baseline:

```text
C99 or later
fixed-width integer types
no dynamic allocation
static configuration
explicit alignment
compile-time feature switches
```

C++ wrappers may be added by a project, but the core contract should remain C-compatible.

---

# Part Q — Canonical Public API Surface

## Q1. Lifecycle API

Canonical public functions:

```c
IR_Result IR_EarlyInit(void);
IR_Result IR_Init(void);
void      IR_SystemStable(void);
```

Semantics:

```text
IR_EarlyInit()
    Perform bounded previous-epoch containment and reset/retention inspection.

IR_Init()
    Initialize normal runtime recorder state using static project/platform adapters.

IR_SystemStable()
    Mark the current boot/epoch as having reached the configured stable point.
```

Lifecycle safety:

```text
Recorder disabled
→ safe no-op / IR_NOT_AVAILABLE

Evidence call before IR_Init()
→ safe drop/no-op

Recorder degraded
→ bounded behavior or safe drop
```

Undefined behavior, blocking initialization waits, and production reset/assert behavior are not allowed merely because the recorder is unavailable.

---

## Q2. Runtime Evidence API

Application probe sites use compile-time-removable macros:

```c
IR_EVENT_TASK(record_type, id, value0, value1);
IR_EVENT_ISR(record_type, id, value0, value1);
```

When `IR_ENABLE != 0`, the macros map to the reference implementation entry points `IR_EventTask()` and `IR_EventIsr()`. When `IR_ENABLE == 0`, they expand to no-ops and do not evaluate their arguments.

Observation / Operation / Transition are evidence semantics represented by `record_type`; they are not separate application-facing functions.

The Task path writes only the Task ring. The ISR path writes only the ISR ring. Neither path waits for persistence, export, filesystem, transport, or service work.

## Q3. First-Abnormal API

Task and ISR probe sites use:

```c
bool claimed = IR_FIRST_ABNORMAL_TASK(object_id,
                                      rule_id,
                                      observed0,
                                      observed1);

bool claimed_isr = IR_FIRST_ABNORMAL_ISR(object_id,
                                         rule_id,
                                         observed0,
                                         observed1);
```

Return value:

```text
true
→ this context atomically claimed and published the first-abnormal snapshot

false
→ another context already owns it, recorder is unavailable, or publication could not be completed
```

The reference implementation uses the project-provided bounded critical primitive for the ownership transition and publishes the fixed snapshot valid state last.

Persistence is requested internally after successful publication; the probe does not perform physical persistence.

## Q4. Fatal-Context API

Fatal integration uses:

```c
IR_FATAL_CAPTURE(&fault_frame);
```

When enabled, the macro maps to `IR_FatalCapture()` and writes only the dedicated fatal snapshot.

It does not overwrite, claim, or reinterpret First Abnormal Evidence. It does not perform filesystem, persistence, export, or service work from fatal context.

The contract in Part L4 remains authoritative for platform-specific fatal-context restrictions.

## Q5. Status API

Canonical aggregate read-only status:

```c
IR_Result IR_GetStatus(IR_Status *status);
```

`IR_Status` may include:

```text
health flags
epoch ID
latest incident ID
pending-persist/export status
lost-record counters
first-abnormal validity
fatal-snapshot validity
```

Detailed adapter/service errors remain internal diagnostics and should not expand the ordinary application-facing result type.

---

## Q6. Service Interface Boundary

Persistence/export execution is outside the ordinary application hot-path API.

The frozen minimal service interface is:

```c
void      IR_ServiceProcess(void);
IR_Result IR_ServiceRequestExport(void);
```

`IR_ServiceProcess()` is called from a project-defined low-priority task or bounded periodic main-loop service point.

`IR_ServiceRequestExport()` only latches a request. A Release service/LCD command may call it after an export destination is made available; it does not perform Flash/SD/filesystem I/O in the caller context.

Task/ISR/Fatal evidence calls never wait for the service.

# Part R — Compile-Time Probe Interface

## R1. Feature Switches and Build Profile

The Development/Release storage profile is a compile-time policy:

```c
#define IR_BUILD_PROFILE_DEVELOPMENT  (1U)
#define IR_BUILD_PROFILE_RELEASE      (2U)

#ifndef IR_BUILD_PROFILE
#define IR_BUILD_PROFILE IR_BUILD_PROFILE_RELEASE
#endif

#if (IR_BUILD_PROFILE == IR_BUILD_PROFILE_DEVELOPMENT)
#define IR_ENABLE_CONTINUOUS_SD_TRACE (1U)
#elif (IR_BUILD_PROFILE == IR_BUILD_PROFILE_RELEASE)
#define IR_ENABLE_CONTINUOUS_SD_TRACE (0U)
#else
#error "Invalid IR_BUILD_PROFILE"
#endif
```

Runtime switching between Development and Release profiles is not permitted.

Recommended feature switches:

```c
#define IR_ENABLE                    1
#define IR_ENABLE_MEMORY_INVARIANT   1
#define IR_ENABLE_STATE_INVARIANT    1
#define IR_ENABLE_TEMPORAL_INVARIANT 1
#define IR_ENABLE_SEQUENCE_INVARIANT 1
#define IR_ENABLE_PERSISTENCE        1
#define IR_ENABLE_EXPORT             1
```

A project may refine individual feature switches, but no runtime configuration may activate continuous Development SD logging in a Release build.

---

## R2. Compile-Out Contract

When `IR_ENABLE == 0`, probe calls compile away and shall not evaluate their arguments.

Example:

```c
#if IR_ENABLE

#define IR_EVENT_TASK(type_, id_, v0_, v1_)     IR_EventTask((type_), (id_), (uint32_t)(v0_), (uint32_t)(v1_))

#define IR_EVENT_ISR(type_, id_, v0_, v1_)     IR_EventIsr((type_), (id_), (uint32_t)(v0_), (uint32_t)(v1_))

#else

#define IR_EVENT_TASK(type_, id_, v0_, v1_) ((void)0)
#define IR_EVENT_ISR(type_, id_, v0_, v1_)  ((void)0)

#endif
```

The public probe macros in `include/incident_recorder.h` are frozen for this minimal reference baseline.

Probe arguments must be side-effect free.

Forbidden:

```c
IR_EVENT_TASK(TYPE_X, EVENT_X, FunctionWithSideEffect(), value);
```

Recommended validation:

- compile with `IR_ENABLE=0`;
- enable normal compiler warnings;
- add a static-analysis/code-review rule where available;
- verify the OFF build does not change application behavior other than removing evidence recording.

---

## R3. Probe Placement Rule

Probe calls may be distributed through application code, but:

```text
core implementation remains centralized
probe macros remain thin
project IDs remain project-owned
```

Avoid embedding persistence/export logic at probe sites.

---

# Part S — Core Type System

## S1. Fixed-Width IDs

Recommended types:

```c
typedef uint32_t IR_EpochId;
typedef uint32_t IR_IncidentId;
typedef uint32_t IR_Sequence;
typedef uint16_t IR_EventId;
typedef uint16_t IR_ObjectId;
typedef uint16_t IR_RuleId;
typedef uint16_t IR_OperationId;
typedef uint16_t IR_RecordType;
```

ID width may be tuned after implementation sizing, but persisted schema widths must remain explicit.

---

## S2. Application-Visible Result Type

The ordinary application-facing result is intentionally small:

```c
typedef enum
{
    IR_OK = 0,
    IR_NOT_AVAILABLE
} IR_Result;
```

The application should not branch on detailed recorder-internal failure causes.

Detailed conditions belong in:

```text
IR_Status / health flags
private core errors
persistence-adapter errors
export-adapter errors
platform diagnostics
```

This separation reduces functional coupling between product behavior and diagnostic infrastructure.

---

## S3. Context Type

Suggested context classification:

```c
typedef enum
{
    IR_CONTEXT_TASK = 0,
    IR_CONTEXT_ISR,
    IR_CONTEXT_FAULT,
    IR_CONTEXT_EARLY_BOOT,
    IR_CONTEXT_SERVICE,
    IR_CONTEXT_UNKNOWN
} IR_ContextType;
```

---

# Part T — Runtime Record Format

## T1. Design Requirements

A runtime record should be:

```text
fixed-size or efficiently parseable
naturally aligned
small
torn-write detectable where required
independent of pointer size where persisted
schema-versioned at container level
```

---

## T2. Selected 24-Byte Runtime Record

`v1.0.0rc02` freezes the reference runtime timeline record as:

```c
typedef struct
{
    uint32_t sequence;
    uint32_t timestamp;
    uint16_t type;
    uint16_t id;
    uint16_t object_id;
    uint16_t flags;
    uint32_t value0;
    uint32_t value1;
} IR_RuntimeRecord;
```

Required compiled size:

```text
24 bytes
```

The 24-byte form is selected because the canonical Task/ISR event interface carries two 32-bit values and context metadata. The previous compact 16-byte candidate is not part of the rc02 reference implementation.

---

## T3. Timeline Density

The reference implementation favors information completeness over maximum record count. The retained-RAM budget therefore contains fewer 24-byte runtime records than a 16-byte design would provide.

Target validation must measure event density and actual history duration before changing this frozen record size or increasing retained RAM.

## T4. Record-Type Candidates

Suggested record categories:

```c
enum
{
    IR_REC_OBSERVATION = 1,
    IR_REC_OPERATION,
    IR_REC_TRANSITION,
    IR_REC_INVARIANT_FAIL,
    IR_REC_PROPAGATION,
    IR_REC_INCIDENT,
    IR_REC_HEALTH,
    IR_REC_BOOT,
    IR_REC_RESET
};
```

Fault snapshots should remain separate from ordinary timeline records.

---

## T5. Pointer/Address Persistence Rule

A raw address may be useful evidence, but it must be stored explicitly as an address value:

```c
uint32_t address;
```

Do not persist native C pointers inside on-flash structures.

Reasons:

```text
pointer-size portability
ABI stability
decoder clarity
host-tool parsing
```

For 64-bit embedded targets, the schema may define a wider address field.

---

# Part U — First-Abnormal Snapshot

## U1. Candidate Structure

```c
typedef struct
{
    uint32_t magic;
    uint32_t epoch_id;
    uint32_t sequence;
    uint32_t timestamp;

    uint16_t object_id;
    uint16_t rule_id;
    uint16_t context_type;
    uint16_t flags;

    uint32_t observed0;
    uint32_t observed1;

    uint32_t last_operation_id;
    uint32_t last_operation_arg0;
    uint32_t last_operation_arg1;

    uint32_t checksum_or_commit;
} IR_FirstAbnormalSnapshot;
```

This is a candidate, not yet frozen.

---

## U2. Ownership Contract

Only one context owns the first-abnormal snapshot per incident/epoch.

The atomic latch shall guarantee:

```text
snapshot written once
subsequent detections do not overwrite it
```

Subsequent abnormal evidence belongs in timeline/propagation records.

---

# Part V — Fatal Snapshot

## V1. Candidate Generic Fault Frame

```c
typedef struct
{
    uint32_t pc;
    uint32_t lr;
    uint32_t sp;
    uint32_t psr;

    uint32_t fault_status0;
    uint32_t fault_status1;
    uint32_t fault_address0;
    uint32_t fault_address1;

    uint32_t context_id;
    uint32_t sequence;
} IR_FaultFrame;
```

The platform adapter maps MCU-specific fault registers into generic fields and may preserve raw extended fields separately.

---

## V2. Fault Snapshot Rule

Fault snapshot must be independent of:

```text
filesystem
normal service task
normal application heap
dynamic allocation
```

It should live in retained/protected recorder memory.

---

# Part W — Retained-RAM Container

## W1. Frozen Reference Header

```c
typedef struct
{
    uint32_t magic;
    uint16_t schema_version;
    uint16_t header_size;
    uint32_t build_id;
    uint32_t epoch_id;
    uint32_t incident_id;
    uint32_t health_flags;
    uint32_t state_flags;
    uint32_t first_abnormal_state;
    uint32_t reset_cause_raw;
} IR_RetainedHeader;
```

Task/ISR ring indices and counters are intentionally stored in separate ring-state objects rather than duplicated in the common header.

---

## W2. Selected Dual-Ring Container

The reference implementation follows the writer topology selected by Part AA:

```c
typedef struct
{
    IR_RetainedHeader         header;
    IR_FirstAbnormalSnapshot  first_abnormal;
    IR_FaultFrame             fatal_snapshot;
    IR_OperationContext       last_operation;

    IR_RingState              task_state;
    IR_RingState              isr_state;

    IR_RuntimeRecord          task_ring[IR_TASK_RECORD_COUNT];
    IR_RuntimeRecord          isr_ring[IR_ISR_RECORD_COUNT];

    uint8_t                   reserved[IR_RETAINED_RESERVED_BYTES];
} IR_RetainedStore;
```

Task and ISR writers therefore never compete for one timeline index. Fatal capture remains outside both rings.

---

## W3. 10 KiB Reference Budget

The default retained-RAM ceiling remains:

```text
10 KiB = 10,240 bytes
```

The reference implementation derives record capacity after subtracting fixed metadata, snapshots, two ring-state objects, operation context, and reserved bytes:

```text
retained budget
- fixed retained structures
- reserved bytes
= timeline bytes

timeline bytes / sizeof(IR_RuntimeRecord)
= total timeline record count
```

The default reference split assigns three quarters of timeline capacity to Task records and the remainder to ISR records. This split is a reference default, not a universal target requirement.

The host validation build reports:

```text
sizeof(IR_RuntimeRecord)        = 24 bytes
IR_TASK_RECORD_COUNT            = 310
IR_ISR_RECORD_COUNT             = 104
IR_TOTAL_TIMELINE_RECORD_COUNT  = 414
sizeof(IR_RetainedStore)        = 10,240 bytes
```

The compiled size and target linker MAP remain authoritative.

---

## W4. Compile-Time Size Guard

C11 builds use `_Static_assert`. C99 builds use an equivalent negative-array-size compile-time guard.

The implementation rejects a retained-store configuration that exceeds `IR_RETAINED_RAM_BYTES`.

The linker/section example places recorder retention in `.incident_ram`; a target project must separately verify the actual retained memory region and startup clearing behavior.

# Part X — Persistent Container

## X1. Persistent Header Candidate

```c
typedef struct
{
    uint32_t magic;
    uint16_t schema_version;
    uint16_t header_size;

    uint32_t generation;
    uint32_t build_id;
    uint32_t epoch_id;
    uint32_t incident_id;

    uint32_t payload_length;
    uint32_t payload_crc;

    uint32_t state;
    uint32_t commit_marker;
} IR_PersistentHeader;
```

---

## X2. Logical Persistent States

The framework defines logical lifecycle states:

```c
typedef enum
{
    IR_SLOT_EMPTY = 0,
    IR_SLOT_WRITING,
    IR_SLOT_COMMITTED,
    IR_SLOT_PENDING_EXPORT,
    IR_SLOT_EXPORTED
} IR_PersistentState;
```

**This enum is a logical state view only.**

It shall not be assumed to be the byte/word pattern written directly to NVM.

The persistence adapter maps logical states to storage-specific physical encoding that respects:

```text
program granularity
erase granularity
allowed bit transitions
atomic-write capability
power-loss behavior
```

Examples:

```text
NOR Flash
→ monotonic 1→0 commit marker / generation scheme

EEPROM
→ adapter-specific transactional marker

FRAM
→ adapter-specific duplicate header / commit record

other NVM
→ equivalent transaction-safe encoding
```

---

## X3. Transactional Commit Contract

The persistence adapter must provide semantics equivalent to:

```text
begin transaction
write payload
verify/check payload
commit transaction
```

Mandatory guarantees:

1. reset/power loss before commit is never reported as committed;
2. after commit, payload integrity can be validated;
3. the previous committed generation remains recoverable until the new generation is safely committed;
4. an interrupted new write does not destroy the last known-good committed incident;
5. physical encoding matches the storage technology.

The framework defines these semantics but does not mandate one universal bit pattern.

---

## X4. Persistent Payload Rule

Preferred payload is a serialized recorder container/schema.

Do not simply dump compiler-native structures without controlling:

```text
endianness
packing
alignment
schema version
field width
```

A simple fixed binary schema is acceptable if explicitly defined.

---

# Part Y — Adapter Contracts

## Y1. Platform Adapter

The frozen reference interface is:

```c
typedef struct
{
    uint32_t (*get_timestamp)(void);
    uint32_t (*get_reset_cause_raw)(void);
    IR_ContextType (*get_context_type)(void);
    uint32_t (*get_context_id)(void);
    IR_CriticalKey (*enter_critical)(void);
    void (*exit_critical)(IR_CriticalKey key);
} IR_PlatformOps;
```

`enter_critical` / `exit_critical` provide the project-specific bounded primitive required for shared recorder metadata and first-abnormal ownership. The target must measure its worst-case effect.

---

## Y2. Persistence Adapter

The reference persistence boundary is semantic rather than physical-media-specific:

```c
typedef struct IR_PersistSource IR_PersistSource;

typedef struct
{
    IR_Result (*persist)(const IR_PersistSource *source);
} IR_PersistenceOps;
```

`IR_PersistSource` exposes fixed evidence metadata, protected snapshots, operation context, and bounded Task/ISR record-reader callbacks.

The adapter owns:

```text
serialization
physical slot/journal selection
CRC/integrity
technology-specific torn-write protection
transaction commit encoding
wear policy
```

The adapter shall not interpret the in-memory `IR_PersistSource` object as permission to dump compiler-native structures directly to NVM.

---

## Y3. Persistent Export Source

Already-persisted evidence is exposed to the export service through:

```c
typedef struct
{
    bool (*has_pending)(void);
    IR_Result (*read_meta)(uint32_t *record_id, uint32_t *payload_length);
    IR_Result (*read_payload)(uint32_t record_id,
                              uint32_t offset,
                              void *dst,
                              uint32_t len);
    IR_Result (*mark_exported)(uint32_t record_id);
} IR_PersistenceExportOps;
```

This boundary allows Release-mode service/LCD export to read persistent evidence without exposing physical Flash layout to recorder core.

---

## Y4. Export Adapter

```c
typedef struct
{
    bool (*is_available)(void);
    IR_Result (*begin)(uint32_t record_id,
                       uint32_t payload_length,
                       uint32_t build_id,
                       uint16_t schema_version);
    IR_Result (*write)(const void *data, uint32_t len);
    IR_Result (*end)(void);
    IR_Result (*verify)(void);
    void (*abort)(void);
} IR_ExportOps;
```

The service writes bounded chunks. `mark_exported()` is called only after `end()` and optional `verify()` succeed. If streaming/finalization fails, optional `abort()` lets the adapter discard the incomplete destination transaction while the persistent source remains pending for retry.

Possible adapters include SD/filesystem, USB service transport, serial service protocol, network, or another project-defined retrieval mechanism.

---

## Y5. Development Continuous-Trace Adapter

Compiled only when `IR_BUILD_PROFILE == IR_BUILD_PROFILE_DEVELOPMENT`:

```c
typedef struct
{
    bool (*is_available)(void);
    IR_Result (*begin)(uint32_t build_id, uint16_t schema_version);
    IR_Result (*write_record)(IR_ContextType context,
                              const IR_RuntimeRecord *record);
    IR_Result (*end)(void);
} IR_ContinuousTraceOps;
```

Task/ISR writers enqueue only to bounded RAM queues. Physical SD/filesystem work occurs from the service path.

---

## Y6. Optional Boot-Policy Interface

Recorder core exports status to any separate project boot-policy layer. Boot policy may decide NORMAL / RECOVERY / SERVICE behavior, but recorder core shall not directly disable application subsystems.

# Part Z — Module Ownership Contract

## Z1. Core

Owns:

```text
record allocation
sequence generation
timeline
first-abnormal ownership
incident state
health counters
```

Must not own:

```text
filesystem
physical NVM driver
USB stack
SD stack
application state machine
```

---

## Z2. Retained Store

Owns:

```text
retained header
timeline memory
protected snapshots
epoch transition metadata
```

Must be located in a linker-controlled section.

---

## Z3. Persistence Service

Owns:

```text
slot selection
journal generation
serialization
CRC
commit sequence
pending/export state
```

Runs outside timing-critical paths.

---

## Z4. Export Service

Owns:

```text
export selection
adapter selection
streaming
retry policy
export-complete marking
```

Must not erase the last persistent copy as part of a normal export.

---

## Z5. Project Adapter

Owns:

```text
event IDs
object IDs
rule IDs
operation IDs
project invariants
project probe placement
project-specific metadata
```

Core must not contain project-specific object names.

---

# Part AA — Concurrency Contract

## AA1. Selected Writer Model

The canonical reference model is selected:

```text
Task context
    ↓
Task Timeline Ring

ISR context
    ↓
ISR Timeline Ring

Fatal / HardFault / NMI context
    ↓
Dedicated Fatal Snapshot
```

Writer topology is no longer an open choice in this specification.

A project may substitute an equivalent implementation only if it proves the same context-safety and bounded-execution guarantees.

---

## AA2. Task / ISR Writer Isolation

Task and ISR writers shall not share service locks.

Neither writer may wait for:

```text
persistence service
export service
filesystem
transport adapter
```

A short bounded platform critical primitive may be used only where needed for:

```text
first-abnormal ownership
small shared metadata
```

Timeline rings are separated specifically to reduce contention.

---

## AA3. Critical-Section Bound

If interrupt masking or another critical primitive is used:

```text
validate shared metadata
claim/update fixed scalar state
restore normal execution
```

No:

```text
loops proportional to data length
CRC over large buffers
serialization
persistence
export
filesystem work
```

inside the critical section.

The maximum interrupt-off/critical duration must be measured on target hardware.

---

## AA4. Sequence Contract

Task and ISR rings maintain monotonic **context-local** sequence numbers.

Each record contains enough information to reconstruct:

```text
epoch
context
local sequence
timestamp
```

A contended global sequence is optional, not required.

The PC tool must not claim exact cross-context ordering when the evidence cannot prove it.

---

## AA5. First-Abnormal Ownership

Task and ISR detectors may compete for one protected first-abnormal slot using the atomic contract in Part L9.

Fatal context does not compete for that slot.

The validity marker is published only after the fixed snapshot fields are complete.

---

## AA6. Recorder Self-Protection

Recorder-owned metadata is untrusted until validated.

Hard rule:

> No recorder-owned index, length, count, offset, or persisted pointer-like value may be used for memory access before bounds validation.

Before a ring access:

```c
idx = stored_index;

if (idx >= IR_RECORD_COUNT)
{
    IR_SetHealth(IR_HEALTH_INDEX_INVALID);
    IR_DropRecord();
    return;
}

record[idx] = new_record;
```

The recorder must prefer loss of evidence over an out-of-bounds access.

An invalid retained index shall not be silently "repaired" in a way that hides the corruption.

---

## AA7. Saturation / Wrap

The implementation must define wrap behavior for:

```text
Task local sequence
ISR local sequence
epoch ID
incident ID
health/lost counters
```

The decoder must not assume these values never wrap.

---

# Part AB — Schema and ABI Contract

## AB1. Schema Version

Persisted data shall contain:

```text
schema_version
record/container length
build identity
```

A decoder must reject or safely skip unsupported structures.

---

## AB2. No Implicit Compiler ABI

Persisted data shall not rely blindly on:

```text
compiler padding
enum width
native pointer width
bitfield layout
```

Use:

```text
fixed-width integer types
explicit reserved fields
explicit serialized sizes
```

---

## AB3. Packing Rule

Do not apply global `#pragma pack(1)` by default.

Reasons:

```text
unaligned access cost
possible fault on some targets
code-generation penalty
toolchain differences
```

Prefer naturally aligned runtime structures and an explicit serialized form if persistent packing is required.

---

## AB4. Build Identity

Recommended build identity options:

```text
monotonic build ID
Git commit-derived 32-bit ID
ELF/map checksum ID
release-version ID
```

The exact mapping belongs to build tooling/project configuration.

---

# Part AC — Service-State Contract

## AC1. Persistence Service State

Conceptual states:

```text
IDLE
PERSIST_REQUESTED
PERSISTING
VERIFYING
COMMITTED
FAILED
```

Application probe calls do not wait for this state machine.

---

## AC2. Export Service State

Conceptual:

```text
IDLE
PENDING
WAIT_DESTINATION
EXPORTING
VERIFYING
EXPORTED
FAILED
```

Repeated failure should use bounded retry/backoff.

---

## AC3. No Infinite Retry

All service retries must be bounded per service iteration.

A missing SD/USB device must not consume a tight CPU loop.

---

# Part AD — Health Flags

## AD1. Frozen Reference Flags

```c
typedef uint32_t IR_HealthFlags;

#define IR_HEALTH_RETENTION_INVALID   (1UL << 0)
#define IR_HEALTH_RECORD_DROPPED      (1UL << 1)
#define IR_HEALTH_PERSIST_FAILED      (1UL << 2)
#define IR_HEALTH_EXPORT_FAILED       (1UL << 3)
#define IR_HEALTH_SCHEMA_INVALID      (1UL << 4)
#define IR_HEALTH_TORN_RECORD_FOUND   (1UL << 5)
#define IR_HEALTH_SALVAGE_USED        (1UL << 6)
#define IR_HEALTH_INDEX_INVALID       (1UL << 7)
#define IR_HEALTH_TRACE_DROPPED       (1UL << 8)
#define IR_HEALTH_DEGRADED            (1UL << 31)
```

The reference implementation treats these flags as diagnostic state only. They do not authorize product-control behavior.

# Part AE — Configuration Contract

## AE1. Static Configuration

The frozen reference configuration is adapter-only and statically supplied by the project:

```c
typedef struct
{
    const IR_PlatformOps          *platform;
    const IR_ContinuousTraceOps   *continuous_trace;
    const IR_PersistenceOps       *persistence;
    const IR_PersistenceExportOps *persistent_export;
    const IR_ExportOps            *export_ops;
} IR_Config;
```

The project provides:

```c
const IR_Config *IR_ProjectConfig(void);
```

No runtime configuration may switch Development/Release profile or activate Release continuous trace. Feature/profile authority remains compile-time configuration in `ir_config.h`.

## AE2. No Runtime Allocation

`IR_Init()` shall use caller/static memory.

It must not allocate retained storage dynamically.

---

# Part AF — Error-Handling Contract

## AF1. Recorder Failure

Default behavior:

```text
mark health/degraded
increment counter
drop evidence if necessary
continue product operation
```

Recorder errors must not normally:

```text
assert
reset
block forever
disable control
```

---

## AF2. Configuration Error

Development builds may assert on invalid compile-time integration.

Production builds should fail recorder initialization safely and leave application operation available.

---

# Part AG — Project Integration Pattern

## AG1. Minimal Existing-Product Integration

Recommended order:

```text
1. reserve a dedicated retained-RAM linker section
2. provide static project/platform adapters
3. call IR_EarlyInit() after minimal platform setup
4. call IR_Init() after normal runtime/platform setup
5. integrate one low-priority persistence/export service model
6. add fatal capture hook
7. add a small number of high-value Task/ISR events
8. add bounded project invariant checks
9. add persistence adapter
10. add export adapter
```

Do not refactor unrelated application architecture merely to integrate the recorder.

---

## AG2. Example Application Skeleton

```c
int main(void)
{
    Platform_MinimalInit();

    (void)IR_EarlyInit();

    Platform_NormalInit();
    RTOS_Init();

    (void)IR_Init();

    App_CreateTasks();
    IncidentService_CreateLowPriorityTask();

    RTOS_Start();

    return 0;
}
```

Notes:

- `IncidentService_CreateLowPriorityTask()` represents project/service integration, not a required public recorder API name.
- A non-RTOS target may use a bounded periodic internal service entry instead.
- The selected service model is compile-time/project-defined.
- This example does not imply recorder ownership of the main application startup policy.

---

## AG3. Example Task Probe Usage

```c
IR_EVENT_TASK(
    IR_REC_OBSERVATION,
    IR_EVT_CONTROL_TRIGGER,
    trigger_count,
    state);

if (!Ring_IsValid(&g_ring))
{
    (void)IR_FIRST_ABNORMAL_TASK(
        IR_OBJ_RING,
        IR_RULE_RING_INDEX,
        g_ring.write_index,
        g_ring.size);
}
```

The first-abnormal API internally latches the persistence request/status required by recorder policy; ordinary application code does not perform persistent I/O.

The invariant checker must be read-only and bounded.

---

## AG4. Example ISR Probe Usage

```c
void PeripheralIsr(void)
{
    uint32_t status = Peripheral_ReadStatus();

    IR_EVENT_ISR(
        IR_REC_OBSERVATION,
        IR_EVT_PERIPHERAL_IRQ,
        status,
        0U);

    Peripheral_ClearStatus(status);
}
```

ISR evidence recording must remain bounded and independent of Task/service locks.

---

## AG5. Example Fatal Capture Usage

```c
void PlatformFatalHandler(const PlatformFaultFrame *platform_fault)
{
    IR_FaultFrame fault;

    Platform_MapFaultFrame(platform_fault, &fault);
    IR_FATAL_CAPTURE(&fault);

    Platform_FatalResetOrStop();
}
```

`Platform_MapFaultFrame()` itself must be fixed, bounded, and fatal-context safe.

---

# Part AH — Reference Implementation Consistency Checklist

`v1.0.0rc02` release checklist:

```text
[x] exactly one canonical lifecycle API definition exists
[x] Task and ISR event paths are separate
[x] first-abnormal Task/ISR ownership is one-shot and validity is published last
[x] fatal snapshot is independent from first-abnormal ownership
[x] selected writer model is Task ring + ISR ring + dedicated fatal snapshot
[x] runtime record is frozen at 24 bytes in the reference implementation
[x] retained container follows the selected dual-ring writer model
[x] retained-store size is compile-time guarded
[x] recorder-owned ring indices are validated before memory access
[x] Development/Release build profile is compile-time only
[x] Release continuous-trace queue/writer code is compiled out
[x] persistence/export work is outside Task/ISR/Fatal hot paths
[x] export completion does not delete the persistent evidence image
[x] generic public examples contain no private project identifiers
```

Target timing and physical retention guarantees are intentionally not checked off here; they belong to rc03.

# Part AI — Current Non-Goals

`v1.0.0rc02` does not yet claim:

```text
target-verified linker memory placement or startup retention behavior
measured atomic-latch / critical-section WCET
measured interrupt-off duration
measured RTOS scheduling impact
measured NVM read-while-write or Flash-stall behavior
validated persistent-media endurance budget
measured retained history duration under real event density
validated early-boot WCET on a target MCU
validated observer effect on control / communication deadlines
real SD/FatFS implementation
real Internal Flash implementation
PC GUI implementation
mandatory recovery boot
all export transports or NVM adapters
complex MCU-side root-cause inference
```

The included host adapters are synthetic validation fixtures. They prove the reference interfaces and service sequencing compile and execute; they do not substitute for target validation.

# Part AJ — Current Working Conclusion

`v1.0.0rc02` demonstrates that the public architecture can be expressed as a small, platform-neutral C reference implementation:

```text
Project Probe / Adapter
        ↓
Task Ring      ISR Ring
     \          /
      \        /
   First-Abnormal Snapshot
          +
    Fatal Snapshot
          ↓
  10 KiB Retained Store
          ↓
 Low-Priority Service
      ├─ Persistence Adapter
      ├─ Release On-Demand Export
      └─ Development Continuous Trace
```

Validated on the host reference build:

```text
Release profile      PASS
Development profile  PASS
Recorder OFF         PASS
C99 compatibility    PASS
C11 warnings-as-error PASS
Retained size        10,240 bytes
Task records         310
ISR records          104
.incident_ram MAP     present
synthetic persist/export verification PASS
```

The implementation does not change the primary recorder rule:

> Recorder, persistence, SD, and export failures must not become product failures or timing dependencies.

The next question is no longer whether the contracts can be implemented. `v1.0.0rc03` must determine their **real target cost and diagnostic value** through known-root-cause and observer-effect validation.

# Part AK — Storage Operating Modes & Evidence Export Contract

## AK1. Purpose

This Part defines the normative storage operating model for the public baseline.

It separates:

```text
Incident Evidence
    durable, high-value, bounded evidence used for postmortem diagnosis

Continuous Development Trace
    optional higher-volume runtime trace used during firmware development
```

These are not interchangeable data classes.

The framework remains media-independent at the core. Internal Flash and SD are reference project/platform roles, not mandatory physical technologies.

---

## AK2. Three-Layer Storage Roles

The reference three-layer model is:

```text
Retained RAM
    ↓
Persistent Internal Flash
    ↓
SD / External Retrieval
```

The roles are:

### Retained RAM

```text
high-frequency
low-latency
bounded
hot-path evidence
first-abnormal / fatal / runtime timeline
```

No SD, filesystem, or persistent-device I/O is allowed in the probe hot path.

### Persistent Internal Flash

```text
incident persistence
reset/power-cycle survivability
bounded incident/history journal
pending-export source
```

Internal Flash is not the normal continuous-development bulk logger.

### SD / External Retrieval

In Development builds it may be a continuous trace destination.

In Release builds it is an on-demand evidence export destination and is not required to be present during normal runtime.

---

## AK3. Compile-Time Build-Profile Authority

Development and Release behavior shall be selected at compile time.

Reference pattern:

```c
#define IR_BUILD_PROFILE_DEVELOPMENT  (1U)
#define IR_BUILD_PROFILE_RELEASE      (2U)

#ifndef IR_BUILD_PROFILE
#define IR_BUILD_PROFILE IR_BUILD_PROFILE_RELEASE
#endif
```

Requirements:

```text
Development/Release selection is compile-time only.
LCD, service UI, configuration file, command, or runtime state shall not switch profiles.
Release code shall not expose a runtime path that enables continuous Development SD logging.
Invalid profile values shall fail the build.
```

The build profile may also control probe density, development diagnostics, or related recorder features in later implementations.

---

## AK4. Development Build Profile

Development builds may enable continuous SD trace for long-duration execution analysis.

Conceptual data paths:

```text
                    ┌──→ Persistent Internal Flash
                    │       Incident Evidence
Runtime → RAM ──────┤
                    │
                    └──→ bounded trace queue
                              ↓
                       low-priority SD writer
                              ↓
                           SD Card
                              ↓
                       DAT / PC analysis
```

Continuous SD trace is a development observation channel, not the durable incident store.

Requirements:

```text
application/probe tasks do not call blocking filesystem writes in the hot path
SD writer runs only in a safe non-critical context
trace buffering is bounded
trace loss is observable
trace failure cannot alter product control behavior
```

---

## AK5. Development Build with SD Unavailable

If the SD card is absent, removed, full, slow, or reports a filesystem/write error:

```text
Incident Evidence
      ↓
Persistent Internal Flash
      ↓
retained according to incident policy
```

General continuous Development Trace shall not automatically consume all persistent incident capacity.

Preferred trace behavior:

```text
bounded RAM queue/buffer
      ↓
capacity exhausted
      ↓
drop oldest/selected trace record
      ↓
increment lost/dropped counter
```

A project may reserve a fixed persistent quota for Development Trace only when explicitly configured.

That quota shall be:

```text
bounded
lower priority than incident evidence
wear-budgeted
unable to consume incident-reserved storage
```

---

## AK6. Release Build Profile

A Release build shall not continuously write the SD card as part of normal recorder operation.

Reference runtime flow:

```text
Runtime
   ↓
Retained RAM
   ↓
Incident / Important Evidence
   ↓
Persistent Internal Flash
```

Normal Release operation therefore does not require:

```text
SD card presence
mounted filesystem
continuous file rollover
SD write latency in the runtime path
```

After a hang, reset, fault, or other incident, persistent evidence remains available for later retrieval.

---

## AK7. Release Service/LCD Export Path

A Release project may expose an LCD Tech/Service page or equivalent service interface:

```text
Device is operable after incident/restart
       ↓
Insert SD card
       ↓
Select Export / Dump Incident Evidence
       ↓
Internal Flash
       ↓
Transactional export
       ↓
SD card
       ↓
PC analysis
```

The UI controls the export request only.

The UI shall not:

```text
switch Development/Release build profile
enable continuous SD logging in a Release build
be required for initial evidence preservation
erase the only persistent copy before export completion
```

---

## AK8. Persistent Retention and Capacity Exhaustion

Persistent incident storage shall be bounded.

Preferred reclaim order remains policy-driven, for example:

```text
oldest EXPORTED record first
then other oldest eligible records according to project retention policy
preserve the newest pending incident and latest terminal/first-abnormal evidence where practical
```

If capacity is exhausted and no reclaimable record remains, the project shall define a bounded oldest-eligible eviction rule.

Any forced loss shall:

```text
be observable through health/counter metadata
never block product operation waiting for storage
never perform unbounded retry
```

The framework favors newer diagnostically relevant evidence over indefinite storage growth.

---

## AK9. Transactional Flash-to-SD Export

Export shall not treat a record as safely exported merely because copying started.

Reference sequence:

```text
Export requested
      ↓
Check destination availability
      ↓
Create/open destination object
      ↓
Copy persistent evidence
      ↓
Finalize destination
      ↓
verify completion / integrity as supported
      ↓
commit EXPORTED state
```

If interrupted by:

```text
SD removal
card full
write error
filesystem error
reset
power loss
```

the original persistent evidence shall remain valid and re-exportable.

---

## AK10. Export Completion Does Not Imply Immediate Deletion

Successful export changes evidence state:

```text
PENDING_EXPORT
      ↓
EXPORTED
```

It does not require immediate erasure of the Internal Flash copy.

Persistent evidence may remain until:

```text
explicit clear/archive action
or
controlled bounded-retention replacement
```

This protects against discovering a damaged/incomplete external file only after the SD card is removed from the device.

---

## AK11. SD Writer Failure Isolation

Development continuous logging and Release on-demand export shall obey the same failure-isolation rule.

Conditions such as:

```text
SD slow
SD busy
SD absent
SD removed
SD full
filesystem error
queue full
```

may cause:

```text
trace/evidence export failure
record drop where permitted
lost/drop counter increment
health/status indication
```

They shall not cause:

```text
control-task blocking
DSP/motor/system deadline violation
product reset solely for logging
application state change
indefinite retry
```

Principle:

> **Lose evidence before losing product functionality.**

---

## AK12. Continuous SD File Lifecycle

Development continuous logging shall be bounded at the file level.

The implementation/project shall define:

```text
file naming or session identity
maximum file size or rotation threshold
rollover policy
card-full policy
format/schema version
firmware/build identity
incomplete-file indication after unexpected reset/power loss
```

Example naming is non-normative:

```text
REC_0001.DAT
REC_0002.DAT
REC_0003.DAT
```

The specification does not mandate FAT/FatFS or a particular filesystem.

---

## AK13. Observer-Effect Requirements

Storage/profile validation shall compare recorder behavior with relevant features enabled and disabled.

Development continuous logging shall be measured for:

```text
task/control latency
CPU load
queue high-water mark
lost trace count
SD writer service time
stack watermark
filesystem/write stalls
```

Release builds shall confirm:

```text
continuous SD writer is absent/compiled out
normal runtime does not depend on SD presence
LCD/service export runs outside timing-critical paths
export failure leaves product operation and persistent evidence intact
```

---

## AK14. Normative Summary

The storage operating contract is summarized by four rules:

> **Incident Evidence is not Continuous Development Trace.**

> **Development/Release storage behavior is selected by compile-time `#define`; runtime profile switching is not permitted.**

> **Release firmware does not continuously write SD during normal operation; persistent Internal Flash evidence can be exported on demand through the service/LCD path.**

> **Recorder, Internal Flash, SD, filesystem, and export failures must not block, alter, or become required for core product operation.**

---

# Part AL — Public Release Hygiene & Genericity Contract

## AL1. Public-Specification Boundary

The public framework specification shall describe reusable engineering concepts without exposing private project identifiers or implementation-specific fingerprints.

Public material may include generic examples of:

```text
ring buffers
queues
control loops
storage operations
communication transactions
fault snapshots
boundary-overwrite validation fixtures
Internal Flash / external NVM / SD / USB as generic media roles
```

Public material shall not require or preserve:

```text
company names
private product names
internal project codenames
employee or team-member names
private symbol names
private variable/function names
private memory addresses copied from a product build
private defect reproduction details that uniquely identify a product implementation
```

## AL2. Generic Validation Fixtures

A known-root-cause test used in the public framework should use synthetic identifiers and generic operations.

Preferred:

```text
STORAGE_WRITE_BEGIN
CRITICAL_RING_INVALID
payload_size
frame_size
critical_object
```

Avoid copying identifiers directly from a private firmware repository.

The purpose is to preserve the **failure pattern and validation method**, not the private implementation vocabulary.

## AL3. Platform-Neutral Examples

Normative examples should prefer framework/platform abstractions such as:

```text
IR_PlatformEnterCritical()
IR_PlatformExitCritical()
IR_PlatformServiceDelay()
storage_frame_write()
```

over vendor- or RTOS-specific API names unless a section is explicitly labeled as a non-normative platform example.

## AL4. Release Audit

Before a public RC is packaged, perform a textual audit for:

```text
company/product/project names
personal names other than intentional copyright attribution
private code symbols
private defect labels
private device-specific addresses or constants
vendor/RTOS-specific APIs presented as framework requirements
```

Any retained technology names must serve a generic architectural purpose rather than identify a private implementation.

---

# Part AM — v1.0.0rc02 Reference Implementation Baseline

## AM1. Repository Artifacts

The rc02 package contains:

```text
Embedded-Incident-Crash-Recorder-Framework/
├─ README.md
├─ CHANGELOG.md
├─ LICENSE
├─ Makefile
├─ include/
│  ├─ incident_recorder.h
│  ├─ incident_recorder_service.h
│  └─ ir_config.h
├─ src/
│  ├─ ir_core.c
│  ├─ ir_service.c
│  └─ ir_internal.h
├─ reference/
│  ├─ main.c
│  ├─ probe_off_check.c
│  ├─ size_report.c
│  ├─ ir_reference_project.c
│  └─ ir_reference_project.h
├─ linker/
│  └─ incident_ram_gnu.ld
├─ scripts/
│  └─ build_reference.sh
└─ validation/
   ├─ build.log
   ├─ reference_release.map
   ├─ reference_development.map
   ├─ section_report.txt
   └─ size_report.txt
```

Generated executables are not part of the source package.

---

## AM2. Frozen Reference Choices

The minimal implementation freezes these choices for the reference line:

```text
C99-compatible core; C11 static assertions where available
no dynamic allocation
10 KiB retained-store ceiling
24-byte runtime record
separate Task and ISR rings
dedicated First-Abnormal Snapshot
dedicated Fatal Snapshot
project-provided bounded critical primitive
compile-time Development / Release profile
Development-only bounded continuous-trace queues
low-priority service processing
on-demand Release export from already-persisted evidence
64-byte bounded export chunk per service step
```

These are reference choices, not claims that every target must use the same Task/ISR capacity split or physical storage implementation.

---

## AM3. Validation Result

The published host validation artifacts show:

```text
Release build/run              PASS
Development build/run          PASS
Recorder-OFF side-effect test  PASS
C99 compatibility build        PASS
C11 warnings-as-error build    PASS
IR_RuntimeRecord               24 bytes
IR_RetainedStore               10,240 bytes
Task ring                      310 records
ISR ring                       104 records
.incident_ram in MAP/ELF       PASS
synthetic persistence          PASS
transactional export verify    PASS
Release continuous trace       compiled out / no activity
Development continuous trace   active through bounded service queue
```

The host `.incident_ram` section is structural evidence only. The supplied GNU linker fragment shows the intended `NOLOAD` retained-section contract, but actual MCU retention and startup clearing behavior must be proven on the target.

---

## AM4. Persistence Snapshot Consistency

The minimal reference service temporarily pauses ordinary Task/ISR timeline capture while the project persistence adapter consumes one logical evidence view. This avoids persisting a self-inconsistent ring image while writers advance it.

Important boundaries:

- application execution is not blocked by this pause;
- first-abnormal/fatal protected snapshots remain available for the current epoch; if protected evidence changes during persistence, the service leaves persistence requested for another cycle instead of falsely marking the newer evidence persisted;
- the pause can lose ordinary timeline events, so persistence duration and event loss must be measured in rc03;
- a target requiring zero recorder-event loss during persistence may replace this reference mechanism with a proven double-buffer/snapshot strategy without moving physical I/O into the hot path.

The reference favors product timing over perfect evidence continuity.

---

## AM5. Build / Validation Command

From the repository root:

```sh
make validate
```

or:

```sh
./scripts/build_reference.sh
```

The script regenerates the host validation evidence and fails if the reference builds/tests or `.incident_ram` MAP check fail.

---

# Appendix A — Planned Next RCs (Non-Normative)

## v1.0.0rc03 — Target Validation

Expected validation:

```text
known defect + recorder OFF
known defect + recorder ON
fixed firmware + recorder ON
execution-time audit
stack audit
ISR latency
interrupt-off timing
NVM stall/read-while-write behavior
early-boot fault injection
torn-write injection
recorder-metadata corruption injection
event-flood/coalescing test
Development continuous-SD observer-effect test
SD absent/removed/full/write-failure test
Release build confirms continuous trace remains compiled out
service/LCD persistent-storage-to-SD transactional export test
```

The goal is to show that the recorder remains a low-coupling observer on real embedded hardware while preserving useful evidence across both storage build profiles.

Copyright © 2026.  
Draft engineering architecture document.  
Use and distribution should follow the license of the target GitHub repository.
