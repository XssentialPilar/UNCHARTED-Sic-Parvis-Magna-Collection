// SPDX-FileCopyrightText: Copyright 2026 gow3 contributors
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef _WIN32

#include "video_core/renderer_vulkan/vk_streamline.h"

namespace Vulkan {
Streamline* Streamline::Get() { return nullptr; }
Streamline::~Streamline() = default;
bool Streamline::InitBeforeInstance() { return false; }
PFN_vkGetInstanceProcAddr Streamline::GetVkInstanceProcAddr() const noexcept { return nullptr; }
void Streamline::AppendInstanceExtensions(std::vector<const char*>&) const {}
void Streamline::PreparePhysicalDevice(vk::Instance, vk::PhysicalDevice,
                                       std::vector<const char*>&,
                                       vk::PhysicalDeviceVulkan12Features&,
                                       vk::PhysicalDeviceVulkan13Features&) {}
void Streamline::OnDeviceCreated(vk::Instance, vk::PhysicalDevice, vk::Device) {}
void Streamline::Shutdown() {}
void Streamline::UnloadInterposer() {}
void Streamline::BeginFrame(u32, SlReflexMode, u32) {}
void Streamline::SetPclMarker(SlPclMarker, u32) {}
bool Streamline::SetConstants(const FrameConstants&, u32, u32) { return false; }
Streamline::FrameConstants Streamline::BuildConstants(const CameraInput&) { return {}; }
bool Streamline::SetCameraConstants(const CameraInput&, u32, u32) { return false; }
bool Streamline::TagImage(vk::CommandBuffer, u32, const ImageResource&, bool, u32, u32, u32, u32) {
    return false;
}
bool Streamline::EvaluateDlss(vk::CommandBuffer, const DlssEvalDesc&, u32, u32) { return false; }
bool Streamline::UpdateDlssG(const DlssGDesc&, u32) { return false; }
bool Streamline::UpdateReflex(SlReflexMode, u32) { return false; }
Streamline::ReflexTelemetry Streamline::QueryTelemetry(u32) { return {}; }
bool Streamline::QueryOptimalDlssSize(u32, u32, SlDlssMode, u32&, u32&, float&) { return false; }
void Streamline::FreeFeatureResources(u32) {}
SlDlssPreset Streamline::PresetFromInt(int v) noexcept { return static_cast<SlDlssPreset>(v); }
SlDlssMode Streamline::ModeFromScalePreset(int) noexcept { return SlDlssMode::Off; }
} // namespace Vulkan

#else

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "common/logging/log.h"
#include "gow3_settings.h"
#include "video_core/renderer_vulkan/vk_streamline.h"

#include <sl.h>
#include <sl_consts.h>
#include <sl_core_api.h>
#include <sl_core_types.h>
#include <sl_dlss.h>
#include <sl_dlss_g.h>
#include <sl_helpers.h>
#include <sl_helpers_vk.h>
#include <sl_matrix_helpers.h>
#include <sl_nis.h>
#include <sl_pcl.h>
#include <sl_reflex.h>
#include <sl_result.h>
#include <sl_version.h>

namespace Vulkan {

namespace {

// Unique non-zero application ID for Uncharted / GOW3 SPM Vulkan runtime (custom range).
constexpr uint32_t kSpmAppId = 0x554E4348u; // 'UNCH'

void StreamlineLogCallback(sl::LogType type, const char* msg) {
    if (!msg) {
        return;
    }
    std::string text(msg);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }
    if (text.empty()) {
        return;
    }
    switch (type) {
    case sl::LogType::eError:
        LOG_ERROR(Render_Vulkan, "[Streamline] {}", text);
        std::printf("[Streamline ERROR] %s\n", text.c_str());
        std::fflush(stdout);
        break;
    case sl::LogType::eWarn:
        LOG_WARNING(Render_Vulkan, "[Streamline] {}", text);
        std::printf("[Streamline WARN] %s\n", text.c_str());
        break;
    case sl::LogType::eInfo:
    default:
        LOG_INFO(Render_Vulkan, "[Streamline] {}", text);
        if (const char* v = std::getenv("GOW3_SL_VERBOSE"); v && std::strcmp(v, "1") == 0) {
            std::printf("[Streamline INFO] %s\n", text.c_str());
        }
        break;
    }
}

bool VerifyNvidiaAuthenticode(const std::wstring& path) {
    if (const char* skip = std::getenv("GOW3_SL_ALLOW_UNSIGNED"); skip && std::strcmp(skip, "1") == 0) {
        return true;
    }
    HMODULE wintrust = LoadLibraryW(L"wintrust.dll");
    HMODULE crypt32 = LoadLibraryW(L"crypt32.dll");
    if (!wintrust || !crypt32) {
        if (wintrust) FreeLibrary(wintrust);
        if (crypt32) FreeLibrary(crypt32);
        return false;
    }
    using PfnWinVerifyTrust = LONG(WINAPI*)(HWND, GUID*, LPVOID);
    using PfnWTHelperProvDataFromStateData = CRYPT_PROVIDER_DATA*(WINAPI*)(HANDLE);
    using PfnWTHelperGetProvSignerFromChain = CRYPT_PROVIDER_SGNR*(WINAPI*)(CRYPT_PROVIDER_DATA*, DWORD, BOOL, DWORD);
    using PfnCertGetNameStringW = DWORD(WINAPI*)(PCCERT_CONTEXT, DWORD, DWORD, void*, LPWSTR, DWORD);

    auto pWinVerifyTrust = reinterpret_cast<PfnWinVerifyTrust>(
        GetProcAddress(wintrust, "WinVerifyTrust"));
    auto pProvData = reinterpret_cast<PfnWTHelperProvDataFromStateData>(
        GetProcAddress(wintrust, "WTHelperProvDataFromStateData"));
    auto pProvSigner = reinterpret_cast<PfnWTHelperGetProvSignerFromChain>(
        GetProcAddress(wintrust, "WTHelperGetProvSignerFromChain"));
    auto pCertGetName = reinterpret_cast<PfnCertGetNameStringW>(
        GetProcAddress(crypt32, "CertGetNameStringW"));

    bool verified = false;
    if (pWinVerifyTrust && pProvData && pProvSigner && pCertGetName) {
        WINTRUST_FILE_INFO file_info{};
        file_info.cbStruct = sizeof(file_info);
        file_info.pcwszFilePath = path.c_str();

        WINTRUST_DATA wt_data{};
        wt_data.cbStruct = sizeof(wt_data);
        wt_data.dwUIChoice = WTD_UI_NONE;
        wt_data.fdwRevocationChecks = WTD_REVOKE_NONE;
        wt_data.dwUnionChoice = WTD_CHOICE_FILE;
        wt_data.dwStateAction = WTD_STATEACTION_VERIFY;
        wt_data.pFile = &file_info;

        GUID policy_guid = WINTRUST_ACTION_GENERIC_VERIFY_V2;
        LONG status = pWinVerifyTrust(nullptr, &policy_guid, &wt_data);
        if (status == ERROR_SUCCESS) {
            if (auto* data = pProvData(wt_data.hWVTStateData)) {
                if (auto* signer = pProvSigner(data, 0, FALSE, 0);
                    signer && signer->csCertChain > 0 && signer->pasCertChain &&
                    signer->pasCertChain[0].pCert) {
                    wchar_t name_buf[256]{};
                    DWORD len = pCertGetName(signer->pasCertChain[0].pCert,
                                             CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr,
                                             name_buf, static_cast<DWORD>(std::size(name_buf)));
                    if (len > 0 && std::wcsstr(name_buf, L"NVIDIA") != nullptr) {
                        verified = true;
                    }
                }
            }
        }
        wt_data.dwStateAction = WTD_STATEACTION_CLOSE;
        pWinVerifyTrust(nullptr, &policy_guid, &wt_data);
    }

    FreeLibrary(crypt32);
    FreeLibrary(wintrust);
    return verified;
}

std::wstring ExecutableDirectory() {
    wchar_t buf[MAX_PATH]{};
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path(buf).parent_path().wstring();
}

void AppendUniqueExt(std::vector<const char*>& dst, const char* ext) {
    if (!ext || !*ext) {
        return;
    }
    for (const char* existing : dst) {
        if (existing && std::strcmp(existing, ext) == 0) {
            return;
        }
    }
    dst.push_back(ext);
}

sl::DLSSMode ToSlDlssMode(SlDlssMode mode) noexcept {
    switch (mode) {
    case SlDlssMode::Dlaa:
        return sl::DLSSMode::eDLAA;
    case SlDlssMode::Quality:
        return sl::DLSSMode::eMaxQuality;
    case SlDlssMode::Balanced:
        return sl::DLSSMode::eBalanced;
    case SlDlssMode::Performance:
        return sl::DLSSMode::eMaxPerformance;
    case SlDlssMode::UltraPerformance:
        return sl::DLSSMode::eUltraPerformance;
    case SlDlssMode::UltraQuality:
        return sl::DLSSMode::eUltraQuality;
    case SlDlssMode::Off:
    default:
        return sl::DLSSMode::eOff;
    }
}

sl::DLSSPreset ToSlDlssPreset(SlDlssPreset preset, SlDlssPreset mode_default) noexcept {
    SlDlssPreset eff = (preset == SlDlssPreset::Auto) ? mode_default : preset;
    switch (eff) {
    case SlDlssPreset::PresetJ:
        return sl::DLSSPreset::ePresetJ;
    case SlDlssPreset::PresetK:
        return sl::DLSSPreset::ePresetK;
    case SlDlssPreset::PresetL:
        return sl::DLSSPreset::ePresetL;
    case SlDlssPreset::PresetM:
        return sl::DLSSPreset::ePresetM;
    case SlDlssPreset::Auto:
    default:
        return sl::DLSSPreset::eDefault;
    }
}

sl::ReflexMode ToSlReflexMode(SlReflexMode mode) noexcept {
    switch (mode) {
    case SlReflexMode::On:
        return sl::eLowLatency;
    case SlReflexMode::Boost:
        return sl::eLowLatencyWithBoost;
    case SlReflexMode::Off:
    default:
        return sl::eOff;
    }
}

sl::PCLMarker ToSlPclMarker(SlPclMarker m) noexcept {
    switch (m) {
    case SlPclMarker::SimulationStart:
        return sl::PCLMarker::eSimulationStart;
    case SlPclMarker::SimulationEnd:
        return sl::PCLMarker::eSimulationEnd;
    case SlPclMarker::RenderSubmitStart:
        return sl::PCLMarker::eRenderSubmitStart;
    case SlPclMarker::RenderSubmitEnd:
        return sl::PCLMarker::eRenderSubmitEnd;
    case SlPclMarker::PresentStart:
        return sl::PCLMarker::ePresentStart;
    case SlPclMarker::PresentEnd:
        return sl::PCLMarker::ePresentEnd;
    case SlPclMarker::InputSample:
    default:
        return sl::PCLMarker::eControllerInputSample;
    }
}

sl::float4x4 CopyRowMajor4x4(const float src[16]) noexcept {
    sl::float4x4 out{};
    out[0] = {src[0], src[1], src[2], src[3]};
    out[1] = {src[4], src[5], src[6], src[7]};
    out[2] = {src[8], src[9], src[10], src[11]};
    out[3] = {src[12], src[13], src[14], src[15]};
    return out;
}

template <typename Fn>
bool LoadProc(HMODULE dll, const char* name, Fn& out) {
    out = reinterpret_cast<Fn>(GetProcAddress(dll, name));
    return out != nullptr;
}

} // namespace

