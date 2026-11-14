// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Hardware Capabilities & Storage Classification Module
//
// Every value returned here comes from an OS API or from the hardware itself:
//   * Physical cores: GetLogicalProcessorInformationEx / sysfs topology.
//   * SIMD: CPUID plus the OS XSAVE state (AVX/AVX-512 need OS support).
//   * Storage: the device backing the volume (bus type + seek penalty / sysfs).
//   * NPU: devices enumerated in the Compute Accelerator class (Windows) or the
//     accel subsystem (Linux). The presence of directml.dll/onnxruntime.dll is
//     reported as software availability only and never as NPU hardware.
#include "spm/spm_hardware_caps.h"
#include "spm_internal.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <devguid.h>

#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__GNUC__)
#include <cpuid.h>
#endif

namespace {

uint64_t ReadXcr0() {
#if defined(_MSC_VER)
    return _xgetbv(0);
#elif defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
    uint32_t eax = 0;
    uint32_t edx = 0;
    __asm__ volatile(".byte 0x0f, 0x01, 0xd0" : "=a"(eax), "=d"(edx) : "c"(0));
    return (static_cast<uint64_t>(edx) << 32) | eax;
#else
    return 0;
#endif
}

struct CpuFeatures {
    bool avx2{false};
    bool avx512{false};
    bool fma{false};
};

CpuFeatures QueryCpuFeatures() {
    CpuFeatures features;

#if defined(_MSC_VER)
    int regs[4] = {0, 0, 0, 0};
    __cpuid(regs, 0);
    const int maxLeaf = regs[0];
    if (maxLeaf >= 1) {
        __cpuidex(regs, 1, 0);
        const bool osxsave = (regs[2] & (1 << 27)) != 0;
        const bool avx = (regs[2] & (1 << 28)) != 0;
        features.fma = (regs[2] & (1 << 12)) != 0;
        if (osxsave && avx && (ReadXcr0() & 0x6) == 0x6) {
            if (maxLeaf >= 7) {
                __cpuidex(regs, 7, 0);
                features.avx2 = (regs[1] & (1 << 5)) != 0;
                const bool avx512f = (regs[1] & (1 << 16)) != 0;
                const bool osAvx512 = (ReadXcr0() & 0xE6) == 0xE6;
                features.avx512 = avx512f && osAvx512;
            }
        }
    }
#elif defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    unsigned int maxLeaf = 0;
    if (__get_cpuid(0, &eax, &ebx, &ecx, &edx)) {
        maxLeaf = eax;
    }
    if (maxLeaf >= 1 && __get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        const bool osxsave = (ecx & (1u << 27)) != 0;
        const bool avx = (ecx & (1u << 28)) != 0;
        features.fma = (ecx & (1u << 12)) != 0;
        if (osxsave && avx && (ReadXcr0() & 0x6) == 0x6 && maxLeaf >= 7 &&
            __get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) {
            features.avx2 = (ebx & (1u << 5)) != 0;
            const bool avx512f = (ebx & (1u << 16)) != 0;
            const bool osAvx512 = (ReadXcr0() & 0xE6) == 0xE6;
            features.avx512 = avx512f && osAvx512;
        }
    }
#endif

    return features;
}

std::string ToUtf8(const wchar_t* text) {
    if (!text || !*text) {
        return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) {
        return {};
    }
    std::string out(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), needed, nullptr, nullptr);
    out.resize(static_cast<size_t>(needed - 1));
    return out;
}

void CopyString(char* destination, size_t capacity, const std::string& source) {
    if (!destination || capacity == 0) {
        return;
    }
    const size_t copied = std::min(source.size(), capacity - 1);
    std::memcpy(destination, source.data(), copied);
    destination[copied] = '\0';
}

// ---------------------------------------------------------------------------
// Storage
// ---------------------------------------------------------------------------

