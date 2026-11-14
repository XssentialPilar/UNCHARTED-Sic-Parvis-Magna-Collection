// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Real Hardware & GPU Enumeration Module
//
// Vulkan capability is obtained from the Vulkan loader itself (instance +
// physical-device enumeration). It is never inferred from Direct3D 11 support.
// The list is ordered Vulkan-first because the runtime selects devices with
// GOW3_GPU_ID as an index into its Vulkan device list.
#include "spm/spm_core.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi.h>
#include <d3d11.h>
#else
#include <dlfcn.h>
#endif

namespace {

// ---------------------------------------------------------------------------
// Minimal Vulkan ABI (avoids a hard SDK dependency for a capability probe).
// ---------------------------------------------------------------------------
using VkInstance = struct VkInstance_T*;
using VkPhysicalDevice = struct VkPhysicalDevice_T*;
using VkResult = int32_t;
using PFN_vkVoidFunction = void (*)();

struct VkApplicationInfo {
    int32_t sType;
    const void* pNext;
    const char* pApplicationName;
    uint32_t applicationVersion;
    const char* pEngineName;
    uint32_t engineVersion;
    uint32_t apiVersion;
};

struct VkInstanceCreateInfo {
    int32_t sType;
    const void* pNext;
    uint32_t flags;
    const VkApplicationInfo* pApplicationInfo;
    uint32_t enabledLayerCount;
    const char* const* ppEnabledLayerNames;
    uint32_t enabledExtensionCount;
    const char* const* ppEnabledExtensionNames;
};

constexpr int32_t kVkStructureTypeApplicationInfo = 0;
constexpr int32_t kVkStructureTypeInstanceCreateInfo = 1;
constexpr VkResult kVkSuccess = 0;
constexpr uint32_t kVkApiVersion10 = (1u << 22);
constexpr uint32_t kVkApiVersion13 = (1u << 22) | (3u << 12);
constexpr int32_t kVkPhysicalDeviceTypeDiscreteGpu = 2;
constexpr int32_t kVkPhysicalDeviceTypeCpu = 4;
constexpr uint32_t kVkMemoryHeapDeviceLocalBit = 0x00000001u;

// Offsets from the Vulkan headers (ABI-stable, x86-64 and x86).
constexpr size_t kVkPropsApiVersion = 0;
constexpr size_t kVkPropsVendorId = 8;
constexpr size_t kVkPropsDeviceId = 12;
constexpr size_t kVkPropsDeviceType = 16;
constexpr size_t kVkPropsDeviceName = 20;
constexpr size_t kVkPropsDeviceNameSize = 256;
constexpr size_t kVkMemoryHeapCount = 260;
constexpr size_t kVkMemoryHeaps = 264;
constexpr size_t kVkMemoryHeapStride = 16;
constexpr uint32_t kVkMaxMemoryHeaps = 16;

struct VulkanDeviceInfo {
    std::string name;
    uint32_t vendorId{0};
    uint32_t deviceId{0};
    uint32_t apiVersion{0};
    int32_t deviceType{0};
    uint64_t deviceLocalBytes{0};
    bool hasSwapchain{false};

    bool MeetsRuntimeRequirements() const {
        // The shadPS4-derived renderer targets Vulkan 1.3 and presents through
        // VK_KHR_swapchain. Reporting 1.1/1.2 hardware as usable would make the
        // launcher choose a GPU the runtime cannot initialize.
        return apiVersion >= kVkApiVersion13 && hasSwapchain;
    }
};

template <typename T>
T LoadSymbol(void* library, const char* name) {
#ifdef _WIN32
    return reinterpret_cast<T>(GetProcAddress(static_cast<HMODULE>(library), name));
#else
    return reinterpret_cast<T>(dlsym(library, name));
#endif
}

void UnloadLibrary(void* library) {
    if (!library) return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(library));
#else
    dlclose(library);
#endif
}

[[maybe_unused]] std::string NormalizeDeviceName(std::string name) {
    std::string out;
    out.reserve(name.size());
    for (const unsigned char c : name) {
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
        }
    }
    return out;
}

bool ProbeVulkan(std::vector<VulkanDeviceInfo>& devices) {
    devices.clear();

#ifdef _WIN32
    void* library = static_cast<void*>(LoadLibraryW(L"vulkan-1.dll"));
#else
    void* library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        library = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    }