struct Streamline::Impl {
    // Core Streamline exports
    PFun_slInit* slInit{nullptr};
    PFun_slShutdown* slShutdown{nullptr};
    PFun_slIsFeatureSupported* slIsFeatureSupported{nullptr};
    PFun_slGetFeatureRequirements* slGetFeatureRequirements{nullptr};
    PFun_slGetNewFrameToken* slGetNewFrameToken{nullptr};
    PFun_slSetConstants* slSetConstants{nullptr};
    PFun_slSetTagForFrame* slSetTagForFrame{nullptr};
    PFun_slEvaluateFeature* slEvaluateFeature{nullptr};
    PFun_slGetFeatureFunction* slGetFeatureFunction{nullptr};
    PFun_slFreeResources* slFreeResources{nullptr};

    // Feature functions (bound after OnDeviceCreated)
    PFun_slDLSSGetOptimalSettings* dlssGetOptimalSettings{nullptr};
    PFun_slDLSSGetState* dlssGetState{nullptr};
    PFun_slDLSSSetOptions* dlssSetOptions{nullptr};

    PFun_slDLSSGSetOptions* dlssGSetOptions{nullptr};
    PFun_slDLSSGGetState* dlssGGetState{nullptr};

    PFun_slReflexSetOptions* reflexSetOptions{nullptr};
    PFun_slReflexSleep* reflexSleep{nullptr};
    PFun_slReflexGetState* reflexGetState{nullptr};

    PFun_slPCLSetMarker* pclSetMarker{nullptr};

    // Cached last options to avoid redundant driver churn
    bool dlss_g_options_dirty{true};
    DlssGDesc last_dlss_g_desc{};
    bool reflex_options_dirty{true};
    SlReflexMode last_reflex_mode{SlReflexMode::Off};
    u32 last_frame_limit_us{0};
    u64 dlss_vram_estimate{0};
    u64 dlss_g_vram_estimate{0};
};

Streamline* Streamline::Get() {
    static Streamline instance;
    return &instance;
}

Streamline::~Streamline() {
    Shutdown();
    UnloadInterposer();
}

