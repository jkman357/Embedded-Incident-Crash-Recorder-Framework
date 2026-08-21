#include <string.h>

#include "incident_recorder.h"
#include "ir_reference_project.h"

#define REFERENCE_BUFFER_BYTES      (16U * 1024U)
#define REFERENCE_PERSIST_MAGIC     (0x49525033UL) /* "IRP3" */
#define REFERENCE_COMMIT_MARKER     (0x434D5433UL) /* "CMT3" */
#define REFERENCE_SLOT_COUNT        (2U)
#define REF_TX_EMPTY                (0U)
#define REF_TX_WRITING              (1U)
#define REF_TX_COMMITTED            (2U)
#define REF_EXPORT_NONE             (0U)
#define REF_EXPORT_PENDING          (1U)
#define REF_EXPORT_EXPORTED         (2U)

typedef struct
{
    uint32_t magic;
    uint32_t generation;
    uint32_t record_id;
    uint32_t payload_length;
    uint32_t payload_crc;
    uint32_t transaction_state;
    uint32_t export_state;
    uint32_t commit_marker;
    uint8_t payload[REFERENCE_BUFFER_BYTES];
} RefPersistSlot;

static RefPersistSlot g_slots[REFERENCE_SLOT_COUNT];
static uint8_t g_staging[REFERENCE_BUFFER_BYTES];
static uint32_t g_next_generation;
static uint32_t g_next_record_id;
static uint32_t g_last_persisted_record_id;
static uint32_t g_persist_failure_step;
static bool g_pause_injection_enabled;

static uint8_t g_exported[REFERENCE_BUFFER_BYTES];
static uint32_t g_exported_length;
static uint32_t g_export_expected_length;
static uint32_t g_export_record_id;

static uint32_t g_timestamp;
static uint32_t g_trace_record_count;

static bool RefPutU16(uint8_t *dst, uint32_t *offset, uint16_t value);
static bool RefPutU32(uint8_t *dst, uint32_t *offset, uint32_t value);
static bool RefPutRuntimeRecord(uint8_t *dst, uint32_t *offset, const IR_RuntimeRecord *record);
static bool RefPutFirstAbnormal(uint8_t *dst, uint32_t *offset, const IR_FirstAbnormalSnapshot *snapshot);
static bool RefPutFault(uint8_t *dst, uint32_t *offset, const IR_FaultFrame *fault);
static bool RefPutOperation(uint8_t *dst, uint32_t *offset, const IR_OperationContext *operation);
static uint32_t RefCrc32(const uint8_t *data, uint32_t len);
static void RefClearSlot(RefPersistSlot *slot);
static bool RefSlotValid(const RefPersistSlot *slot);
static void RefRecoverSlots(void);
static RefPersistSlot *RefFindPendingOldest(void);
static RefPersistSlot *RefFindRecord(uint32_t record_id);
static RefPersistSlot *RefFindLatestCommitted(void);
static RefPersistSlot *RefSelectTargetSlot(void);
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
static bool RefTryClaimU32(volatile uint32_t *value, uint32_t expected, uint32_t desired);
static void RefPublishBarrier(void);