float EstimatedBandwidth(SpmStorageType type) {
    switch (type) {
        case SPM_STORAGE_NVME_SSD: return 2000.0f; // lower bound of a PCIe 3.0 x4 device
        case SPM_STORAGE_SATA_SSD: return 400.0f;  // lower bound of a SATA III device
        case SPM_STORAGE_HDD:      return 80.0f;   // conservative 5400/7200 rpm figure
        default:                   return 0.0f;    // unknown: do not invent a value
    }
}

void QueryStorage(const char* targetPath, SpmHardwareCapabilities* caps) {
    caps->storageType = SPM_STORAGE_UNKNOWN;
    caps->storageEstimatedBandwidthMBps = EstimatedBandwidth(SPM_STORAGE_UNKNOWN);
    caps->storageDirectStorageSupported = 0;
    caps->storageDriveLetter[0] = '\0';

    // Resolve the volume actually holding the target path (works for UNC and
    // for paths whose drive letter is not simply the first character).
    wchar_t widePath[MAX_PATH * 2]{};
    if (targetPath && targetPath[0] != '\0') {
        MultiByteToWideChar(CP_UTF8, 0, targetPath, -1, widePath,
                            static_cast<int>(std::size(widePath)));
    } else {
        GetCurrentDirectoryW(static_cast<DWORD>(std::size(widePath)), widePath);
    }

    wchar_t volumeRoot[MAX_PATH]{};
    if (!GetVolumePathNameW(widePath, volumeRoot, static_cast<DWORD>(std::size(volumeRoot)))) {
        return;
    }
    CopyString(caps->storageDriveLetter, sizeof(caps->storageDriveLetter), ToUtf8(volumeRoot));

    // A drive root ("C:\") must be opened as "\\.\C:"; volume GUID paths
    // ("\\?\Volume{...}\") only need the trailing backslash removed.
    std::wstring volumeDevice;
    if (volumeRoot[0] != L'\0' && volumeRoot[1] == L':') {
        volumeDevice = L"\\\\.\\";
        volumeDevice.push_back(volumeRoot[0]);
        volumeDevice.push_back(L':');
    } else {
        volumeDevice = volumeRoot;
        while (volumeDevice.size() > 4 &&
               (volumeDevice.back() == L'\\' || volumeDevice.back() == L'/')) {
            volumeDevice.pop_back();
        }
    }

    HANDLE device = CreateFileW(volumeDevice.c_str(), 0,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, 0, nullptr);
    if (device == INVALID_HANDLE_VALUE) {
        return;
    }

    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;

    alignas(STORAGE_DEVICE_DESCRIPTOR) unsigned char buffer[1024]{};
    DWORD returned = 0;
    if (!DeviceIoControl(device, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), buffer,
                         sizeof(buffer), &returned, nullptr)) {
        CloseHandle(device);
        return;
    }

    const auto* descriptor = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(buffer);
    bool incursSeekPenalty = false;
    bool seekPenaltyKnown = false;

    STORAGE_PROPERTY_QUERY seekQuery{};
    seekQuery.PropertyId = StorageDeviceSeekPenaltyProperty;
    seekQuery.QueryType = PropertyStandardQuery;

    DEVICE_SEEK_PENALTY_DESCRIPTOR seekDescriptor{};
    DWORD seekReturned = 0;
    if (DeviceIoControl(device, IOCTL_STORAGE_QUERY_PROPERTY, &seekQuery, sizeof(seekQuery),
                        &seekDescriptor, sizeof(seekDescriptor), &seekReturned, nullptr)) {
        incursSeekPenalty = seekDescriptor.IncursSeekPenalty != FALSE;
        seekPenaltyKnown = true;
    }
    CloseHandle(device);

    if (descriptor->BusType == BusTypeNvme) {
        caps->storageType = SPM_STORAGE_NVME_SSD;
    } else if (descriptor->BusType == BusTypeSata) {
        caps->storageType = (seekPenaltyKnown && incursSeekPenalty) ? SPM_STORAGE_HDD
                                                                   : SPM_STORAGE_SATA_SSD;
    } else if (descriptor->BusType == BusTypeAta || descriptor->BusType == BusTypeUsb ||
               descriptor->BusType == BusTypeScsi || descriptor->BusType == BusTypeSd ||
               descriptor->BusType == BusTypeMmc) {
        caps->storageType = (seekPenaltyKnown && incursSeekPenalty) ? SPM_STORAGE_HDD
                                                                   : SPM_STORAGE_SATA_SSD;
    } else if (seekPenaltyKnown) {
        caps->storageType = incursSeekPenalty ? SPM_STORAGE_HDD : SPM_STORAGE_SATA_SSD;
    }

    caps->storageEstimatedBandwidthMBps = EstimatedBandwidth(caps->storageType);
    // DirectStorage requires NVMe (or at least an SSD with the right driver).
    // The runtime does not use it yet, so this flag only reports feasibility.
    caps->storageDirectStorageSupported =
        caps->storageType == SPM_STORAGE_NVME_SSD ? 1 : 0;
}