bool Streamline::InitBeforeInstance() {
    std::scoped_lock lock(mutex_);
    if (init_attempted_) {
        return initialized_;
    }
    init_attempted_ = true;

    if (const char* env = std::getenv("GOW3_STREAMLINE"); env && std::strcmp(env, "0") == 0) {
        status_summary_ = "Disabled by GOW3_STREAMLINE=0";
        return false;
    }

    const std::wstring exe_dir = ExecutableDirectory();
    if (exe_dir.empty()) {
        status_summary_ = "Cannot determine executable directory for sl.interposer.dll";
        std::printf("Streamline: %s\n", status_summary_.c_str());
        return false;
    }

    std::filesystem::path plugin_path;
    {
        std::vector<std::filesystem::path> candidates;
        if (const char* env_dir = std::getenv("GOW3_STREAMLINE_DIR"); env_dir && env_dir[0]) {
            candidates.emplace_back(env_dir);
        }
        const std::filesystem::path exe_p(exe_dir);
        candidates.push_back(exe_p);
        candidates.push_back(exe_p / L"..");
        candidates.push_back(exe_p / L".." / L"..");
        std::error_code ec;
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        if (!cwd.empty()) {
            candidates.push_back(cwd);
            candidates.push_back(cwd / L"core");
            candidates.push_back(cwd / L"Project" / L"gow3_pc" / L"gpu" / L"third_party" / L"streamline" / L"bin" / L"x64");
        }
        candidates.push_back(exe_p / L".." / L"gpu" / L"third_party" / L"streamline" / L"bin" / L"x64");
        candidates.push_back(exe_p / L".." / L".." / L"gpu" / L"third_party" / L"streamline" / L"bin" / L"x64");

        for (const auto& dir : candidates) {
            const auto candidate = dir / L"sl.interposer.dll";
            if (std::filesystem::is_regular_file(candidate, ec)) {
                plugin_path = std::filesystem::weakly_canonical(dir, ec);
                if (plugin_path.empty()) {
                    plugin_path = dir;
                }
                break;
            }
        }
    }

    if (plugin_path.empty()) {
        status_summary_ = "sl.interposer.dll not found next to executable or parent core dir";
        std::printf("Streamline: %s (exe_dir=%ls)\n", status_summary_.c_str(), exe_dir.c_str());
        LOG_INFO(Render_Vulkan, "Streamline not loaded: {}", status_summary_);
        return false;
    }

    const std::wstring plugin_dir = plugin_path.wstring();
    const std::wstring interposer_path = (plugin_path / L"sl.interposer.dll").wstring();

    if (!VerifyNvidiaAuthenticode(interposer_path)) {
        status_summary_ = "sl.interposer.dll failed NVIDIA Authenticode signature check";
        std::printf("Streamline: refused (%s)\n", status_summary_.c_str());
        LOG_WARNING(Render_Vulkan, "Streamline refused: {}", status_summary_);
        return false;
    }

    HMODULE dll = LoadLibraryExW(interposer_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!dll) {
        status_summary_ = "LoadLibraryExW(sl.interposer.dll) failed";
        std::printf("Streamline: %s (err=%lu)\n", status_summary_.c_str(), GetLastError());
        LOG_WARNING(Render_Vulkan, "{}", status_summary_);
        return false;
    }

    auto* new_impl = new Impl();
    bool core_ok =
        LoadProc(dll, "slInit", new_impl->slInit) &&
        LoadProc(dll, "slShutdown", new_impl->slShutdown) &&
        LoadProc(dll, "slIsFeatureSupported", new_impl->slIsFeatureSupported) &&
        LoadProc(dll, "slGetFeatureRequirements", new_impl->slGetFeatureRequirements) &&
        LoadProc(dll, "slGetNewFrameToken", new_impl->slGetNewFrameToken) &&
        LoadProc(dll, "slSetConstants", new_impl->slSetConstants) &&
        LoadProc(dll, "slSetTagForFrame", new_impl->slSetTagForFrame) &&
        LoadProc(dll, "slEvaluateFeature", new_impl->slEvaluateFeature) &&
        LoadProc(dll, "slGetFeatureFunction", new_impl->slGetFeatureFunction) &&
        LoadProc(dll, "slFreeResources", new_impl->slFreeResources);

    auto sl_vk_proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        GetProcAddress(dll, "vkGetInstanceProcAddr"));

    if (!core_ok || !sl_vk_proc) {
        delete new_impl;
        FreeLibrary(dll);
        status_summary_ = "sl.interposer.dll missing required core/Vulkan exports";
        LOG_WARNING(Render_Vulkan, "{}", status_summary_);
        return false;
    }

    static constexpr std::array<sl::Feature, 5> kWantedFeatures = {
        sl::kFeatureDLSS,
        sl::kFeatureDLSS_G,
        sl::kFeatureReflex,
        sl::kFeaturePCL,
        sl::kFeatureNIS,
    };

    sl::Preferences pref{};
    pref.showConsole = false;
    pref.logLevel = sl::LogLevel::eDefault;
    if (const char* lvl = std::getenv("GOW3_SL_VERBOSE"); lvl && std::strcmp(lvl, "1") == 0) {
        pref.logLevel = sl::LogLevel::eVerbose;
    }
    pref.logMessageCallback = &StreamlineLogCallback;
    pref.featuresToLoad = kWantedFeatures.data();
    pref.numFeaturesToLoad = static_cast<uint32_t>(kWantedFeatures.size());
    pref.applicationId = kSpmAppId;
    pref.engine = sl::EngineType::eCustom;
    pref.engineVersion = "0.6.1";
    pref.renderAPI = sl::RenderAPI::eVulkan;
    pref.flags = sl::PreferenceFlags::eDisableCLStateTracking |
                 sl::PreferenceFlags::eUseFrameBasedResourceTagging;

    const wchar_t* paths_to_plugins[] = {plugin_dir.c_str()};
    pref.pathsToPlugins = paths_to_plugins;
    pref.numPathsToPlugins = 1;

    sl::Result res = new_impl->slInit(pref, sl::kSDKVersion);
    if (res != sl::Result::eOk) {
        status_summary_ = std::string("slInit failed: ") + sl::getResultAsStr(res);
        std::printf("Streamline: %s (plugins=%ls)\n", status_summary_.c_str(), plugin_dir.c_str());
        LOG_WARNING(Render_Vulkan, "Streamline slInit failed: {}", status_summary_);
        delete new_impl;
        FreeLibrary(dll);
        return false;
    }

    interposer_dll_ = dll;
    impl_ = new_impl;
    sl_vk_get_instance_proc_ = sl_vk_proc;
    initialized_ = true;
    status_summary_ = "Streamline v2.14.16 initialized (Vulkan interposer active)";
    std::printf("Streamline: loaded %ls (plugins=%ls)\n", interposer_path.c_str(), plugin_dir.c_str());
    LOG_INFO(Render_Vulkan, "{}", status_summary_);
    return true;
}

PFN_vkGetInstanceProcAddr Streamline::GetVkInstanceProcAddr() const noexcept {
    return initialized_ ? sl_vk_get_instance_proc_ : nullptr;
}

void Streamline::AppendInstanceExtensions(std::vector<const char*>& enabled) const {
    if (!initialized_ || !impl_) {
        return;
    }
    static constexpr std::array<sl::Feature, 5> kFeatures = {
        sl::kFeatureDLSS,
        sl::kFeatureDLSS_G,
        sl::kFeatureReflex,
        sl::kFeaturePCL,
        sl::kFeatureNIS,
    };
    for (sl::Feature feat : kFeatures) {
        sl::FeatureRequirements req{};
        if (impl_->slGetFeatureRequirements(feat, req) == sl::Result::eOk) {
            if (req.flags & sl::FeatureRequirementFlags::eVulkanSupported) {
                for (uint32_t i = 0; i < req.vkNumInstanceExtensions; ++i) {
                    AppendUniqueExt(enabled, req.vkInstanceExtensions[i]);
                }
            }
        }
    }
}

