# EICRF v1.0.0rc05 Target Validation Results

**Status:** PENDING TARGET EVIDENCE

## 1. Target Identity

| Field | Value |
|---|---|
| Target / board | PENDING |
| MCU / SoC | PENDING |
| RTOS / scheduler | PENDING |
| Compiler / version | PENDING |
| Optimization / LTO | PENDING |
| Linker script | PENDING |
| Recorder source identity | PENDING |
| Application build identity | PENDING |
| Persistent storage | PENDING |
| Export medium | PENDING |

## 2. Acceptance Criteria

State product/project-specific acceptance limits before entering results.

| Metric | Acceptance criterion |
|---|---|
| Task probe WCET | PENDING |
| ISR probe WCET | PENDING |
| Recorder critical-section WCET | PENDING |
| ISR latency change | PENDING |
| CPU-load change | PENDING |
| Stack impact | PENDING |
| Diagnostic record loss | PENDING |
| Persistence-service impact | PENDING |
| Development trace impact | PENDING / N/A |

## 3. Build / Probe Lifecycle / Retention Evidence

| Test | Result | Evidence |
|---|---|---|
| TV-BUILD-01 Build-profile separation | PENDING | |
| TV-LIFE-01 Probe lifecycle / Remove-or-Promote closure | PENDING | |
| TV-RET-01 Retained section placement | PENDING | |
| TV-RET-02 Reset-class survival | PENDING | |

## 4. Timing / Observer Effect

| Test | Result | Baseline | Recorder ON | Delta | Evidence |
|---|---|---:|---:|---:|---|
| TV-TIME-01 Task probe | PENDING | | | | |
| TV-TIME-02 ISR / critical section | PENDING | | | | |
| TV-TIME-03 Fatal publication | PENDING | | | | |
| TV-OBS-01 OFF vs Release ON | PENDING | | | | |
| TV-OBS-01 OFF vs Development ON | PENDING / N/A | | | | |

## 5. Persistence

| Test | Result | Evidence / notes |
|---|---|---|
| TV-PERSIST-01 Real NVM transaction | PENDING | |
| TV-PERSIST-02 Interrupted persistence | PENDING | |
| TV-PERSIST-03 Reboot reconstruction | PENDING | |
| TV-CAP-01 Capacity pressure | PENDING | |

## 6. Export

| Test | Result | Evidence / notes |
|---|---|---|
| TV-EXPORT-01 Successful explicit export | PENDING | |
| TV-EXPORT-02 Missing media | PENDING / N/A | |
| TV-EXPORT-02 Removal during export | PENDING / N/A | |
| TV-EXPORT-02 Destination full | PENDING / N/A | |
| TV-EXPORT-02 Write/finalize/verify failure | PENDING / N/A | |
| TV-EXPORT-02 Reset during export | PENDING / N/A | |

## 7. Development Continuous Trace

| Test | Result | Evidence / notes |
|---|---|---|
| TV-DEV-01 Sustained trace | PENDING / N/A | |
| Slow/unavailable destination | PENDING / N/A | |
| Queue loss/high-water behavior | PENDING / N/A | |
| TV-WRAP-01 Timestamp rollover | PENDING / N/A | |

## 8. Known-Root-Cause Comparison

| Run | Result | Evidence / notes |
|---|---|---|
| Defect + Recorder OFF | PENDING | |
| Same defect + Recorder ON | PENDING | |
| Fixed firmware + Recorder ON | PENDING | |

State whether Recorder ON materially changed reproduction behavior or timing, and whether captured evidence identifies/narrows the independently known origin.

## 9. Deviations / Limitations

PENDING

## 10. Final Target Verdict

Use one only after all required evidence is complete:

```text
TARGET VALIDATION: PASS
TARGET VALIDATION: PASS WITH FINDINGS
TARGET VALIDATION: FAIL
```

Until then retain:

```text
TARGET VALIDATION: PENDING
```
