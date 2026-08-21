#include "ir_internal.h"

#define IR_EXPORT_CHUNK_BYTES (64U)

static IR_Result IR_ServiceReadTaskRecord(void *context, uint32_t logical_index, IR_RuntimeRecord *record);
static IR_Result IR_ServiceReadIsrRecord(void *context, uint32_t logical_index, IR_RuntimeRecord *record);
static IR_Result IR_ServiceReadRingRecord(const IR_RingState *state,
                                          const IR_RuntimeRecord *ring,
                                          uint32_t capacity,
                                          uint32_t logical_index,
                                          IR_RuntimeRecord *record);
static IR_Result IR_ServicePersistCurrent(bool *snapshot_unchanged);
static void IR_ServiceProcessContinuousTrace(void);
static void IR_ServiceProcessExport(void);
static void IR_ServiceResetExport(bool call_abort);

/* Queues a Development-only runtime copy without involving physical SD I/O. */
void IR_InternalQueueDevelopmentTrace(IR_ContextType context, const IR_RuntimeRecord *record)
{
#if IR_ENABLE_CONTINUOUS_SD_TRACE
    IR_TraceQueue *queue;
    IR_CriticalKey key;

    if ((record == NULL) || !g_ir_runtime.initialized)
    {
        return;
    }

    queue = (context == IR_CONTEXT_ISR) ? &g_ir_isr_trace_queue : &g_ir_task_trace_queue;

    key = IR_InternalEnterCritical();
    if (queue->count >= IR_DEV_TRACE_QUEUE_RECORDS)
    {
        IR_InternalSaturatingIncrement(&g_ir_runtime.dev_trace_lost_count);
        IR_InternalExitCritical(key);
        IR_InternalSetHealth(IR_HEALTH_TRACE_DROPPED | IR_HEALTH_RECORD_DROPPED);
        return;
    }

    queue->record[queue->write_index] = *record;
    ++queue->write_index;
    if (queue->write_index >= IR_DEV_TRACE_QUEUE_RECORDS)
    {
        queue->write_index = 0U;
    }
    ++queue->count;
    IR_InternalExitCritical(key);
#else
    (void)context;
    (void)record;
#endif
}

/* Pops the oldest Development trace record across Task/ISR queues by timestamp. */
bool IR_InternalPopDevelopmentTrace(IR_ContextType *context, IR_RuntimeRecord *record)
{
#if IR_ENABLE_CONTINUOUS_SD_TRACE
    IR_TraceQueue *queue = NULL;
    IR_CriticalKey key;

    if ((context == NULL) || (record == NULL))
    {
        return false;
    }

    key = IR_InternalEnterCritical();

    if ((g_ir_task_trace_queue.count == 0U) && (g_ir_isr_trace_queue.count == 0U))
    {
        IR_InternalExitCritical(key);
        return false;
    }

    if (g_ir_task_trace_queue.count == 0U)
    {
        queue = &g_ir_isr_trace_queue;
        *context = IR_CONTEXT_ISR;
    }
    else if (g_ir_isr_trace_queue.count == 0U)
    {
        queue = &g_ir_task_trace_queue;
        *context = IR_CONTEXT_TASK;
    }
    else if (g_ir_task_trace_queue.record[g_ir_task_trace_queue.read_index].timestamp <=
             g_ir_isr_trace_queue.record[g_ir_isr_trace_queue.read_index].timestamp)
    {
        queue = &g_ir_task_trace_queue;
        *context = IR_CONTEXT_TASK;
    }
    else
    {
        queue = &g_ir_isr_trace_queue;
        *context = IR_CONTEXT_ISR;
    }

    *record = queue->record[queue->read_index];
    ++queue->read_index;
    if (queue->read_index >= IR_DEV_TRACE_QUEUE_RECORDS)
    {
        queue->read_index = 0U;
    }
    --queue->count;

    IR_InternalExitCritical(key);
    return true;
#else
    (void)context;
    (void)record;
    return false;
#endif
}

/* Requests a service-context export; no storage or filesystem I/O occurs here. */
IR_Result IR_ServiceRequestExport(void)
{
#if !IR_ENABLE || !IR_ENABLE_EXPORT
    return IR_NOT_AVAILABLE;
#else
    if (!g_ir_runtime.initialized)
    {
        return IR_NOT_AVAILABLE;
    }

    g_ir_runtime.export_requested = true;
    return IR_OK;
#endif
}