// ---------------------------------------------------------------------------
// NPU / AI accelerators
// ---------------------------------------------------------------------------

struct NpuProbeResult {
    SpmNpuVendor vendor{SPM_NPU_NONE};
    std::string name;
    std::string driverVersion;
};

// The Compute Accelerator device class (Windows 10 1809+). Newer SDKs ship
// GUID_DEVCLASS_COMPUTEACCELERATOR; older ones do not, so it is defined here.
const GUID kComputeAcceleratorClass = {
    0xF01A9D53, 0x3FF6, 0x48D2, {0x9F, 0x97, 0xC8, 0xA7, 0x00, 0x4B, 0xE1, 0x0C}};

std::wstring DevicePropertyString(HDEVINFO set, SP_DEVINFO_DATA& data, DWORD property) {
    DWORD type = 0;
    DWORD needed = 0;
    SetupDiGetDeviceRegistryPropertyW(set, &data, property, &type, nullptr, 0, &needed);
    if (needed == 0) {
        return {};
    }
    std::wstring value(needed / sizeof(wchar_t) + 1, L'\0');
    if (!SetupDiGetDeviceRegistryPropertyW(set, &data, property, &type,
                                           reinterpret_cast<PBYTE>(value.data()),
                                           static_cast<DWORD>(value.size() * sizeof(wchar_t)),
                                           &needed)) {
        return {};
    }
    value.resize(needed / sizeof(wchar_t));
    return value;
}

std::string DriverVersionFor(HDEVINFO set, SP_DEVINFO_DATA& data) {
    const std::wstring driverKey = DevicePropertyString(set, data, SPDRP_DRIVER);
    if (driverKey.empty()) {
        return {};
    }

    // SPDRP_DRIVER returns either the full subkey ("{class-guid}\0000") or just
    // the instance suffix ("0000"), depending on the driver. Handle both.
    wchar_t classKey[512]{};
    std::swprintf(classKey, std::size(classKey),
                  L"SYSTEM\\CurrentControlSet\\Control\\Class\\{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
                  kComputeAcceleratorClass.Data1, kComputeAcceleratorClass.Data2,
                  kComputeAcceleratorClass.Data3, kComputeAcceleratorClass.Data4[0],
                  kComputeAcceleratorClass.Data4[1], kComputeAcceleratorClass.Data4[2],
                  kComputeAcceleratorClass.Data4[3], kComputeAcceleratorClass.Data4[4],
                  kComputeAcceleratorClass.Data4[5], kComputeAcceleratorClass.Data4[6],
                  kComputeAcceleratorClass.Data4[7]);

    HKEY key = nullptr;
    const std::wstring path = driverKey.front() == L'{'
                                  ? std::wstring(L"SYSTEM\\CurrentControlSet\\Control\\Class\\") + driverKey
                                  : std::wstring(classKey) + L"\\" + driverKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return {};
    }

    wchar_t version[128]{};
    DWORD size = sizeof(version);
    DWORD type = 0;
    const LONG status = RegQueryValueExW(key, L"DriverVersion", nullptr, &type,
                                         reinterpret_cast<LPBYTE>(version), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ) {
        return {};
    }
    version[(sizeof(version) / sizeof(version[0])) - 1] = L'\0';
    return ToUtf8(version);
}

