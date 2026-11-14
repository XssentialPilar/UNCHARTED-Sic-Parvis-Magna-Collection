// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore Test Suite - Concurrent streaming, callback hot-swap and lifecycle
#include "spm/spm_streaming.h"
#include "test_check.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

struct Harness {
    std::atomic<uint64_t> uploaded{0};
    std::atomic<uint32_t> reads{0};
};

int32_t SPM_CALLBACK ReadCallback(void* userData, uint64_t assetId, uint64_t offset,
                                  void* destination, uint32_t bytes) {
    auto* harness = static_cast<Harness*>(userData);
    harness->reads.fetch_add(1);
    // The callback is the data source; the engine never fabricates contents.
    std::memset(destination, static_cast<int>(assetId & 0xFFu), bytes);
    (void)offset;
    return static_cast<int32_t>(bytes);
}

int32_t SPM_CALLBACK UploadCallback(void* userData, uint64_t assetId, const void* source,
                                    uint32_t bytes, uint64_t vramOffset) {
    auto* harness = static_cast<Harness*>(userData);
    harness->uploaded.fetch_add(bytes);
    (void)assetId;
    (void)source;
    (void)vramOffset;
    return static_cast<int32_t>(bytes);
}

SpmAssetDescriptor MakeAsset(uint64_t assetId) {
    SpmAssetDescriptor descriptor{};
    descriptor.assetId = assetId;
    descriptor.type = SPM_RES_GEOMETRY_LOD;
    descriptor.sizeBytes = static_cast<uint32_t>(4096u + (assetId % 7u) * 512u);
    descriptor.isVisible = 0;
    return descriptor;
}

} // namespace

int main() {
    std::printf("[TEST] Running test_spm_concurrency...\n");

    for (int cycle = 0; cycle < 8; ++cycle) {
        REQUIRE(SPM_StreamingInit(64, 1, 3) == 1);
        Harness harness;
        REQUIRE(SPM_StreamingSetIoCallbacks(ReadCallback, UploadCallback, nullptr,
                                            &harness) == 1);

        std::atomic<bool> stop{false};
        std::atomic<uint64_t> registered{0};

        std::thread producer([&] {
            uint64_t assetId = 1;
            while (!stop.load(std::memory_order_relaxed)) {
                const SpmAssetDescriptor descriptor = MakeAsset(assetId);
                if (SPM_StreamingRegisterAsset(&descriptor) == 1) {
                    SPM_StreamingQueueRequest(
                        assetId,
                        static_cast<SpmStreamPriority>(assetId %
                                                       static_cast<uint64_t>(
                                                           SPM_PRIORITY_COUNT)));
                }
                ++assetId;
                registered.store(assetId, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });

        std::thread consumer([&] {
            while (!stop.load(std::memory_order_relaxed)) {
                SPM_StreamingProcessTimeSlice(256u * 1024u);
                SPM_StreamingEnforceBudget();
                SpmStreamingStats stats{};
                SPM_StreamingGetStats(&stats);
                SpmStreamingIntegration integration{};
                SPM_StreamingGetIntegrationStatus(&integration);
            }
        });

        std::thread cameraThread([&] {
            SpmCameraState camera{};
            camera.position = SpmVector3{0.0f, 0.0f, 0.0f};
            camera.forward = SpmVector3{0.0f, 0.0f, 1.0f};
            camera.velocity = SpmVector3{0.0f, 0.0f, 4.0f};
            camera.fovDegrees = 60.0f;
            while (!stop.load(std::memory_order_relaxed)) {
                SPM_StreamingUpdateCamera(&camera);
                SpmAssetDescriptor probe = MakeAsset(1);
                SPM_StreamingEvaluatePriority(&probe);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });

        // Callback hot-swap while workers are waiting on the condition variable.
        std::thread swapThread([&] {
            bool connected = true;
            while (!stop.load(std::memory_order_relaxed)) {
                if (connected) {
                    SPM_StreamingSetIoCallbacks(ReadCallback, UploadCallback, nullptr,
                                                &harness);
                } else {
                    SPM_StreamingSetIoCallbacks(nullptr, nullptr, nullptr, nullptr);
                }
                connected = !connected;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            SPM_StreamingSetIoCallbacks(ReadCallback, UploadCallback, nullptr, &harness);
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        stop.store(true, std::memory_order_relaxed);

        producer.join();
        consumer.join();
        cameraThread.join();
        swapThread.join();

        SpmStreamingStats stats{};
        SPM_StreamingGetStats(&stats);
        CHECK(stats.currentAllocatedBytes <= stats.totalBudgetBytes);
        CHECK(stats.totalBudgetBytes == 64ull * 1024ull * 1024ull);
        CHECK(stats.activeWorkers <= 3);
        CHECK(stats.pendingQueueBytes <= 64ull * 1024ull * 1024ull);

        SPM_StreamingShutdown();

        const uint64_t assetsRegistered = registered.load(std::memory_order_relaxed);
        CHECK(assetsRegistered > 10);
        std::printf("  cycle %d: %llu assets registered, %llu bytes uploaded\n", cycle,
                    static_cast<unsigned long long>(assetsRegistered),
                    static_cast<unsigned long long>(harness.uploaded.load()));
    }

    // Repeated lifecycle churn must not leak threads or deadlock on shutdown.
    for (int cycle = 0; cycle < 20; ++cycle) {
        REQUIRE(SPM_StreamingInit(32, 1, 1) == 1);
        const SpmAssetDescriptor descriptor = MakeAsset(1);
        REQUIRE(SPM_StreamingRegisterAsset(&descriptor) == 1);
        REQUIRE(SPM_StreamingQueueRequest(1, SPM_PRIORITY_NORMAL) == 1);
        SPM_StreamingShutdown();
    }

    SpmStreamingStats finalStats{};
    SPM_StreamingGetStats(&finalStats);
    CHECK(finalStats.totalBudgetBytes == 0);

    return spm_test::Finish("test_spm_concurrency");
}
