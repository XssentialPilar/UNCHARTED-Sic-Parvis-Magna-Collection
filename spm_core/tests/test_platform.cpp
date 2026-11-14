// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore Test Suite - Platform Detection
#include "spm/spm_core.h"
#include <cstdio>
#include "test_check.h"
#include <cstring>

int main() {
    printf("[TEST] Running test_spm_platform...\n");

    SPMPlatformId platId = SPM_GetPlatformId();
    printf("  Detected Platform ID: %d\n", (int)platId);
    REQUIRE(platId != SPM_PLATFORM_UNKNOWN);

    const char* platStr = SPM_GetPlatformIdString();
    printf("  Platform ID String: %s\n", platStr);
    REQUIRE(platStr != nullptr);
    REQUIRE(strlen(platStr) > 0);

    // On Windows 64-bit modern host, expect win10-x64 or win11-x64
    [[maybe_unused]] bool isExpectedWin = (strcmp(platStr, "win10-x64") == 0 ||
                          strcmp(platStr, "win11-x64") == 0 ||
                          strcmp(platStr, "win7-x64") == 0 ||
                          strcmp(platStr, "win8-x64") == 0 ||
                          strcmp(platStr, "win81-x64") == 0 ||
                          strcmp(platStr, "linux-x64") == 0);
    REQUIRE(isExpectedWin);

    const char* ver = SPM_GetCoreVersion();
    printf("  Core Version: %s\n", ver);
    REQUIRE(strcmp(ver, "0.6.1") == 0);

    printf("[PASS] test_spm_platform passed successfully.\n");
    return spm_test::Finish("test_spm_platform");
}
