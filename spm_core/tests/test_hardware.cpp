// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore Test Suite - Hardware & GPU Enumeration
#include "spm/spm_core.h"
#include <cstdio>
#include "test_check.h"
#include <cstring>

int main() {
    printf("[TEST] Running test_spm_hardware...\n");

    SPMGpuInfo gpus[8];
    int count = 0;
    int res = SPM_DetectGpus(gpus, 8, &count);
    printf("  SPM_DetectGpus result: %d, count: %d\n", res, count);
    REQUIRE(count > 0);

    for (int i = 0; i < count; ++i) {
        printf("  GPU[%d]:\n", i);
        printf("    Name: %s\n", gpus[i].name);
        printf("    Vendor ID: 0x%04X, Device ID: 0x%04X\n", gpus[i].vendorId, gpus[i].deviceId);
        printf("    VRAM: %llu MB, Shared: %llu MB\n",
               (unsigned long long)(gpus[i].dedicatedVramBytes / (1024 * 1024)),
               (unsigned long long)(gpus[i].sharedSystemMemoryBytes / (1024 * 1024)));
        printf("    Discrete: %s, D3D11: %s, Vulkan: %s\n",
               gpus[i].isDiscrete ? "YES" : "NO",
               gpus[i].supportsD3D11 ? "YES" : "NO",
               gpus[i].supportsVulkan ? "YES" : "NO");
        printf("    Stable LUID: %s\n", gpus[i].stableLuid);

        REQUIRE(strlen(gpus[i].name) > 0);
#ifdef _WIN32
        REQUIRE(gpus[i].supportsD3D11 == 1);
#else
        REQUIRE(gpus[i].supportsVulkan == 1);
#endif
    }

    int recIndex = SPM_GetRecommendedGpuIndex(gpus, count);
    printf("  Recommended GPU Index: %d (%s)\n", recIndex, (recIndex >= 0 && recIndex < count) ? gpus[recIndex].name : "NONE");
    REQUIRE(recIndex >= 0 && recIndex < count);

    // On hybrid machines (e.g. RTX 5050 Laptop dGPU + AMD Radeon 760M iGPU),
    // the recommended GPU must be the discrete GPU!
    bool hasDiscrete = false;
    for (int i = 0; i < count; ++i) {
        if (gpus[i].isDiscrete) hasDiscrete = true;
    }
    if (hasDiscrete) {
        REQUIRE(gpus[recIndex].isDiscrete == 1);
    }

    printf("[PASS] test_spm_hardware passed successfully.\n");
    return spm_test::Finish("test_spm_hardware");
}
