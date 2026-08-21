#include <stdbool.h>
#include <stdint.h>
#include "incident_recorder.h"

int main(void)
{
    uint32_t side_effect = 0U;
    bool claimed;

    IR_EVENT_TASK(IR_REC_OBSERVATION, 1U, ++side_effect, 0U);
    IR_EVENT_ISR(IR_REC_OBSERVATION, 2U, ++side_effect, 0U);
    claimed = IR_FIRST_ABNORMAL_TASK(1U, 1U, ++side_effect, 0U);
    claimed = claimed || IR_FIRST_ABNORMAL_ISR(1U, 1U, ++side_effect, 0U);
    IR_FATAL_CAPTURE((const IR_FaultFrame *)(uintptr_t)(++side_effect));

    return ((side_effect == 0U) && !claimed) ? 0 : 1;
}
