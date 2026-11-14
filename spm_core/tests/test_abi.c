/* SPDX-License-Identifier: GPL-2.0-or-later
 * SPMCore Test Suite - Public headers must compile and link as plain C.
 *
 * This file is compiled with the C compiler, not C++, so a header that leaks
 * C++ constructs (or an API that only links with C++ name mangling) fails.
 */
#include "spm/spm_core.h"
#include "spm/spm_streaming.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                          \
    do {                                                                          \
        if (!(condition)) {                                                       \
            printf("CHECK failed: %s (%s:%d)\n", #condition, __FILE__, __LINE__);  \
            return 1;                                                             \
        }                                                                         \
    } while (0)

int main(void) {
    const char* version = SPM_GetCoreVersion();
    CHECK(version != NULL);
    CHECK(strlen(version) > 0);
    CHECK(strcmp(version, "0.6.1") == 0);

    const char* platform = SPM_GetPlatformIdString();
    CHECK(platform != NULL);
    CHECK(strlen(platform) > 0);

    SPMConfig config;
    SPM_GetDefaultConfig(&config);
    CHECK(strcmp(config.language, "auto") == 0);
    /* The default game directory is always empty, never a personal path. */
    CHECK(config.gameDir[0] == '\0');
    CHECK(sizeof(config.language) == 16);
    CHECK(sizeof(config.resolution) == 32);

    /* Struct layout used across the C ABI. */
    CHECK(sizeof(SPMUpdateInfo) >= 32 + 32 + 32 + 128 + 512 + 65 + 8 + 4);
    CHECK(sizeof(SpmVector3) == 3 * sizeof(float));
    CHECK(sizeof(SpmAssetDescriptor) >= 8 + 4 + 24 + 4 + 4 + 4 + 4 + 8);

    CHECK(SPM_StreamingInit(64, 1, 1) == 1);

    SpmStreamingStats stats;
    SPM_StreamingGetStats(&stats);
    CHECK(stats.totalBudgetBytes == 64ull * 1024ull * 1024ull);
    CHECK(stats.currentAllocatedBytes == 0);
    CHECK(stats.queuedRequests == 0);

    SpmStreamingIntegration integration;
    CHECK(SPM_StreamingGetIntegrationStatus(&integration) == 1);
    CHECK(integration.ioConnected == 0);
    CHECK(integration.uploadConnected == 0);
    CHECK(integration.releaseConnected == 0);
    CHECK(integration.gpuResidencyValidated == 0);
    CHECK(integration.residentAssets == 0);

    SpmAssetDescriptor asset;
    memset(&asset, 0, sizeof(asset));
    asset.assetId = 1;
    asset.type = SPM_RES_TEXTURE_MIP;
    asset.sizeBytes = 4096;
    CHECK(SPM_StreamingRegisterAsset(&asset) == 1);
    CHECK(SPM_StreamingQueueRequest(1, SPM_PRIORITY_NORMAL) == 1);
    CHECK(SPM_StreamingEvaluatePriority(&asset) == SPM_PRIORITY_BACKGROUND);

    /* No runtime callbacks are connected, so nothing may be uploaded. */
    CHECK(SPM_StreamingProcessTimeSlice(0) == 0);
    SPM_StreamingGetStats(&stats);
    CHECK(stats.uploadedThisFrameBytes == 0);

    SPM_StreamingShutdown();
    SPM_StreamingGetStats(&stats);
    CHECK(stats.totalBudgetBytes == 0);

    printf("[PASS] test_spm_abi\n");
    return 0;
}
