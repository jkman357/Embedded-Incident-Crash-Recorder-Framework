#include <string.h>

#include "incident_recorder.h"
#include "ir_reference_project.h"

#define REFERENCE_BUFFER_BYTES (16U * 1024U)
#define REFERENCE_PERSIST_MAGIC (0x49525032UL) /* "IRP2" */

static uint8_t g_persisted[REFERENCE_BUFFER_BYTES];
static uint32_t g_persisted_length;
static uint32_t g_persisted_record_id;
static bool g_persisted_pending;

static uint8_t g_exported[REFERENCE_BUFFER_BYTES];
static uint32_t g_exported_length;
static uint32_t g_export_expected_length;

static uint32_t g_timestamp;
static uint32_t g_trace_record_count;

static bool RefPutU16(uint32_t *offset, uint16_t value);
static bool RefPutU32(uint32_t *offset, uint32_t value);
static bool RefPutRuntimeRecord(uint32_t *offset, const IR_RuntimeRecord *record);
static bool RefPutFirstAbnormal(uint32_t *offset, const IR_FirstAbnormalSnapshot *snapshot);
static bool RefPutFault(uint32_t *offset, const IR_FaultFrame *fault);
static bool RefPutOperation(uint32_t *offset, const IR_OperationContext *operation);
static IR_Result RefPersist(const IR_PersistSource *source);
static bool RefPersistentHasPending(void);
static IR_Result RefPersistentReadMeta(uint32_t *record_id, uint32_t *payload_length);
static IR_Result RefPersistentReadPayload(uint32_t record_id, uint32_t offset, void *dst, uint32_t len);
static IR_Result RefPersistentMarkExported(uint32_t record_id);
static bool RefExportAvailable(void);
static IR_Result RefExportBegin(uint32_t record_id,
                                uint32_t payload_length,
                                uint32_t build_id,
                                uint16_t schema_version);
static IR_Result RefExportWrite(const void *data, uint32_t len);
static IR_Result RefExportEnd(void);
static IR_Result RefExportVerify(void);
static void RefExportAbort(void);
#if IR_ENABLE_CONTINUOUS_SD_TRACE
static bool RefTraceAvailable(void);
static IR_Result RefTraceBegin(uint32_t build_id, uint16_t schema_version);
static IR_Result RefTraceWrite(IR_ContextType context, const IR_RuntimeRecord *record);
static IR_Result RefTraceEnd(void);
#endif
static uint32_t RefGetTimestamp(void);
static uint32_t RefGetResetCause(void);
static IR_ContextType RefGetContextType(void);
static uint32_t RefGetContextId(void);
static IR_CriticalKey RefEnterCritical(void);
static void RefExitCritical(IR_CriticalKey key);

static const IR_PlatformOps g_platform_ops =
{
    RefGetTimestamp,
    RefGetResetCause,
    RefGetContextType,
    RefGetContextId,
    RefEnterCritical,
    RefExitCritical
};

static const IR_PersistenceOps g_persistence_ops =
{
    RefPersist
};

static const IR_PersistenceExportOps g_persistent_export_ops =
{
    RefPersistentHasPending,
    RefPersistentReadMeta,
    RefPersistentReadPayload,
    RefPersistentMarkExported
};

static const IR_ExportOps g_export_ops =
{
    RefExportAvailable,
    RefExportBegin,
    RefExportWrite,
    RefExportEnd,
    RefExportVerify,
    RefExportAbort
};

#if IR_ENABLE_CONTINUOUS_SD_TRACE
static const IR_ContinuousTraceOps g_trace_ops =
{
    RefTraceAvailable,
    RefTraceBegin,
    RefTraceWrite,
    RefTraceEnd
};
#define REF_TRACE_OPS (&g_trace_ops)
#else
#define REF_TRACE_OPS (NULL)
#endif

static const IR_Config g_reference_config =
{
    &g_platform_ops,
    REF_TRACE_OPS,
    &g_persistence_ops,
    &g_persistent_export_ops,
    &g_export_ops
};

/* Returns the static adapter set used only by the portable reference build. */
const IR_Config *IR_ProjectConfig(void)
{
    return &g_reference_config;
}