static const IR_PlatformOps g_platform_ops =
{
    RefGetTimestamp,
    RefGetResetCause,
    RefGetContextType,
    RefGetContextId,
    RefEnterCritical,
    RefExitCritical,
    RefTryClaimU32,
    RefPublishBarrier
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

/* Appends one little-endian 16-bit value to a bounded serialized image. */
static bool RefPutU16(uint8_t *dst, uint32_t *offset, uint16_t value)
{
    if ((dst == NULL) || (offset == NULL) || ((*offset + 2U) > REFERENCE_BUFFER_BYTES))
    {
        return false;
    }

    dst[*offset + 0U] = (uint8_t)(value & 0xFFU);
    dst[*offset + 1U] = (uint8_t)((value >> 8) & 0xFFU);
    *offset += 2U;
    return true;
}

/* Appends one little-endian 32-bit value to a bounded serialized image. */
static bool RefPutU32(uint8_t *dst, uint32_t *offset, uint32_t value)
{
    if ((dst == NULL) || (offset == NULL) || ((*offset + 4U) > REFERENCE_BUFFER_BYTES))
    {
        return false;
    }

    dst[*offset + 0U] = (uint8_t)(value & 0xFFU);
    dst[*offset + 1U] = (uint8_t)((value >> 8) & 0xFFU);
    dst[*offset + 2U] = (uint8_t)((value >> 16) & 0xFFU);
    dst[*offset + 3U] = (uint8_t)((value >> 24) & 0xFFU);
    *offset += 4U;
    return true;
}

/* Serializes one runtime record without relying on compiler padding. */
static bool RefPutRuntimeRecord(uint8_t *dst, uint32_t *offset, const IR_RuntimeRecord *record)
{
    return (record != NULL) &&
           RefPutU32(dst, offset, record->sequence) &&
           RefPutU32(dst, offset, record->timestamp) &&
           RefPutU16(dst, offset, record->type) &&
           RefPutU16(dst, offset, record->id) &&
           RefPutU16(dst, offset, record->object_id) &&
           RefPutU16(dst, offset, record->flags) &&
           RefPutU32(dst, offset, record->value0) &&
           RefPutU32(dst, offset, record->value1);
}

/* Serializes the fixed first-abnormal snapshot field by field. */
static bool RefPutFirstAbnormal(uint8_t *dst, uint32_t *offset, const IR_FirstAbnormalSnapshot *snapshot)
{
    return (snapshot != NULL) &&
           RefPutU32(dst, offset, snapshot->magic) &&
           RefPutU32(dst, offset, snapshot->epoch_id) &&
           RefPutU32(dst, offset, snapshot->sequence) &&
           RefPutU32(dst, offset, snapshot->timestamp) &&
           RefPutU16(dst, offset, snapshot->object_id) &&
           RefPutU16(dst, offset, snapshot->rule_id) &&
           RefPutU16(dst, offset, snapshot->context_type) &&
           RefPutU16(dst, offset, snapshot->flags) &&
           RefPutU32(dst, offset, snapshot->observed0) &&
           RefPutU32(dst, offset, snapshot->observed1) &&
           RefPutU32(dst, offset, snapshot->last_operation_id) &&
           RefPutU32(dst, offset, snapshot->last_operation_arg0) &&
           RefPutU32(dst, offset, snapshot->last_operation_arg1) &&
           RefPutU32(dst, offset, snapshot->integrity_sentinel);
}

/* Serializes the generic fatal frame field by field. */
static bool RefPutFault(uint8_t *dst, uint32_t *offset, const IR_FaultFrame *fault)
{
    return (fault != NULL) &&
           RefPutU32(dst, offset, fault->pc) &&
           RefPutU32(dst, offset, fault->lr) &&
           RefPutU32(dst, offset, fault->sp) &&
           RefPutU32(dst, offset, fault->psr) &&
           RefPutU32(dst, offset, fault->fault_status0) &&
           RefPutU32(dst, offset, fault->fault_status1) &&
           RefPutU32(dst, offset, fault->fault_address0) &&
           RefPutU32(dst, offset, fault->fault_address1) &&
           RefPutU32(dst, offset, fault->context_id) &&
           RefPutU32(dst, offset, fault->sequence);
}

/* Serializes the latest operation context field by field. */
static bool RefPutOperation(uint8_t *dst, uint32_t *offset, const IR_OperationContext *operation)
{
    return (operation != NULL) &&
           RefPutU16(dst, offset, operation->domain_id) &&
           RefPutU16(dst, offset, operation->operation_id) &&
           RefPutU32(dst, offset, operation->object_or_address) &&
           RefPutU32(dst, offset, operation->arg0) &&
           RefPutU32(dst, offset, operation->arg1) &&
           RefPutU16(dst, offset, operation->result) &&
           RefPutU16(dst, offset, operation->flags);
}

/* Computes a compact CRC-32 for reference transaction integrity validation. */
static uint32_t RefCrc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t i;
    uint32_t bit;

    if (data == NULL)
    {
        return 0U;
    }

    for (i = 0U; i < len; ++i)
    {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; ++bit)
        {
            uint32_t mask = (uint32_t)(0U - (crc & 1U));
            crc = (crc >> 1) ^ (0xEDB88320UL & mask);
        }
    }

    return ~crc;
}

