// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Hardware Capabilities & Storage Classification Module
#include "spm/spm_hardware_caps.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <string>
#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <dxgi.h>

#if defined(_MSC_VER) || defined(__clang__)
#include <intrin.h>
#elif defined(__GNUC__)
#include <cpuid.h>
#endif

typedef HRESULT (WINAPI *CreateDXGIFactory1Fn)(REFIID, void**);

// BusTypeNvme is 17 in Windows 10 SDK winioctl.h. Define safely if missing.
#ifndef BusTypeNvme
#define BusTypeNvme 17
#endif
#ifndef StorageDeviceSeekPenaltyProperty
#define StorageDeviceSeekPenaltyProperty 7
#endif

typedef struct _LOCAL_DEVICE_SEEK_PENALTY_DESCRIPTOR {
    DWORD   Version;
    DWORD   Size;
    BOOLEAN IncursSeekPenalty;
} LOCAL_DEVICE_SEEK_PENALTY_DESCRIPTOR;

static void QueryCpuSimd(SpmHardwareCapabilities* caps) {
#if defined(_MSC_VER) || defined(__clang__)
    int cpuInfo[4] = {0};
    __cpuid(cpuInfo, 0);
    int nIds = cpuInfo[0];

    if (nIds >= 1) {
        __cpuidex(cpuInfo, 1, 0);
        // ECX bit 12: FMA
        caps->cpuHasFma = (cpuInfo[2] & (1 << 12)) ? 1 : 0;
    }
    if (nIds >= 7) {
        __cpuidex(cpuInfo, 7, 0);
        // EBX bit 5: AVX2
        caps->cpuHasAvx2 = (cpuInfo[1] & (1 << 5)) ? 1 : 0;
        // EBX bit 16: AVX512F
        caps->cpuHasAvx512 = (cpuInfo[1] & (1 << 16)) ? 1 : 0;
    }
#elif defined(__GNUC__)
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        caps->cpuHasFma = (ecx & (1 << 12)) ? 1 : 0;
    }
    unsigned int count = 0;
    if (__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
        caps->cpuHasAvx2 = (ebx & (1 << 5)) ? 1 : 0;
        caps->cpuHasAvx512 = (ebx & (1 << 16)) ? 1 : 0;
    }
#endif
}

static void QueryStorage(const char* targetPath, SpmHardwareCapabilities* caps) {
    caps->storageType = SPM_STORAGE_UNKNOWN;
    caps->storageEstimatedBandwidthMBps = 500.0f;
    caps->storageDirectStorageSupported = 0;

    char fullPath[MAX_PATH] = {0};
    if (targetPath && targetPath[0] != '\0') {
        GetFullPathNameA(targetPath, MAX_PATH, fullPath, nullptr);
    } else {
        GetFullPathNameA(".", MAX_PATH, fullPath, nullptr);
    }

    char driveLetter = 'C';
    if (isalpha(fullPath[0]) && fullPath[1] == ':') {
        driveLetter = (char)toupper(fullPath[0]);
    }
    snprintf(caps->storageDriveLetter, sizeof(caps->storageDriveLetter), "%c:", driveLetter);

    char volumeDevice[32];
    snprintf(volumeDevice, sizeof(volumeDevice), "\\\\.\\%c:", driveLetter);

    HANDLE hDevice = CreateFileA(volumeDevice, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 nullptr, OPEN_EXISTING, 0, nullptr);
    if (hDevice == INVALID_HANDLE_VALUE) {
        // Fallback default: assume SATA SSD for modern PCs
        caps->storageType = SPM_STORAGE_SATA_SSD;
        caps->storageEstimatedBandwidthMBps = 550.0f;
        return;
    }

    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;

    BYTE buffer[1024] = {0};
    DWORD bytesReturned = 0;

    if (DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY,
                        &query, sizeof(query),
                        buffer, sizeof(buffer),
                        &bytesReturned, nullptr)) {
        STORAGE_DEVICE_DESCRIPTOR* desc = (STORAGE_DEVICE_DESCRIPTOR*)buffer;
        if (desc->BusType == BusTypeNvme) {
            caps->storageType = SPM_STORAGE_NVME_SSD;
            caps->storageEstimatedBandwidthMBps = 5000.0f;
            caps->storageDirectStorageSupported = 1;
        } else {
            // Check seek penalty to differentiate SATA SSD vs mechanical HDD
            STORAGE_PROPERTY_QUERY seekQuery{};
            seekQuery.PropertyId = (STORAGE_PROPERTY_ID)StorageDeviceSeekPenaltyProperty;
            seekQuery.QueryType = PropertyStandardQuery;

            LOCAL_DEVICE_SEEK_PENALTY_DESCRIPTOR seekDesc{};
            DWORD seekBytes = 0;
            if (DeviceIoControl(hDevice, IOCTL_STORAGE_QUERY_PROPERTY,
                                &seekQuery, sizeof(seekQuery),
                                &seekDesc, sizeof(seekDesc),
                                &seekBytes, nullptr)) {
                if (seekDesc.IncursSeekPenalty) {
                    caps->storageType = SPM_STORAGE_HDD;
                    caps->storageEstimatedBandwidthMBps = 140.0f;
                    caps->storageDirectStorageSupported = 0;
                } else {
                    caps->storageType = SPM_STORAGE_SATA_SSD;
                    caps->storageEstimatedBandwidthMBps = 550.0f;
                    caps->storageDirectStorageSupported = 0;
                }
            } else {
                caps->storageType = SPM_STORAGE_SATA_SSD;
                caps->storageEstimatedBandwidthMBps = 550.0f;
            }
        }
    } else {
        caps->storageType = SPM_STORAGE_SATA_SSD;
        caps->storageEstimatedBandwidthMBps = 550.0f;
    }

    CloseHandle(hDevice);
}