/* Appends one little-endian 16-bit value to the synthetic persistent image. */
static bool RefPutU16(uint32_t *offset, uint16_t value)
{
    if ((offset == NULL) || ((*offset + 2U) > REFERENCE_BUFFER_BYTES))
    {
        return false;
    }

    g_persisted[*offset + 0U] = (uint8_t)(value & 0xFFU);
    g_persisted[*offset + 1U] = (uint8_t)((value >> 8) & 0xFFU);
    *offset += 2U;
    return true;
}

/* Appends one little-endian 32-bit value to the synthetic persistent image. */
static bool RefPutU32(uint32_t *offset, uint32_t value)
{
    if ((offset == NULL) || ((*offset + 4U) > REFERENCE_BUFFER_BYTES))
    {
        return false;
    }

    g_persisted[*offset + 0U] = (uint8_t)(value & 0xFFU);
    g_persisted[*offset + 1U] = (uint8_t)((value >> 8) & 0xFFU);
    g_persisted[*offset + 2U] = (uint8_t)((value >> 16) & 0xFFU);
    g_persisted[*offset + 3U] = (uint8_t)((value >> 24) & 0xFFU);
    *offset += 4U;
    return true;
}

/* Serializes one runtime record without relying on compiler padding. */
static bool RefPutRuntimeRecord(uint32_t *offset, const IR_RuntimeRecord *record)
{
    return (record != NULL) &&
           RefPutU32(offset, record->sequence) &&
           RefPutU32(offset, record->timestamp) &&
           RefPutU16(offset, record->type) &&
           RefPutU16(offset, record->id) &&
           RefPutU16(offset, record->object_id) &&
           RefPutU16(offset, record->flags) &&
           RefPutU32(offset, record->value0) &&
           RefPutU32(offset, record->value1);
}

/* Serializes the fixed first-abnormal snapshot field by field. */
static bool RefPutFirstAbnormal(uint32_t *offset, const IR_FirstAbnormalSnapshot *snapshot)
{
    return (snapshot != NULL) &&
           RefPutU32(offset, snapshot->magic) &&
           RefPutU32(offset, snapshot->epoch_id) &&
           RefPutU32(offset, snapshot->sequence) &&
           RefPutU32(offset, snapshot->timestamp) &&
           RefPutU16(offset, snapshot->object_id) &&
           RefPutU16(offset, snapshot->rule_id) &&
           RefPutU16(offset, snapshot->context_type) &&
           RefPutU16(offset, snapshot->flags) &&
           RefPutU32(offset, snapshot->observed0) &&
           RefPutU32(offset, snapshot->observed1) &&
           RefPutU32(offset, snapshot->last_operation_id) &&
           RefPutU32(offset, snapshot->last_operation_arg0) &&
           RefPutU32(offset, snapshot->last_operation_arg1) &&
           RefPutU32(offset, snapshot->checksum_or_commit);
}

/* Serializes the generic fatal frame field by field. */
static bool RefPutFault(uint32_t *offset, const IR_FaultFrame *fault)
{
    return (fault != NULL) &&
           RefPutU32(offset, fault->pc) &&
           RefPutU32(offset, fault->lr) &&
           RefPutU32(offset, fault->sp) &&
           RefPutU32(offset, fault->psr) &&
           RefPutU32(offset, fault->fault_status0) &&
           RefPutU32(offset, fault->fault_status1) &&
           RefPutU32(offset, fault->fault_address0) &&
           RefPutU32(offset, fault->fault_address1) &&
           RefPutU32(offset, fault->context_id) &&
           RefPutU32(offset, fault->sequence);
}

/* Serializes the latest operation context field by field. */
static bool RefPutOperation(uint32_t *offset, const IR_OperationContext *operation)
{
    return (operation != NULL) &&
           RefPutU16(offset, operation->domain_id) &&
           RefPutU16(offset, operation->operation_id) &&
           RefPutU32(offset, operation->object_or_address) &&
           RefPutU32(offset, operation->arg0) &&
           RefPutU32(offset, operation->arg1) &&
           RefPutU16(offset, operation->result) &&
           RefPutU16(offset, operation->flags);
}

