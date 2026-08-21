#ifndef IR_REFERENCE_PROJECT_H
#define IR_REFERENCE_PROJECT_H

#include <stdbool.h>
#include <stdint.h>

uint32_t IR_ReferencePersistedBytes(void);
uint32_t IR_ReferenceExportedBytes(void);
uint32_t IR_ReferenceContinuousTraceRecords(void);
bool IR_ReferenceExportMatchesPersistence(void);
bool IR_ReferenceHasPendingPersistentEvidence(void);
uint32_t IR_ReferencePendingPersistentCount(void);
uint32_t IR_ReferenceLastPersistedRecordId(void);
bool IR_ReferenceReadPersistentByte(uint32_t record_id, uint32_t offset, uint8_t *value);
void IR_ReferenceSetPersistFailureStep(uint32_t step);
void IR_ReferenceEnablePersistPauseInjection(bool enable);
void IR_ReferenceSimulateRecovery(void);
void IR_ReferenceSimulateRestart(void);
void IR_ReferenceResetPersistentStore(void);

#endif /* IR_REFERENCE_PROJECT_H */
