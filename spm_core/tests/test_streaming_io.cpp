// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore Test Suite - Real streaming I/O, VRAM upload accounting and eviction
//
// A temporary file on disk is the asset source; the read callback performs the
// real file I/O and the upload callback copies into a RAM buffer that stands in
// for the renderer's VRAM allocation. The test verifies byte-for-byte that what
// arrived in "VRAM" is exactly what the file contained.
#include "spm/spm_streaming.h"
#include "test_check.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kMiB = 1024u * 1024u;
constexpr uint64_t kBudgetMb = 16;

long CurrentPid() {
#ifdef _WIN32
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}

struct Harness {
    std::unordered_map<uint64_t, std::string> paths;
    std::set<uint64_t> failingReads;
    std::vector<uint8_t> vram;
    std::vector<uint64_t> releasedIds;
    std::set<uint64_t> pinnedIds;
    size_t bytesUploaded{0};
    uint32_t uploadCalls{0};
    bool failUploads{false};
    bool uploadOutOfRange{false};
    std::atomic<uint32_t> readCalls{0};
};

int32_t SPM_CALLBACK ReadCallback(void* userData, uint64_t assetId, uint64_t offset,
                                  void* destination, uint32_t bytes) {
    auto* harness = static_cast<Harness*>(userData);
    harness->readCalls.fetch_add(1);
    if (harness->failingReads.count(assetId) != 0) {
        return -1;
    }
    const auto it = harness->paths.find(assetId);
    if (it == harness->paths.end()) {
        return -1;
    }

    std::ifstream file(it->second, std::ios::binary);
    if (!file.is_open()) {
        return -1;
    }
    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file) {
        return -1;
    }
    file.read(static_cast<char*>(destination), static_cast<std::streamsize>(bytes));
    const std::streamsize copied = file.gcount();
    return copied > 0 ? static_cast<int32_t>(copied) : 0;
}

int32_t SPM_CALLBACK UploadCallback(void* userData, uint64_t assetId, const void* source,
                                    uint32_t bytes, uint64_t vramOffset) {
    auto* harness = static_cast<Harness*>(userData);
    if (harness->failUploads) {
        return -1;
    }
    if (vramOffset + bytes > harness->vram.size()) {
        harness->uploadOutOfRange = true;
        return -1;
    }
    std::memcpy(harness->vram.data() + static_cast<size_t>(vramOffset), source, bytes);
    harness->bytesUploaded += bytes;
    harness->uploadCalls++;
    (void)assetId;
    return static_cast<int32_t>(bytes);
}

int32_t SPM_CALLBACK ReleaseCallback(void* userData, uint64_t assetId, uint64_t vramOffset) {
    auto* harness = static_cast<Harness*>(userData);
    harness->releasedIds.push_back(assetId);
    (void)vramOffset;
    return 1;
}

std::string MakeAssetFile(const fs::path& directory, uint64_t assetId, uint32_t size) {
    std::vector<uint8_t> data(size);
    for (uint32_t index = 0; index < size; ++index) {
        data[index] = static_cast<uint8_t>((index * 31u + assetId * 17u) & 0xFFu);
    }
    const fs::path path = directory / ("asset_" + std::to_string(assetId) + ".bin");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    REQUIRE(out.is_open());
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
    out.close();
    REQUIRE(out.good());
    return path.string();
}

std::vector<uint8_t> ReadWholeFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.is_open());
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(file),
                                std::istreambuf_iterator<char>());
}

