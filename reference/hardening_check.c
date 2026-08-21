#include <stdio.h>
#include <string.h>

#include "incident_recorder.h"
#include "incident_recorder_service.h"
#include "ir_internal.h"
#include "ir_reference_project.h"

#define REF_SERIALIZED_FIRST_ABNORMAL_OFFSET (60U)
#define REF_SERIALIZED_FATAL_OFFSET          (108U)

static int Check(bool condition, const char *message)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

static IR_Result EmptyReader(void *context, uint32_t logical_index, IR_RuntimeRecord *record)
{
    (void)context;
    (void)logical_index;
    (void)record;
    return IR_NOT_AVAILABLE;
}

static void FillSource(IR_PersistSource *source, uint32_t incident_id)
{
    memset(source, 0, sizeof(*source));
    source->build_id = IR_BUILD_ID;
    source->schema_version = (uint16_t)IR_SCHEMA_VERSION;
    source->epoch_id = 1U;
    source->incident_id = incident_id;
    source->reader_context = NULL;
    source->read_task_record = EmptyReader;
    source->read_isr_record = EmptyReader;
}

int main(void)
{
    int failed = 0;
    IR_RuntimeRecord record = {0};
    IR_ContextType context = IR_CONTEXT_UNKNOWN;
    IR_Status before;
    IR_Status after;
    IR_PersistSource source;
    const IR_Config *config;
    uint32_t id1;
    uint32_t id2;
    uint8_t byte_before = 0U;
    uint8_t byte_after = 0U;
    IR_FaultFrame fatal1 = {0};
    IR_FaultFrame fatal2 = {0};

    failed |= Check(IR_EarlyInit() == IR_OK, "early init");
    failed |= Check(IR_Init() == IR_OK, "init");
    config = IR_ProjectConfig();
    failed |= Check((config != NULL) && (config->persistence != NULL) &&
                    (config->persistence->persist != NULL), "persistence adapter available");

#if IR_ENABLE_CONTINUOUS_SD_TRACE
    record.timestamp = 1U;
    g_ir_task_trace_queue.count = 0U;
    g_ir_task_trace_queue.write_index = IR_DEV_TRACE_QUEUE_RECORDS + 5U;
    IR_InternalQueueDevelopmentTrace(IR_CONTEXT_TASK, &record);
    failed |= Check(g_ir_runtime.dev_trace_lost_count == 1U,
                    "invalid Development write_index is dropped and counted");

    memset(&g_ir_task_trace_queue, 0, sizeof(g_ir_task_trace_queue));
    memset(&g_ir_isr_trace_queue, 0, sizeof(g_ir_isr_trace_queue));
    g_ir_task_trace_queue.count = 1U;
    g_ir_task_trace_queue.read_index = IR_DEV_TRACE_QUEUE_RECORDS + 7U;
    failed |= Check(!IR_InternalPopDevelopmentTrace(&context, &record),
                    "invalid Development read_index is rejected");
    failed |= Check(g_ir_runtime.dev_trace_lost_count == 2U,
                    "invalid Development read_index is counted");
    memset(&g_ir_task_trace_queue, 0, sizeof(g_ir_task_trace_queue));
#endif

    FillSource(&source, 100U);
    failed |= Check(config->persistence->persist(&source) == IR_OK, "persist generation 1");
    id1 = IR_ReferenceLastPersistedRecordId();
    failed |= Check(IR_ReferenceReadPersistentByte(id1, 0U, &byte_before),
                    "generation 1 readable");

    FillSource(&source, 101U);
    failed |= Check(config->persistence->persist(&source) == IR_OK, "persist generation 2");
    id2 = IR_ReferenceLastPersistedRecordId();
    failed |= Check(id2 != id1, "generation IDs differ");
    failed |= Check(IR_ReferencePendingPersistentCount() == 2U,
                    "two pending generations retained");
    failed |= Check(IR_ReferenceReadPersistentByte(id1, 0U, &byte_after) &&
                    (byte_after == byte_before), "older pending generation remains readable");

    IR_ReferenceSimulateRestart();
    failed |= Check(IR_ReferenceLastPersistedRecordId() == id2,
                    "restart reconstructs newest persistent allocation state");
    failed |= Check(IR_ReferenceReadPersistentByte(id1, 0U, &byte_after),
                    "restart preserves older pending generation");

    FillSource(&source, 102U);
    failed |= Check(config->persistence->persist(&source) == IR_NOT_AVAILABLE,
                    "journal refuses to overwrite two pending generations");
    failed |= Check(IR_ReferencePendingPersistentCount() == 2U,
                    "capacity exhaustion preserves both pending generations");

    failed |= Check(config->persistent_export->mark_exported(id1) == IR_OK,
                    "oldest generation can become reclaimable only after export mark");

    IR_ReferenceSetPersistFailureStep(1U);
    FillSource(&source, 103U);
    failed |= Check(config->persistence->persist(&source) == IR_NOT_AVAILABLE,
                    "injected interruption after transaction begin fails");
    IR_ReferenceSetPersistFailureStep(0U);
    IR_ReferenceSimulateRecovery();
    failed |= Check(IR_ReferenceReadPersistentByte(id2, 0U, &byte_after),
                    "pending generation survives interruption after begin");
    failed |= Check(IR_ReferencePendingPersistentCount() == 1U,
                    "recovery rejects begin-only incomplete slot");

    IR_ReferenceSetPersistFailureStep(2U);
    FillSource(&source, 104U);
    failed |= Check(config->persistence->persist(&source) == IR_NOT_AVAILABLE,
                    "injected interruption after payload write fails");
    IR_ReferenceSetPersistFailureStep(0U);
    IR_ReferenceSimulateRecovery();
    failed |= Check(IR_ReferenceReadPersistentByte(id2, 0U, &byte_after),
                    "pending generation survives interruption after payload write");
    failed |= Check(IR_ReferencePendingPersistentCount() == 1U,
                    "recovery rejects payload-only incomplete slot");

    IR_ReferenceResetPersistentStore();

    /* Unpublished protected payload bytes must not be copied into a persistence image. */
    g_ir_retained.first_abnormal.magic = 0xA5A5A5A5UL;
    g_ir_retained.fatal_snapshot.pc = 0x5A5A5A5AUL;
    g_ir_retained.header.first_abnormal_state = IR_STATE_FIRST_EMPTY;
    g_ir_retained.header.fatal_state = IR_STATE_FATAL_EMPTY;
    g_ir_retained.header.state_flags |= IR_STATE_PERSIST_REQUESTED;
    IR_ServiceProcess();
    id1 = IR_ReferenceLastPersistedRecordId();
    failed |= Check(IR_ReferenceReadPersistentByte(id1, REF_SERIALIZED_FIRST_ABNORMAL_OFFSET, &byte_after) &&
                    (byte_after == 0U), "invalid First-Abnormal payload is not copied");
    failed |= Check(IR_ReferenceReadPersistentByte(id1, REF_SERIALIZED_FATAL_OFFSET, &byte_after) &&
                    (byte_after == 0U), "invalid Fatal payload is not copied");
    IR_ReferenceResetPersistentStore();
    memset(&g_ir_retained.first_abnormal, 0, sizeof(g_ir_retained.first_abnormal));
    memset(&g_ir_retained.fatal_snapshot, 0, sizeof(g_ir_retained.fatal_snapshot));

    fatal1.pc = 0x111U;
    fatal2.pc = 0x222U;
    IR_FATAL_CAPTURE(&fatal1);
    IR_FATAL_CAPTURE(&fatal2);
    failed |= Check(g_ir_retained.header.fatal_state == IR_STATE_FATAL_VALID,
                    "fatal state published valid");
    failed |= Check(g_ir_retained.header.fatal_publish_sequence == 1U,
                    "fatal snapshot is one-shot per epoch");
    failed |= Check(g_ir_retained.fatal_snapshot.pc == fatal1.pc,
                    "second fatal capture cannot overwrite first published fatal snapshot");

    failed |= Check(IR_FIRST_ABNORMAL_TASK(1U, 2U, 3U, 4U),
                    "first abnormal requested persistence");
    failed |= Check(IR_GetStatus(&before) == IR_OK, "status before pause test");
    IR_ReferenceEnablePersistPauseInjection(true);
    IR_ServiceProcess();
    IR_ReferenceEnablePersistPauseInjection(false);
    failed |= Check(IR_GetStatus(&after) == IR_OK, "status after pause test");
    failed |= Check(after.task_lost_count == (before.task_lost_count + 1U),
                    "Task event dropped during persistence pause is counted");
    failed |= Check(after.isr_lost_count == (before.isr_lost_count + 1U),
                    "ISR event dropped during persistence pause is counted");
    IR_ServiceProcess();
    failed |= Check((g_ir_retained.header.state_flags & IR_STATE_PERSIST_REQUESTED) == 0U,
                    "updated loss metadata is persisted on the follow-up pass");

    if (failed != 0)
    {
        return 1;
    }

    printf("PASS hardening pending=%u task_lost=%u isr_lost=%u fatal_seq=%u\n",
           (unsigned)IR_ReferencePendingPersistentCount(),
           (unsigned)after.task_lost_count,
           (unsigned)after.isr_lost_count,
           (unsigned)g_ir_retained.header.fatal_publish_sequence);
    return 0;
}
