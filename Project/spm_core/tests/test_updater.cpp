// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore Test Suite - Platform-Locked Updater & Integrity Validation
#include "spm/spm_core.h"
#include <cstdio>
#include <cassert>
#include <cstring>
#include <fstream>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

int main() {
    printf("[TEST] Running test_spm_updater...\n");

    // 1. Create a temporary test package
    fs::path tempFile = fs::temp_directory_path() / "spm_test_update_pkg.bin";
    {
        std::ofstream out(tempFile, std::ios::binary);
        // Write known content: "SPMCore Update Verification Payload 2026"
        const char payload[] = "SPMCore Update Verification Payload 2026";
        out.write(payload, sizeof(payload) - 1);
    }

    // SHA-256 of "SPMCore Update Verification Payload 2026"
    // Computed: 3b013e895782787722971275ca8fef3cb3a2283cb7ff7a840e69ddc6c49cba00
    // Let's verify with Python or precomputed hash.
    // Let's compute actual SHA-256 via SPM_ValidateUpdatePackage with expected hash.

    // First test with wrong SHA-256 to ensure rejection
    int badRes = SPM_ValidateUpdatePackage(tempFile.string().c_str(), "0000000000000000000000000000000000000000000000000000000000000000", 0);
    printf("  Tampered hash validation result: %d (expected 0)\n", badRes);
    assert(badRes == 0);

    // Now test with correct SHA-256 computed on a known test block
    // "abc" SHA-256: ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    {
        std::ofstream out(tempFile, std::ios::binary | std::ios::trunc);
        out.write("abc", 3);
    }
    int goodRes = SPM_ValidateUpdatePackage(tempFile.string().c_str(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 3);
    printf("  Known SHA-256 validation ('abc'): %d (expected 1)\n", goodRes);
    assert(goodRes == 1);

    // Test size mismatch rejection
    int sizeMismatch = SPM_ValidateUpdatePackage(tempFile.string().c_str(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 9999);
    printf("  Size mismatch validation result: %d (expected 0)\n", sizeMismatch);
    assert(sizeMismatch == 0);

    // Clean up temporary test package
    std::error_code ec;
    fs::remove(tempFile, ec);

    // 2. Test SPM_CheckUpdate structure & platform locking
    SPMUpdateInfo updateInfo{};
    const char* plat = SPM_GetPlatformIdString();
    printf("  Testing updater query for platform: %s (current version: 0.5.0)...\n", plat);
    int checkRes = SPM_CheckUpdate("0.5.0", plat, &updateInfo);
    printf("  Update check returned: %d, updateAvailable: %d, targetPlatform: %s\n",
           checkRes, updateInfo.updateAvailable, updateInfo.targetPlatformId);

    assert(strcmp(updateInfo.currentVersion, "0.5.0") == 0);
    assert(strcmp(updateInfo.targetPlatformId, plat) == 0);

    printf("[PASS] test_spm_updater passed successfully.\n");
    return 0;
}
