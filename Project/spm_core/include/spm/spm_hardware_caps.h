// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Unified Hardware & Storage Capabilities Audit
#ifndef SPM_HARDWARE_CAPS_H
#define SPM_HARDWARE_CAPS_H

#include "spm_types.h"
#include <stdint.h>

#ifdef _WIN32
  #ifdef SPM_CORE_EXPORTS
    #define SPM_CAPS_API __declspec(dllexport)
  #else
    #define SPM_CAPS_API __declspec(dllimport)
  #endif
#else
  #define SPM_CAPS_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum SpmStorageType {
    SPM_STORAGE_UNKNOWN = 0,
    SPM_STORAGE_HDD = 1,
    SPM_STORAGE_SATA_SSD = 2,
    SPM_STORAGE_NVME_SSD = 3
} SpmStorageType;

typedef enum SpmNpuVendor {
    SPM_NPU_NONE = 0,
    SPM_NPU_AMD_RYZEN_AI = 1,
    SPM_NPU_INTEL_AI_BOOST = 2,
    SPM_NPU_QUALCOMM_HEXAGON = 3,
    SPM_NPU_GENERIC_MCDM = 4
} SpmNpuVendor;

typedef struct SpmHardwareCapabilities {
    // CPU Capabilities
    uint32_t cpuLogicalCores;
    uint32_t cpuPhysicalCores;
    uint8_t  cpuHasAvx2;
    uint8_t  cpuHasAvx512;
    uint8_t  cpuHasFma;

    // RAM Capabilities
    uint64_t totalPhysicalRamMb;
    uint64_t availableRamMb;

    // Storage Capabilities
    SpmStorageType storageType;
    float          storageEstimatedBandwidthMBps;
    uint8_t        storageDirectStorageSupported;
    char           storageDriveLetter[8];

    // NPU / AI Accelerator Capabilities
    SpmNpuVendor npuVendor;
    char         npuName[128];
    char         npuDriverVersion[64];
    uint8_t      npuSupported;
    uint8_t      directMlSupported;
    uint8_t      onnxRuntimeSupported;
} SpmHardwareCapabilities;

SPM_CAPS_API int  SPM_DetectHardwareCapabilities(const char* targetPath, SpmHardwareCapabilities* outCaps);
SPM_CAPS_API const char* SPM_GetStorageTypeName(SpmStorageType type);
SPM_CAPS_API const char* SPM_GetNpuVendorName(SpmNpuVendor vendor);

#ifdef __cplusplus
}
#endif

#endif // SPM_HARDWARE_CAPS_H
