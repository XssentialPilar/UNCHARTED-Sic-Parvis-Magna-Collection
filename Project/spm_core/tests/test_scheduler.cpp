// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Unit Test for Adaptive CPU Scheduler
#include "spm/spm_scheduler.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    printf("[SPM Test] Testing SPM Adaptive CPU Scheduler...\n");

    // 1. Test CPU Topology Detection
    SpmCpuTopology topology{};
    int topoOk = SPM_DetectCpuTopology(&topology);
    assert(topoOk == 1);
    assert(topology.logicalCores >= 1);
    assert(topology.physicalCores >= 1);
    assert(topology.maxWorkerThreads >= 1);
    printf("   -> Detected: %u logical cores, %u physical cores, max workers: %u, heterogeneous: %u\n",
           topology.logicalCores, topology.physicalCores, topology.maxWorkerThreads, topology.isHeterogeneous);

    // 2. Test Headroom / Normal Gameplay Scenario (60 FPS, 16.0ms, 40% CPU)
    SpmFrameTelemetry telemetryNormal{};
    telemetryNormal.currentFrameTimeMs = 16.0f;
    telemetryNormal.targetFrameTimeMs = 16.666f;
    telemetryNormal.cpuUsagePercent = 40.0f;
    telemetryNormal.p95FrameTimeMs = 16.5f;
    telemetryNormal.droppedFramesCount = 0;

    SpmSchedulerDecision decisionNormal{};
    SPM_EvaluateScheduler(&topology, &telemetryNormal, &decisionNormal);
    assert(decisionNormal.isCriticalLoad == 0);
    assert(decisionNormal.throttleBackgroundWork == 0);
    assert(decisionNormal.activeWorkerThreads == topology.maxWorkerThreads);
    assert(decisionNormal.maxStreamingSliceMs == 8);
    printf("   -> Normal load passed: %s\n", decisionNormal.strategyDescription);

    // 3. Test Critical Frametime Spike Scenario (60 FPS target, but 24.0ms spike)
    SpmFrameTelemetry telemetrySpike{};
    telemetrySpike.currentFrameTimeMs = 24.0f; // > 16.666 * 1.25 = 20.83ms
    telemetrySpike.targetFrameTimeMs = 16.666f;
    telemetrySpike.cpuUsagePercent = 55.0f;
    telemetrySpike.p95FrameTimeMs = 25.0f;
    telemetrySpike.droppedFramesCount = 0;

    SpmSchedulerDecision decisionSpike{};
    SPM_EvaluateScheduler(&topology, &telemetrySpike, &decisionSpike);
    assert(decisionSpike.isCriticalLoad == 1);
    assert(decisionSpike.throttleBackgroundWork == 1);
    assert(decisionSpike.activeWorkerThreads == 1);
    assert(decisionSpike.maxStreamingSliceMs == 2);
    printf("   -> Frametime spike passed: %s\n", decisionSpike.strategyDescription);

    // 4. Test High CPU Load Scenario (CPU 92%)
    SpmFrameTelemetry telemetryHighCpu{};
    telemetryHighCpu.currentFrameTimeMs = 15.5f;
    telemetryHighCpu.targetFrameTimeMs = 16.666f;
    telemetryHighCpu.cpuUsagePercent = 92.0f;
    telemetryHighCpu.p95FrameTimeMs = 16.0f;
    telemetryHighCpu.droppedFramesCount = 0;

    SpmSchedulerDecision decisionHighCpu{};
    SPM_EvaluateScheduler(&topology, &telemetryHighCpu, &decisionHighCpu);
    assert(decisionHighCpu.isCriticalLoad == 1);
    assert(decisionHighCpu.throttleBackgroundWork == 1);
    assert(decisionHighCpu.activeWorkerThreads == 1);
    assert(decisionHighCpu.maxStreamingSliceMs == 2);
    printf("   -> High CPU load passed: %s\n", decisionHighCpu.strategyDescription);

    // 5. Test Dropped Frames Scenario
    SpmFrameTelemetry telemetryDrops{};
    telemetryDrops.currentFrameTimeMs = 16.0f;
    telemetryDrops.targetFrameTimeMs = 16.666f;
    telemetryDrops.cpuUsagePercent = 60.0f;
    telemetryDrops.p95FrameTimeMs = 16.5f;
    telemetryDrops.droppedFramesCount = 3;

    SpmSchedulerDecision decisionDrops{};
    SPM_EvaluateScheduler(&topology, &telemetryDrops, &decisionDrops);
    assert(decisionDrops.isCriticalLoad == 1);
    assert(decisionDrops.throttleBackgroundWork == 1);
    assert(decisionDrops.activeWorkerThreads == 1);
    printf("   -> Dropped frames recovery passed: %s\n", decisionDrops.strategyDescription);

    printf("[SPM Test] All Adaptive CPU Scheduler assertions PASSED successfully!\n");
    return 0;
}