void Streamline::PreparePhysicalDevice(vk::Instance /*instance*/, vk::PhysicalDevice physical,
                                       std::vector<const char*>& enabled_device_exts,
                                       vk::PhysicalDeviceVulkan12Features& vk12,
                                       vk::PhysicalDeviceVulkan13Features& vk13) {
    std::scoped_lock lock(mutex_);
    if (!initialized_ || !impl_) {
        return;
    }

    sl::AdapterInfo adapter{};
    adapter.vkPhysicalDevice = static_cast<void*>(static_cast<VkPhysicalDevice>(physical));

    auto probe = [&](sl::Feature f, const char* name, bool& flag) {
        sl::Result sr = impl_->slIsFeatureSupported(f, adapter);
        flag = (sr == sl::Result::eOk);
        if (!flag) {
            LOG_INFO(Render_Vulkan, "Streamline feature {} unavailable on GPU: {}",
                     name, sl::getResultAsStr(sr));
            return;
        }
        sl::FeatureRequirements req{};
        if (impl_->slGetFeatureRequirements(f, req) == sl::Result::eOk &&
            (req.flags & sl::FeatureRequirementFlags::eVulkanSupported)) {
            for (uint32_t i = 0; i < req.vkNumDeviceExtensions; ++i) {
                AppendUniqueExt(enabled_device_exts, req.vkDeviceExtensions[i]);
            }
            if (req.vkNumFeatures12 > 0 && req.vkFeatures12) {
                VkPhysicalDeviceVulkan12Features req12 =
                    sl::getVkPhysicalDeviceVulkan12Features(req.vkNumFeatures12, req.vkFeatures12);
                VkPhysicalDeviceVulkan12Features dst12 = vk12;
                if (req12.timelineSemaphore) dst12.timelineSemaphore = VK_TRUE;
                if (req12.bufferDeviceAddress) dst12.bufferDeviceAddress = VK_TRUE;
                if (req12.descriptorIndexing) dst12.descriptorIndexing = VK_TRUE;
                if (req12.hostQueryReset) dst12.hostQueryReset = VK_TRUE;
                if (req12.scalarBlockLayout) dst12.scalarBlockLayout = VK_TRUE;
                if (req12.shaderFloat16) dst12.shaderFloat16 = VK_TRUE;
                if (req12.shaderSubgroupExtendedTypes) dst12.shaderSubgroupExtendedTypes = VK_TRUE;
                if (req12.vulkanMemoryModel) dst12.vulkanMemoryModel = VK_TRUE;
                if (req12.vulkanMemoryModelDeviceScope) dst12.vulkanMemoryModelDeviceScope = VK_TRUE;
                vk12 = dst12;
            }
            if (req.vkNumFeatures13 > 0 && req.vkFeatures13) {
                VkPhysicalDeviceVulkan13Features req13 =
                    sl::getVkPhysicalDeviceVulkan13Features(req.vkNumFeatures13, req.vkFeatures13);
                VkPhysicalDeviceVulkan13Features dst13 = vk13;
                if (req13.synchronization2) dst13.synchronization2 = VK_TRUE;
                if (req13.dynamicRendering) dst13.dynamicRendering = VK_TRUE;
                if (req13.maintenance4) dst13.maintenance4 = VK_TRUE;
                if (req13.computeFullSubgroups) dst13.computeFullSubgroups = VK_TRUE;
                if (req13.subgroupSizeControl) dst13.subgroupSizeControl = VK_TRUE;
                vk13 = dst13;
            }
        }
        LOG_INFO(Render_Vulkan, "Streamline feature {} supported on selected GPU", name);
    };

    probe(sl::kFeatureDLSS, "DLSS-SR", dlss_supported_);
    probe(sl::kFeatureDLSS_G, "DLSS-G", dlss_g_supported_);
    probe(sl::kFeatureReflex, "Reflex", reflex_supported_);
    probe(sl::kFeaturePCL, "PCL", pcl_supported_);
    probe(sl::kFeatureNIS, "NIS", nis_supported_);
}

void Streamline::OnDeviceCreated(vk::Instance /*instance*/, vk::PhysicalDevice /*physical*/,
                                 vk::Device /*device*/) {
    std::scoped_lock lock(mutex_);
    if (!initialized_ || !impl_ || device_initialized_) {
        return;
    }

    auto bind = [&](sl::Feature feat, const char* fn_name, auto& fn_ptr) {
        void* p = nullptr;
        if (impl_->slGetFeatureFunction(feat, fn_name, p) == sl::Result::eOk && p) {
            fn_ptr = reinterpret_cast<std::remove_reference_t<decltype(fn_ptr)>>(p);
            return true;
        }
        return false;
    };

    if (dlss_supported_) {
        bool ok = bind(sl::kFeatureDLSS, "slDLSSGetOptimalSettings", impl_->dlssGetOptimalSettings) &&
                  bind(sl::kFeatureDLSS, "slDLSSSetOptions", impl_->dlssSetOptions);
        bind(sl::kFeatureDLSS, "slDLSSGetState", impl_->dlssGetState);
        if (!ok) {
            dlss_supported_ = false;
            LOG_WARNING(Render_Vulkan, "Streamline DLSS function lookup failed; disabling DLSS");
        }
    }

    if (dlss_g_supported_) {
        bool ok = bind(sl::kFeatureDLSS_G, "slDLSSGSetOptions", impl_->dlssGSetOptions) &&
                  bind(sl::kFeatureDLSS_G, "slDLSSGGetState", impl_->dlssGGetState);
        if (!ok) {
            dlss_g_supported_ = false;
            std::printf("Streamline: DLSS-G function lookup failed; disabling DLSS-G\n");
            LOG_WARNING(Render_Vulkan, "Streamline DLSS-G function lookup failed; disabling DLSS-G");
        }
    }

    if (reflex_supported_) {
        bool ok = bind(sl::kFeatureReflex, "slReflexSetOptions", impl_->reflexSetOptions) &&
                  bind(sl::kFeatureReflex, "slReflexSleep", impl_->reflexSleep) &&
                  bind(sl::kFeatureReflex, "slReflexGetState", impl_->reflexGetState);
        if (!ok) {
            reflex_supported_ = false;
            std::printf("Streamline: Reflex function lookup failed\n");
            LOG_WARNING(Render_Vulkan, "Streamline Reflex function lookup failed");
        }
    }

    if (pcl_supported_) {
        if (!bind(sl::kFeaturePCL, "slPCLSetMarker", impl_->pclSetMarker)) {
            pcl_supported_ = false;
        }
    }

    if (dlss_g_supported_) {
        RefreshDlssGStateLocked(0);
    }

    device_initialized_ = true;
    const bool fsr_fg_ok = Gow3Settings::Get().fsr_fg_supported.load(std::memory_order_relaxed);
    Gow3Settings::ConfigureStreamlineSupport(
        dlss_supported_, dlss_g_supported_,
        dlss_g_supported_ ? static_cast<int>(dlss_g_max_frames_ + 1) : 1,
        reflex_supported_, dlss_g_vsync_supported_, fsr_fg_ok);
    status_summary_ = fmt::format(
        "Streamline v2.14.16 | DLSS:{} | DLSS-G:{}(max={}x,dyn={}) | Reflex:{} | PCL:{}",
        dlss_supported_ ? "ON" : "NO",
        dlss_g_supported_ ? "ON" : "NO",
        dlss_g_supported_ ? (dlss_g_max_frames_ + 1) : 1,
        dlss_g_dynamic_mfg_supported_ ? "YES" : "NO",
        reflex_supported_ ? "ON" : "NO",
        pcl_supported_ ? "ON" : "NO");
    std::printf("Streamline: %s\n", status_summary_.c_str());
    std::fflush(stdout);
    LOG_INFO(Render_Vulkan, "{}", status_summary_);
}