uint32_t ParsePciVendor(const std::wstring& hardwareIds) {
    const size_t position = hardwareIds.find(L"VEN_");
    if (position == std::wstring::npos || position + 8 > hardwareIds.size()) {
        return 0;
    }
    uint32_t vendor = 0;
    for (size_t i = position + 4; i < position + 8; ++i) {
        const wchar_t c = hardwareIds[i];
        vendor <<= 4;
        if (c >= L'0' && c <= L'9') {
            vendor |= static_cast<uint32_t>(c - L'0');
        } else if (c >= L'A' && c <= L'F') {
            vendor |= static_cast<uint32_t>(c - L'A' + 10);
        } else if (c >= L'a' && c <= L'f') {
            vendor |= static_cast<uint32_t>(c - L'a' + 10);
        } else {
            return 0;
        }
    }
    return vendor;
}

NpuProbeResult DetectNpu() {
    NpuProbeResult result;

    HDEVINFO set = SetupDiGetClassDevsW(&kComputeAcceleratorClass, nullptr, nullptr,
                                        DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) {
        return result;
    }

    SP_DEVINFO_DATA data{};
    data.cbSize = sizeof(data);
    for (DWORD index = 0; SetupDiEnumDeviceInfo(set, index, &data); ++index) {
        std::wstring name = DevicePropertyString(set, data, SPDRP_FRIENDLYNAME);
        if (name.empty()) {
            name = DevicePropertyString(set, data, SPDRP_DEVICEDESC);
        }
        const std::wstring hardwareIds = DevicePropertyString(set, data, SPDRP_HARDWAREID);
        const uint32_t vendor = ParsePciVendor(hardwareIds);

        switch (vendor) {
            case 0x1022: // AMD (XDNA / Ryzen AI)
                result.vendor = SPM_NPU_AMD_RYZEN_AI;
                break;
            case 0x8086: // Intel (AI Boost / VPU)
                result.vendor = SPM_NPU_INTEL_AI_BOOST;
                break;
            case 0x17CB: // Qualcomm (Hexagon NPU)
                result.vendor = SPM_NPU_QUALCOMM_HEXAGON;
                break;
            default:
                result.vendor = SPM_NPU_GENERIC_MCDM;
                break;
        }

        result.name = name.empty() ? std::string("Compute Accelerator") : ToUtf8(name.c_str());
        result.driverVersion = DriverVersionFor(set, data);
        break;
    }

    SetupDiDestroyDeviceInfoList(set);
    return result;
}

bool SoftwareRuntimeInstalled(const wchar_t* moduleName) {
    HMODULE module = LoadLibraryW(moduleName);
    if (!module) {
        return false;
    }
    FreeLibrary(module);
    return true;
}

} // namespace

#elif defined(__linux__)

#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <set>

namespace {

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return {};
    }
    std::string value;
    std::getline(file, value);
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')) {
        value.pop_back();
    }
    return value;
}

long ReadLongFile(const std::filesystem::path& path, long fallback = -1) {
    const std::string text = ReadTextFile(path);
    if (text.empty()) {
        return fallback;
    }
    try {
        return std::stol(text);
    } catch (...) {
        return fallback;
    }
}

float EstimatedBandwidth(SpmStorageType type) {
    switch (type) {
        case SPM_STORAGE_NVME_SSD: return 2000.0f;
        case SPM_STORAGE_SATA_SSD: return 400.0f;
        case SPM_STORAGE_HDD:      return 80.0f;
        default:                   return 0.0f;
    }
}