/* Processes bounded persistence, Development trace, and on-demand export work. */
void IR_ServiceProcess(void)
{
#if IR_ENABLE
    if (!g_ir_runtime.initialized || (g_ir_runtime.config == NULL))
    {
        return;
    }

    if ((g_ir_runtime.config->persistent_export != NULL) &&
        (g_ir_runtime.config->persistent_export->has_pending != NULL))
    {
        g_ir_runtime.persistent_export_pending =
            g_ir_runtime.config->persistent_export->has_pending();
    }

#if IR_ENABLE_PERSISTENCE
    if (g_ir_runtime.previous_epoch_pending ||
        (g_ir_runtime.retained_valid &&
         ((g_ir_retained.header.state_flags & IR_STATE_PERSIST_REQUESTED) != 0U)))
    {
        bool snapshot_unchanged = false;
        IR_Result result = IR_ServicePersistCurrent(&snapshot_unchanged);
        if (result == IR_OK)
        {
            IR_CriticalKey key;
            uint32_t previous_epoch;
            bool was_previous;

            if ((g_ir_runtime.config->persistent_export != NULL) &&
                (g_ir_runtime.config->persistent_export->has_pending != NULL))
            {
                g_ir_runtime.persistent_export_pending =
                    g_ir_runtime.config->persistent_export->has_pending();
            }

            key = IR_InternalEnterCritical();
            previous_epoch = g_ir_retained.header.epoch_id;
            was_previous = g_ir_runtime.previous_epoch_pending;
            if (snapshot_unchanged)
            {
                g_ir_retained.header.state_flags &= ~IR_STATE_PERSIST_REQUESTED;
                g_ir_retained.header.state_flags |= IR_STATE_PERSISTED;
            }
            else
            {
                g_ir_retained.header.state_flags &= ~IR_STATE_PERSISTED;
                g_ir_retained.header.state_flags |= IR_STATE_PERSIST_REQUESTED;
            }
            IR_InternalExitCritical(key);

            if (was_previous && snapshot_unchanged)
            {
                g_ir_runtime.transient_health |= IR_HEALTH_SALVAGE_USED;
                IR_InternalBeginFreshEpoch(previous_epoch);
            }
        }
        else if (result == IR_NOT_AVAILABLE)
        {
            IR_InternalSetHealth(IR_HEALTH_PERSIST_FAILED | IR_HEALTH_DEGRADED);
        }
    }
#endif

    IR_ServiceProcessContinuousTrace();
    IR_ServiceProcessExport();
#endif
}

/* Presents a stable logical evidence view to the project persistence adapter. */
static IR_Result IR_ServicePersistCurrent(bool *snapshot_unchanged)
{
    IR_PersistSource source;
    IR_CriticalKey key;
    bool restore_capture;
    const IR_PersistenceOps *ops;

    if (snapshot_unchanged == NULL)
    {
        return IR_NOT_AVAILABLE;
    }
    *snapshot_unchanged = false;

    ops = g_ir_runtime.config->persistence;
    if ((ops == NULL) || (ops->persist == NULL))
    {
        return IR_NOT_AVAILABLE;
    }

    memset(&source, 0, sizeof(source));

    key = IR_InternalEnterCritical();
    restore_capture = g_ir_runtime.capture_enabled && !g_ir_runtime.previous_epoch_pending;
    g_ir_runtime.capture_enabled = false;
    source.build_id = g_ir_retained.header.build_id;
    source.schema_version = g_ir_retained.header.schema_version;
    source.epoch_id = g_ir_retained.header.epoch_id;
    source.incident_id = g_ir_retained.header.incident_id;
    source.reset_cause_raw = g_ir_retained.header.reset_cause_raw;
    source.health_flags = g_ir_retained.header.health_flags | g_ir_runtime.transient_health;
    source.task_record_count = g_ir_retained.task_state.record_count;
    source.isr_record_count = g_ir_retained.isr_state.record_count;
    source.first_abnormal_valid =
        (g_ir_retained.header.first_abnormal_state == IR_STATE_FIRST_VALID) &&
        (g_ir_retained.first_abnormal.checksum_or_commit == IR_FIRST_ABNORMAL_COMMIT);
    source.fatal_snapshot_valid =
        ((g_ir_retained.header.state_flags & IR_STATE_FATAL_VALID) != 0U);
    source.first_abnormal = g_ir_retained.first_abnormal;
    source.fatal_snapshot = g_ir_retained.fatal_snapshot;
    source.last_operation = g_ir_retained.last_operation;
    IR_InternalExitCritical(key);

    source.reader_context = &g_ir_retained;
    source.read_task_record = IR_ServiceReadTaskRecord;
    source.read_isr_record = IR_ServiceReadIsrRecord;

    {
        IR_Result result = ops->persist(&source);
        key = IR_InternalEnterCritical();
        if (result == IR_OK)
        {
            bool current_first_valid =
                (g_ir_retained.header.first_abnormal_state == IR_STATE_FIRST_VALID) &&
                (g_ir_retained.first_abnormal.checksum_or_commit == IR_FIRST_ABNORMAL_COMMIT);
            bool current_fatal_valid =
                ((g_ir_retained.header.state_flags & IR_STATE_FATAL_VALID) != 0U);

            *snapshot_unchanged =
                (g_ir_retained.header.epoch_id == source.epoch_id) &&
                (g_ir_retained.header.incident_id == source.incident_id) &&
                (current_first_valid == source.first_abnormal_valid) &&
                (current_fatal_valid == source.fatal_snapshot_valid) &&
                (!current_first_valid ||
                 (memcmp(&g_ir_retained.first_abnormal,
                         &source.first_abnormal,
                         sizeof(source.first_abnormal)) == 0)) &&
                (!current_fatal_valid ||
                 (memcmp(&g_ir_retained.fatal_snapshot,
                         &source.fatal_snapshot,
                         sizeof(source.fatal_snapshot)) == 0));
        }

        if (restore_capture)
        {
            g_ir_runtime.capture_enabled = true;
        }
        IR_InternalExitCritical(key);
        return result;
    }
}

