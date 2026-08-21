#include <stdio.h>
#include "ir_internal.h"

int main(void)
{
    printf("IR_RETAINED_RAM_BYTES=%u\n", (unsigned)IR_RETAINED_RAM_BYTES);
    printf("sizeof(IR_RuntimeRecord)=%u\n", (unsigned)sizeof(IR_RuntimeRecord));
    printf("sizeof(IR_RetainedHeader)=%u\n", (unsigned)sizeof(IR_RetainedHeader));
    printf("sizeof(IR_FirstAbnormalSnapshot)=%u\n", (unsigned)sizeof(IR_FirstAbnormalSnapshot));
    printf("sizeof(IR_FaultFrame)=%u\n", (unsigned)sizeof(IR_FaultFrame));
    printf("sizeof(IR_OperationContext)=%u\n", (unsigned)sizeof(IR_OperationContext));
    printf("IR_TASK_RECORD_COUNT=%u\n", (unsigned)IR_TASK_RECORD_COUNT);
    printf("IR_ISR_RECORD_COUNT=%u\n", (unsigned)IR_ISR_RECORD_COUNT);
    printf("IR_TOTAL_TIMELINE_RECORD_COUNT=%u\n", (unsigned)IR_TOTAL_TIMELINE_RECORD_COUNT);
    printf("sizeof(IR_RetainedStore)=%u\n", (unsigned)sizeof(IR_RetainedStore));
    printf("retained_slack_bytes=%u\n",
           (unsigned)(IR_RETAINED_RAM_BYTES - sizeof(IR_RetainedStore)));
    return 0;
}
