// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection - High-Performance Native Launcher Bridge (launcher_native.dll)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_4.h>
#include <psapi.h>
#include <timeapi.h>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "winmm.lib")

extern "C" {

__declspec(dllexport) void Launcher_InitTimerResolution() {
    timeBeginPeriod(1);
}

__declspec(dllexport) void Launcher_AllowSetForegroundWindow(DWORD processId) {
    AllowSetForegroundWindow(processId ? processId : ASFW_ANY);
}

__declspec(dllexport) int Launcher_DetectGpus(char* outBuffer, int maxLen) {
    if (!outBuffer || maxLen <= 0) return 0;
    outBuffer[0] = '\0';

    typedef HRESULT (WINAPI *CreateDXGIFactory1Fn)(REFIID, void**);
    HMODULE hDxgi = LoadLibraryW(L"dxgi.dll");
    if (!hDxgi) return 0;

    CreateDXGIFactory1Fn createFactory1 = (CreateDXGIFactory1Fn)GetProcAddress(hDxgi, "CreateDXGIFactory1");
    if (!createFactory1) {
        FreeLibrary(hDxgi);
        return 0;
    }

    IDXGIFactory1* factory = nullptr;
    if (FAILED(createFactory1(__uuidof(IDXGIFactory1), (void**)&factory)) || !factory) {
        FreeLibrary(hDxgi);
        return 0;
    }

    int count = 0;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc))) {
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
                adapter->Release();
                continue;
            }
            char name[128] = {0};
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name) - 1, nullptr, nullptr);
            unsigned long long vramMb = desc.DedicatedVideoMemory / (1024 * 1024);
            unsigned long long sharedMb = desc.SharedSystemMemory / (1024 * 1024);

            char line[256];
            snprintf(line, sizeof(line), "%s|%llu|%llu|%08X-%08X\n", name, vramMb, sharedMb, desc.AdapterLuid.HighPart, desc.AdapterLuid.LowPart);
            if ((int)strlen(outBuffer) + (int)strlen(line) < maxLen - 1) {
                strcat(outBuffer, line);
                count++;
            }
        }
        adapter->Release();
    }
    factory->Release();
    FreeLibrary(hDxgi);
    return count;
}

__declspec(dllexport) int Launcher_GetProcessMemory(DWORD processId, unsigned long long* outWorkingSet, unsigned long long* outCommit) {
    if (!processId) return 0;
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!hProcess) return 0;

    PROCESS_MEMORY_COUNTERS_EX pmc{};
    pmc.cb = sizeof(pmc);
    int ok = 0;
    if (GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        if (outWorkingSet) *outWorkingSet = pmc.WorkingSetSize;
        if (outCommit) *outCommit = pmc.PrivateUsage;
        ok = 1;
    }
    CloseHandle(hProcess);
    return ok;
}

}