/* Reads one Task record in chronological order using a bounded critical copy. */
static IR_Result IR_ServiceReadTaskRecord(void *context, uint32_t logical_index, IR_RuntimeRecord *record)
{
    IR_RetainedStore *store = (IR_RetainedStore *)context;
    if (store == NULL)
    {
        return IR_NOT_AVAILABLE;
    }
    return IR_ServiceReadRingRecord(&store->task_state,
                                    store->task_ring,
                                    IR_TASK_RECORD_COUNT,
                                    logical_index,
                                    record);
}

/* Reads one ISR record in chronological order using a bounded critical copy. */
static IR_Result IR_ServiceReadIsrRecord(void *context, uint32_t logical_index, IR_RuntimeRecord *record)
{
    IR_RetainedStore *store = (IR_RetainedStore *)context;
    if (store == NULL)
    {
        return IR_NOT_AVAILABLE;
    }
    return IR_ServiceReadRingRecord(&store->isr_state,
                                    store->isr_ring,
                                    IR_ISR_RECORD_COUNT,
                                    logical_index,
                                    record);
}

/* Converts a logical oldest-to-newest index into a validated physical ring index. */
static IR_Result IR_ServiceReadRingRecord(const IR_RingState *state,
                                          const IR_RuntimeRecord *ring,
                                          uint32_t capacity,
                                          uint32_t logical_index,
                                          IR_RuntimeRecord *record)
{
    IR_CriticalKey key;
    uint32_t oldest;
    uint32_t physical;

    if ((state == NULL) || (ring == NULL) || (record == NULL) || (capacity == 0U))
    {
        return IR_NOT_AVAILABLE;
    }

    key = IR_InternalEnterCritical();
    if ((state->write_index >= capacity) ||
        (state->record_count > capacity) ||
        (logical_index >= state->record_count))
    {
        IR_InternalExitCritical(key);
        return IR_NOT_AVAILABLE;
    }

    oldest = (state->record_count == capacity) ? state->write_index : 0U;
    physical = oldest + logical_index;
    if (physical >= capacity)
    {
        physical -= capacity;
    }

    *record = ring[physical];
    IR_InternalExitCritical(key);
    return IR_OK;
}

/* Drains only a bounded number of Development trace records per service call. */
static void IR_ServiceProcessContinuousTrace(void)
{
#if IR_ENABLE_CONTINUOUS_SD_TRACE
    uint32_t budget;
    IR_ContextType context;
    IR_RuntimeRecord record;
    const IR_ContinuousTraceOps *ops = g_ir_runtime.config->continuous_trace;

    if ((ops == NULL) || (ops->is_available == NULL) ||
        (ops->write_record == NULL) || !ops->is_available())
    {
        return;
    }

    if (!g_ir_runtime.trace_started)
    {
        if ((ops->begin == NULL) ||
            (ops->begin((uint32_t)IR_BUILD_ID, (uint16_t)IR_SCHEMA_VERSION) != IR_OK))
        {
            IR_InternalSetHealth(IR_HEALTH_TRACE_DROPPED);
            return;
        }
        g_ir_runtime.trace_started = true;
    }

    for (budget = 0U; budget < IR_SERVICE_TRACE_BUDGET; ++budget)
    {
        if (!IR_InternalPopDevelopmentTrace(&context, &record))
        {
            break;
        }

        if (ops->write_record(context, &record) != IR_OK)
        {
            IR_InternalSaturatingIncrement(&g_ir_runtime.dev_trace_lost_count);
            IR_InternalSetHealth(IR_HEALTH_TRACE_DROPPED | IR_HEALTH_RECORD_DROPPED);
            break;
        }
    }
#else
    /* Release builds contain no continuous-trace queue or writer path. */
#endif
}