#endif
    if (!library) {
        return false;
    }

    using PFN_vkGetInstanceProcAddr = PFN_vkVoidFunction (*)(VkInstance, const char*);
    using PFN_vkCreateInstance = VkResult (*)(const VkInstanceCreateInfo*, const void*, VkInstance*);
    using PFN_vkDestroyInstance = void (*)(VkInstance, const void*);
    using PFN_vkEnumeratePhysicalDevices = VkResult (*)(VkInstance, uint32_t*, VkPhysicalDevice*);
    using PFN_vkGetPhysicalDeviceProperties = void (*)(VkPhysicalDevice, void*);
    using PFN_vkGetPhysicalDeviceMemoryProperties = void (*)(VkPhysicalDevice, void*);
    using PFN_vkEnumerateDeviceExtensionProperties = VkResult (*)(
        VkPhysicalDevice, const char*, uint32_t*, void*);

    auto getInstanceProcAddr = LoadSymbol<PFN_vkGetInstanceProcAddr>(library, "vkGetInstanceProcAddr");
    if (!getInstanceProcAddr) {
        UnloadLibrary(library);
        return false;
    }

    auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(
        getInstanceProcAddr(nullptr, "vkCreateInstance"));
    if (!createInstance) {
        UnloadLibrary(library);
        return false;
    }

    VkApplicationInfo application{};
    application.sType = kVkStructureTypeApplicationInfo;
    application.pApplicationName = "SPMCore";
    application.applicationVersion = 1;
    application.pEngineName = "SPMCore";
    application.engineVersion = 1;
    application.apiVersion = kVkApiVersion10;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = kVkStructureTypeInstanceCreateInfo;
    createInfo.pApplicationInfo = &application;

    VkInstance instance = nullptr;
    if (createInstance(&createInfo, nullptr, &instance) != kVkSuccess || !instance) {
        UnloadLibrary(library);
        return false;
    }

    auto destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(
        getInstanceProcAddr(instance, "vkDestroyInstance"));
    auto enumeratePhysicalDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        getInstanceProcAddr(instance, "vkEnumeratePhysicalDevices"));
    auto getProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
        getInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties"));
    auto getMemoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
        getInstanceProcAddr(instance, "vkGetPhysicalDeviceMemoryProperties"));
    auto enumerateDeviceExtensions = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
        getInstanceProcAddr(instance, "vkEnumerateDeviceExtensionProperties"));

    if (!destroyInstance || !enumeratePhysicalDevices || !getProperties ||
        !getMemoryProperties || !enumerateDeviceExtensions) {
        if (destroyInstance) destroyInstance(instance, nullptr);
        UnloadLibrary(library);
        return false;
    }

    uint32_t count = 0;
    if (enumeratePhysicalDevices(instance, &count, nullptr) != kVkSuccess || count == 0) {
        destroyInstance(instance, nullptr);
        UnloadLibrary(library);
        return false;
    }
    if (count > 32) {
        count = 32;
    }

    std::vector<VkPhysicalDevice> handles(count, nullptr);
    if (enumeratePhysicalDevices(instance, &count, handles.data()) != kVkSuccess) {
        destroyInstance(instance, nullptr);
        UnloadLibrary(library);
        return false;
    }

    for (uint32_t i = 0; i < count; ++i) {
        alignas(8) unsigned char properties[1024]{};
        getProperties(handles[i], properties);

        VulkanDeviceInfo info{};
        std::memcpy(&info.apiVersion, properties + kVkPropsApiVersion, sizeof(info.apiVersion));
        std::memcpy(&info.vendorId, properties + kVkPropsVendorId, sizeof(info.vendorId));
        std::memcpy(&info.deviceId, properties + kVkPropsDeviceId, sizeof(info.deviceId));
        std::memcpy(&info.deviceType, properties + kVkPropsDeviceType, sizeof(info.deviceType));

        char name[kVkPropsDeviceNameSize]{};
        std::memcpy(name, properties + kVkPropsDeviceName, sizeof(name));
        name[sizeof(name) - 1] = '\0';
        info.name = name;

        if (info.deviceType == kVkPhysicalDeviceTypeCpu || info.name.empty()) {
            continue;
        }

        alignas(8) unsigned char memory[1024]{};
        getMemoryProperties(handles[i], memory);
        uint32_t heapCount = 0;
        std::memcpy(&heapCount, memory + kVkMemoryHeapCount, sizeof(heapCount));
        heapCount = std::min(heapCount, kVkMaxMemoryHeaps);
        for (uint32_t heap = 0; heap < heapCount; ++heap) {
            uint64_t size = 0;
            uint32_t flags = 0;
            const size_t offset = kVkMemoryHeaps + static_cast<size_t>(heap) * kVkMemoryHeapStride;
            std::memcpy(&size, memory + offset, sizeof(size));
            std::memcpy(&flags, memory + offset + sizeof(size), sizeof(flags));
            if ((flags & kVkMemoryHeapDeviceLocalBit) != 0) {
                info.deviceLocalBytes += size;
            }
        }

        uint32_t extensionCount = 0;
        if (enumerateDeviceExtensions(handles[i], nullptr, &extensionCount, nullptr) == kVkSuccess) {
            std::vector<unsigned char> extensions(
                static_cast<size_t>(extensionCount) * 260u);
            if (extensionCount > 0 &&
                enumerateDeviceExtensions(handles[i], nullptr, &extensionCount, extensions.data()) == kVkSuccess) {
                for (uint32_t e = 0; e < extensionCount; ++e) {
                    const char* extensionName =
                        reinterpret_cast<const char*>(extensions.data() + static_cast<size_t>(e) * 260u);
                    if (std::strncmp(extensionName, "VK_KHR_swapchain", 256) == 0) {
                        info.hasSwapchain = true;
                        break;
                    }
                }
            }
        }

        devices.push_back(std::move(info));
    }

    destroyInstance(instance, nullptr);
    UnloadLibrary(library);
    return !devices.empty();
}

