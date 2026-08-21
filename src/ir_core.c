#include "ir_internal.h"

IR_RetainedStore g_ir_retained IR_RETAINED_ATTR;
IR_RuntimeState g_ir_runtime;

#if IR_ENABLE_CONTINUOUS_SD_TRACE
IR_TraceQueue g_ir_task_trace_queue;
IR_TraceQueue g_ir_isr_trace_queue;
#endif

static bool IR_InternalValidateRetainedHeader(void);
static bool IR_InternalValidateRingState(const IR_RingState *state, uint32_t capacity);
static void IR_InternalWriteRecord(IR_ContextType context,
                                   IR_RingState *state,
                                   IR_RuntimeRecord *ring,
                                   uint32_t capacity,
                                   uint16_t record_type,
                                   uint16_t id,
                                   uint32_t value0,
                                   uint32_t value1);
static bool IR_InternalFirstAbnormal(IR_ContextType context,
                                     uint16_t object_id,
                                     uint16_t rule_id,
                                     uint32_t observed0,
                                     uint32_t observed1);

/* Returns a timestamp only when the project adapter is available. */
uint32_t IR_InternalTimestamp(void)
{
    if ((g_ir_runtime.config == NULL) ||
        (g_ir_runtime.config->platform == NULL) ||
        (g_ir_runtime.config->platform->get_timestamp == NULL))
    {
        return 0U;
    }

    return g_ir_runtime.config->platform->get_timestamp();
}

/* Enters the project-defined short critical primitive used by recorder metadata. */
IR_CriticalKey IR_InternalEnterCritical(void)
{
    if ((g_ir_runtime.config == NULL) ||
        (g_ir_runtime.config->platform == NULL) ||
        (g_ir_runtime.config->platform->enter_critical == NULL))
    {
        return 0U;
    }

    return g_ir_runtime.config->platform->enter_critical();
}

/* Leaves the project-defined short critical primitive. */
void IR_InternalExitCritical(IR_CriticalKey key)
{
    if ((g_ir_runtime.config != NULL) &&
        (g_ir_runtime.config->platform != NULL) &&
        (g_ir_runtime.config->platform->exit_critical != NULL))
    {
        g_ir_runtime.config->platform->exit_critical(key);
    }
}

/* Sets recorder health without changing application behavior. */
void IR_InternalSetHealth(IR_HealthFlags flags)
{
    IR_CriticalKey key = IR_InternalEnterCritical();

    g_ir_runtime.transient_health |= flags;
    if (g_ir_runtime.initialized || g_ir_runtime.retained_valid)
    {
        g_ir_retained.header.health_flags |= flags;
    }

    IR_InternalExitCritical(key);
}

/* Increments a diagnostic counter and saturates instead of silently wrapping. */
void IR_InternalSaturatingIncrement(uint32_t *value)
{
    if ((value != NULL) && (*value != UINT32_MAX))
    {
        ++(*value);
    }
}

/* Starts an empty recorder epoch after previous evidence is no longer at risk. */
void IR_InternalBeginFreshEpoch(uint32_t previous_epoch)
{
    uint32_t reset_cause = 0U;

    if ((g_ir_runtime.config != NULL) &&
        (g_ir_runtime.config->platform != NULL) &&
        (g_ir_runtime.config->platform->get_reset_cause_raw != NULL))
    {
        reset_cause = g_ir_runtime.config->platform->get_reset_cause_raw();
    }

    memset(&g_ir_retained, 0, sizeof(g_ir_retained));

    g_ir_retained.header.magic = IR_RETAINED_MAGIC;
    g_ir_retained.header.schema_version = (uint16_t)IR_SCHEMA_VERSION;
    g_ir_retained.header.header_size = (uint16_t)sizeof(IR_RetainedHeader);
    g_ir_retained.header.build_id = (uint32_t)IR_BUILD_ID;
    g_ir_retained.header.epoch_id = previous_epoch + 1U;
    g_ir_retained.header.first_abnormal_state = IR_STATE_FIRST_EMPTY;
    g_ir_retained.header.reset_cause_raw = reset_cause;
    g_ir_retained.header.health_flags = g_ir_runtime.transient_health;

    g_ir_runtime.retained_valid = true;
    g_ir_runtime.previous_epoch_pending = false;
    g_ir_runtime.capture_enabled = true;
}