static void QueryNpuAndAccelerators(SpmHardwareCapabilities* caps) {
    caps->npuVendor = SPM_NPU_NONE;
    caps->npuSupported = 0;
    caps->directMlSupported = 0;
    caps->onnxRuntimeSupported = 0;
    caps->npuName[0] = '\0';
    caps->npuDriverVersion[0] = '\0';

    // 1. Check DXGI adapters for dedicated NPU/MCDM/VPU accelerators
    HMODULE hDxgi = LoadLibraryW(L"dxgi.dll");
    if (hDxgi) {
        CreateDXGIFactory1Fn createFactory1 = (CreateDXGIFactory1Fn)GetProcAddress(hDxgi, "CreateDXGIFactory1");
        if (createFactory1) {
            IDXGIFactory1* factory = nullptr;
            if (SUCCEEDED(createFactory1(__uuidof(IDXGIFactory1), (void**)&factory)) && factory) {
                IDXGIAdapter1* adapter = nullptr;
                for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
                    DXGI_ADAPTER_DESC1 desc{};
                    if (SUCCEEDED(adapter->GetDesc1(&desc))) {
                        char name[128] = {0};
                        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name) - 1, nullptr, nullptr);

                        std::string lowerName = name;
                        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                                       [](unsigned char c){ return (char)std::tolower(c); });

                        if (lowerName.find("ryzen ai") != std::string::npos ||
                            lowerName.find("xdna") != std::string::npos ||
                            (lowerName.find("npu") != std::string::npos && desc.VendorId == 0x1002)) {
                            caps->npuVendor = SPM_NPU_AMD_RYZEN_AI;
                            caps->npuSupported = 1;
                            snprintf(caps->npuName, sizeof(caps->npuName), "%s", name);
                            adapter->Release();
                            break;
                        } else if (lowerName.find("ai boost") != std::string::npos ||
                                   lowerName.find("intel npu") != std::string::npos ||
                                   lowerName.find("vpu") != std::string::npos ||
                                   (lowerName.find("npu") != std::string::npos && desc.VendorId == 0x8086)) {
                            caps->npuVendor = SPM_NPU_INTEL_AI_BOOST;
                            caps->npuSupported = 1;
                            snprintf(caps->npuName, sizeof(caps->npuName), "%s", name);
                            adapter->Release();
                            break;
                        } else if (lowerName.find("hexagon") != std::string::npos ||
                                   lowerName.find("snapdragon") != std::string::npos ||
                                   lowerName.find("qnn") != std::string::npos) {
                            caps->npuVendor = SPM_NPU_QUALCOMM_HEXAGON;
                            caps->npuSupported = 1;
                            snprintf(caps->npuName, sizeof(caps->npuName), "%s", name);
                            adapter->Release();
                            break;
                        } else if (lowerName.find("npu") != std::string::npos ||
                                   lowerName.find("neural") != std::string::npos) {
                            caps->npuVendor = SPM_NPU_GENERIC_MCDM;
                            caps->npuSupported = 1;
                            snprintf(caps->npuName, sizeof(caps->npuName), "%s", name);
                            adapter->Release();
                            break;
                        }
                    }
                    adapter->Release();
                }
                factory->Release();
            }
        }
        FreeLibrary(hDxgi);
    }

    // 2. Safe dynamic probe for DirectML runtime
    HMODULE hDml = LoadLibraryW(L"directml.dll");
    if (hDml) {
        caps->directMlSupported = 1;
        FreeLibrary(hDml);
    }

    // 3. Safe dynamic probe for ONNX Runtime
    HMODULE hOrt = LoadLibraryW(L"onnxruntime.dll");
    if (hOrt) {
        caps->onnxRuntimeSupported = 1;
        FreeLibrary(hOrt);
    }

    // If DirectML is available and no specific NPU hardware string matched,
    // DirectML can execute NPU/GPU accelerated operators via MCDM / Direct3D 12
    if (!caps->npuSupported && (caps->directMlSupported || caps->onnxRuntimeSupported)) {
        caps->npuVendor = SPM_NPU_GENERIC_MCDM;
        caps->npuSupported = 1;
        snprintf(caps->npuName, sizeof(caps->npuName), "DirectML / MCDM Compute Engine");
    }
}