#ifdef _WIN32

struct DxgiAdapter {
    SPMGpuInfo info{};
    std::string normalizedName;
};

bool EnumerateDxgiAdapters(std::vector<DxgiAdapter>& adapters) {
    adapters.clear();

    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    if (!dxgi) return false;

    using CreateDXGIFactory1Fn = HRESULT(WINAPI*)(REFIID, void**);
    auto createFactory1 = reinterpret_cast<CreateDXGIFactory1Fn>(
        GetProcAddress(dxgi, "CreateDXGIFactory1"));
    if (!createFactory1) {
        FreeLibrary(dxgi);
        return false;
    }

    IDXGIFactory1* factory = nullptr;
    if (FAILED(createFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) || !factory) {
        FreeLibrary(dxgi);
        return false;
    }

    HMODULE d3d11 = LoadLibraryW(L"d3d11.dll");
    using D3D11CreateDeviceFn = HRESULT(WINAPI*)(
        IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
        const D3D_FEATURE_LEVEL*, UINT, UINT,
        ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);
    auto d3d11CreateDevice = d3d11
        ? reinterpret_cast<D3D11CreateDeviceFn>(GetProcAddress(d3d11, "D3D11CreateDevice"))
        : nullptr;

    IDXGIAdapter1* adapter = nullptr;
    for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc))) {
            // Skip WARP and the Microsoft Basic Render Driver.
            if ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
                adapter->Release();
                continue;
            }

            DxgiAdapter entry{};
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, entry.info.name,
                                sizeof(entry.info.name) - 1, nullptr, nullptr);
            entry.info.vendorId = desc.VendorId;
            entry.info.deviceId = desc.DeviceId;
            entry.info.dedicatedVramBytes = desc.DedicatedVideoMemory;
            entry.info.sharedSystemMemoryBytes = desc.SharedSystemMemory;
            std::snprintf(entry.info.stableLuid, sizeof(entry.info.stableLuid), "%08X-%08X",
                          static_cast<unsigned int>(desc.AdapterLuid.HighPart),
                          static_cast<unsigned int>(desc.AdapterLuid.LowPart));

            if (desc.VendorId == 0x10DE) {
                entry.info.isDiscrete = 1;
            } else if (desc.VendorId == 0x1002) {
                const bool looksIntegrated =
                    desc.DedicatedVideoMemory < 1024ULL * 1024ULL * 1024ULL ||
                    std::strstr(entry.info.name, "Radeon Graphics") != nullptr ||
                    std::strstr(entry.info.name, "Radeon(TM) Graphics") != nullptr;
                entry.info.isDiscrete = looksIntegrated ? 0 : 1;
            } else {
                entry.info.isDiscrete = 0;
            }

            if (d3d11CreateDevice) {
                const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
                D3D_FEATURE_LEVEL outLevel{};
                ID3D11Device* testDevice = nullptr;
                if (SUCCEEDED(d3d11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0,
                                                levels, 1, D3D11_SDK_VERSION, &testDevice,
                                                &outLevel, nullptr)) &&
                    testDevice) {
                    entry.info.supportsD3D11 = 1;
                    testDevice->Release();
                }
            }

            entry.normalizedName = NormalizeDeviceName(entry.info.name);
            adapters.push_back(std::move(entry));
        }
        adapter->Release();
    }

    if (d3d11) FreeLibrary(d3d11);
    factory->Release();
    FreeLibrary(dxgi);
    return !adapters.empty();
}

#endif // _WIN32

