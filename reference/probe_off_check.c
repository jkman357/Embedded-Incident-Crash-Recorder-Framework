#include <stdint.h>
#include "incident_recorder.h"

int main(void)
{
    uint32_t side_effect = 0U;

    IR_EVENT_TASK(IR_REC_OBSERVATION, 1U, ++side_effect, 0U);
    IR_EVENT_ISR(IR_REC_OBSERVATION, 2U, ++side_effect, 0U);

    return (side_effect == 0U) ? 0 : 1;
}