/* Clears one reclaimable or invalid host reference slot. */
static void RefClearSlot(RefPersistSlot *slot)
{
    if (slot != NULL)
    {
        memset(slot, 0, sizeof(*slot));
    }
}

/* Validates transaction metadata, commit marker, bounds, and payload CRC. */
static bool RefSlotValid(const RefPersistSlot *slot)
{
    if ((slot == NULL) ||
        (slot->magic != REFERENCE_PERSIST_MAGIC) ||
        (slot->transaction_state != REF_TX_COMMITTED) ||
        (slot->commit_marker != REFERENCE_COMMIT_MARKER) ||
        (slot->payload_length > REFERENCE_BUFFER_BYTES))
    {
        return false;
    }

    return RefCrc32(slot->payload, slot->payload_length) == slot->payload_crc;
}

/* Models reboot recovery: incomplete/invalid writes are discarded, committed data survives. */
static void RefRecoverSlots(void)
{
    uint32_t i;

    for (i = 0U; i < REFERENCE_SLOT_COUNT; ++i)
    {
        RefPersistSlot *slot = &g_slots[i];

        if (slot->transaction_state == REF_TX_EMPTY)
        {
            continue;
        }

        if (!RefSlotValid(slot))
        {
            RefClearSlot(slot);
            continue;
        }

        if ((slot->export_state != REF_EXPORT_PENDING) &&
            (slot->export_state != REF_EXPORT_EXPORTED))
        {
            slot->export_state = REF_EXPORT_PENDING;
        }
    }
}

/* Finds the oldest pending slot so pending evidence is exported before newer evidence. */
static RefPersistSlot *RefFindPendingOldest(void)
{
    RefPersistSlot *selected = NULL;
    uint32_t i;

    RefRecoverSlots();
    for (i = 0U; i < REFERENCE_SLOT_COUNT; ++i)
    {
        RefPersistSlot *slot = &g_slots[i];
        if (RefSlotValid(slot) && (slot->export_state == REF_EXPORT_PENDING) &&
            ((selected == NULL) || ((int32_t)(slot->generation - selected->generation) < 0)))
        {
            selected = slot;
        }
    }

    return selected;
}

/* Finds one valid persistent generation by public record identifier. */
static RefPersistSlot *RefFindRecord(uint32_t record_id)
{
    uint32_t i;

    RefRecoverSlots();
    for (i = 0U; i < REFERENCE_SLOT_COUNT; ++i)
    {
        if (RefSlotValid(&g_slots[i]) && (g_slots[i].record_id == record_id))
        {
            return &g_slots[i];
        }
    }

    return NULL;
}

/* Finds the newest valid committed generation, pending or exported. */
static RefPersistSlot *RefFindLatestCommitted(void)
{
    RefPersistSlot *selected = NULL;
    uint32_t i;

    RefRecoverSlots();
    for (i = 0U; i < REFERENCE_SLOT_COUNT; ++i)
    {
        RefPersistSlot *slot = &g_slots[i];
        if (RefSlotValid(slot) &&
            ((selected == NULL) || ((int32_t)(slot->generation - selected->generation) > 0)))
        {
            selected = slot;
        }
    }

    return selected;
}