template <typename Predicate>
bool WaitFor(Predicate predicate, int timeoutMs = 5000) {
    for (int elapsed = 0; elapsed < timeoutMs; elapsed += 10) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

SpmStreamingIntegration IntegrationStatus() {
    SpmStreamingIntegration status{};
    REQUIRE(SPM_StreamingGetIntegrationStatus(&status) == 1);
    return status;
}

int RegisterAsset(uint64_t assetId, uint32_t size, int visible) {
    SpmAssetDescriptor descriptor{};
    descriptor.assetId = assetId;
    descriptor.type = SPM_RES_TEXTURE_MIP;
    descriptor.sizeBytes = size;
    descriptor.isVisible = visible;
    return SPM_StreamingRegisterAsset(&descriptor);
}

// Drives the time-sliced uploader until every staged asset is resident.
bool DrainResident(uint64_t expectedResident, uint32_t quota) {
    for (int attempt = 0; attempt < 500; ++attempt) {
        const SpmStreamingIntegration status = IntegrationStatus();
        if (status.stagedAssets == 0 && status.residentAssets >= expectedResident) {
            return true;
        }
        SPM_StreamingProcessTimeSlice(quota);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}

} // namespace

int main() {
    std::printf("[TEST] Running test_spm_streaming_io...\n");

    const fs::path workDir =
        fs::temp_directory_path() / ("spm_streaming_io_" + std::to_string(CurrentPid()));
    std::error_code ec;
    fs::remove_all(workDir, ec);
    REQUIRE(fs::create_directories(workDir, ec));

    Harness harness;
    harness.vram.resize(kBudgetMb * kMiB);

    const uint32_t sizeA = 3u * kMiB + 12345u;
    harness.paths[1] = MakeAssetFile(workDir, 1, sizeA);
    harness.paths[2] = MakeAssetFile(workDir, 2, 512u * 1024u);
    harness.paths[3] = MakeAssetFile(workDir, 3, 256u * 1024u);
    for (uint64_t assetId = 4; assetId <= 6; ++assetId) {
        harness.paths[assetId] = MakeAssetFile(workDir, assetId, 4u * kMiB);
    }
    harness.failingReads.insert(2);

    REQUIRE(SPM_StreamingInit(kBudgetMb, 1, 2) == 1);
    REQUIRE(SPM_StreamingRegisterAsset(nullptr) == 0);

    REQUIRE(RegisterAsset(1, sizeA, 1) == 1);
    REQUIRE(SPM_StreamingQueueRequest(1, SPM_PRIORITY_CRITICAL) == 1);

    // 1. No runtime connected: the request stays queued and nothing is staged.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    SpmStreamingIntegration status = IntegrationStatus();
    CHECK(status.ioConnected == 0);
    CHECK(status.stagedAssets == 0);
    CHECK(SPM_StreamingProcessTimeSlice(1u * kMiB) == 0);
    SpmStreamingStats stats{};
    SPM_StreamingGetStats(&stats);
    CHECK(stats.queuedRequests == 1);
    CHECK(stats.currentAllocatedBytes == 0);

    // 2. Connect the runtime: the worker reads the real file.
    REQUIRE(SPM_StreamingSetIoCallbacks(ReadCallback, UploadCallback, ReleaseCallback,
                                        &harness) == 1);
    status = IntegrationStatus();
    CHECK(status.ioConnected == 1);
    CHECK(status.uploadConnected == 1);
    CHECK(status.releaseConnected == 1);

    REQUIRE(WaitFor([] {
        return IntegrationStatus().stagedAssets == 1;
    }));
    status = IntegrationStatus();
    CHECK(status.stagedBytes == sizeA);
    CHECK(status.gpuResidencyValidated == 0);
    CHECK(harness.readCalls.load() >= 1);

    // 3. Time-sliced upload respects the per-call quota.
    CHECK(SPM_StreamingProcessTimeSlice(1u * kMiB) == 1u * kMiB);
    status = IntegrationStatus();
    CHECK(status.stagedBytes == sizeA - 1u * kMiB);
    CHECK(status.gpuResidencyValidated == 0);

    REQUIRE(DrainResident(1, 1u * kMiB));
    status = IntegrationStatus();
    CHECK(status.stagedAssets == 0);
    CHECK(status.residentAssets == 1);
    CHECK(status.gpuResidencyValidated == 1);
    CHECK(status.failedAssets == 0);

    SPM_StreamingGetStats(&stats);
    CHECK(stats.currentAllocatedBytes == sizeA);
    CHECK(stats.uploadedThisFrameBytes <= 1u * kMiB);
    CHECK(harness.bytesUploaded == sizeA);
    CHECK(harness.uploadCalls >= 4); // 1 MiB chunks + remainder
    CHECK(!harness.uploadOutOfRange);

    // 4. The bytes stored in "VRAM" are exactly the file contents.
    const std::vector<uint8_t> expectedA = ReadWholeFile(harness.paths[1]);
    REQUIRE(expectedA.size() == sizeA);
    CHECK(std::memcmp(harness.vram.data(), expectedA.data(), sizeA) == 0);

    // A resident asset is not queued again.
    CHECK(SPM_StreamingQueueRequest(1, SPM_PRIORITY_CRITICAL) == 1);

    // 5. Read failure: the worker reports the failure instead of inventing data.
    REQUIRE(RegisterAsset(2, 512u * 1024u, 0) == 1);
    REQUIRE(SPM_StreamingQueueRequest(2, SPM_PRIORITY_NORMAL) == 1);
    REQUIRE(WaitFor([] {
        return IntegrationStatus().failedAssets >= 1;
    }));
    status = IntegrationStatus();
    CHECK(status.stagedAssets == 0);
    CHECK(status.residentAssets == 1);

    // 6. Upload failure: the asset is not counted as resident.
    harness.failUploads = true;
    REQUIRE(RegisterAsset(3, 256u * 1024u, 0) == 1);
    REQUIRE(SPM_StreamingQueueRequest(3, SPM_PRIORITY_NORMAL) == 1);
    REQUIRE(WaitFor([] {
        return IntegrationStatus().stagedAssets == 1;
    }));
    CHECK(SPM_StreamingProcessTimeSlice(1u * kMiB) == 0);
    status = IntegrationStatus();
    CHECK(status.stagedAssets == 0);
    CHECK(status.residentAssets == 1);
    CHECK(status.failedAssets >= 2);
    harness.failUploads = false;

    const size_t uploadedBeforeFill = harness.bytesUploaded;

    // 7. Fill the budget with real residents.
    for (uint64_t assetId = 4; assetId <= 6; ++assetId) {
        REQUIRE(RegisterAsset(assetId, 4u * kMiB, 0) == 1);
        REQUIRE(SPM_StreamingQueueRequest(assetId, SPM_PRIORITY_NORMAL) == 1);
        REQUIRE(WaitFor([] {
            return IntegrationStatus().stagedAssets == 1;
        }));
        REQUIRE(SPM_StreamingProcessTimeSlice(4u * kMiB) == 4u * kMiB);
    }
    REQUIRE(DrainResident(4, 4u * kMiB));
    CHECK(harness.bytesUploaded == uploadedBeforeFill + 12u * kMiB);

    SPM_StreamingGetStats(&stats);
    const uint64_t filledBytes = sizeA + 12u * kMiB;
    CHECK(stats.currentAllocatedBytes == filledBytes);
    CHECK(filledBytes > (kBudgetMb * kMiB * 88u) / 100u);

    // Asset 4 is pin-protected; asset 1 is visible. Both are older than 5 and 6
    // in LRU order, so eviction must skip them and release asset 5.
    CHECK(SPM_StreamingSetAssetPinned(4, 1) == 1);

    const int evicted = SPM_StreamingEnforceBudget();
    CHECK(evicted == 1);
    REQUIRE(harness.releasedIds.size() == 1);
    CHECK(harness.releasedIds[0] == 5);

    SPM_StreamingGetStats(&stats);
    CHECK(stats.currentAllocatedBytes <= (kBudgetMb * kMiB * 75u) / 100u);
    CHECK(stats.evictedCount == 1);
    status = IntegrationStatus();
    CHECK(status.residentAssets == 3);

    // The pinned asset can be unpinned after enforcement.
    CHECK(SPM_StreamingSetAssetPinned(4, 0) == 1);

    // 7b. Three-state visibility, temporal hysteresis, GPU fence lifetime safety & dynamic budget.
    SPM_StreamingSetHysteresisFrames(60);
    // Remaining resident assets right now: 1 (4 MiB), 4 (4 MiB), 6 (4 MiB) = 12 MiB.
    CHECK(SPM_StreamingSetVisibility(1, SPM_VISIBILITY_POTENTIALLY_VISIBLE, 100, 1.0f) == 1);
    CHECK(SPM_StreamingSetVisibility(4, SPM_VISIBILITY_VISIBLE, 100, 1.0f) == 1);
    CHECK(SPM_StreamingSetVisibility(6, SPM_VISIBILITY_INACTIVE, 100, 1.0f) == 1);
    // Mark asset 6 as referenced by in-flight GPU work at fence 50 (completed fence = 40).
    SPM_StreamingSetGpuFence(50, 40);
    CHECK(SPM_StreamingSetAssetFence(6, 50) == 1);
    SPM_StreamingUpdateGpuBudget(kBudgetMb, 13, 0);
    // Asset 6 cannot be evicted while its GPU fence 50 > completed 40, even though INACTIVE.
    (void)SPM_StreamingEnforceBudget();
    bool asset6StillResident = false;
    // Asset 6 was NOT released before fence completion:
    for (uint64_t relId : harness.releasedIds) {
        if (relId == 6) {
            asset6StillResident = true;
        }
    }
    CHECK(!asset6StillResident);

    // Once GPU completes fence 50 and frame advances past hysteresis (frame 170), asset 6 is safely evicted.
    SPM_StreamingSetGpuFence(50, 50);
    CHECK(SPM_StreamingSetVisibility(1, SPM_VISIBILITY_VISIBLE, 170, 1.0f) == 1);
    // Ensure pressure still triggers on 13 MiB OS budget if asset 1 + 4 + 6 aren't already reduced;
    // if asset 1 was evicted as last-resort in step above, asset 6 still exceeds target when tightened to 9 MiB.
    SPM_StreamingUpdateGpuBudget(kBudgetMb, 9, 0);
    const int evictedPostFence = SPM_StreamingEnforceBudget();
    CHECK(evictedPostFence >= 1);
    bool asset6EvictedNow = false;
    for (uint64_t relId : harness.releasedIds) {
        if (relId == 6) {
            asset6EvictedNow = true;
        }
    }
    CHECK(asset6EvictedNow);
    // Integrated GPU shared-RAM budget (osAvailableBudgetMb = 12 MiB -> 85% aligned down to 256 B)
    SPM_StreamingUpdateGpuBudget(0, 12, 1);
    SPM_StreamingGetStats(&stats);
    CHECK(stats.totalBudgetBytes == (((12ull * 1024ull * 1024ull * 85ull) / 100ull) & ~255ull));

    // 8. Shutdown with pending work returns promptly and leaves empty state.
    REQUIRE(RegisterAsset(7, 4u * kMiB, 0) == 1);
    CHECK(SPM_StreamingQueueRequest(7, SPM_PRIORITY_BACKGROUND) == 1);
    SPM_StreamingShutdown();
    SPM_StreamingGetStats(&stats);
    CHECK(stats.totalBudgetBytes == 0);
    CHECK(SPM_StreamingGetIntegrationStatus(&status) == 0);

    fs::remove_all(workDir, ec);
    return spm_test::Finish("test_spm_streaming_io");
}