/* Validates only fixed retained metadata and bounded ring indices. */
static bool IR_InternalValidateRetainedHeader(void)
{
    if (g_ir_retained.header.magic != IR_RETAINED_MAGIC)
    {
        return false;
    }

    if (g_ir_retained.header.schema_version != (uint16_t)IR_SCHEMA_VERSION)
    {
        g_ir_runtime.transient_health |= IR_HEALTH_SCHEMA_INVALID;
        return false;
    }

    if (g_ir_retained.header.header_size != (uint16_t)sizeof(IR_RetainedHeader))
    {
        return false;
    }

    if (!IR_InternalValidateRingState(&g_ir_retained.task_state, IR_TASK_RECORD_COUNT) ||
        !IR_InternalValidateRingState(&g_ir_retained.isr_state, IR_ISR_RECORD_COUNT))
    {
        g_ir_runtime.transient_health |= IR_HEALTH_INDEX_INVALID;
        return false;
    }

    return true;
}

/* Checks recorder-owned ring metadata before it can index recorder memory. */
static bool IR_InternalValidateRingState(const IR_RingState *state, uint32_t capacity)
{
    if ((state == NULL) || (capacity == 0U))
    {
        return false;
    }

    if (state->write_index >= capacity)
    {
        return false;
    }

    if (state->record_count > capacity)
    {
        return false;
    }

    return true;
}

/* Performs bounded previous-epoch containment without external I/O. */
IR_Result IR_EarlyInit(void)
{
#if !IR_ENABLE
    return IR_NOT_AVAILABLE;
#else
    bool has_previous_evidence;

    memset(&g_ir_runtime, 0, sizeof(g_ir_runtime));
    g_ir_runtime.config = IR_ProjectConfig();
    g_ir_runtime.early_called = true;

    if ((g_ir_runtime.config == NULL) || (g_ir_runtime.config->platform == NULL))
    {
        return IR_NOT_AVAILABLE;
    }

    g_ir_runtime.retained_valid = IR_InternalValidateRetainedHeader();
    if (!g_ir_runtime.retained_valid)
    {
        g_ir_runtime.transient_health |= IR_HEALTH_RETENTION_INVALID;
        return IR_OK;
    }

    if (g_ir_retained.header.first_abnormal_state == IR_STATE_FIRST_WRITING)
    {
        g_ir_runtime.transient_health |= IR_HEALTH_TORN_RECORD_FOUND;
    }

    has_previous_evidence =
        ((((g_ir_retained.header.first_abnormal_state == IR_STATE_FIRST_VALID) ||
           ((g_ir_retained.header.state_flags & IR_STATE_FATAL_VALID) != 0U)) &&
          ((g_ir_retained.header.state_flags & IR_STATE_PERSISTED) == 0U)) ||
         ((g_ir_retained.header.state_flags & IR_STATE_PERSIST_REQUESTED) != 0U));

    g_ir_runtime.previous_epoch_pending = has_previous_evidence;
    g_ir_runtime.capture_enabled = !has_previous_evidence;
    return IR_OK;
#endif
}

/* Initializes runtime capture while preserving unarchived previous-epoch evidence. */
IR_Result IR_Init(void)
{
#if !IR_ENABLE
    return IR_NOT_AVAILABLE;
#else
    uint32_t previous_epoch = 0U;
    const IR_PlatformOps *platform;

    if (!g_ir_runtime.early_called)
    {
        (void)IR_EarlyInit();
    }

    if ((g_ir_runtime.config == NULL) || (g_ir_runtime.config->platform == NULL))
    {
        return IR_NOT_AVAILABLE;
    }

    platform = g_ir_runtime.config->platform;
    if ((platform->get_timestamp == NULL) ||
        (platform->enter_critical == NULL) ||
        (platform->exit_critical == NULL))
    {
        g_ir_runtime.transient_health |= IR_HEALTH_DEGRADED;
        return IR_NOT_AVAILABLE;
    }

    if (g_ir_runtime.retained_valid)
    {
        previous_epoch = g_ir_retained.header.epoch_id;
    }

    g_ir_runtime.initialized = true;

    if (!g_ir_runtime.previous_epoch_pending)
    {
        IR_InternalBeginFreshEpoch(previous_epoch);
    }

    return IR_OK;
#endif
}

/* Marks the current epoch as having reached the project-defined stable point. */
void IR_SystemStable(void)
{
#if IR_ENABLE
    IR_CriticalKey key;

    if (!g_ir_runtime.initialized || !g_ir_runtime.capture_enabled)
    {
        return;
    }

    key = IR_InternalEnterCritical();
    g_ir_retained.header.state_flags |= IR_STATE_SYSTEM_STABLE;
    IR_InternalExitCritical(key);
#endif
}

/* Records a Task-context event into the dedicated Task ring. */
void IR_EventTask(uint16_t record_type, uint16_t id, uint32_t value0, uint32_t value1)
{
#if IR_ENABLE
    IR_InternalWriteRecord(IR_CONTEXT_TASK,
                           &g_ir_retained.task_state,
                           g_ir_retained.task_ring,
                           IR_TASK_RECORD_COUNT,
                           record_type,
                           id,
                           value0,
                           value1);
#else
    (void)record_type;
    (void)id;
    (void)value0;
    (void)value1;
#endif
}

