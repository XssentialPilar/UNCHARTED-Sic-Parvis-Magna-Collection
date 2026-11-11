// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore Test Suite - Platform Detection
#include "spm/spm_core.h"
#include <cstdio>
#include <cassert>
#include <cstring>

int main() {
    printf("[TEST] Running test_spm_platform...\n");

    SPMPlatformId platId = SPM_GetPlatformId();
    printf("  Detected Platform ID: %d\n", (int)platId);
    assert(platId != SPM_PLATFORM_UNKNOWN);

    const char* platStr = SPM_GetPlatformIdString();
    printf("  Platform ID String: %s\n", platStr);
    assert(platStr != nullptr);
    assert(strlen(platStr) > 0);

    // On Windows 64-bit modern host, expect win10-x64 or win11-x64
    bool isExpectedWin = (strcmp(platStr, "win10-x64") == 0 ||
                          strcmp(platStr, "win11-x64") == 0 ||
                          strcmp(platStr, "win7-x64") == 0 ||
                          strcmp(platStr, "win8-x64") == 0 ||
                          strcmp(platStr, "win81-x64") == 0 ||
                          strcmp(platStr, "linux-x64") == 0);
    assert(isExpectedWin);

    const char* ver = SPM_GetCoreVersion();
    printf("  Core Version: %s\n", ver);
    assert(strcmp(ver, "0.6.1") == 0);

    printf("[PASS] test_spm_platform passed successfully.\n");
    return 0;
}