/*
 * Builds a synthetic transactional persistent image for host validation.
 * Real projects replace this adapter with technology-specific NVM handling.
 */
static IR_Result RefPersist(const IR_PersistSource *source)
{
    uint32_t i;
    uint32_t offset = 0U;
    IR_RuntimeRecord record;

    if ((source == NULL) ||
        (source->read_task_record == NULL) ||
        (source->read_isr_record == NULL))
    {
        return IR_NOT_AVAILABLE;
    }

    memset(g_persisted, 0, sizeof(g_persisted));

    if (!RefPutU32(&offset, REFERENCE_PERSIST_MAGIC) ||
        !RefPutU16(&offset, source->schema_version) ||
        !RefPutU16(&offset, 1U) ||
        !RefPutU32(&offset, source->build_id) ||
        !RefPutU32(&offset, source->epoch_id) ||
        !RefPutU32(&offset, source->incident_id) ||
        !RefPutU32(&offset, source->reset_cause_raw) ||
        !RefPutU32(&offset, source->health_flags) ||
        !RefPutU32(&offset, source->task_record_count) ||
        !RefPutU32(&offset, source->isr_record_count) ||
        !RefPutU32(&offset, source->first_abnormal_valid ? 1U : 0U) ||
        !RefPutU32(&offset, source->fatal_snapshot_valid ? 1U : 0U) ||
        !RefPutFirstAbnormal(&offset, &source->first_abnormal) ||
        !RefPutFault(&offset, &source->fatal_snapshot) ||
        !RefPutOperation(&offset, &source->last_operation))
    {
        return IR_NOT_AVAILABLE;
    }

    for (i = 0U; i < source->task_record_count; ++i)
    {
        if ((source->read_task_record(source->reader_context, i, &record) != IR_OK) ||
            !RefPutRuntimeRecord(&offset, &record))
        {
            return IR_NOT_AVAILABLE;
        }
    }

    for (i = 0U; i < source->isr_record_count; ++i)
    {
        if ((source->read_isr_record(source->reader_context, i, &record) != IR_OK) ||
            !RefPutRuntimeRecord(&offset, &record))
        {
            return IR_NOT_AVAILABLE;
        }
    }

    g_persisted_length = offset;
    ++g_persisted_record_id;
    g_persisted_pending = true;
    return IR_OK;
}

/* Reports whether the synthetic persistent store has evidence awaiting export. */
static bool RefPersistentHasPending(void)
{
    return g_persisted_pending;
}

/* Returns metadata for the synthetic pending persistent record. */
static IR_Result RefPersistentReadMeta(uint32_t *record_id, uint32_t *payload_length)
{
    if ((record_id == NULL) || (payload_length == NULL) || !g_persisted_pending)
    {
        return IR_NOT_AVAILABLE;
    }

    *record_id = g_persisted_record_id;
    *payload_length = g_persisted_length;
    return IR_OK;
}

/* Reads one bounded chunk from the synthetic persistent record. */
static IR_Result RefPersistentReadPayload(uint32_t record_id, uint32_t offset, void *dst, uint32_t len)
{
    if ((dst == NULL) ||
        (record_id != g_persisted_record_id) ||
        (offset > g_persisted_length) ||
        (len > (g_persisted_length - offset)))
    {
        return IR_NOT_AVAILABLE;
    }

    memcpy(dst, &g_persisted[offset], len);
    return IR_OK;
}

/* Marks the record exported without deleting the persistent evidence bytes. */
static IR_Result RefPersistentMarkExported(uint32_t record_id)
{
    if (record_id != g_persisted_record_id)
    {
        return IR_NOT_AVAILABLE;
    }

    g_persisted_pending = false;
    return IR_OK;
}

/* Reports the synthetic export destination as available. */
static bool RefExportAvailable(void)
{
    return true;
}

/* Starts a synthetic export transaction without touching the persistent copy. */
static IR_Result RefExportBegin(uint32_t record_id,
                                uint32_t payload_length,
                                uint32_t build_id,
                                uint16_t schema_version)
{
    (void)record_id;
    (void)build_id;
    (void)schema_version;

    if (payload_length > REFERENCE_BUFFER_BYTES)
    {
        return IR_NOT_AVAILABLE;
    }

    g_exported_length = 0U;
    g_export_expected_length = payload_length;
    memset(g_exported, 0, sizeof(g_exported));
    return IR_OK;
}