/* Selects only an empty or already-exported slot; pending evidence is never reclaimed. */
static RefPersistSlot *RefSelectTargetSlot(void)
{
    RefPersistSlot *oldest_exported = NULL;
    uint32_t i;

    RefRecoverSlots();
    for (i = 0U; i < REFERENCE_SLOT_COUNT; ++i)
    {
        RefPersistSlot *slot = &g_slots[i];
        if (slot->transaction_state == REF_TX_EMPTY)
        {
            return slot;
        }

        if (RefSlotValid(slot) && (slot->export_state == REF_EXPORT_EXPORTED) &&
            ((oldest_exported == NULL) ||
             ((int32_t)(slot->generation - oldest_exported->generation) < 0)))
        {
            oldest_exported = slot;
        }
    }

    return oldest_exported;
}

/* Builds and atomically publishes one bounded persistent generation in the host reference journal. */
static IR_Result RefPersist(const IR_PersistSource *source)
{
    uint32_t i;
    uint32_t offset = 0U;
    uint32_t generation;
    uint32_t record_id;
    uint32_t crc;
    IR_RuntimeRecord record;
    RefPersistSlot *target;

    if ((source == NULL) ||
        (source->read_task_record == NULL) ||
        (source->read_isr_record == NULL))
    {
        return IR_NOT_AVAILABLE;
    }

    if (g_pause_injection_enabled)
    {
        IR_EventTask(IR_REC_OBSERVATION, 0xE1U, 1U, 0U);
        IR_EventIsr(IR_REC_OBSERVATION, 0xE2U, 1U, 0U);
    }

    memset(g_staging, 0, sizeof(g_staging));

    if (!RefPutU32(g_staging, &offset, REFERENCE_PERSIST_MAGIC) ||
        !RefPutU16(g_staging, &offset, source->schema_version) ||
        !RefPutU16(g_staging, &offset, 2U) ||
        !RefPutU32(g_staging, &offset, source->build_id) ||
        !RefPutU32(g_staging, &offset, source->epoch_id) ||
        !RefPutU32(g_staging, &offset, source->incident_id) ||
        !RefPutU32(g_staging, &offset, source->reset_cause_raw) ||
        !RefPutU32(g_staging, &offset, source->health_flags) ||
        !RefPutU32(g_staging, &offset, source->task_record_count) ||
        !RefPutU32(g_staging, &offset, source->isr_record_count) ||
        !RefPutU32(g_staging, &offset, source->task_lost_count) ||
        !RefPutU32(g_staging, &offset, source->isr_lost_count) ||
        !RefPutU32(g_staging, &offset, source->development_trace_lost_count) ||
        !RefPutU32(g_staging, &offset, source->first_abnormal_valid ? 1U : 0U) ||
        !RefPutU32(g_staging, &offset, source->fatal_snapshot_valid ? 1U : 0U) ||
        !RefPutU32(g_staging, &offset, source->fatal_publish_sequence) ||
        !RefPutFirstAbnormal(g_staging, &offset, &source->first_abnormal) ||
        !RefPutFault(g_staging, &offset, &source->fatal_snapshot) ||
        !RefPutOperation(g_staging, &offset, &source->last_operation))
    {
        return IR_NOT_AVAILABLE;
    }

    for (i = 0U; i < source->task_record_count; ++i)
    {
        if ((source->read_task_record(source->reader_context, i, &record) != IR_OK) ||
            !RefPutRuntimeRecord(g_staging, &offset, &record))
        {
            return IR_NOT_AVAILABLE;
        }
    }

    for (i = 0U; i < source->isr_record_count; ++i)
    {
        if ((source->read_isr_record(source->reader_context, i, &record) != IR_OK) ||
            !RefPutRuntimeRecord(g_staging, &offset, &record))
        {
            return IR_NOT_AVAILABLE;
        }
    }

    target = RefSelectTargetSlot();
    if (target == NULL)
    {
        return IR_NOT_AVAILABLE;
    }

    generation = g_next_generation + 1U;
    record_id = g_next_record_id + 1U;
    crc = RefCrc32(g_staging, offset);

    RefClearSlot(target);
    target->transaction_state = REF_TX_WRITING;
    target->magic = REFERENCE_PERSIST_MAGIC;
    target->generation = generation;
    target->record_id = record_id;
    target->payload_length = offset;
    target->payload_crc = crc;
    target->export_state = REF_EXPORT_PENDING;

    if (g_persist_failure_step == 1U)
    {
        return IR_NOT_AVAILABLE;
    }

    memcpy(target->payload, g_staging, offset);

    if (g_persist_failure_step == 2U)
    {
        return IR_NOT_AVAILABLE;
    }

    if (RefCrc32(target->payload, target->payload_length) != target->payload_crc)
    {
        return IR_NOT_AVAILABLE;
    }

    target->transaction_state = REF_TX_COMMITTED;
    RefPublishBarrier();
    target->commit_marker = REFERENCE_COMMIT_MARKER;

    g_next_generation = generation;
    g_next_record_id = record_id;
    g_last_persisted_record_id = record_id;
    return IR_OK;
}