void FillFromVulkan(SPMGpuInfo& info, const VulkanDeviceInfo& device) {
    std::memset(&info, 0, sizeof(info));
    std::snprintf(info.name, sizeof(info.name), "%s", device.name.c_str());
    info.vendorId = device.vendorId;
    info.deviceId = device.deviceId;
    info.dedicatedVramBytes = device.deviceLocalBytes;
    info.sharedSystemMemoryBytes = 0;
    info.isDiscrete = device.deviceType == kVkPhysicalDeviceTypeDiscreteGpu ? 1 : 0;
    info.supportsVulkan = device.MeetsRuntimeRequirements() ? 1 : 0;
    info.supportsD3D11 = 0;
    std::snprintf(info.stableLuid, sizeof(info.stableLuid), "VK-%04X-%04X",
                  device.vendorId & 0xFFFFu, device.deviceId & 0xFFFFu);
}

} // namespace

int SPM_DetectGpus(SPMGpuInfo* outGpus, int maxGpus, int* outCount) {
    if (outCount) *outCount = 0;
    if (!outGpus || maxGpus <= 0) return 0;

    std::vector<VulkanDeviceInfo> vulkanDevices;
    ProbeVulkan(vulkanDevices);

    int count = 0;

#ifdef _WIN32
    std::vector<DxgiAdapter> dxgiAdapters;
    EnumerateDxgiAdapters(dxgiAdapters);
    std::vector<bool> usedDxgi(dxgiAdapters.size(), false);

    // Vulkan order first: the runtime's GOW3_GPU_ID is an index into this list.
    for (size_t i = 0; i < vulkanDevices.size() && count < maxGpus; ++i) {
        SPMGpuInfo& info = outGpus[count];
        FillFromVulkan(info, vulkanDevices[i]);

        const std::string vulkanName = NormalizeDeviceName(vulkanDevices[i].name);
        for (size_t d = 0; d < dxgiAdapters.size(); ++d) {
            if (usedDxgi[d]) continue;
            const DxgiAdapter& adapter = dxgiAdapters[d];
            if ((adapter.info.vendorId != info.vendorId || adapter.info.deviceId != info.deviceId) &&
                adapter.normalizedName != vulkanName) {
                continue;
            }
            // Prefer the DXGI numbers for dedicated/shared memory and LUID, but
            // keep Vulkan's authoritative device type and capability flag.
            info.dedicatedVramBytes =
                std::max<uint64_t>(info.dedicatedVramBytes, adapter.info.dedicatedVramBytes);
            info.sharedSystemMemoryBytes = adapter.info.sharedSystemMemoryBytes;
            info.supportsD3D11 = adapter.info.supportsD3D11;
            std::snprintf(info.stableLuid, sizeof(info.stableLuid), "%s", adapter.info.stableLuid);
            usedDxgi[d] = true;
            break;
        }

        ++count;
    }

    // Adapters that only expose D3D11 are still shown, after every Vulkan device.
    // They cannot run this Vulkan runtime, but hiding them would confuse users.
    for (size_t d = 0; d < dxgiAdapters.size(); ++d) {
        if (count >= maxGpus) break;
        if (usedDxgi[d]) continue;
        outGpus[count] = dxgiAdapters[d].info;
        outGpus[count].supportsVulkan = 0;
        ++count;
    }
#else
    for (const VulkanDeviceInfo& device : vulkanDevices) {
        if (count >= maxGpus) break;
        FillFromVulkan(outGpus[count], device);
        ++count;
    }
#endif

    if (outCount) *outCount = count;
    return count;
}

int SPM_GetRecommendedGpuIndex(const SPMGpuInfo* gpus, int count) {
    if (!gpus || count <= 0) return -1;

    int bestIndex = -1;
    uint64_t bestVram = 0;

    auto consider = [&](int index) {
        if (bestIndex == -1 || gpus[index].dedicatedVramBytes > bestVram) {
            bestIndex = index;
            bestVram = gpus[index].dedicatedVramBytes;
        }
    };

    // The runtime presents through Vulkan, so a Vulkan-capable device always
    // wins over a D3D11-only adapter with more memory.
    for (int i = 0; i < count; ++i) {
        if (gpus[i].isDiscrete && gpus[i].supportsVulkan) consider(i);
    }
    if (bestIndex != -1) return bestIndex;
    for (int i = 0; i < count; ++i) {
        if (gpus[i].supportsVulkan) consider(i);
    }
    if (bestIndex != -1) return bestIndex;
    for (int i = 0; i < count; ++i) {
        if (gpus[i].isDiscrete && gpus[i].supportsD3D11) consider(i);
    }
    if (bestIndex != -1) return bestIndex;
    for (int i = 0; i < count; ++i) {
        if (gpus[i].supportsD3D11) consider(i);
    }
    return bestIndex != -1 ? bestIndex : 0;
}
