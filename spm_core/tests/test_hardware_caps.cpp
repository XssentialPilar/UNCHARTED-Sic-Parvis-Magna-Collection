// SPDX-License-Identifier: GPL-2.0-or-later
// Test suite for SPM Hardware Capabilities and Storage Detection
#include "spm/spm_hardware_caps.h"
#include "test_check.h"
#include <cstdio>
#include <cstring>

int main() {
    printf("=== SPMCore Hardware Capabilities Test ===\n");

    // Null safety test
    REQUIRE(SPM_DetectHardwareCapabilities(nullptr, nullptr) == 0);

    SpmHardwareCapabilities caps{};
    int res = SPM_DetectHardwareCapabilities(".", &caps);
    REQUIRE(res == 1);

    printf("CPU Logical Cores: %u\n", caps.cpuLogicalCores);
    printf("CPU Physical Cores: %u\n", caps.cpuPhysicalCores);
    printf("CPU AVX2: %s, AVX-512: %s, FMA: %s\n",
           caps.cpuHasAvx2 ? "Yes" : "No",
           caps.cpuHasAvx512 ? "Yes" : "No",
           caps.cpuHasFma ? "Yes" : "No");

    REQUIRE(caps.cpuLogicalCores >= 1);

    printf("RAM Total: %llu MB, Available: %llu MB\n",
           (unsigned long long)caps.totalPhysicalRamMb,
           (unsigned long long)caps.availableRamMb);
    REQUIRE(caps.totalPhysicalRamMb > 0);

    printf("Storage Type: %s (Drive: %s, Est. Bandwidth: %.1f MB/s, DirectStorage: %s)\n",
           SPM_GetStorageTypeName(caps.storageType),
           caps.storageDriveLetter,
           caps.storageEstimatedBandwidthMBps,
           caps.storageDirectStorageSupported ? "Yes" : "No");
    REQUIRE(caps.storageType >= SPM_STORAGE_UNKNOWN && caps.storageType <= SPM_STORAGE_NVME_SSD);

    printf("NPU Vendor: %s (Supported: %s, Device: '%s', DirectML: %s, ONNX: %s)\n",
           SPM_GetNpuVendorName(caps.npuVendor),
           caps.npuSupported ? "Yes" : "No",
           caps.npuName,
           caps.directMlSupported ? "Yes" : "No",
           caps.onnxRuntimeSupported ? "Yes" : "No");

    // Verify string helpers return valid values
    REQUIRE(strcmp(SPM_GetStorageTypeName(SPM_STORAGE_NVME_SSD), "NVMe SSD") == 0);
    REQUIRE(strcmp(SPM_GetStorageTypeName(SPM_STORAGE_SATA_SSD), "SATA SSD") == 0);
    REQUIRE(strcmp(SPM_GetStorageTypeName(SPM_STORAGE_HDD), "HDD") == 0);
    REQUIRE(strcmp(SPM_GetNpuVendorName(SPM_NPU_NONE), "None") == 0);

    printf("=== SPMCore Hardware Capabilities Test PASSED ===\n");
    return spm_test::Finish("test_spm_hardware_caps");
}
