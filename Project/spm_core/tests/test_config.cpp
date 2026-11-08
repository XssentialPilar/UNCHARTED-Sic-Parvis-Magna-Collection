// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore Test Suite - Configuration & INI Sync Validation
#include "spm/spm_core.h"
#include <cstdio>
#include <cassert>
#include <cstring>
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

int main() {
    printf("[TEST] Running test_spm_config...\n");

    // 1. Validate Default Configuration
    SPMConfig defaultCfg{};
    SPM_GetDefaultConfig(&defaultCfg);

    // CRITICAL USER MANDATE: Default GameDir MUST be empty!
    printf("  Default GameDir: '%s' (MUST BE EMPTY)\n", defaultCfg.gameDir);
    assert(defaultCfg.gameDir[0] == '\0');
    assert(strcmp(defaultCfg.graphicsApi, "dx11") == 0);
    assert(strcmp(defaultCfg.vramBudgetMb, "0") == 0);
    assert(strcmp(defaultCfg.fpsLimit, "60") == 0);
    assert(strcmp(defaultCfg.framePacingMode, "hybrid") == 0);
    assert(defaultCfg.vibration == 1);

    // 2. Test Save & Load
    fs::path tempConfig = fs::temp_directory_path() / "spm_test_launcher.json";

    SPMConfig saveCfg = defaultCfg;
    strncpy(saveCfg.language, "es_419", sizeof(saveCfg.language) - 1);
    strncpy(saveCfg.resolution, "2560x1440", sizeof(saveCfg.resolution) - 1);
    strncpy(saveCfg.antiAliasing, "dlaa", sizeof(saveCfg.antiAliasing) - 1);
    strncpy(saveCfg.frameGeneration, "dlss3x", sizeof(saveCfg.frameGeneration) - 1);
    strncpy(saveCfg.reflexMode, "boost", sizeof(saveCfg.reflexMode) - 1);
    strncpy(saveCfg.streamingBudgetMb, "2048", sizeof(saveCfg.streamingBudgetMb) - 1);
    strncpy(saveCfg.dynamicResolution, "1", sizeof(saveCfg.dynamicResolution) - 1);
    strncpy(saveCfg.neuralAssistMode, "enabled", sizeof(saveCfg.neuralAssistMode) - 1);
    strncpy(saveCfg.storageOptimization, "nvme", sizeof(saveCfg.storageOptimization) - 1);
    saveCfg.sharpening = 0.65;
    saveCfg.mouseSensitivity = 2.45;
    saveCfg.mouseInvertY = 1;

    int saveRes = SPM_SaveConfig(tempConfig.string().c_str(), &saveCfg);
    printf("  SPM_SaveConfig result: %d\n", saveRes);
    assert(saveRes == 1);
    assert(fs::exists(tempConfig));

    SPMConfig loadCfg{};
    int loadRes = SPM_LoadConfig(tempConfig.string().c_str(), &loadCfg);
    printf("  SPM_LoadConfig result: %d\n", loadRes);
    assert(loadRes == 1);
    assert(strcmp(loadCfg.language, "es_419") == 0);
    assert(strcmp(loadCfg.resolution, "2560x1440") == 0);
    assert(strcmp(loadCfg.antiAliasing, "dlaa") == 0);
    assert(strcmp(loadCfg.frameGeneration, "dlss3x") == 0);
    assert(strcmp(loadCfg.reflexMode, "boost") == 0);
    assert(strcmp(loadCfg.streamingBudgetMb, "2048") == 0);
    assert(strcmp(loadCfg.dynamicResolution, "1") == 0);
    assert(strcmp(loadCfg.neuralAssistMode, "enabled") == 0);
    assert(strcmp(loadCfg.storageOptimization, "nvme") == 0);
    assert(loadCfg.sharpening == 0.65);
    assert(loadCfg.mouseSensitivity == 2.45);
    assert(loadCfg.mouseInvertY == 1);
    assert(loadCfg.gameDir[0] == '\0'); // Preserved empty

    // 3. Test INI Synchronization
    fs::path tempWs = fs::temp_directory_path() / "spm_test_ws";
    std::error_code ec;
    fs::create_directories(tempWs, ec);

    int syncRes = SPM_SyncConfigToIni(tempWs.string().c_str(), &loadCfg);
    printf("  SPM_SyncConfigToIni result: %d\n", syncRes);
    assert(syncRes == 1);

    fs::path probeIni = tempWs / "analysis" / "uncharted-user" / "probe.ini";
    fs::path probeU1Ini = tempWs / "analysis" / "uncharted-user" / "probe-u1.ini";
    assert(fs::exists(probeIni));
    assert(fs::exists(probeU1Ini));

    // Verify probe.ini contents
    std::ifstream iniFile(probeIni);
    std::string line;
    bool foundApi = false, foundRes = false, foundNeural = false;
    while (std::getline(iniFile, line)) {
        if (line == "graphics_api=dx11") foundApi = true;
        if (line == "resolution=2560x1440") foundRes = true;
        if (line == "neural_assist_mode=enabled") foundNeural = true;
    }
    assert(foundApi);
    assert(foundRes);
    assert(foundNeural);

    // 4. Test Game Directory Validation
    SPMValidationResult valRes{};
    int emptyDirVal = SPM_ValidateGameFiles("", &valRes);
    assert(emptyDirVal == 0);
    assert(valRes.isValid == 0);

    // Create mock valid game directory
    fs::path mockGameDir = tempWs / "CUSA02344";
    fs::create_directories(mockGameDir, ec);
    {
        std::ofstream mockEboot(mockGameDir / "eboot.bin");
        mockEboot << "MOCK_EBOOT";
    }

    int mockVal = SPM_ValidateGameFiles(mockGameDir.string().c_str(), &valRes);
    printf("  Mock game validation result: %d, isValid: %d\n", mockVal, valRes.isValid);
    assert(mockVal == 1);
    assert(valRes.isValid == 1);

    // Clean up
    fs::remove_all(tempWs, ec);
    fs::remove(tempConfig, ec);

    printf("[PASS] test_spm_config passed successfully.\n");
    return 0;
}