/* Reports whether the bounded reference journal has evidence awaiting export. */
static bool RefPersistentHasPending(void)
{
    return RefFindPendingOldest() != NULL;
}

/* Returns metadata for the oldest pending persistent record. */
static IR_Result RefPersistentReadMeta(uint32_t *record_id, uint32_t *payload_length)
{
    RefPersistSlot *slot;

    if ((record_id == NULL) || (payload_length == NULL))
    {
        return IR_NOT_AVAILABLE;
    }

    slot = RefFindPendingOldest();
    if (slot == NULL)
    {
        return IR_NOT_AVAILABLE;
    }

    *record_id = slot->record_id;
    *payload_length = slot->payload_length;
    return IR_OK;
}

/* Reads one bounded chunk from a validated persistent generation. */
static IR_Result RefPersistentReadPayload(uint32_t record_id, uint32_t offset, void *dst, uint32_t len)
{
    RefPersistSlot *slot = RefFindRecord(record_id);

    if ((dst == NULL) || (slot == NULL) ||
        (offset > slot->payload_length) ||
        (len > (slot->payload_length - offset)))
    {
        return IR_NOT_AVAILABLE;
    }

    memcpy(dst, &slot->payload[offset], len);
    return IR_OK;
}

/* Marks one committed generation exported without deleting its persistent bytes. */
static IR_Result RefPersistentMarkExported(uint32_t record_id)
{
    RefPersistSlot *slot = RefFindRecord(record_id);

    if ((slot == NULL) || (slot->export_state != REF_EXPORT_PENDING))
    {
        return IR_NOT_AVAILABLE;
    }

    slot->export_state = REF_EXPORT_EXPORTED;
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
    RefPersistSlot *slot = RefFindRecord(record_id);
    (void)build_id;
    (void)schema_version;

    if ((slot == NULL) || (payload_length != slot->payload_length) ||
        (payload_length > REFERENCE_BUFFER_BYTES))
    {
        return IR_NOT_AVAILABLE;
    }

    g_exported_length = 0U;
    g_export_expected_length = payload_length;
    g_export_record_id = record_id;
    memset(g_exported, 0, sizeof(g_exported));
    return IR_OK;
}

