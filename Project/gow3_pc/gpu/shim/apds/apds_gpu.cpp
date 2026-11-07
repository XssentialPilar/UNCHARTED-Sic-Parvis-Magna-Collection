// SPDX-License-Identifier: GPL-2.0-or-later
// Advanced Performance & Diagnostics System (APDS) - GPU Telemetry Collector
#include "apds_gpu.h"
#include "apds_time.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <dxgi1_4.h>
#include <pdh.h>
#include <pdhmsg.h>
#endif

namespace APDS {

#ifdef _WIN32

namespace {

std::string WideToUtf8(const wchar_t* wstr) {
    if (!wstr) return {};
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (size_needed <= 1) return {};
    std::string str(size_needed - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &str[0], size_needed, nullptr, nullptr);
    return str;
}

// NVML function prototypes
using NvmlInitFn = int (*)();
using NvmlCountFn = int (*)(unsigned*);
using NvmlHandleFn = int (*)(unsigned, void**);
using NvmlNameFn = int (*)(void*, char*, unsigned);
using NvmlTempFn = int (*)(void*, int, unsigned*);
using NvmlClockFn = int (*)(void*, int, unsigned*);
using NvmlPowerFn = int (*)(void*, unsigned*);

class NvmlTelemetry {
public:
    NvmlTelemetry() { TryLoad(); }
    ~NvmlTelemetry() {
        if (lib) {
            FreeLibrary(lib);
            lib = nullptr;
        }
    }

    bool IsAvailable() const noexcept { return handle != nullptr; }

    float GetTemperatureC() {
        if (!handle || !fn_get_temp) return -1.0f;
        unsigned temp = 0;
        if (fn_get_temp(handle, 0 /* NVML_TEMPERATURE_GPU */, &temp) == 0) {
            return (temp <= 150) ? static_cast<float>(temp) : -1.0f;
        }
        return -1.0f;
    }

    float GetCoreClockMhz() {
        if (!handle || !fn_get_clock) return -1.0f;
        unsigned mhz = 0;
        if (fn_get_clock(handle, 0 /* NVML_CLOCK_GRAPHICS */, &mhz) == 0) {
            return static_cast<float>(mhz);
        }
        return -1.0f;
    }

    float GetMemClockMhz() {
        if (!handle || !fn_get_clock) return -1.0f;
        unsigned mhz = 0;
        if (fn_get_clock(handle, 2 /* NVML_CLOCK_MEM */, &mhz) == 0) {
            return static_cast<float>(mhz);
        }
        return -1.0f;
    }

    float GetPowerWatts() {
        if (!handle || !fn_get_power) return -1.0f;
        unsigned mw = 0;
        if (fn_get_power(handle, &mw) == 0) {
            return static_cast<float>(mw) / 1000.0f;
        }
        return -1.0f;
    }

    float GetPowerLimitWatts() {
        if (!handle || !fn_get_power_limit) return -1.0f;
        unsigned mw = 0;
        if (fn_get_power_limit(handle, &mw) == 0) {
            return static_cast<float>(mw) / 1000.0f;
        }
        return -1.0f;
    }

private:
    void TryLoad() {
        lib = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!lib) return;

        auto fn_init = reinterpret_cast<NvmlInitFn>(GetProcAddress(lib, "nvmlInit_v2"));
        if (!fn_init) fn_init = reinterpret_cast<NvmlInitFn>(GetProcAddress(lib, "nvmlInit"));
        auto fn_count = reinterpret_cast<NvmlCountFn>(GetProcAddress(lib, "nvmlDeviceGetCount_v2"));
        if (!fn_count) fn_count = reinterpret_cast<NvmlCountFn>(GetProcAddress(lib, "nvmlDeviceGetCount"));
        auto fn_handle = reinterpret_cast<NvmlHandleFn>(GetProcAddress(lib, "nvmlDeviceGetHandleByIndex_v2"));
        if (!fn_handle) fn_handle = reinterpret_cast<NvmlHandleFn>(GetProcAddress(lib, "nvmlDeviceGetHandleByIndex"));

        fn_get_temp = reinterpret_cast<NvmlTempFn>(GetProcAddress(lib, "nvmlDeviceGetTemperature"));
        fn_get_clock = reinterpret_cast<NvmlClockFn>(GetProcAddress(lib, "nvmlDeviceGetClockInfo"));
        fn_get_power = reinterpret_cast<NvmlPowerFn>(GetProcAddress(lib, "nvmlDeviceGetPowerUsage"));
        fn_get_power_limit = reinterpret_cast<NvmlPowerFn>(GetProcAddress(lib, "nvmlDeviceGetEnforcedPowerLimit"));

        if (!fn_init || !fn_count || !fn_handle || fn_init() != 0) {
            FreeLibrary(lib);
            lib = nullptr;
            return;
        }

        unsigned count = 0;
        if (fn_count(&count) != 0 || count == 0) {
            FreeLibrary(lib);
            lib = nullptr;
            return;
        }

        // Get primary device (index 0)
        void* dev = nullptr;
        if (fn_handle(0, &dev) == 0) {
            handle = dev;
        }
    }