/* Records an ISR-context event into the dedicated ISR ring. */
void IR_EventIsr(uint16_t record_type, uint16_t id, uint32_t value0, uint32_t value1)
{
#if IR_ENABLE
    IR_InternalWriteRecord(IR_CONTEXT_ISR,
                           &g_ir_retained.isr_state,
                           g_ir_retained.isr_ring,
                           IR_ISR_RECORD_COUNT,
                           record_type,
                           id,
                           value0,
                           value1);
#else
    (void)record_type;
    (void)id;
    (void)value0;
    (void)value1;
#endif
}

/* Writes one fixed-size runtime record with bounded metadata validation. */
static void IR_InternalWriteRecord(IR_ContextType context,
                                   IR_RingState *state,
                                   IR_RuntimeRecord *ring,
                                   uint32_t capacity,
                                   uint16_t record_type,
                                   uint16_t id,
                                   uint32_t value0,
                                   uint32_t value1)
{
    IR_CriticalKey key;
    IR_RuntimeRecord record;
    uint32_t index;

    if (!g_ir_runtime.initialized || !g_ir_runtime.capture_enabled)
    {
        return;
    }

    key = IR_InternalEnterCritical();

    if (!g_ir_runtime.capture_enabled)
    {
        IR_InternalExitCritical(key);
        return;
    }

    if (!IR_InternalValidateRingState(state, capacity))
    {
        IR_InternalSaturatingIncrement(&state->lost_count);
        IR_InternalExitCritical(key);
        IR_InternalSetHealth(IR_HEALTH_INDEX_INVALID | IR_HEALTH_RECORD_DROPPED | IR_HEALTH_DEGRADED);
        return;
    }

    index = state->write_index;
    record.sequence = state->next_sequence++;
    record.timestamp = IR_InternalTimestamp();
    record.type = record_type;
    record.id = id;
    record.object_id = 0U;
    record.flags = (uint16_t)context;
    record.value0 = value0;
    record.value1 = value1;

    ring[index] = record;

    if (record_type == IR_REC_OPERATION)
    {
        g_ir_retained.last_operation.domain_id = 0U;
        g_ir_retained.last_operation.operation_id = id;
        g_ir_retained.last_operation.object_or_address = 0U;
        g_ir_retained.last_operation.arg0 = value0;
        g_ir_retained.last_operation.arg1 = value1;
        g_ir_retained.last_operation.result = 0U;
        g_ir_retained.last_operation.flags = (uint16_t)context;
    }

    ++index;
    if (index >= capacity)
    {
        index = 0U;
    }
    state->write_index = index;

    if (state->record_count < capacity)
    {
        ++state->record_count;
    }

    IR_InternalExitCritical(key);

    IR_InternalQueueDevelopmentTrace(context, &record);
}

/* Attempts to publish the first abnormal Task-context evidence once per epoch. */
bool IR_FirstAbnormalTask(uint16_t object_id, uint16_t rule_id, uint32_t observed0, uint32_t observed1)
{
#if IR_ENABLE
    return IR_InternalFirstAbnormal(IR_CONTEXT_TASK, object_id, rule_id, observed0, observed1);
#else
    (void)object_id;
    (void)rule_id;
    (void)observed0;
    (void)observed1;
    return false;
#endif
}

/* Attempts to publish the first abnormal ISR-context evidence once per epoch. */
bool IR_FirstAbnormalIsr(uint16_t object_id, uint16_t rule_id, uint32_t observed0, uint32_t observed1)
{
#if IR_ENABLE
    return IR_InternalFirstAbnormal(IR_CONTEXT_ISR, object_id, rule_id, observed0, observed1);
#else
    (void)object_id;
    (void)rule_id;
    (void)observed0;
    (void)observed1;
    return false;
#endif
}

