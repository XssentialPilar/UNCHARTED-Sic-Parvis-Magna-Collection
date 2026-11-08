// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Consolidated Native Runtime C API
#ifndef SPM_CORE_H
#define SPM_CORE_H

#include "spm_types.h"
#include "spm_hardware_caps.h"
#include "spm_neural_assist.h"

#ifdef _WIN32
  #ifdef SPM_CORE_EXPORTS
    #define SPM_API __declspec(dllexport)
  #else
    #define SPM_API __declspec(dllimport)
  #endif
#else
  #define SPM_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// --- Platform & Version ---
SPM_API SPMPlatformId SPM_GetPlatformId(void);
SPM_API const char*   SPM_GetPlatformIdString(void);
SPM_API const char*   SPM_GetCoreVersion(void);

// --- Hardware & GPU Detection ---
SPM_API int SPM_DetectGpus(SPMGpuInfo* outGpus, int maxGpus, int* outCount);
SPM_API int SPM_GetRecommendedGpuIndex(const SPMGpuInfo* gpus, int count);

// --- Configuration Management ---
SPM_API void SPM_GetDefaultConfig(SPMConfig* outConfig);
SPM_API int  SPM_LoadConfig(const char* path, SPMConfig* outConfig);
SPM_API int  SPM_SaveConfig(const char* path, const SPMConfig* config);
SPM_API int  SPM_SyncConfigToIni(const char* workspaceRoot, const SPMConfig* config);

// --- Localization ---
SPM_API const char* SPM_GetLocalizedString(const char* langCode, const char* key, const char* fallback);
SPM_API const char* SPM_DetectSystemLanguage(void);

// --- Game Validation & Launch Coordination ---
SPM_API int  SPM_ValidateGameFiles(const char* gameDir, SPMValidationResult* outRes);
SPM_API int  SPM_PrepareEnvironment(const SPMConfig* config, const char* workspaceRoot);

// --- Platform-Locked Automatic Updater ---
SPM_API int  SPM_CheckUpdate(const char* currentVersion, const char* platformId, SPMUpdateInfo* outUpdate);
SPM_API int  SPM_ValidateUpdatePackage(const char* packagePath, const char* expectedSha256, uint64_t expectedSize);

// --- High-Performance System Bridge ---
SPM_API void SPM_InitTimerResolution(void);
SPM_API void SPM_AllowSetForegroundWindow(uint32_t processId);
SPM_API int  SPM_GetProcessMemory(uint32_t processId, uint64_t* outWorkingSet, uint64_t* outCommit);

#ifdef __cplusplus
}
#endif

#endif // SPM_CORE_H
