// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Adaptive CPU Scheduler & Frametime Load Balancer
#ifndef SPM_SCHEDULER_H
#define SPM_SCHEDULER_H

#include "spm_types.h"
#include <stdint.h>

#ifdef _WIN32
  #ifdef SPM_CORE_EXPORTS
    #define SPM_SCHED_API __declspec(dllexport)
  #else
    #define SPM_SCHED_API __declspec(dllimport)
  #endif
#else
  #define SPM_SCHED_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SpmCpuTopology {
    uint32_t logicalCores;
    uint32_t physicalCores;
    uint32_t maxWorkerThreads;
    uint8_t  isHeterogeneous; // E.g. Intel Hybrid P/E cores
} SpmCpuTopology;

typedef struct SpmFrameTelemetry {
    float currentFrameTimeMs;
    float targetFrameTimeMs;
    float cpuUsagePercent;
    float p95FrameTimeMs;
    uint32_t droppedFramesCount;
} SpmFrameTelemetry;

typedef struct SpmSchedulerDecision {
    uint8_t  isCriticalLoad;          // 1 = Critical high load, 0 = Normal / Headroom
    uint32_t activeWorkerThreads;    // Number of background worker threads permitted
    uint32_t maxStreamingSliceMs;    // Max ms permitted for asset streaming per frame
    uint8_t  throttleBackgroundWork;  // 1 = Throttle shader compilation / unneeded work
    char     strategyDescription[128];
} SpmSchedulerDecision;

SPM_SCHED_API int  SPM_DetectCpuTopology(SpmCpuTopology* outTopology);
SPM_SCHED_API void SPM_EvaluateScheduler(const SpmCpuTopology* topology,
                                         const SpmFrameTelemetry* telemetry,
                                         SpmSchedulerDecision* outDecision);

#ifdef __cplusplus
}
#endif

#endif // SPM_SCHEDULER_H