/* Claims, fills, and publishes the one-shot first-abnormal snapshot. */
static bool IR_InternalFirstAbnormal(IR_ContextType context,
                                     uint16_t object_id,
                                     uint16_t rule_id,
                                     uint32_t observed0,
                                     uint32_t observed1)
{
    IR_CriticalKey key;
    uint32_t sequence;
    uint32_t timestamp;
    IR_OperationContext operation;

    if (!g_ir_runtime.initialized || !g_ir_runtime.retained_valid ||
        g_ir_runtime.previous_epoch_pending)
    {
        return false;
    }

    key = IR_InternalEnterCritical();
    if (g_ir_retained.header.first_abnormal_state != IR_STATE_FIRST_EMPTY)
    {
        IR_InternalExitCritical(key);
        return false;
    }

    g_ir_retained.header.first_abnormal_state = IR_STATE_FIRST_WRITING;
    ++g_ir_retained.header.incident_id;
    sequence = (context == IR_CONTEXT_ISR)
                   ? g_ir_retained.isr_state.next_sequence
                   : g_ir_retained.task_state.next_sequence;
    operation = g_ir_retained.last_operation;
    IR_InternalExitCritical(key);

    timestamp = IR_InternalTimestamp();

    g_ir_retained.first_abnormal.magic = IR_FIRST_ABNORMAL_MAGIC;
    g_ir_retained.first_abnormal.epoch_id = g_ir_retained.header.epoch_id;
    g_ir_retained.first_abnormal.sequence = sequence;
    g_ir_retained.first_abnormal.timestamp = timestamp;
    g_ir_retained.first_abnormal.object_id = object_id;
    g_ir_retained.first_abnormal.rule_id = rule_id;
    g_ir_retained.first_abnormal.context_type = (uint16_t)context;
    g_ir_retained.first_abnormal.flags = 0U;
    g_ir_retained.first_abnormal.observed0 = observed0;
    g_ir_retained.first_abnormal.observed1 = observed1;
    g_ir_retained.first_abnormal.last_operation_id = operation.operation_id;
    g_ir_retained.first_abnormal.last_operation_arg0 = operation.arg0;
    g_ir_retained.first_abnormal.last_operation_arg1 = operation.arg1;
    g_ir_retained.first_abnormal.checksum_or_commit = IR_FIRST_ABNORMAL_COMMIT;

    key = IR_InternalEnterCritical();
    g_ir_retained.header.first_abnormal_state = IR_STATE_FIRST_VALID;
    g_ir_retained.header.state_flags &= ~IR_STATE_PERSISTED;
    g_ir_retained.header.state_flags |= IR_STATE_PERSIST_REQUESTED;
    IR_InternalExitCritical(key);

    return true;
}

/* Captures a fixed fatal snapshot without competing for first-abnormal ownership. */
void IR_FatalCapture(const IR_FaultFrame *fault)
{
#if IR_ENABLE
    if ((fault == NULL) || !g_ir_runtime.initialized || !g_ir_runtime.retained_valid ||
        g_ir_runtime.previous_epoch_pending)
    {
        return;
    }

    /*
     * Fatal capture intentionally avoids Task/service locks. The fixed snapshot
     * is published before the state bits that advertise its validity.
     */
    g_ir_retained.fatal_snapshot = *fault;
    g_ir_retained.header.state_flags &= ~IR_STATE_PERSISTED;
    g_ir_retained.header.state_flags |= (IR_STATE_FATAL_VALID | IR_STATE_PERSIST_REQUESTED);
#else
    (void)fault;
#endif
}

/* Returns a bounded aggregate status snapshot for service/UI diagnostics. */
IR_Result IR_GetStatus(IR_Status *status)
{
#if !IR_ENABLE
    (void)status;
    return IR_NOT_AVAILABLE;
#else
    IR_CriticalKey key;

    if ((status == NULL) || !g_ir_runtime.initialized)
    {
        return IR_NOT_AVAILABLE;
    }

    key = IR_InternalEnterCritical();
    status->health_flags = g_ir_retained.header.health_flags | g_ir_runtime.transient_health;
    status->epoch_id = g_ir_retained.header.epoch_id;
    status->incident_id = g_ir_retained.header.incident_id;
    status->reset_cause_raw = g_ir_retained.header.reset_cause_raw;
    status->task_record_count = g_ir_retained.task_state.record_count;
    status->isr_record_count = g_ir_retained.isr_state.record_count;
    status->task_lost_count = g_ir_retained.task_state.lost_count;
    status->isr_lost_count = g_ir_retained.isr_state.lost_count;
    status->development_trace_lost_count = g_ir_runtime.dev_trace_lost_count;
    status->first_abnormal_valid =
        (g_ir_retained.header.first_abnormal_state == IR_STATE_FIRST_VALID) &&
        (g_ir_retained.first_abnormal.checksum_or_commit == IR_FIRST_ABNORMAL_COMMIT);
    status->fatal_snapshot_valid =
        ((g_ir_retained.header.state_flags & IR_STATE_FATAL_VALID) != 0U);
    status->previous_epoch_pending = g_ir_runtime.previous_epoch_pending;
    status->export_pending = g_ir_runtime.export_requested ||
                             g_ir_runtime.persistent_export_pending;
    status->build_profile = IR_BUILD_PROFILE;
    IR_InternalExitCritical(key);

    return IR_OK;
#endif
}