void Streamline::Shutdown() {
    std::scoped_lock lock(mutex_);
    if (!initialized_) {
        return;
    }
    if (impl_ && impl_->slShutdown) {
        impl_->slShutdown();
    }
    frame_tokens_.clear();
    delete impl_;
    impl_ = nullptr;
    sl_vk_get_instance_proc_ = nullptr;
    initialized_ = false;
    device_initialized_ = false;
    dlss_supported_ = false;
    dlss_g_supported_ = false;
    reflex_supported_ = false;
    pcl_supported_ = false;
    nis_supported_ = false;
}

void Streamline::UnloadInterposer() {
    std::scoped_lock lock(mutex_);
    if (interposer_dll_) {
        FreeLibrary(static_cast<HMODULE>(interposer_dll_));
        interposer_dll_ = nullptr;
    }
    init_attempted_ = false;
}

sl::FrameToken* Streamline::GetOrCreateTokenLocked(u32 frame_index) {
    if (!impl_ || !impl_->slGetNewFrameToken) {
        return nullptr;
    }
    if (frame_index == UINT32_MAX) {
        frame_index = last_frame_index_;
    }
    if (auto it = frame_tokens_.find(frame_index); it != frame_tokens_.end() && it->second) {
        return it->second;
    }
    sl::FrameToken* tok = nullptr;
    uint32_t fi = frame_index;
    if (impl_->slGetNewFrameToken(tok, &fi) == sl::Result::eOk && tok) {
        frame_tokens_[frame_index] = tok;
        // Bound ring history size cleanly.
        if (frame_tokens_.size() > 16) {
            for (auto it = frame_tokens_.begin(); it != frame_tokens_.end();) {
                if (frame_index > it->first + 8 || it->first > frame_index + 8) {
                    it = frame_tokens_.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }
    return tok;
}

void Streamline::BeginFrame(u32 frame_index, SlReflexMode reflex_mode, u32 frame_limit_us) {
    std::scoped_lock lock(mutex_);
    if (!device_initialized_ || !impl_) {
        return;
    }
    last_frame_index_ = frame_index;

    // Force-acquire a fresh token for `frame_index` each rendered frame.
    sl::FrameToken* tok = nullptr;
    uint32_t fi = frame_index;
    if (impl_->slGetNewFrameToken(tok, &fi) == sl::Result::eOk && tok) {
        frame_tokens_[frame_index] = tok;
    }

    if (reflex_supported_ && impl_->reflexSetOptions) {
        if (impl_->reflex_options_dirty || impl_->last_reflex_mode != reflex_mode ||
            impl_->last_frame_limit_us != frame_limit_us) {
            sl::ReflexOptions opts{};
            opts.mode = ToSlReflexMode(reflex_mode);
            opts.frameLimitUs = frame_limit_us;
            opts.useMarkersToOptimize = pcl_supported_;
            if (impl_->reflexSetOptions(opts) == sl::Result::eOk) {
                impl_->last_reflex_mode = reflex_mode;
                impl_->last_frame_limit_us = frame_limit_us;
                impl_->reflex_options_dirty = false;
            }
        }
        if (reflex_mode != SlReflexMode::Off && impl_->reflexSleep && tok) {
            impl_->reflexSleep(*tok);
        }
    }
}

void Streamline::SetPclMarker(SlPclMarker marker, u32 frame_index) {
    std::scoped_lock lock(mutex_);
    if (!device_initialized_ || !impl_ || !pcl_supported_ || !impl_->pclSetMarker) {
        return;
    }
    sl::FrameToken* tok = GetOrCreateTokenLocked(frame_index);
    if (!tok) {
        return;
    }
    impl_->pclSetMarker(ToSlPclMarker(marker), *tok);
}

bool Streamline::SetConstants(const FrameConstants& c, u32 frame_index, u32 viewport_id) {
    std::scoped_lock lock(mutex_);
    if (!device_initialized_ || !impl_ || !impl_->slSetConstants) {
        return false;
    }
    sl::FrameToken* tok = GetOrCreateTokenLocked(frame_index);
    if (!tok) {
        return false;
    }

    sl::Constants sc{};
    sc.cameraViewToClip = CopyRowMajor4x4(c.camera_view_to_clip);
    sc.clipToCameraView = CopyRowMajor4x4(c.clip_to_camera_view);
    sc.clipToPrevClip = CopyRowMajor4x4(c.clip_to_prev_clip);
    sc.prevClipToClip = CopyRowMajor4x4(c.prev_clip_to_clip);
    sc.jitterOffset = {c.jitter_x, c.jitter_y};
    sc.mvecScale = {c.mvec_scale_x, c.mvec_scale_y};
    sc.cameraPinholeOffset = {0.0f, 0.0f};
    sc.cameraPos = {c.camera_pos[0], c.camera_pos[1], c.camera_pos[2]};
    sc.cameraUp = {c.camera_up[0], c.camera_up[1], c.camera_up[2]};
    sc.cameraRight = {c.camera_right[0], c.camera_right[1], c.camera_right[2]};
    sc.cameraFwd = {c.camera_fwd[0], c.camera_fwd[1], c.camera_fwd[2]};
    sc.cameraNear = c.camera_near;
    sc.cameraFar = c.camera_far;
    sc.cameraFOV = c.camera_fov;
    sc.cameraAspectRatio = c.camera_aspect;
    sc.motionVectorsInvalidValue = sl::INVALID_FLOAT;
    sc.depthInverted = c.depth_inverted ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    sc.cameraMotionIncluded = c.camera_valid ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    sc.motionVectors3D = sl::Boolean::eFalse;
    sc.reset = c.reset ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    sc.orthographicProjection = sl::Boolean::eFalse;
    sc.motionVectorsDilated = sl::Boolean::eFalse;
    sc.motionVectorsJittered = c.motion_vectors_jittered ? sl::Boolean::eTrue : sl::Boolean::eFalse;

    sl::ViewportHandle vp(viewport_id);
    return impl_->slSetConstants(sc, *tok, vp) == sl::Result::eOk;
}

Streamline::FrameConstants Streamline::BuildConstants(const CameraInput& cam) {
    FrameConstants fc{};
    const u32 rw = cam.render_width ? cam.render_width : 1u;
    const u32 rh = cam.render_height ? cam.render_height : 1u;
    fc.jitter_x = cam.jitter_x;
    fc.jitter_y = cam.jitter_y;
    fc.mvec_scale_x = 1.0f / static_cast<float>(rw);
    fc.mvec_scale_y = 1.0f / static_cast<float>(rh);
    fc.motion_vector_resolution_width = static_cast<float>(rw);
    fc.motion_vector_resolution_height = static_cast<float>(rh);
    fc.camera_near = cam.camera_near > 0.0f ? cam.camera_near : 0.1f;
    fc.camera_far = 5000.0f;
    fc.camera_fov = cam.camera_fov > 0.0f ? cam.camera_fov : 0.785398f;
    fc.camera_aspect = static_cast<float>(rw) / static_cast<float>(rh);
    fc.camera_valid = cam.valid;
    fc.reset = cam.reset || !cam.valid || !cam.prev_valid;
    fc.depth_inverted = false;
    fc.motion_vectors_jittered = false;

    auto make_proj = [](const float p[4]) -> sl::float4x4 {
        sl::float4x4 m{};
        m[0] = {p[0], 0.0f, 0.0f, 0.0f};
        m[1] = {0.0f, p[1], 0.0f, 0.0f};
        m[2] = {0.0f, 0.0f, p[2], 1.0f};
        m[3] = {0.0f, 0.0f, p[3], 0.0f};
        return m;
    };
    auto make_view_to_world = [](const float iv[12]) -> sl::float4x4 {
        sl::float4x4 m{};
        m[0] = {iv[0], iv[4], iv[8], 0.0f};
        m[1] = {iv[1], iv[5], iv[9], 0.0f};
        m[2] = {iv[2], iv[6], iv[10], 0.0f};
        m[3] = {iv[3], iv[7], iv[11], 1.0f};
        return m;
    };
    auto copy_mat = [](const sl::float4x4& m, float dst[16]) {
        dst[0] = m[0].x; dst[1] = m[0].y; dst[2] = m[0].z; dst[3] = m[0].w;
        dst[4] = m[1].x; dst[5] = m[1].y; dst[6] = m[1].z; dst[7] = m[1].w;
        dst[8] = m[2].x; dst[9] = m[2].y; dst[10] = m[2].z; dst[11] = m[2].w;
        dst[12] = m[3].x; dst[13] = m[3].y; dst[14] = m[3].z; dst[15] = m[3].w;
    };

    if (cam.valid) {
        fc.camera_right[0] = cam.cur_inv_view[0];
        fc.camera_right[1] = cam.cur_inv_view[4];
        fc.camera_right[2] = cam.cur_inv_view[8];
        fc.camera_up[0] = cam.cur_inv_view[1];
        fc.camera_up[1] = cam.cur_inv_view[5];
        fc.camera_up[2] = cam.cur_inv_view[9];
        fc.camera_fwd[0] = cam.cur_inv_view[2];
        fc.camera_fwd[1] = cam.cur_inv_view[6];
        fc.camera_fwd[2] = cam.cur_inv_view[10];
        fc.camera_pos[0] = cam.cur_inv_view[3];
        fc.camera_pos[1] = cam.cur_inv_view[7];
        fc.camera_pos[2] = cam.cur_inv_view[11];

        sl::float4x4 view_to_clip = make_proj(cam.cur_proj);
        sl::float4x4 clip_to_view{};
        sl::matrixFullInvert(clip_to_view, view_to_clip);
        copy_mat(view_to_clip, fc.camera_view_to_clip);
        copy_mat(clip_to_view, fc.clip_to_camera_view);

        sl::float4x4 prev_view_to_clip = make_proj(cam.prev_valid ? cam.prev_proj : cam.cur_proj);
        sl::float4x4 cur_v2w = make_view_to_world(cam.cur_inv_view);
        sl::float4x4 prev_v2w = make_view_to_world(cam.prev_valid ? cam.prev_inv_view : cam.cur_inv_view);
        sl::float4x4 v2pv{};
        sl::calcCameraToPrevCamera(v2pv, cur_v2w, prev_v2w);
        sl::float4x4 clip_to_pv{};
        sl::float4x4 clip_to_pclip{};
        sl::float4x4 pclip_to_clip{};
        sl::matrixMul(clip_to_pv, clip_to_view, v2pv);
        sl::matrixMul(clip_to_pclip, clip_to_pv, prev_view_to_clip);
        sl::matrixFullInvert(pclip_to_clip, clip_to_pclip);
        copy_mat(clip_to_pclip, fc.clip_to_prev_clip);
        copy_mat(pclip_to_clip, fc.prev_clip_to_clip);
    } else {
        auto set_ident = [](float m[16]) {
            std::memset(m, 0, 16 * sizeof(float));
            m[0] = m[5] = m[10] = m[15] = 1.0f;
        };
        set_ident(fc.camera_view_to_clip);
        set_ident(fc.clip_to_camera_view);
        set_ident(fc.clip_to_prev_clip);
        set_ident(fc.prev_clip_to_clip);
    }
    return fc;
}

bool Streamline::SetCameraConstants(const CameraInput& cam, u32 frame_index, u32 viewport_id) {
    return SetConstants(BuildConstants(cam), frame_index, viewport_id);
}

bool Streamline::TagImage(vk::CommandBuffer cmd, u32 buffer_type, const ImageResource& img,
                          bool valid_until_present, u32 extent_width, u32 extent_height,
                          u32 frame_index, u32 viewport_id) {
    std::scoped_lock lock(mutex_);
    if (!device_initialized_ || !impl_ || !impl_->slSetTagForFrame || !img.image) {
        return false;
    }
    sl::FrameToken* tok = GetOrCreateTokenLocked(frame_index);
    if (!tok) {
        return false;
    }

    sl::Resource res(
        sl::ResourceType::eTex2d,
        static_cast<void*>(static_cast<VkImage>(img.image)),
        static_cast<void*>(static_cast<VkDeviceMemory>(img.memory)),
        static_cast<void*>(static_cast<VkImageView>(img.view)),
        static_cast<uint32_t>(img.layout));
    res.width = img.width;
    res.height = img.height;
    res.nativeFormat = static_cast<uint32_t>(img.format);
    res.mipLevels = img.mip_levels ? img.mip_levels : 1;
    res.arrayLayers = img.array_layers ? img.array_layers : 1;
    res.usage = img.usage;

    sl::Extent extent{0, 0, extent_width ? extent_width : img.width,
                       extent_height ? extent_height : img.height};
    sl::ResourceTag tag(
        &res,
        static_cast<sl::BufferType>(buffer_type),
        valid_until_present ? sl::ResourceLifecycle::eValidUntilPresent
                            : sl::ResourceLifecycle::eOnlyValidNow,
        &extent);

    sl::ViewportHandle vp(viewport_id);
    return impl_->slSetTagForFrame(*tok, vp, &tag, 1,
                                   static_cast<sl::CommandBuffer*>(static_cast<VkCommandBuffer>(cmd))) ==
           sl::Result::eOk;
}

bool Streamline::EvaluateDlss(vk::CommandBuffer cmd, const DlssEvalDesc& desc, u32 frame_index,
                              u32 viewport_id) {
    std::scoped_lock lock(mutex_);
    if (!device_initialized_ || !dlss_supported_ || !impl_ || !impl_->dlssSetOptions ||
        !impl_->slEvaluateFeature || desc.mode == SlDlssMode::Off) {
        return false;
    }
    sl::FrameToken* tok = GetOrCreateTokenLocked(frame_index);
    if (!tok) {
        return false;
    }

    sl::DLSSOptions opts{};
    opts.mode = ToSlDlssMode(desc.mode);
    opts.outputWidth = desc.output_width;
    opts.outputHeight = desc.output_height;
    opts.sharpness = std::clamp(desc.sharpness, 0.0f, 1.0f);
    opts.colorBuffersHDR = desc.hdr ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    opts.useAutoExposure = desc.auto_exposure ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    opts.alphaUpscalingEnabled = sl::Boolean::eFalse;
    // NVIDIA official DLSS 4.5 preset mapping (auto defaults: K for DLAA..Balanced, M for Perf, L for UltraPerf)
    opts.dlaaPreset = ToSlDlssPreset(desc.preset, SlDlssPreset::PresetK);
    opts.qualityPreset = ToSlDlssPreset(desc.preset, SlDlssPreset::PresetK);
    opts.balancedPreset = ToSlDlssPreset(desc.preset, SlDlssPreset::PresetK);
    opts.performancePreset = ToSlDlssPreset(desc.preset, SlDlssPreset::PresetM);
    opts.ultraPerformancePreset = ToSlDlssPreset(desc.preset, SlDlssPreset::PresetL);
    opts.ultraQualityPreset = ToSlDlssPreset(desc.preset, SlDlssPreset::PresetK);

    sl::ViewportHandle vp(viewport_id);
    if (impl_->dlssSetOptions(vp, opts) != sl::Result::eOk) {
        return false;
    }

    auto make_res = [](const ImageResource& img) {
        sl::Resource r(
            sl::ResourceType::eTex2d,
            static_cast<void*>(static_cast<VkImage>(img.image)),
            static_cast<void*>(static_cast<VkDeviceMemory>(img.memory)),
            static_cast<void*>(static_cast<VkImageView>(img.view)),
            static_cast<uint32_t>(img.layout));
        r.width = img.width;
        r.height = img.height;
        r.nativeFormat = static_cast<uint32_t>(img.format);
        r.mipLevels = img.mip_levels ? img.mip_levels : 1;
        r.arrayLayers = img.array_layers ? img.array_layers : 1;
        r.usage = img.usage;
        return r;
    };

    sl::Resource r_color_in = make_res(desc.color_in);
    sl::Resource r_color_out = make_res(desc.color_out);
    sl::Resource r_depth = make_res(desc.depth);
    sl::Resource r_mvec = make_res(desc.motion_vectors);
    sl::Resource r_exp = make_res(desc.exposure);

    sl::Extent render_ext{0, 0, desc.render_width, desc.render_height};
    sl::Extent out_ext{0, 0, desc.output_width, desc.output_height};

    std::array<sl::ResourceTag, 5> tags{};
    uint32_t tag_count = 0;
    // Tag Depth & MotionVectors as eValidUntilPresent so DLSS-G can also consume them cleanly.
    tags[tag_count++] = sl::ResourceTag{&r_color_in, sl::kBufferTypeScalingInputColor,
                                        sl::ResourceLifecycle::eOnlyValidNow, &render_ext};
    tags[tag_count++] = sl::ResourceTag{&r_color_out, sl::kBufferTypeScalingOutputColor,
                                        sl::ResourceLifecycle::eValidUntilPresent, &out_ext};
    tags[tag_count++] = sl::ResourceTag{&r_depth, sl::kBufferTypeDepth,
                                        sl::ResourceLifecycle::eValidUntilPresent, &render_ext};
    tags[tag_count++] = sl::ResourceTag{&r_mvec, sl::kBufferTypeMotionVectors,
                                        sl::ResourceLifecycle::eValidUntilPresent, &render_ext};
    if (desc.exposure.image) {
        sl::Extent exp_ext{0, 0, 1, 1};
        tags[tag_count++] = sl::ResourceTag{&r_exp, sl::kBufferTypeExposure,
                                            sl::ResourceLifecycle::eOnlyValidNow, &exp_ext};
    }

    auto* sl_cmd = static_cast<sl::CommandBuffer*>(static_cast<VkCommandBuffer>(cmd));
    if (impl_->slSetTagForFrame(*tok, vp, tags.data(), tag_count, sl_cmd) != sl::Result::eOk) {
        return false;
    }

    const sl::BaseStructure* inputs[] = {&vp};
    sl::Result er = impl_->slEvaluateFeature(sl::kFeatureDLSS, *tok, inputs, 1, sl_cmd);
    if (er == sl::Result::eOk && impl_->dlssGetState) {
        sl::DLSSState st{};
        if (impl_->dlssGetState(vp, st) == sl::Result::eOk) {
            impl_->dlss_vram_estimate = st.estimatedVRAMUsageInBytes;
        }
    }
    return er == sl::Result::eOk;
}

void Streamline::RefreshDlssGStateLocked(u32 viewport_id) {
    if (!dlss_g_supported_ || !impl_ || !impl_->dlssGGetState) {
        return;
    }
    sl::DLSSGState st{};
    sl::ViewportHandle vp(viewport_id);
    if (impl_->dlssGGetState(vp, st, nullptr) == sl::Result::eOk) {
        dlss_g_max_frames_ = std::max<u32>(1, st.numFramesToGenerateMax);
        dlss_g_vsync_supported_ = st.bIsVsyncSupportAvailable;
        dlss_g_dynamic_mfg_supported_ = st.bIsDynamicMFGSupported;
        impl_->dlss_g_vram_estimate = st.estimatedVRAMUsageInBytes;
    }
}

bool Streamline::UpdateDlssG(const DlssGDesc& desc, u32 viewport_id) {
    std::scoped_lock lock(mutex_);
    if (!device_initialized_ || !dlss_g_supported_ || !impl_ || !impl_->dlssGSetOptions) {
        return false;
    }
    RefreshDlssGStateLocked(viewport_id);

    sl::DLSSGOptions opts{};
    // Dynamic MFG is D3D12-only in SL 2.14.16; map cleanly to eOn with max multiplier on Vulkan.
    if (desc.mode == SlDlssGMode::Off) {
        opts.mode = sl::DLSSGMode::eOff;
    } else if (desc.mode == SlDlssGMode::Dynamic && dlss_g_dynamic_mfg_supported_) {
        opts.mode = sl::DLSSGMode::eDynamic;
        opts.dynamicTargetFrameRate = desc.dynamic_target_fps;
    } else if (desc.mode == SlDlssGMode::Auto) {
        opts.mode = sl::DLSSGMode::eAuto;
    } else {
        opts.mode = sl::DLSSGMode::eOn;
    }

    const u32 max_gen = dlss_g_max_frames_ ? dlss_g_max_frames_ : 1u;
    opts.numFramesToGenerate = std::clamp(desc.num_frames_to_generate, 1u, max_gen);
    opts.flags = sl::DLSSGFlags::eRetainResourcesWhenOff |
                 sl::DLSSGFlags::eEnableFullscreenMenuDetection;
    opts.mvecDepthWidth = desc.render_width;
    opts.mvecDepthHeight = desc.render_height;
    opts.colorWidth = desc.output_width;
    opts.colorHeight = desc.output_height;
    opts.colorBufferFormat = static_cast<uint32_t>(desc.color_format);
    opts.mvecBufferFormat = static_cast<uint32_t>(desc.mvec_format);
    opts.depthBufferFormat = static_cast<uint32_t>(desc.depth_format);
    opts.hudLessBufferFormat = static_cast<uint32_t>(desc.hudless_format);
    opts.queueParallelismMode = sl::DLSSGQueueParallelismMode::eBlockPresentingClientQueue;
    opts.enableUserInterfaceRecomposition =
        desc.ui_recomposition ? sl::Boolean::eTrue : sl::Boolean::eFalse;

    sl::ViewportHandle vp(viewport_id);
    sl::Result r = impl_->dlssGSetOptions(vp, opts);
    if (r == sl::Result::eOk) {
        impl_->last_dlss_g_desc = desc;
        impl_->dlss_g_options_dirty = false;
        if (impl_->dlssGGetState) {
            sl::DLSSGState st{};
            if (impl_->dlssGGetState(vp, st, nullptr) == sl::Result::eOk) {
                impl_->dlss_g_vram_estimate = st.estimatedVRAMUsageInBytes;
                dlss_g_max_frames_ = std::max<u32>(1, st.numFramesToGenerateMax);
                static u32 s_last_status = 0;
                const u32 st_bits = static_cast<u32>(st.status);
                if (st_bits != s_last_status) {
                    s_last_status = st_bits;
                    std::printf("Streamline DLSS-G state: status=0x%x maxGen=%u presented=%u\n",
                                st_bits, st.numFramesToGenerateMax, st.numFramesActuallyPresented);
                    std::fflush(stdout);
                }
            }
        }
        return true;
    }
    static sl::Result s_last_err = sl::Result::eOk;
    if (r != s_last_err) {
        s_last_err = r;
        std::printf("Streamline DLSS-G: slDLSSGSetOptions failed: %s (mode=%u gen=%u color=%ux%u fmt=%u)\n",
                    sl::getResultAsStr(r), static_cast<unsigned>(opts.mode),
                    opts.numFramesToGenerate, opts.colorWidth, opts.colorHeight,
                    opts.colorBufferFormat);
        std::fflush(stdout);
    }
    return false;
}

bool Streamline::UpdateReflex(SlReflexMode mode, u32 frame_limit_us) {
    std::scoped_lock lock(mutex_);
    if (!device_initialized_ || !reflex_supported_ || !impl_ || !impl_->reflexSetOptions) {
        return false;
    }
    sl::ReflexOptions opts{};
    opts.mode = ToSlReflexMode(mode);
    opts.frameLimitUs = frame_limit_us;
    opts.useMarkersToOptimize = pcl_supported_;
    if (impl_->reflexSetOptions(opts) == sl::Result::eOk) {
        impl_->last_reflex_mode = mode;
        impl_->last_frame_limit_us = frame_limit_us;
        impl_->reflex_options_dirty = false;
        return true;
    }
    return false;
}

Streamline::ReflexTelemetry Streamline::QueryTelemetry(u32 viewport_id) {
    std::scoped_lock lock(mutex_);
    ReflexTelemetry out{};
    if (!device_initialized_ || !impl_) {
        return out;
    }
    out.supported = reflex_supported_;
    out.dlss_vram_bytes = impl_->dlss_vram_estimate;

    if (reflex_supported_ && impl_->reflexGetState) {
        sl::ReflexState st{};
        if (impl_->reflexGetState(st) == sl::Result::eOk) {
            out.low_latency_active = st.lowLatencyAvailable;
            out.latency_report_available = st.latencyReportAvailable;
            if (st.latencyReportAvailable) {
                double sum_pc = 0.0;
                double sum_sim = 0.0;
                double sum_rnd = 0.0;
                double sum_gpu = 0.0;
                int count = 0;
                for (int i = 0; i < 64; ++i) {
                    const auto& fr = st.frameReport[i];
                    if (fr.presentEndTime > fr.simStartTime && fr.simStartTime > 0) {
                        sum_pc += static_cast<double>(fr.presentEndTime - fr.simStartTime) * 0.001;
                        if (fr.simEndTime >= fr.simStartTime) {
                            sum_sim += static_cast<double>(fr.simEndTime - fr.simStartTime) * 0.001;
                        }
                        if (fr.renderSubmitEndTime >= fr.renderSubmitStartTime) {
                            sum_rnd += static_cast<double>(fr.renderSubmitEndTime - fr.renderSubmitStartTime) * 0.001;
                        }
                        if (fr.gpuRenderEndTime >= fr.gpuRenderStartTime) {
                            sum_gpu += static_cast<double>(fr.gpuRenderEndTime - fr.gpuRenderStartTime) * 0.001;
                        }
                        ++count;
                    }
                }
                if (count > 0) {
                    const double inv = 1.0 / static_cast<double>(count);
                    out.pc_latency_ms = static_cast<float>(sum_pc * inv);
                    out.sim_latency_ms = static_cast<float>(sum_sim * inv);
                    out.render_latency_ms = static_cast<float>(sum_rnd * inv);
                    out.gpu_latency_ms = static_cast<float>(sum_gpu * inv);
                }
            }
        }
    }

    if (dlss_g_supported_ && impl_->dlssGGetState) {
        sl::DLSSGState gst{};
        sl::ViewportHandle vp(viewport_id);
        if (impl_->dlssGGetState(vp, gst, nullptr) == sl::Result::eOk) {
            out.max_generated_frames = gst.numFramesToGenerateMax;
            out.frames_actually_presented = gst.numFramesActuallyPresented;
            out.dlss_g_vsync_supported = gst.bIsVsyncSupportAvailable;
            out.dlss_g_dynamic_mfg_supported = gst.bIsDynamicMFGSupported;
            out.dlss_g_vram_bytes = gst.estimatedVRAMUsageInBytes;
            out.dlss_g_status = static_cast<u32>(gst.status);
        }
    }
    return out;
}

bool Streamline::QueryOptimalDlssSize(u32 output_width, u32 output_height, SlDlssMode mode,
                                      u32& out_render_width, u32& out_render_height,
                                      float& out_sharpness) {
    std::scoped_lock lock(mutex_);
    if (!device_initialized_ || !dlss_supported_ || !impl_ || !impl_->dlssGetOptimalSettings ||
        mode == SlDlssMode::Off || output_width == 0 || output_height == 0) {
        return false;
    }
    sl::DLSSOptions opts{};
    opts.mode = ToSlDlssMode(mode);
    opts.outputWidth = output_width;
    opts.outputHeight = output_height;
    sl::DLSSOptimalSettings opt_st{};
    if (impl_->dlssGetOptimalSettings(opts, opt_st) == sl::Result::eOk &&
        opt_st.optimalRenderWidth > 0 && opt_st.optimalRenderHeight > 0) {
        out_render_width = opt_st.optimalRenderWidth;
        out_render_height = opt_st.optimalRenderHeight;
        out_sharpness = opt_st.optimalSharpness;
        return true;
    }
    return false;
}

void Streamline::FreeFeatureResources(u32 viewport_id) {
    std::scoped_lock lock(mutex_);
    if (!device_initialized_ || !impl_ || !impl_->slFreeResources) {
        return;
    }
    sl::ViewportHandle vp(viewport_id);
    if (dlss_supported_) {
        impl_->slFreeResources(sl::kFeatureDLSS, vp);
    }
    if (dlss_g_supported_) {
        impl_->slFreeResources(sl::kFeatureDLSS_G, vp);
        impl_->dlss_g_options_dirty = true;
    }
}

SlDlssPreset Streamline::PresetFromInt(int preset_id) noexcept {
    switch (preset_id) {
    case 1:
        return SlDlssPreset::PresetJ;
    case 2:
        return SlDlssPreset::PresetK;
    case 3:
        return SlDlssPreset::PresetL;
    case 4:
        return SlDlssPreset::PresetM;
    case 0:
    default:
        return SlDlssPreset::Auto;
    }
}

SlDlssMode Streamline::ModeFromScalePreset(int scale_preset) noexcept {
    switch (scale_preset) {
    case 0:
        return SlDlssMode::Dlaa;
    case 1:
        return SlDlssMode::Quality;
    case 2:
        return SlDlssMode::Balanced;
    case 3:
        return SlDlssMode::Performance;
    case 4:
        return SlDlssMode::UltraPerformance;
    default:
        return SlDlssMode::Off;
    }
}

} // namespace Vulkan

#endif // _WIN32