/* Appends one bounded chunk to the synthetic export destination. */
static IR_Result RefExportWrite(const void *data, uint32_t len)
{
    if ((data == NULL) || (len > (REFERENCE_BUFFER_BYTES - g_exported_length)))
    {
        return IR_NOT_AVAILABLE;
    }

    memcpy(&g_exported[g_exported_length], data, len);
    g_exported_length += len;
    return IR_OK;
}

/* Finalizes the synthetic export transaction. */
static IR_Result RefExportEnd(void)
{
    return (g_exported_length == g_export_expected_length) ? IR_OK : IR_NOT_AVAILABLE;
}

/* Verifies that the export copy exactly matches the retained persistent image. */
static IR_Result RefExportVerify(void)
{
    if ((g_exported_length != g_persisted_length) ||
        (memcmp(g_exported, g_persisted, g_exported_length) != 0))
    {
        return IR_NOT_AVAILABLE;
    }

    return IR_OK;
}

/* Aborts the synthetic destination transaction while preserving persistent evidence. */
static void RefExportAbort(void)
{
    g_exported_length = 0U;
    g_export_expected_length = 0U;
}

#if IR_ENABLE_CONTINUOUS_SD_TRACE
/* Reports the Development trace sink as available in the host reference build. */
static bool RefTraceAvailable(void)
{
    return true;
}

/* Starts a new synthetic Development trace session. */
static IR_Result RefTraceBegin(uint32_t build_id, uint16_t schema_version)
{
    (void)build_id;
    (void)schema_version;
    g_trace_record_count = 0U;
    return IR_OK;
}

/* Counts a Development trace record without performing real filesystem I/O. */
static IR_Result RefTraceWrite(IR_ContextType context, const IR_RuntimeRecord *record)
{
    (void)context;
    if (record == NULL)
    {
        return IR_NOT_AVAILABLE;
    }

    ++g_trace_record_count;
    return IR_OK;
}

/* Ends the synthetic Development trace session. */
static IR_Result RefTraceEnd(void)
{
    return IR_OK;
}

#endif

/* Supplies a monotonic synthetic timestamp for host-only validation. */
static uint32_t RefGetTimestamp(void)
{
    return ++g_timestamp;
}

/* Supplies a synthetic raw reset cause. */
static uint32_t RefGetResetCause(void)
{
    return 0x1U;
}

/* Returns a default Task context for the single-threaded host reference. */
static IR_ContextType RefGetContextType(void)
{
    return IR_CONTEXT_TASK;
}

/* Returns a stable synthetic context identifier. */
static uint32_t RefGetContextId(void)
{
    return 1U;
}

/*
 * Host-only critical primitive. Real embedded adapters must replace this with
 * a measured bounded primitive that protects Task/ISR shared recorder state.
 */
static IR_CriticalKey RefEnterCritical(void)
{
    return 0U;
}

/* Completes the host-only no-op critical primitive. */
static void RefExitCritical(IR_CriticalKey key)
{
    (void)key;
}

/* Returns the current synthetic persistent payload size. */
uint32_t IR_ReferencePersistedBytes(void)
{
    return g_persisted_length;
}

/* Returns the current synthetic exported payload size. */
uint32_t IR_ReferenceExportedBytes(void)
{
    return g_exported_length;
}

/* Returns how many Development trace records reached the synthetic sink. */
uint32_t IR_ReferenceContinuousTraceRecords(void)
{
    return g_trace_record_count;
}

/* Confirms that export verification retained byte-for-byte evidence identity. */
bool IR_ReferenceExportMatchesPersistence(void)
{
    return (g_exported_length == g_persisted_length) &&
           (memcmp(g_exported, g_persisted, g_exported_length) == 0);
}

/* Exposes the synthetic pending-export state to the validation executable. */
bool IR_ReferenceHasPendingPersistentEvidence(void)
{
    return g_persisted_pending;
}
