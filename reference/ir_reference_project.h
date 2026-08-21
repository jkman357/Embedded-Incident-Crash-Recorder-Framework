#ifndef IR_REFERENCE_PROJECT_H
#define IR_REFERENCE_PROJECT_H

#include <stdbool.h>
#include <stdint.h>

uint32_t IR_ReferencePersistedBytes(void);
uint32_t IR_ReferenceExportedBytes(void);
uint32_t IR_ReferenceContinuousTraceRecords(void);
bool IR_ReferenceExportMatchesPersistence(void);
bool IR_ReferenceHasPendingPersistentEvidence(void);

#endif /* IR_REFERENCE_PROJECT_H */
