// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Adaptive CPU Scheduler & Frametime Load Balancer
#include "spm/spm_scheduler.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

extern "C" {

SPM_SCHED_API int SPM_DetectCpuTopology(SpmCpuTopology* outTopology) {
    if (!outTopology) return 0;
    memset(outTopology, 0, sizeof(*outTopology));

#ifdef _WIN32
    SYSTEM_INFO sysInfo{};
    GetSystemInfo(&sysInfo);
    outTopology->logicalCores = sysInfo.dwNumberOfProcessors;

    // Attempt precise physical core detection via GetLogicalProcessorInformationEx
    DWORD returnLength = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &returnLength);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && returnLength > 0) {
        char* buffer = new char[returnLength];
        if (GetLogicalProcessorInformationEx(RelationProcessorCore,
                                             reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer),
                                             &returnLength)) {
            DWORD offset = 0;
            uint32_t physicalCount = 0;
            while (offset < returnLength) {
                auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer + offset);
                if (info->Relationship == RelationProcessorCore) {
                    physicalCount++;
                }
                offset += info->Size;
            }
            outTopology->physicalCores = physicalCount;
        }
        delete[] buffer;
    }

    if (outTopology->physicalCores == 0) {
        // Fallback heuristic: logical / 2 for hyperthreaded CPUs, minimum 1
        outTopology->physicalCores = (outTopology->logicalCores > 1) ? (outTopology->logicalCores / 2) : 1;
    }

    // Worker threads budget: leave at least 1-2 cores free for OS and Game Main/Render threads
    if (outTopology->physicalCores > 2) {
        outTopology->maxWorkerThreads = std::clamp(outTopology->physicalCores - 1, 2u, 8u);
    } else {
        outTopology->maxWorkerThreads = 1;
    }

    // Detect heterogeneous topology (e.g. Intel Hybrid with differing efficiency/performance cores)
    outTopology->isHeterogeneous = (outTopology->logicalCores > (outTopology->physicalCores * 2)) ? 1 : 0;
#else
    long nprocs = sysconf(_SC_NPROCESSORS_ONLN);
    outTopology->logicalCores = (nprocs > 0) ? static_cast<uint32_t>(nprocs) : 4;
    outTopology->physicalCores = (outTopology->logicalCores > 1) ? (outTopology->logicalCores / 2) : 1;
    outTopology->maxWorkerThreads = std::clamp(outTopology->physicalCores - 1, 1u, 6u);
    outTopology->isHeterogeneous = 0;
#endif

    return 1;
}

SPM_SCHED_API void SPM_EvaluateScheduler(const SpmCpuTopology* topology,
                                         const SpmFrameTelemetry* telemetry,
                                         SpmSchedulerDecision* outDecision) {
    if (!outDecision) return;
    memset(outDecision, 0, sizeof(*outDecision));

    // Safe fallbacks if inputs are incomplete
    uint32_t physicalCores = (topology && topology->physicalCores > 0) ? topology->physicalCores : 4;
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