void QueryStorage(const char* targetPath, SpmHardwareCapabilities* caps) {
    caps->storageType = SPM_STORAGE_UNKNOWN;
    caps->storageEstimatedBandwidthMBps = 0.0f;
    caps->storageDirectStorageSupported = 0;
    std::snprintf(caps->storageDriveLetter, sizeof(caps->storageDriveLetter), "%s",
                  targetPath && targetPath[0] ? targetPath : ".");

    struct stat status {};
    if (stat(targetPath && targetPath[0] ? targetPath : ".", &status) != 0) {
        return;
    }

    const unsigned int deviceMajor = major(status.st_dev);
    const unsigned int deviceMinor = minor(status.st_dev);
    char linkPath[64];
    std::snprintf(linkPath, sizeof(linkPath), "/sys/dev/block/%u:%u", deviceMajor, deviceMinor);

    std::error_code ec;
    const std::filesystem::path resolved = std::filesystem::canonical(linkPath, ec);
    if (ec) {
        return;
    }

    // Resolve .../block/nvme0n1/nvme0n1p2 to the whole-disk name "nvme0n1".
    std::string diskName;
    for (const auto& part : resolved) {
        const std::string component = part.string();
        if (component.rfind("nvme", 0) == 0 || component.rfind("sd", 0) == 0 ||
            component.rfind("vd", 0) == 0 || component.rfind("mmcblk", 0) == 0) {
            if (component.back() >= '0' && component.back() <= '9') {
                const bool isPartition =
                    component.find('p') != std::string::npos && component.rfind("nvme", 0) == 0;
                if (!isPartition) {
                    diskName = component;
                }
            }
        }
    }
    if (diskName.empty()) {
        return;
    }

    const std::filesystem::path sysBlock = std::filesystem::path("/sys/block") / diskName;
    const long rotational = ReadLongFile(sysBlock / "queue" / "rotational", -1);
    if (diskName.rfind("nvme", 0) == 0) {
        caps->storageType = SPM_STORAGE_NVME_SSD;
    } else if (rotational == 1) {
        caps->storageType = SPM_STORAGE_HDD;
    } else if (rotational == 0) {
        caps->storageType = SPM_STORAGE_SATA_SSD;
    }
    caps->storageEstimatedBandwidthMBps = EstimatedBandwidth(caps->storageType);
    caps->storageDirectStorageSupported = 0;
}

struct NpuProbeResult {
    SpmNpuVendor vendor{SPM_NPU_NONE};
    std::string name;
    std::string driverVersion;
};

NpuProbeResult DetectNpu() {
    NpuProbeResult result;

    std::error_code ec;
    const std::filesystem::path accelRoot("/sys/class/accel");
    if (!std::filesystem::is_directory(accelRoot, ec)) {
        return result;
    }

    for (const auto& entry : std::filesystem::directory_iterator(accelRoot, ec)) {
        if (ec) {
            break;
        }
        std::filesystem::path driver = entry.path() / "device" / "driver";
        std::error_code driverError;
        const std::filesystem::path target = std::filesystem::read_symlink(driver, driverError);
        const std::string driverName = driverError ? std::string() : target.filename().string();

        if (driverName == "intel_vpu" || driverName == "ivpu") {
            result.vendor = SPM_NPU_INTEL_AI_BOOST;
        } else if (driverName == "amdxdna") {
            result.vendor = SPM_NPU_AMD_RYZEN_AI;
        } else if (driverName.find("qcom") != std::string::npos ||
                   driverName.find("hexagon") != std::string::npos) {
            result.vendor = SPM_NPU_QUALCOMM_HEXAGON;
        } else {
            result.vendor = SPM_NPU_GENERIC_MCDM;
        }

        result.name = entry.path().filename().string();
        const std::string moduleVersion = ReadTextFile(entry.path() / "device" / "driver" / "module" /
                                                      "version");
        result.driverVersion = moduleVersion;
        break;
    }

    return result;
}

} // namespace

