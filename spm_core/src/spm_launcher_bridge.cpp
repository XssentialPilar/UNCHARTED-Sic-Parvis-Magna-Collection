// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Launcher Bridge, Game File Validation & Process Control Module
#include "spm/spm_core.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <vector>

namespace fs = std::filesystem;

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <psapi.h>
#else
#include <unistd.h>
#include <sys/types.h>
#endif

int SPM_ValidateGameFiles(const char* gameDir, SPMValidationResult* outRes) {
    if (!outRes) return 0;
    memset(outRes, 0, sizeof(*outRes));

    if (!gameDir || gameDir[0] == '\0') {
        outRes->isValid = 0;
        return 0;
    }

    std::error_code ec;
    fs::path dir(gameDir);
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
        outRes->isValid = 0;
        return 0;
    }

    // List of candidate files that indicate a valid Uncharted dump
    const char* candidates[] = {
        "eboot.bin",
        "uroot/eboot.bin",
        "big1-ps4_Shipping.elf",
        "big2-ps4_Shipping.elf",
        "big3-ps4_Shipping.elf",
        "sce_sys/param.sfo"
    };

    uint32_t checked = 0;
    bool foundExecutable = false;

    for (const char* rel : candidates) {
        checked++;
        if (fs::exists(dir / rel, ec)) {
            foundExecutable = true;
        }
    }

    outRes->totalFilesChecked = checked;
    outRes->missingFilesCount = foundExecutable ? 0 : checked;
    outRes->isValid = foundExecutable ? 1 : 0;

    return outRes->isValid;
}

int SPM_PrepareEnvironment(const SPMConfig* config, const char* workspaceRoot) {
    if (!config || !workspaceRoot) return 0;

#ifdef _WIN32
    fs::path ws(workspaceRoot);
    fs::path coreDir = fs::exists(ws / "core") ? (ws / "core") : ws;
    fs::path scriptsDir = fs::exists(coreDir / "gow3_pc" / "scripts")
        ? (coreDir / "gow3_pc" / "scripts")
        : (ws / "gow3_pc" / "scripts");

    // Configure PATH
    std::string currentPath = "";
    char pathBuf[8192];
    DWORD pathLen = GetEnvironmentVariableA("PATH", pathBuf, sizeof(pathBuf));
    if (pathLen > 0) currentPath = pathBuf;

    std::string newPath = coreDir.string() + ";" + (coreDir / "python").string() + ";" + currentPath;
    SetEnvironmentVariableA("PATH", newPath.c_str());

    // Configure PYTHONPATH
    std::string currentPyPath = "";
    DWORD pyLen = GetEnvironmentVariableA("PYTHONPATH", pathBuf, sizeof(pathBuf));
    if (pyLen > 0) currentPyPath = pathBuf;

    std::string newPyPath = scriptsDir.string() + ";" + (ws / "gow3_pc" / "scripts").string() + ";" + currentPyPath;
    SetEnvironmentVariableA("PYTHONPATH", newPyPath.c_str());
    SetEnvironmentVariableA("PYTHONUNBUFFERED", "1");

    // Graphics & Game Engine environment overrides
    SetEnvironmentVariableA("GOW3_GRAPHICS_API", config->graphicsApi);
    SetEnvironmentVariableA("GOW3_VRAM_BUDGET_MB", config->vramBudgetMb);
    SetEnvironmentVariableA("GOW3_FRAME_PACER", config->framePacingMode);
    SetEnvironmentVariableA("GOW3_FPS_LIMIT", config->fpsLimit);
    SetEnvironmentVariableA("GOW3_VBLANK_HZ", (strcmp(config->fpsLimit, "60") == 0) ? "60" : "0");
    SetEnvironmentVariableA("GOW3_VSYNC", config->vsync);
    SetEnvironmentVariableA("GOW3_TEXTURE_QUALITY", config->textureQuality);
    SetEnvironmentVariableA("GOW3_SHADOW_QUALITY", config->shadowQuality);
    SetEnvironmentVariableA("GOW3_SSAO_QUALITY", config->ssaoQuality);
    SetEnvironmentVariableA("UNCHARTED_ANISOTROPY", config->anisotropy);
    SetEnvironmentVariableA("UNCHARTED_RESOLUTION", config->resolution);
    if (config->outputResolution[0] != '\0') {
        SetEnvironmentVariableA("UNCHARTED_OUTPUT_RES", config->outputResolution);
    }
    if (config->selectedGpuId[0] != '\0' && strcmp(config->selectedGpuId, "-1") != 0) {
        SetEnvironmentVariableA("GOW3_GPU_ID", config->selectedGpuId);
        SetEnvironmentVariableA("VK_DEVICE_INDEX", config->selectedGpuId);
    }
    if (config->selectedGpuName[0] != '\0') {
        SetEnvironmentVariableA("GOW3_GPU_NAME", config->selectedGpuName);
        SetEnvironmentVariableA("GOW3_GPU", config->selectedGpuName);
    }
#else
    fs::path ws(workspaceRoot);
    fs::path coreDir = fs::exists(ws / "core") ? (ws / "core") : ws;
    fs::path scriptsDir = fs::exists(coreDir / "gow3_pc" / "scripts")
        ? (coreDir / "gow3_pc" / "scripts")
        : (ws / "gow3_pc" / "scripts");

    const char* curPath = getenv("PATH");
    std::string newPath = coreDir.string() + ":" + (coreDir / "python").string() + (curPath ? (std::string(":") + curPath) : "");
    setenv("PATH", newPath.c_str(), 1);

    const char* curPyPath = getenv("PYTHONPATH");
    std::string newPyPath = scriptsDir.string() + ":" + (ws / "gow3_pc" / "scripts").string() + (curPyPath ? (std::string(":") + curPyPath) : "");
    setenv("PYTHONPATH", newPyPath.c_str(), 1);
    setenv("PYTHONUNBUFFERED", "1", 1);

    setenv("GOW3_GRAPHICS_API", config->graphicsApi, 1);
    setenv("GOW3_VRAM_BUDGET_MB", config->vramBudgetMb, 1);
    setenv("GOW3_FRAME_PACER", config->framePacingMode, 1);
    setenv("GOW3_FPS_LIMIT", config->fpsLimit, 1);
    setenv("GOW3_VBLANK_HZ", (strcmp(config->fpsLimit, "60") == 0) ? "60" : "0", 1);
    setenv("GOW3_VSYNC", config->vsync, 1);
    setenv("GOW3_TEXTURE_QUALITY", config->textureQuality, 1);
    setenv("GOW3_SHADOW_QUALITY", config->shadowQuality, 1);
    setenv("GOW3_SSAO_QUALITY", config->ssaoQuality, 1);
    setenv("UNCHARTED_ANISOTROPY", config->anisotropy, 1);
    setenv("UNCHARTED_RESOLUTION", config->resolution, 1);
    if (config->outputResolution[0] != '\0') {
        setenv("UNCHARTED_OUTPUT_RES", config->outputResolution, 1);
    }
    if (config->selectedGpuId[0] != '\0' && strcmp(config->selectedGpuId, "-1") != 0) {
        setenv("GOW3_GPU_ID", config->selectedGpuId, 1);
        setenv("VK_DEVICE_INDEX", config->selectedGpuId, 1);
    }
    if (config->selectedGpuName[0] != '\0') {
        setenv("GOW3_GPU_NAME", config->selectedGpuName, 1);
        setenv("GOW3_GPU", config->selectedGpuName, 1);
    }
#endif

    return 1;
}

