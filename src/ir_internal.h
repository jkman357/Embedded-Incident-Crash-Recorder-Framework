#ifndef IR_INTERNAL_H
#define IR_INTERNAL_H

#include <limits.h>
#include <string.h>

#include "incident_recorder.h"
#include "incident_recorder_service.h"

#define IR_RETAINED_MAGIC               (0x49525233UL) /* "IRR3" */
#define IR_FIRST_ABNORMAL_MAGIC          (0x4641424EUL) /* "FABN" */
#define IR_FIRST_ABNORMAL_SENTINEL       (0x53454E54UL) /* "SENT" */
#define IR_STATE_FIRST_EMPTY             (0UL)
#define IR_STATE_FIRST_WRITING           (1UL)
#define IR_STATE_FIRST_VALID             (2UL)
#define IR_STATE_FATAL_EMPTY             (0UL)
#define IR_STATE_FATAL_WRITING           (1UL)
#define IR_STATE_FATAL_VALID             (2UL)
#define IR_STATE_SYSTEM_STABLE           (1UL << 0)
#define IR_STATE_PERSIST_REQUESTED       (1UL << 1)
#define IR_STATE_PERSISTED               (1UL << 2)

#define IR_TASK_RING_NUMERATOR           (3U)
#define IR_RING_DENOMINATOR              (4U)

#if defined(__GNUC__)
#define IR_RETAINED_ATTR __attribute__((section(".incident_ram"), used, aligned(8)))
#else
#define IR_RETAINED_ATTR
#endif

typedef struct
{
    uint32_t write_index;
    uint32_t record_count;
    uint32_t lost_count;
    uint32_t next_sequence;
} IR_RingState;

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
    volatile uint32_t first_abnormal_state;
    volatile uint32_t fatal_state;
    volatile uint32_t fatal_publish_sequence;
    uint32_t fatal_persisted_sequence;
    uint32_t reset_cause_raw;
} IR_RetainedHeader;

#define IR_RETAINED_FIXED_BYTES \
    (sizeof(IR_RetainedHeader) + \
     sizeof(IR_FirstAbnormalSnapshot) + \
     sizeof(IR_FaultFrame) + \
     sizeof(IR_OperationContext) + \
     (2U * sizeof(IR_RingState)) + \
     IR_RETAINED_RESERVED_BYTES)

#define IR_TIMELINE_BYTES \
    (IR_RETAINED_RAM_BYTES - IR_RETAINED_FIXED_BYTES)

#define IR_TOTAL_TIMELINE_RECORD_COUNT \
    (IR_TIMELINE_BYTES / sizeof(IR_RuntimeRecord))

#define IR_TASK_RECORD_COUNT \
    ((IR_TOTAL_TIMELINE_RECORD_COUNT * IR_TASK_RING_NUMERATOR) / IR_RING_DENOMINATOR)

#define IR_ISR_RECORD_COUNT \
    (IR_TOTAL_TIMELINE_RECORD_COUNT - IR_TASK_RECORD_COUNT)

typedef struct
{
    IR_RetainedHeader header;
    IR_FirstAbnormalSnapshot first_abnormal;
    IR_FaultFrame fatal_snapshot;
    IR_OperationContext last_operation;
    IR_RingState task_state;
    IR_RingState isr_state;
    IR_RuntimeRecord task_ring[IR_TASK_RECORD_COUNT];
    IR_RuntimeRecord isr_ring[IR_ISR_RECORD_COUNT];
    uint8_t reserved[IR_RETAINED_RESERVED_BYTES];
} IR_RetainedStore;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(IR_RuntimeRecord) == 24U, "IR_RuntimeRecord must remain 24 bytes");
_Static_assert(IR_RETAINED_RAM_BYTES > IR_RETAINED_FIXED_BYTES,
               "Retained RAM budget is smaller than fixed recorder metadata");
_Static_assert(IR_TOTAL_TIMELINE_RECORD_COUNT > 0U, "Retained RAM budget leaves no timeline capacity");
_Static_assert(sizeof(IR_RetainedStore) <= IR_RETAINED_RAM_BYTES,
               "Incident Recorder retained RAM exceeds configured budget");
#else
typedef char IR_StaticAssertRuntimeRecord24[(sizeof(IR_RuntimeRecord) == 24U) ? 1 : -1];
typedef char IR_StaticAssertFixedBudget[(IR_RETAINED_RAM_BYTES > IR_RETAINED_FIXED_BYTES) ? 1 : -1];
typedef char IR_StaticAssertTimelineCapacity[(IR_TOTAL_TIMELINE_RECORD_COUNT > 0U) ? 1 : -1];
typedef char IR_StaticAssertRetainedBudget[(sizeof(IR_RetainedStore) <= IR_RETAINED_RAM_BYTES) ? 1 : -1];
#endif

typedef struct
{
    bool early_called;
    bool initialized;
    bool retained_valid;
    bool previous_epoch_pending;
    volatile bool capture_enabled;
    volatile bool persistence_capture_paused;
    bool export_requested;
    bool trace_started;
    bool persistent_export_pending;
    uint8_t export_state;
    uint8_t reserved0;
    uint16_t reserved1;
    uint32_t export_retry_countdown;
    uint32_t persistence_retry_countdown;
    IR_HealthFlags transient_health;
    uint32_t dev_trace_lost_count;
    uint32_t export_record_id;
    uint32_t export_payload_length;
    uint32_t export_offset;
    const IR_Config *config;
} IR_RuntimeState;

#if IR_ENABLE_CONTINUOUS_SD_TRACE
typedef struct
{
    IR_RuntimeRecord record[IR_DEV_TRACE_QUEUE_RECORDS];
    uint32_t read_index;
    uint32_t write_index;
    uint32_t count;
} IR_TraceQueue;
#endif

#if IR_ENABLE
extern IR_RetainedStore g_ir_retained;
extern IR_RuntimeState g_ir_runtime;

#if IR_ENABLE_CONTINUOUS_SD_TRACE
extern IR_TraceQueue g_ir_task_trace_queue;
extern IR_TraceQueue g_ir_isr_trace_queue;
#endif

uint32_t IR_InternalTimestamp(void);
IR_CriticalKey IR_InternalEnterCritical(void);
void IR_InternalExitCritical(IR_CriticalKey key);
void IR_InternalPublishBarrier(void);
void IR_InternalSetHealth(IR_HealthFlags flags);
void IR_InternalSaturatingIncrement(uint32_t *value);
void IR_InternalBeginFreshEpoch(uint32_t previous_epoch);
void IR_InternalQueueDevelopmentTrace(IR_ContextType context, const IR_RuntimeRecord *record);
bool IR_InternalPopDevelopmentTrace(IR_ContextType *context, IR_RuntimeRecord *record);
#endif

#endif /* IR_INTERNAL_H */