#endif // _WIN32

#if defined(__linux__)
namespace {

bool CpuSupports(const char* feature) {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_cpu_init();
    return __builtin_cpu_supports(feature);
#else
    (void)feature;
    return false;
#endif
}

} // namespace
#endif

extern "C" {

SPM_CAPS_API int SPM_DetectHardwareCapabilities(const char* targetPath, SpmHardwareCapabilities* outCaps) {
    if (!outCaps) return 0;
    std::memset(outCaps, 0, sizeof(*outCaps));

    const spm::CpuTopologyInfo topology = spm::QueryCpuTopology();
    outCaps->cpuLogicalCores = topology.logicalCores;
    outCaps->cpuPhysicalCores = topology.physicalCores;

#ifdef _WIN32
    const CpuFeatures features = QueryCpuFeatures();
    outCaps->cpuHasAvx2 = features.avx2 ? 1 : 0;
    outCaps->cpuHasAvx512 = features.avx512 ? 1 : 0;
    outCaps->cpuHasFma = features.fma ? 1 : 0;

    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (GlobalMemoryStatusEx(&memory)) {
        outCaps->totalPhysicalRamMb = memory.ullTotalPhys / (1024ULL * 1024ULL);
        outCaps->availableRamMb = memory.ullAvailPhys / (1024ULL * 1024ULL);
    }

    QueryStorage(targetPath, outCaps);

    const NpuProbeResult npu = DetectNpu();
    outCaps->npuVendor = npu.vendor;
    outCaps->npuSupported = npu.vendor != SPM_NPU_NONE ? 1 : 0;
    CopyString(outCaps->npuName, sizeof(outCaps->npuName), npu.name);
    CopyString(outCaps->npuDriverVersion, sizeof(outCaps->npuDriverVersion), npu.driverVersion);

    // Software availability, explicitly separate from hardware acceleration.
    outCaps->directMlSupported = SoftwareRuntimeInstalled(L"directml.dll") ? 1 : 0;
    outCaps->onnxRuntimeSupported = SoftwareRuntimeInstalled(L"onnxruntime.dll") ? 1 : 0;
#else
    outCaps->cpuHasAvx2 = CpuSupports("avx2") ? 1 : 0;
    outCaps->cpuHasAvx512 = CpuSupports("avx512f") ? 1 : 0;
    outCaps->cpuHasFma = CpuSupports("fma") ? 1 : 0;

    const long pageSize = sysconf(_SC_PAGESIZE);
    const long totalPages = sysconf(_SC_PHYS_PAGES);
    const long availablePages = sysconf(_SC_AVPHYS_PAGES);
    if (pageSize > 0 && totalPages > 0) {
        outCaps->totalPhysicalRamMb =
            static_cast<uint64_t>(pageSize) * static_cast<uint64_t>(totalPages) /
            (1024ULL * 1024ULL);
    }
    if (pageSize > 0 && availablePages > 0) {
        outCaps->availableRamMb =
            static_cast<uint64_t>(pageSize) * static_cast<uint64_t>(availablePages) /
            (1024ULL * 1024ULL);
    }

    QueryStorage(targetPath, outCaps);

    const NpuProbeResult npu = DetectNpu();
    outCaps->npuVendor = npu.vendor;
    outCaps->npuSupported = npu.vendor != SPM_NPU_NONE ? 1 : 0;
    CopyString(outCaps->npuName, sizeof(outCaps->npuName), npu.name);
    CopyString(outCaps->npuDriverVersion, sizeof(outCaps->npuDriverVersion), npu.driverVersion);

    // DirectML does not exist on Linux. Report it accordingly instead of
    // inheriting the Windows software stack.
    outCaps->directMlSupported = 0;
    outCaps->onnxRuntimeSupported = 0;
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
        case SPM_NPU_GENERIC_MCDM:     return "Generic Compute Accelerator";
        default:                       return "None";
    }
}

} // extern "C"