#endif // _WIN32

extern "C" {

SPM_CAPS_API int SPM_DetectHardwareCapabilities(const char* targetPath, SpmHardwareCapabilities* outCaps) {
    if (!outCaps) return 0;
    memset(outCaps, 0, sizeof(*outCaps));

#ifdef _WIN32
    // CPU detection
    SYSTEM_INFO sysInfo{};
    GetSystemInfo(&sysInfo);
    outCaps->cpuLogicalCores = sysInfo.dwNumberOfProcessors;
    outCaps->cpuPhysicalCores = (sysInfo.dwNumberOfProcessors > 1) ? (sysInfo.dwNumberOfProcessors / 2) : 1;

    QueryCpuSimd(outCaps);

    // RAM detection
    MEMORYSTATUSEX memStatus{};
    memStatus.dwLength = sizeof(memStatus);
    if (GlobalMemoryStatusEx(&memStatus)) {
        outCaps->totalPhysicalRamMb = memStatus.ullTotalPhys / (1024ULL * 1024ULL);
        outCaps->availableRamMb = memStatus.ullAvailPhys / (1024ULL * 1024ULL);
    }

    // Storage detection
    QueryStorage(targetPath, outCaps);

    // NPU / AI accelerator detection
    QueryNpuAndAccelerators(outCaps);
#else
    // POSIX / Linux Fallback
    outCaps->cpuLogicalCores = 4;
    outCaps->cpuPhysicalCores = 4;
    outCaps->cpuHasAvx2 = 1;
    outCaps->totalPhysicalRamMb = 16384;
    outCaps->availableRamMb = 8192;
    outCaps->storageType = SPM_STORAGE_NVME_SSD;
    outCaps->storageEstimatedBandwidthMBps = 3500.0f;
    outCaps->storageDirectStorageSupported = 0;
    snprintf(outCaps->storageDriveLetter, sizeof(outCaps->storageDriveLetter), "/");
    outCaps->npuVendor = SPM_NPU_NONE;
#endif

    return 1;
}

SPM_CAPS_API const char* SPM_GetStorageTypeName(SpmStorageType type) {
    switch (type) {
        case SPM_STORAGE_NVME_SSD: return "NVMe SSD";
        case SPM_STORAGE_SATA_SSD: return "SATA SSD";
        case SPM_STORAGE_HDD:      return "HDD";
        default:                   return "Unknown";
    }
}

SPM_CAPS_API const char* SPM_GetNpuVendorName(SpmNpuVendor vendor) {
    switch (vendor) {
        case SPM_NPU_AMD_RYZEN_AI:     return "AMD Ryzen AI";
        case SPM_NPU_INTEL_AI_BOOST:   return "Intel AI Boost";
        case SPM_NPU_QUALCOMM_HEXAGON: return "Qualcomm Hexagon";
        case SPM_NPU_GENERIC_MCDM:     return "Generic MCDM";
        default:                       return "None";
    }
}

} // extern "C"
