#include <stdio.h>

#include "incident_recorder.h"
#include "incident_recorder_service.h"
#include "ir_reference_project.h"

static int Check(bool condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }

    return 0;
}

int main(void)
{
    IR_Status status;
    IR_FaultFrame fault = {0};
    uint32_t i;
    int failed = 0;

    failed |= Check(IR_EarlyInit() == IR_OK, "IR_EarlyInit");
    failed |= Check(IR_Init() == IR_OK, "IR_Init");

    IR_EVENT_TASK(IR_REC_OBSERVATION, 1U, 10U, 11U);
    IR_EVENT_TASK(IR_REC_OPERATION, 2U, 20U, 21U);
    IR_EVENT_ISR(IR_REC_OBSERVATION, 3U, 30U, 31U);

    failed |= Check(IR_FIRST_ABNORMAL_TASK(7U, 9U, 0xAAU, 0xBBU),
                    "first abnormal should claim once");
    failed |= Check(!IR_FIRST_ABNORMAL_ISR(8U, 10U, 0xCCU, 0xDDU),
                    "second first-abnormal claim must fail");

    fault.pc = 0x100U;
    fault.lr = 0x200U;
    fault.sp = 0x300U;
    fault.psr = 0x400U;
    fault.sequence = 5U;
    IR_FATAL_CAPTURE(&fault);

    failed |= Check(IR_GetStatus(&status) == IR_OK, "IR_GetStatus before persistence");
    failed |= Check(status.first_abnormal_valid, "first-abnormal snapshot valid");
    failed |= Check(status.fatal_snapshot_valid, "fatal snapshot valid");
    failed |= Check(status.task_record_count == 2U, "Task ring count");
    failed |= Check(status.isr_record_count == 1U, "ISR ring count");

    IR_ServiceProcess();
    failed |= Check(IR_ReferenceHasPendingPersistentEvidence(), "persistence produced pending evidence");
    failed |= Check(IR_ReferencePersistedBytes() > 0U, "persistent payload non-empty");

    failed |= Check(IR_ServiceRequestExport() == IR_OK, "export request accepted");
    for (i = 0U; i < 512U; ++i)
    {
        IR_ServiceProcess();
        if (!IR_ReferenceHasPendingPersistentEvidence())
        {
            break;
        }
    }

    failed |= Check(!IR_ReferenceHasPendingPersistentEvidence(), "export completed");
    failed |= Check(IR_ReferenceExportMatchesPersistence(), "export copy matches persistent evidence");
    failed |= Check(IR_ReferenceExportedBytes() == IR_ReferencePersistedBytes(), "export byte count");

#if IR_ENABLE_CONTINUOUS_SD_TRACE
    failed |= Check(IR_ReferenceContinuousTraceRecords() >= 3U,
                    "Development profile drained continuous trace records");
#else
    failed |= Check(IR_ReferenceContinuousTraceRecords() == 0U,
                    "Release profile contains no continuous trace writer activity");
#endif

    IR_SystemStable();

    if (failed != 0)
    {
        return 1;
    }

    printf("PASS profile=%u task=%u isr=%u persisted=%u exported=%u dev_trace=%u\n",
           (unsigned)status.build_profile,
           (unsigned)status.task_record_count,
           (unsigned)status.isr_record_count,
           (unsigned)IR_ReferencePersistedBytes(),
           (unsigned)IR_ReferenceExportedBytes(),
           (unsigned)IR_ReferenceContinuousTraceRecords());
    return 0;
}
