#include "ir_internal.h"

#if IR_ENABLE

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
static void IR_InternalCountPausedLoss(IR_RingState *state);

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

/* Enforces the project/toolchain publication barrier before a valid marker is published. */
void IR_InternalPublishBarrier(void)
{
    if ((g_ir_runtime.config != NULL) &&
        (g_ir_runtime.config->platform != NULL) &&
        (g_ir_runtime.config->platform->publish_barrier != NULL))
    {
        g_ir_runtime.config->platform->publish_barrier();
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
    g_ir_retained.header.fatal_state = IR_STATE_FATAL_EMPTY;
    g_ir_retained.header.reset_cause_raw = reset_cause;
    g_ir_retained.header.health_flags = g_ir_runtime.transient_health;

    g_ir_runtime.retained_valid = true;
    g_ir_runtime.previous_epoch_pending = false;
    g_ir_runtime.capture_enabled = true;
    g_ir_runtime.persistence_capture_paused = false;
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

    if ((g_ir_retained.header.first_abnormal_state > IR_STATE_FIRST_VALID) ||
        (g_ir_retained.header.fatal_state > IR_STATE_FATAL_VALID))
    {
        g_ir_runtime.transient_health |= IR_HEALTH_TORN_RECORD_FOUND;
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
    bool has_previous_evidence;
    bool fatal_unpersisted;

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

    if ((g_ir_retained.header.first_abnormal_state == IR_STATE_FIRST_WRITING) ||
        (g_ir_retained.header.fatal_state == IR_STATE_FATAL_WRITING))
    {
        g_ir_runtime.transient_health |= IR_HEALTH_TORN_RECORD_FOUND;
    }

    fatal_unpersisted =
        (g_ir_retained.header.fatal_state == IR_STATE_FATAL_VALID) &&
        (g_ir_retained.header.fatal_publish_sequence !=
         g_ir_retained.header.fatal_persisted_sequence);

    has_previous_evidence =
        (g_ir_retained.header.first_abnormal_state == IR_STATE_FIRST_WRITING) ||
        (g_ir_retained.header.fatal_state == IR_STATE_FATAL_WRITING) ||
        (((g_ir_retained.header.first_abnormal_state == IR_STATE_FIRST_VALID) &&
          ((g_ir_retained.header.state_flags & IR_STATE_PERSISTED) == 0U)) ||
         ((g_ir_retained.header.state_flags & IR_STATE_PERSIST_REQUESTED) != 0U) ||
         fatal_unpersisted);

    g_ir_runtime.previous_epoch_pending = has_previous_evidence;
    g_ir_runtime.capture_enabled = !has_previous_evidence;
    return IR_OK;
}

/* Initializes runtime capture while preserving unarchived previous-epoch evidence. */
IR_Result IR_Init(void)
{
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
        (platform->exit_critical == NULL) ||
        (platform->try_claim_u32 == NULL) ||
        (platform->publish_barrier == NULL))
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
}

/* Marks the current epoch as having reached the project-defined stable point. */
void IR_SystemStable(void)
{
    IR_CriticalKey key;

    if (!g_ir_runtime.initialized || !g_ir_runtime.capture_enabled)
    {
        return;
    }

    key = IR_InternalEnterCritical();
    g_ir_retained.header.state_flags |= IR_STATE_SYSTEM_STABLE;
    IR_InternalExitCritical(key);
}

/* Records a Task-context event into the dedicated Task ring. */
void IR_EventTask(uint16_t record_type, uint16_t id, uint32_t value0, uint32_t value1)
{
    IR_InternalWriteRecord(IR_CONTEXT_TASK,
                           &g_ir_retained.task_state,
                           g_ir_retained.task_ring,
                           IR_TASK_RECORD_COUNT,
                           record_type,
                           id,
                           value0,
                           value1);
}

/* Records an ISR-context event into the dedicated ISR ring. */
void IR_EventIsr(uint16_t record_type, uint16_t id, uint32_t value0, uint32_t value1)
{
    IR_InternalWriteRecord(IR_CONTEXT_ISR,
                           &g_ir_retained.isr_state,
                           g_ir_retained.isr_ring,
                           IR_ISR_RECORD_COUNT,
                           record_type,
                           id,
                           value0,
                           value1);
}

/* Counts one event intentionally omitted only because persistence paused capture. */
static void IR_InternalCountPausedLoss(IR_RingState *state)
{
    IR_CriticalKey key;

    if (state == NULL)
    {
        return;
    }

    key = IR_InternalEnterCritical();
    if (g_ir_runtime.persistence_capture_paused)
    {
        IR_InternalSaturatingIncrement(&state->lost_count);
        g_ir_runtime.transient_health |= IR_HEALTH_RECORD_DROPPED;
        g_ir_retained.header.health_flags |= IR_HEALTH_RECORD_DROPPED;
    }
    IR_InternalExitCritical(key);
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

    if (!g_ir_runtime.initialized)
    {
        return;
    }

    if (!g_ir_runtime.capture_enabled)
    {
        IR_InternalCountPausedLoss(state);
        return;
    }

    key = IR_InternalEnterCritical();

    if (!g_ir_runtime.capture_enabled)
    {
        if (g_ir_runtime.persistence_capture_paused)
        {
            IR_InternalSaturatingIncrement(&state->lost_count);
            g_ir_runtime.transient_health |= IR_HEALTH_RECORD_DROPPED;
            g_ir_retained.header.health_flags |= IR_HEALTH_RECORD_DROPPED;
        }
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
    return IR_InternalFirstAbnormal(IR_CONTEXT_TASK, object_id, rule_id, observed0, observed1);
}

/* Attempts to publish the first abnormal ISR-context evidence once per epoch. */
bool IR_FirstAbnormalIsr(uint16_t object_id, uint16_t rule_id, uint32_t observed0, uint32_t observed1)
{
    return IR_InternalFirstAbnormal(IR_CONTEXT_ISR, object_id, rule_id, observed0, observed1);
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
    uint32_t epoch_id;
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
    epoch_id = g_ir_retained.header.epoch_id;
    sequence = (context == IR_CONTEXT_ISR)
                   ? g_ir_retained.isr_state.next_sequence
                   : g_ir_retained.task_state.next_sequence;
    operation = g_ir_retained.last_operation;
    IR_InternalExitCritical(key);

    timestamp = IR_InternalTimestamp();

    g_ir_retained.first_abnormal.magic = IR_FIRST_ABNORMAL_MAGIC;
    g_ir_retained.first_abnormal.epoch_id = epoch_id;
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
    g_ir_retained.first_abnormal.integrity_sentinel = IR_FIRST_ABNORMAL_SENTINEL;

    /* first_abnormal_state == VALID is the sole publication marker. */
    IR_InternalPublishBarrier();
    key = IR_InternalEnterCritical();
    g_ir_retained.header.first_abnormal_state = IR_STATE_FIRST_VALID;
    g_ir_retained.header.state_flags &= ~IR_STATE_PERSISTED;
    g_ir_retained.header.state_flags |= IR_STATE_PERSIST_REQUESTED;
    IR_InternalExitCritical(key);

    return true;
}

/* Captures the first fatal snapshot using a dedicated atomic-claim path. */
void IR_FatalCapture(const IR_FaultFrame *fault)
{
    const IR_PlatformOps *platform;
    uint32_t publish_sequence;

    if ((fault == NULL) || !g_ir_runtime.initialized || !g_ir_runtime.retained_valid ||
        g_ir_runtime.previous_epoch_pending || (g_ir_runtime.config == NULL) ||
        (g_ir_runtime.config->platform == NULL))
    {
        return;
    }

    platform = g_ir_runtime.config->platform;
    if ((platform->try_claim_u32 == NULL) || (platform->publish_barrier == NULL))
    {
        return;
    }

    if (!platform->try_claim_u32(&g_ir_retained.header.fatal_state,
                                 IR_STATE_FATAL_EMPTY,
                                 IR_STATE_FATAL_WRITING))
    {
        return;
    }

    publish_sequence = g_ir_retained.header.fatal_publish_sequence + 1U;
    g_ir_retained.fatal_snapshot = *fault;
    g_ir_retained.header.fatal_publish_sequence = publish_sequence;

    /* fatal_state == VALID is the sole fatal publication marker. */
    IR_InternalPublishBarrier();
    g_ir_retained.header.fatal_state = IR_STATE_FATAL_VALID;
}

/* Returns a bounded aggregate status snapshot for service/UI diagnostics. */
IR_Result IR_GetStatus(IR_Status *status)
{
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
        (g_ir_retained.first_abnormal.integrity_sentinel == IR_FIRST_ABNORMAL_SENTINEL);
    status->fatal_snapshot_valid =
        (g_ir_retained.header.fatal_state == IR_STATE_FATAL_VALID);
    status->previous_epoch_pending = g_ir_runtime.previous_epoch_pending;
    status->export_pending = g_ir_runtime.export_requested ||
                             g_ir_runtime.persistent_export_pending;
    status->build_profile = IR_BUILD_PROFILE;
    IR_InternalExitCritical(key);

    return IR_OK;
}

#else /* IR_ENABLE == 0 */

IR_Result IR_EarlyInit(void)
{
    return IR_NOT_AVAILABLE;
}

IR_Result IR_Init(void)
{
    return IR_NOT_AVAILABLE;
}

void IR_SystemStable(void)
{
}

void IR_EventTask(uint16_t record_type, uint16_t id, uint32_t value0, uint32_t value1)
{
    (void)record_type;
    (void)id;
    (void)value0;
    (void)value1;
}

void IR_EventIsr(uint16_t record_type, uint16_t id, uint32_t value0, uint32_t value1)
{
    (void)record_type;
    (void)id;
    (void)value0;
    (void)value1;
}

bool IR_FirstAbnormalTask(uint16_t object_id, uint16_t rule_id, uint32_t observed0, uint32_t observed1)
{
    (void)object_id;
    (void)rule_id;
    (void)observed0;
    (void)observed1;
    return false;
}

bool IR_FirstAbnormalIsr(uint16_t object_id, uint16_t rule_id, uint32_t observed0, uint32_t observed1)
{
    (void)object_id;
    (void)rule_id;
    (void)observed0;
    (void)observed1;
    return false;
}

void IR_FatalCapture(const IR_FaultFrame *fault)
{
    (void)fault;
}

IR_Result IR_GetStatus(IR_Status *status)
{
    (void)status;
    return IR_NOT_AVAILABLE;
}

#endif /* IR_ENABLE */