/* Resets the export state without touching the persistent evidence source. */
static void IR_ServiceResetExport(bool call_abort)
{
    const IR_ExportOps *ops = NULL;

    if ((g_ir_runtime.config != NULL) && (g_ir_runtime.config->export_ops != NULL))
    {
        ops = g_ir_runtime.config->export_ops;
    }

    if (call_abort && (ops != NULL) && (ops->abort != NULL))
    {
        ops->abort();
    }

    g_ir_runtime.export_state = 0U;
    g_ir_runtime.export_record_id = 0U;
    g_ir_runtime.export_payload_length = 0U;
    g_ir_runtime.export_offset = 0U;
}

/* Advances the on-demand persistent-evidence export by one bounded step. */
static void IR_ServiceProcessExport(void)
{
#if IR_ENABLE_EXPORT
    enum
    {
        IR_EXPORT_IDLE = 0,
        IR_EXPORT_STREAMING,
        IR_EXPORT_FINALIZING
    };

    uint8_t chunk[IR_EXPORT_CHUNK_BYTES];
    uint32_t remaining;
    uint32_t length;
    const IR_PersistenceExportOps *persistent;
    const IR_ExportOps *export_ops;

    if (!g_ir_runtime.export_requested)
    {
        return;
    }

    persistent = g_ir_runtime.config->persistent_export;
    export_ops = g_ir_runtime.config->export_ops;

    if ((persistent == NULL) || (export_ops == NULL) ||
        (persistent->has_pending == NULL) ||
        (persistent->read_meta == NULL) ||
        (persistent->read_payload == NULL) ||
        (persistent->mark_exported == NULL) ||
        (export_ops->is_available == NULL) ||
        (export_ops->begin == NULL) ||
        (export_ops->write == NULL) ||
        (export_ops->end == NULL))
    {
        IR_InternalSetHealth(IR_HEALTH_EXPORT_FAILED);
        return;
    }

    if (g_ir_runtime.export_state == IR_EXPORT_IDLE)
    {
        if (!persistent->has_pending() || !export_ops->is_available())
        {
            IR_InternalSetHealth(IR_HEALTH_EXPORT_FAILED);
            return;
        }

        if (persistent->read_meta(&g_ir_runtime.export_record_id,
                                  &g_ir_runtime.export_payload_length) != IR_OK)
        {
            IR_InternalSetHealth(IR_HEALTH_EXPORT_FAILED);
            return;
        }

        if (export_ops->begin(g_ir_runtime.export_record_id,
                              g_ir_runtime.export_payload_length,
                              (uint32_t)IR_BUILD_ID,
                              (uint16_t)IR_SCHEMA_VERSION) != IR_OK)
        {
            IR_InternalSetHealth(IR_HEALTH_EXPORT_FAILED);
            return;
        }

        g_ir_runtime.export_offset = 0U;
        g_ir_runtime.export_state = IR_EXPORT_STREAMING;
        return;
    }

    if (g_ir_runtime.export_state == IR_EXPORT_STREAMING)
    {
        if (!export_ops->is_available())
        {
            IR_InternalSetHealth(IR_HEALTH_EXPORT_FAILED);
            IR_ServiceResetExport(true);
            return;
        }

        if (g_ir_runtime.export_offset >= g_ir_runtime.export_payload_length)
        {
            g_ir_runtime.export_state = IR_EXPORT_FINALIZING;
            return;
        }

        remaining = g_ir_runtime.export_payload_length - g_ir_runtime.export_offset;
        length = (remaining > IR_EXPORT_CHUNK_BYTES) ? IR_EXPORT_CHUNK_BYTES : remaining;

        if ((persistent->read_payload(g_ir_runtime.export_record_id,
                                      g_ir_runtime.export_offset,
                                      chunk,
                                      length) != IR_OK) ||
            (export_ops->write(chunk, length) != IR_OK))
        {
            IR_InternalSetHealth(IR_HEALTH_EXPORT_FAILED);
            IR_ServiceResetExport(true);
            return;
        }

        g_ir_runtime.export_offset += length;
        return;
    }

    if (export_ops->end() != IR_OK)
    {
        IR_InternalSetHealth(IR_HEALTH_EXPORT_FAILED);
        IR_ServiceResetExport(true);
        return;
    }

    if ((export_ops->verify != NULL) && (export_ops->verify() != IR_OK))
    {
        IR_InternalSetHealth(IR_HEALTH_EXPORT_FAILED);
        IR_ServiceResetExport(true);
        return;
    }

    if (persistent->mark_exported(g_ir_runtime.export_record_id) != IR_OK)
    {
        IR_InternalSetHealth(IR_HEALTH_EXPORT_FAILED);
        IR_ServiceResetExport(false);
        return;
    }

    g_ir_runtime.persistent_export_pending = false;
    g_ir_runtime.export_requested = false;
    IR_ServiceResetExport(false);
#endif
}