namespace {

#ifdef _WIN32
std::mutex g_timerMutex;
int g_timerRefs = 0;
std::once_flag g_timerExitOnce;

void RestoreTimerResolutionAtExit() {
    std::lock_guard<std::mutex> lock(g_timerMutex);
    if (g_timerRefs > 0) {
        timeEndPeriod(1);
        g_timerRefs = 0;
    }
}
#endif

} // namespace

void SPM_InitTimerResolution(void) {
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(g_timerMutex);
    if (g_timerRefs == 0) {
        if (timeBeginPeriod(1) != TIMERR_NOERROR) {
            return;
        }
    }
    ++g_timerRefs;
    // std::atexit runs after static destructors, outside the loader lock.
    std::call_once(g_timerExitOnce, []() { std::atexit(RestoreTimerResolutionAtExit); });
#endif
}

void SPM_ShutdownTimerResolution(void) {
#ifdef _WIN32
    std::lock_guard<std::mutex> lock(g_timerMutex);
    if (g_timerRefs > 0 && --g_timerRefs == 0) {
        timeEndPeriod(1);
    }
#endif
}

void SPM_AllowSetForegroundWindow(uint32_t processId) {
#ifdef _WIN32
    AllowSetForegroundWindow(processId == 0 ? ASFW_ANY : processId);
#else
    (void)processId;
#endif
}

int SPM_GetProcessMemory(uint32_t processId, uint64_t* outWorkingSet, uint64_t* outCommit) {
    if (outWorkingSet) *outWorkingSet = 0;
    if (outCommit) *outCommit = 0;

#ifdef _WIN32
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, processId);
    if (!hProcess) return 0;

    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        if (outWorkingSet) *outWorkingSet = pmc.WorkingSetSize;
        if (outCommit) *outCommit = pmc.PrivateUsage;
        CloseHandle(hProcess);
        return 1;
    }

    CloseHandle(hProcess);
    return 0;
#else
    char statmPath[64];
    snprintf(statmPath, sizeof(statmPath), "/proc/%u/statm", (unsigned int)processId);
    FILE* f = fopen(statmPath, "r");
    if (!f) return 0;
    unsigned long size = 0, resident = 0, share = 0;
    if (fscanf(f, "%lu %lu %lu", &size, &resident, &share) >= 2) {
        long pageSize = sysconf(_SC_PAGESIZE);
        if (pageSize <= 0) pageSize = 4096;
        if (outWorkingSet) *outWorkingSet = (uint64_t)resident * (uint64_t)pageSize;
        if (outCommit) *outCommit = (uint64_t)(size > share ? size - share : size) * (uint64_t)pageSize;
        fclose(f);
        return 1;
    }
    fclose(f);
    return 0;
#endif
}
