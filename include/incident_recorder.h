#ifndef INCIDENT_RECORDER_H
#define INCIDENT_RECORDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ir_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t IR_EpochId;
typedef uint32_t IR_IncidentId;
typedef uint32_t IR_Sequence;
typedef uint16_t IR_EventId;
typedef uint16_t IR_ObjectId;
typedef uint16_t IR_RuleId;
typedef uint16_t IR_OperationId;
typedef uint16_t IR_RecordType;
typedef uint32_t IR_HealthFlags;
typedef uint32_t IR_CriticalKey;

typedef enum
{
    IR_OK = 0,
    IR_NOT_AVAILABLE
} IR_Result;

typedef enum
{
    IR_CONTEXT_TASK = 0,
    IR_CONTEXT_ISR,
    IR_CONTEXT_FAULT,
    IR_CONTEXT_EARLY_BOOT,
    IR_CONTEXT_SERVICE,
    IR_CONTEXT_UNKNOWN
} IR_ContextType;

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

typedef struct
{
    uint16_t domain_id;
    uint16_t operation_id;
    uint32_t object_or_address;
    uint32_t arg0;
    uint32_t arg1;
    uint16_t result;
    uint16_t flags;
} IR_OperationContext;

typedef struct
{
    uint32_t (*get_timestamp)(void);
    uint32_t (*get_reset_cause_raw)(void);
    IR_ContextType (*get_context_type)(void);
    uint32_t (*get_context_id)(void);
    IR_CriticalKey (*enter_critical)(void);
    void (*exit_critical)(IR_CriticalKey key);
} IR_PlatformOps;

typedef struct
{
    bool (*is_available)(void);
    IR_Result (*begin)(uint32_t build_id, uint16_t schema_version);
    IR_Result (*write_record)(IR_ContextType context, const IR_RuntimeRecord *record);
    IR_Result (*end)(void);
} IR_ContinuousTraceOps;


typedef struct IR_PersistSource IR_PersistSource;

typedef struct
{
    IR_Result (*persist)(const IR_PersistSource *source);
} IR_PersistenceOps;

struct IR_PersistSource
{
    uint32_t build_id;
    uint16_t schema_version;
    uint16_t reserved0;
    uint32_t epoch_id;
    uint32_t incident_id;
    uint32_t reset_cause_raw;
    IR_HealthFlags health_flags;
    uint32_t task_record_count;
    uint32_t isr_record_count;
    bool first_abnormal_valid;
    bool fatal_snapshot_valid;
    uint16_t reserved1;
    IR_FirstAbnormalSnapshot first_abnormal;
    IR_FaultFrame fatal_snapshot;
    IR_OperationContext last_operation;
    void *reader_context;
    IR_Result (*read_task_record)(void *context, uint32_t logical_index, IR_RuntimeRecord *record);
    IR_Result (*read_isr_record)(void *context, uint32_t logical_index, IR_RuntimeRecord *record);
};

typedef struct
{
    bool (*has_pending)(void);
    IR_Result (*read_meta)(uint32_t *record_id, uint32_t *payload_length);
    IR_Result (*read_payload)(uint32_t record_id, uint32_t offset, void *dst, uint32_t len);
    IR_Result (*mark_exported)(uint32_t record_id);
} IR_PersistenceExportOps;

typedef struct
{
    bool (*is_available)(void);
    IR_Result (*begin)(uint32_t record_id, uint32_t payload_length, uint32_t build_id, uint16_t schema_version);
    IR_Result (*write)(const void *data, uint32_t len);
    IR_Result (*end)(void);
    IR_Result (*verify)(void);
    void (*abort)(void);
} IR_ExportOps;

typedef struct
{
    const IR_PlatformOps *platform;
    const IR_ContinuousTraceOps *continuous_trace;
    const IR_PersistenceOps *persistence;
    const IR_PersistenceExportOps *persistent_export;
    const IR_ExportOps *export_ops;
} IR_Config;

typedef struct
{
    IR_HealthFlags health_flags;
    uint32_t epoch_id;
    uint32_t incident_id;
    uint32_t reset_cause_raw;
    uint32_t task_record_count;
    uint32_t isr_record_count;
    uint32_t task_lost_count;
    uint32_t isr_lost_count;
    uint32_t development_trace_lost_count;
    bool first_abnormal_valid;
    bool fatal_snapshot_valid;
    bool previous_epoch_pending;
    bool export_pending;
    uint32_t build_profile;
} IR_Status;

/* Project integration point. A project supplies one static configuration object. */
const IR_Config *IR_ProjectConfig(void);

IR_Result IR_EarlyInit(void);
IR_Result IR_Init(void);
void IR_SystemStable(void);

void IR_EventTask(uint16_t record_type, uint16_t id, uint32_t value0, uint32_t value1);
void IR_EventIsr(uint16_t record_type, uint16_t id, uint32_t value0, uint32_t value1);

bool IR_FirstAbnormalTask(uint16_t object_id, uint16_t rule_id, uint32_t observed0, uint32_t observed1);
bool IR_FirstAbnormalIsr(uint16_t object_id, uint16_t rule_id, uint32_t observed0, uint32_t observed1);

void IR_FatalCapture(const IR_FaultFrame *fault);
IR_Result IR_GetStatus(IR_Status *status);


#if IR_ENABLE
#define IR_EVENT_TASK(type_, id_, v0_, v1_) \
    IR_EventTask((uint16_t)(type_), (uint16_t)(id_), (uint32_t)(v0_), (uint32_t)(v1_))
#define IR_EVENT_ISR(type_, id_, v0_, v1_) \
    IR_EventIsr((uint16_t)(type_), (uint16_t)(id_), (uint32_t)(v0_), (uint32_t)(v1_))
#define IR_FIRST_ABNORMAL_TASK(object_, rule_, v0_, v1_) \
    IR_FirstAbnormalTask((uint16_t)(object_), (uint16_t)(rule_), (uint32_t)(v0_), (uint32_t)(v1_))
#define IR_FIRST_ABNORMAL_ISR(object_, rule_, v0_, v1_) \
    IR_FirstAbnormalIsr((uint16_t)(object_), (uint16_t)(rule_), (uint32_t)(v0_), (uint32_t)(v1_))
#define IR_FATAL_CAPTURE(fault_) IR_FatalCapture((fault_))
#else
#define IR_EVENT_TASK(type_, id_, v0_, v1_) ((void)0)
#define IR_EVENT_ISR(type_, id_, v0_, v1_) ((void)0)
#define IR_FIRST_ABNORMAL_TASK(object_, rule_, v0_, v1_) (false)
#define IR_FIRST_ABNORMAL_ISR(object_, rule_, v0_, v1_) (false)
#define IR_FATAL_CAPTURE(fault_) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* INCIDENT_RECORDER_H */
