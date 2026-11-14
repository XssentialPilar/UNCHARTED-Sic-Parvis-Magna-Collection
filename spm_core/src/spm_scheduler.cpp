// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Adaptive CPU Scheduler & Frametime Load Balancer
#include "spm/spm_scheduler.h"
#include "spm_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

extern "C" {

SPM_SCHED_API int SPM_DetectCpuTopology(SpmCpuTopology* outTopology) {
    if (!outTopology) return 0;
    memset(outTopology, 0, sizeof(*outTopology));

    const spm::CpuTopologyInfo topology = spm::QueryCpuTopology();
    outTopology->logicalCores = topology.logicalCores;
    outTopology->physicalCores = topology.physicalCores;
    outTopology->isHeterogeneous = topology.heterogeneous ? 1 : 0;

    // Leave at least one core for the OS and the game's main/render threads.
    if (outTopology->physicalCores > 2) {
        outTopology->maxWorkerThreads = std::clamp(outTopology->physicalCores - 1, 2u, 8u);
    } else {
        outTopology->maxWorkerThreads = 1;
    }

    return 1;
}

SPM_SCHED_API void SPM_EvaluateScheduler(const SpmCpuTopology* topology,
                                         const SpmFrameTelemetry* telemetry,
                                         SpmSchedulerDecision* outDecision) {
    if (!outDecision) return;
    memset(outDecision, 0, sizeof(*outDecision));

    // Safe fallbacks if inputs are incomplete
    uint32_t maxWorkers = (topology && topology->maxWorkerThreads > 0) ? topology->maxWorkerThreads : 4;

    float currentMs = telemetry ? telemetry->currentFrameTimeMs : 16.0f;
    float targetMs = (telemetry && telemetry->targetFrameTimeMs > 0.0f) ? telemetry->targetFrameTimeMs : 16.666f;
    float cpuPct = telemetry ? telemetry->cpuUsagePercent : 50.0f;
    float p95Ms = (telemetry && telemetry->p95FrameTimeMs > 0.0f) ? telemetry->p95FrameTimeMs : currentMs;
    uint32_t drops = telemetry ? telemetry->droppedFramesCount : 0;

    // Check critical load thresholds:
    // 1. Current frame time exceeded target by 25% (e.g. >20.8ms on a 60 FPS target)
    // 2. CPU usage is 85% or higher
    // 3. P95 frametime exceeded target by 35%
    // 4. Repeated dropped frames
    bool criticalFrametime = (currentMs >= (targetMs * 1.25f));
    bool criticalCpu = (cpuPct >= 85.0f);
    bool criticalP95 = (p95Ms >= (targetMs * 1.35f));
    bool hasDrops = (drops > 0);

    if (criticalFrametime || criticalCpu || criticalP95 || hasDrops) {
        // High Critical Load: Prioritize gameplay main loop, clamp secondary tasks
        outDecision->isCriticalLoad = 1;
        outDecision->activeWorkerThreads = 1; // Strict clamp to prevent thread contention
        outDecision->maxStreamingSliceMs = 2; // Strict 2ms upload slice
        outDecision->throttleBackgroundWork = 1;
        snprintf(outDecision->strategyDescription, sizeof(outDecision->strategyDescription),
                 "CRITICAL_LOAD: Prioritizing gameplay main thread; workers clamped to 1, streaming slice 2ms");
    } else {
        // Headroom: Distribute parallel work across available worker threads
        outDecision->isCriticalLoad = 0;
        outDecision->activeWorkerThreads = maxWorkers;
        outDecision->maxStreamingSliceMs = 8; // Full 8ms upload slice
        outDecision->throttleBackgroundWork = 0;
        snprintf(outDecision->strategyDescription, sizeof(outDecision->strategyDescription),
                 "HEADROOM: Parallel work distributed across %u worker threads, streaming slice 8ms",
                 outDecision->activeWorkerThreads);
    }
}

} // extern "C"