/* Appends one bounded chunk to the synthetic export destination. */
static IR_Result RefExportWrite(const void *data, uint32_t len)
{
    if ((data == NULL) || (g_exported_length > REFERENCE_BUFFER_BYTES) ||
        (len > (REFERENCE_BUFFER_BYTES - g_exported_length)))
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

/* Verifies that the export copy exactly matches the selected persistent generation. */
static IR_Result RefExportVerify(void)
{
    RefPersistSlot *slot = RefFindRecord(g_export_record_id);

    if ((slot == NULL) ||
        (g_exported_length != slot->payload_length) ||
        (memcmp(g_exported, slot->payload, g_exported_length) != 0))
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
    g_export_record_id = 0U;
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

/* Ends a project-owned Development trace session when the project elects to close it. */
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

/* Returns a default Task context for the host reference. */
static IR_ContextType RefGetContextType(void)
{
    return IR_CONTEXT_TASK;
}

/* Returns a stable synthetic context identifier. */
static uint32_t RefGetContextId(void)
{
    return 1U;
}

/* Host reference critical primitive; target projects replace and measure it. */
static IR_CriticalKey RefEnterCritical(void)
{
    return 0U;
}

/* Completes the host reference critical primitive. */
static void RefExitCritical(IR_CriticalKey key)
{
    (void)key;
}

/* Provides an atomic compare-and-claim primitive for fatal-snapshot ownership. */
static bool RefTryClaimU32(volatile uint32_t *value, uint32_t expected, uint32_t desired)
{
    if (value == NULL)
    {
        return false;
    }

#if defined(__GNUC__) || defined(__clang__)
    return __atomic_compare_exchange_n(value,
                                       &expected,
                                       desired,
                                       false,
                                       __ATOMIC_ACQ_REL,
                                       __ATOMIC_ACQUIRE);
#else
    if (*value != expected)
    {
        return false;
    }
    *value = desired;
    return true;
#endif
}

/* Provides the host compiler/publication barrier used before valid markers. */
static void RefPublishBarrier(void)
{
#if defined(__GNUC__) || defined(__clang__)
    __atomic_thread_fence(__ATOMIC_RELEASE);
#else
    volatile uint32_t barrier = 0U;
    (void)barrier;
#endif
}

/* Returns the newest valid persistent payload size. */
uint32_t IR_ReferencePersistedBytes(void)
{
    RefPersistSlot *slot = RefFindLatestCommitted();
    return (slot != NULL) ? slot->payload_length : 0U;
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
    RefPersistSlot *slot = RefFindRecord(g_export_record_id);
    return (slot != NULL) &&
           (g_exported_length == slot->payload_length) &&
           (memcmp(g_exported, slot->payload, g_exported_length) == 0);
}

/* Exposes whether any committed generation is still pending export. */
bool IR_ReferenceHasPendingPersistentEvidence(void)
{
    return RefPersistentHasPending();
}

/* Returns the number of committed generations currently pending export. */
uint32_t IR_ReferencePendingPersistentCount(void)
{
    uint32_t i;
    uint32_t count = 0U;

    RefRecoverSlots();
    for (i = 0U; i < REFERENCE_SLOT_COUNT; ++i)
    {
        if (RefSlotValid(&g_slots[i]) && (g_slots[i].export_state == REF_EXPORT_PENDING))
        {
            ++count;
        }
    }
    return count;
}

/* Returns the most recently committed public record identifier. */
uint32_t IR_ReferenceLastPersistedRecordId(void)
{
    return g_last_persisted_record_id;
}

/* Reads one byte from a committed generation for host validation. */
bool IR_ReferenceReadPersistentByte(uint32_t record_id, uint32_t offset, uint8_t *value)
{
    RefPersistSlot *slot = RefFindRecord(record_id);
    if ((slot == NULL) || (value == NULL) || (offset >= slot->payload_length))
    {
        return false;
    }
    *value = slot->payload[offset];
    return true;
}

/* Selects a bounded reference failure point: 0=none, 1=after begin, 2=after body. */
void IR_ReferenceSetPersistFailureStep(uint32_t step)
{
    g_persist_failure_step = step;
}

/* Enables Task/ISR event injection while persistence has paused ordinary capture. */
void IR_ReferenceEnablePersistPauseInjection(bool enable)
{
    g_pause_injection_enabled = enable;
}

/* Runs the same bounded recovery scan that a host reboot model would perform. */
void IR_ReferenceSimulateRecovery(void)
{
    RefRecoverSlots();
}

/* Resets only the host reference persistent journal for isolated validation fixtures. */
void IR_ReferenceResetPersistentStore(void)
{
    memset(g_slots, 0, sizeof(g_slots));
    memset(g_staging, 0, sizeof(g_staging));
    g_next_generation = 0U;
    g_next_record_id = 0U;
    g_last_persisted_record_id = 0U;
    g_persist_failure_step = 0U;
    g_exported_length = 0U;
    g_export_expected_length = 0U;
    g_export_record_id = 0U;
}
