// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Types and Data Structures
#ifndef SPM_TYPES_H
#define SPM_TYPES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Immutable platform identifier matching the release channel
typedef enum SPMPlatformId {
    SPM_PLATFORM_UNKNOWN = 0,
    SPM_PLATFORM_WIN7_X86,
    SPM_PLATFORM_WIN7_X64,
    SPM_PLATFORM_WIN8_X86,
    SPM_PLATFORM_WIN8_X64,
    SPM_PLATFORM_WIN81_X86,
    SPM_PLATFORM_WIN81_X64,
    SPM_PLATFORM_WIN10_X86,
    SPM_PLATFORM_WIN10_X64,
    SPM_PLATFORM_WIN11_X64,
    SPM_PLATFORM_LINUX_X64
} SPMPlatformId;

typedef struct SPMGpuInfo {
    char name[128];
    uint32_t vendorId;
    uint32_t deviceId;
    uint64_t dedicatedVramBytes;
    uint64_t sharedSystemMemoryBytes;
    uint32_t isDiscrete;       // 1 = Discrete GPU (NVIDIA/AMD dGPU), 0 = Integrated
    uint32_t supportsD3D11;    // 1 = Supports DX11 (Feature Level 11.0+)
    uint32_t supportsVulkan;   // 1 = Supports Vulkan 1.1+
    char stableLuid[32];      // Hex representation of Adapter LUID
} SPMGpuInfo;

typedef struct SPMConfig {
    char language[16];         // auto, en, es_419, es_ES, pt_BR, fr, de, it, ru, ja, zh_CN
    char gameDir[512];         // Path to dumped game files (CUSA02344)
    char selectedGame[16];     // u1, u2, u3
    char graphicsApi[16];      // dx11, dx12, vulkan, dx9
    char vramBudgetMb[16];     // 0 (Auto), 820, 1720, 3200, 6144
    char framePacingMode[16];  // hybrid, waitable, sleep, off
    char textureQuality[16];   // low, medium, high, ultra
    char shadowQuality[16];    // low, medium, high, ultra
    char ssaoQuality[16];      // off, half, full
    char anisotropy[16];       // 1, 2, 4, 8, 16
    char fpsLimit[16];         // 30, 60, 90, 120, 0 (unlimited)
    char vsync[8];             // 0, 1
    char displayMode[16];      // fullscreen, borderless, windowed
    char resolution[32];       // 1920x1080, etc.
    char outputResolution[32];
    char upscaler[16];         // off, fsr1, fsr3, fsr4, dlaa, dlss
    char selectedGpuName[128];
    char selectedGpuId[32];    // Adapter LUID or index
    double mouseSensitivity;
    double mouseAimSensitivity;
    double mouseVerticalSensitivity;
    int mouseInvertY;
    int mouseInvertX;
    double stickSpeed;
    int vibration;
    char prompts[16];          // auto, ps, xbox, kb
    char keyMap[1024];
    char padMap[512];
    char mouseMap[512];
    char antiAliasing[16];       // off, fxaa, smaa, msaa2x, msaa4x, msaa8x, dlaa
    char frameGeneration[16];    // off, fsr2x, dlss2x, dlss3x, dlss4x, dlss5x, dlss6x, dynamic
    char reflexMode[16];         // off, on, boost
    char streamingBudgetMb[16];  // 0 (Auto), 512, 1024, 2048, 4096
    char dynamicResolution[8];   // 0, 1
    double sharpening;           // 0.0 to 1.0
    char neuralAssistMode[16];   // auto, enabled, disabled
    char storageOptimization[16];// auto, nvme, ssd, hdd
} SPMConfig;

typedef struct SPMUpdateInfo {
    char targetPlatformId[32];
    char currentVersion[32];
    char latestVersion[32];
    char releaseTitle[128];
    char downloadUrl[512];
    char sha256Expected[65];
    uint64_t expectedSizeBytes;
    int updateAvailable;       // 1 if eligible and newer, 0 otherwise
} SPMUpdateInfo;

typedef struct SPMValidationResult {
    uint32_t totalFilesChecked;
    uint32_t missingFilesCount;
    uint32_t wrongSizeCount;
    int isValid;               // 1 if all required game files are present
} SPMValidationResult;

#ifdef __cplusplus
}
#endif

#endif // SPM_TYPES_H