    HMODULE lib = nullptr;
    void* handle = nullptr;
    NvmlTempFn fn_get_temp = nullptr;
    NvmlClockFn fn_get_clock = nullptr;
    NvmlPowerFn fn_get_power = nullptr;
    NvmlPowerFn fn_get_power_limit = nullptr;
};

class PdhGpuUsage {
public:
    PdhGpuUsage() {
        if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) {
            query = nullptr;
            return;
        }
        if (PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter) != ERROR_SUCCESS) {
            PdhCloseQuery(query);
            query = nullptr;
            return;
        }
        PdhCollectQueryData(query);
        pid_needle = L"pid_" + std::to_wstring(GetCurrentProcessId()) + L"_";
    }

    ~PdhGpuUsage() {
        if (query) {
            PdhCloseQuery(query);
            query = nullptr;
        }
    }

    float Sample() {
        if (!query || PdhCollectQueryData(query) != ERROR_SUCCESS) {
            return -1.0f;
        }
        DWORD size = 0, count = 0;
        if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count, nullptr) != PDH_MORE_DATA) {
            return -1.0f;
        }
        buffer.resize(size);
        auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
        if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, &size, &count, items) != ERROR_SUCCESS) {
            return -1.0f;
        }
        double total_3d = 0.0;
        for (DWORD i = 0; i < count; ++i) {
            const wchar_t* name = items[i].szName;
            if (items[i].FmtValue.CStatus == PDH_CSTATUS_VALID_DATA &&
                wcsstr(name, pid_needle.c_str()) &&
                wcsstr(name, L"engtype_3D")) {
                total_3d += items[i].FmtValue.doubleValue;
            }
        }
        return std::clamp(static_cast<float>(total_3d), 0.0f, 100.0f);
    }

private:
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER counter = nullptr;
    std::wstring pid_needle;
    std::vector<uint8_t> buffer;
};

} // namespace

#endif

struct GpuCollector::Impl {
#ifdef _WIN32
    HMODULE dxgi_lib = nullptr;
    IDXGIFactory4* factory4 = nullptr;
    IDXGIAdapter3* primary_adapter3 = nullptr;
    GpuInfo cached_info;
    NvmlTelemetry nvml;
    PdhGpuUsage pdh;

    Impl() {
        dxgi_lib = LoadLibraryExW(L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (dxgi_lib) {
            using CreateFactoryFn = HRESULT(WINAPI*)(REFIID, void**);
            auto pfnCreate = reinterpret_cast<CreateFactoryFn>(GetProcAddress(dxgi_lib, "CreateDXGIFactory1"));
            if (pfnCreate) {
                pfnCreate(__uuidof(IDXGIFactory4), reinterpret_cast<void**>(&factory4));
            }
        }
        SelectPrimaryAdapter();
    }

    ~Impl() {
        if (primary_adapter3) {
            primary_adapter3->Release();
            primary_adapter3 = nullptr;
        }
        if (factory4) {
            factory4->Release();
            factory4 = nullptr;
        }
        if (dxgi_lib) {
            FreeLibrary(dxgi_lib);
            dxgi_lib = nullptr;
        }
    }

    void SelectPrimaryAdapter() {
        if (!factory4) return;
        UINT i = 0;
        IDXGIAdapter1* adapter1 = nullptr;
        IDXGIAdapter3* best_adapter = nullptr;
        uint64_t max_vram = 0;

        while (factory4->EnumAdapters1(i++, &adapter1) != DXGI_ERROR_NOT_FOUND) {
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(adapter1->GetDesc1(&desc))) {
                if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) {
                    IDXGIAdapter3* ad3 = nullptr;
                    if (SUCCEEDED(adapter1->QueryInterface(__uuidof(IDXGIAdapter3), reinterpret_cast<void**>(&ad3)))) {
                        if (desc.DedicatedVideoMemory > max_vram || !best_adapter) {
                            if (best_adapter) best_adapter->Release();
                            best_adapter = ad3;
                            max_vram = desc.DedicatedVideoMemory;

                            cached_info.adapter_name = WideToUtf8(desc.Description);
                            cached_info.vendor_id = desc.VendorId;
                            cached_info.device_id = desc.DeviceId;
                            cached_info.dedicated_vram_bytes = desc.DedicatedVideoMemory;
                            cached_info.shared_vram_bytes = desc.SharedSystemMemory;
                            cached_info.is_discrete = (desc.DedicatedVideoMemory > 256 * 1024 * 1024);
                        } else {
                            ad3->Release();
                        }
                    }
                }
            }
            adapter1->Release();
        }
        primary_adapter3 = best_adapter;
        cached_info.is_nvml_supported = nvml.IsAvailable();
    }
#endif
};

GpuCollector::GpuCollector() : impl(new Impl()) {}
GpuCollector::~GpuCollector() { delete impl; }

GpuInfo GpuCollector::DetectGpuInfo() {
#ifdef _WIN32
    return impl->cached_info;
#else
    return {};
#endif
}

bool GpuCollector::Sample(GpuSample& out_sample) {
    out_sample = {};
    out_sample.timestamp_us = NowUs();
#ifdef _WIN32
    out_sample.gpu_engine_3d_percent = impl->pdh.Sample();
    out_sample.gpu_temp_c = impl->nvml.GetTemperatureC();
    out_sample.core_clock_mhz = impl->nvml.GetCoreClockMhz();
    out_sample.mem_clock_mhz = impl->nvml.GetMemClockMhz();
    out_sample.power_watts = impl->nvml.GetPowerWatts();
    out_sample.power_limit_watts = impl->nvml.GetPowerLimitWatts();

    if (impl->primary_adapter3) {
        DXGI_QUERY_VIDEO_MEMORY_INFO local_info{};
        if (SUCCEEDED(impl->primary_adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local_info))) {
            out_sample.vram_local_used_bytes = local_info.CurrentUsage;
            out_sample.vram_local_budget_bytes = local_info.Budget;
        }
        DXGI_QUERY_VIDEO_MEMORY_INFO non_local_info{};
        if (SUCCEEDED(impl->primary_adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &non_local_info))) {
            out_sample.vram_non_local_used_bytes = non_local_info.CurrentUsage;
        }
    }
    return true;
#else
    return false;
#endif
}

} // namespace APDS
