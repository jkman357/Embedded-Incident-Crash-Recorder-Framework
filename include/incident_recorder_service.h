#ifndef INCIDENT_RECORDER_SERVICE_H
#define INCIDENT_RECORDER_SERVICE_H

#include "incident_recorder.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Runs bounded, non-hot-path recorder work. Call from a low-priority task or
 * periodic main-loop service point. One call performs at most the configured
 * amount of continuous-trace work and one export-state transition.
 */
void IR_ServiceProcess(void);

/*
 * Requests on-demand export of already-persisted evidence. In a Release build
 * this is the service/LCD path used to copy persistent evidence to an export
 * destination such as SD. The request never performs physical I/O directly.
 */
IR_Result IR_ServiceRequestExport(void);

#ifdef __cplusplus
}
#endif

#endif /* INCIDENT_RECORDER_SERVICE_H */
