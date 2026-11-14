// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Configuration Management & INI Sync Module
//
// The launcher config is a JSON object. This module parses it with the shared
// JSON parser, validates every value against the values the runtime accepts,
// preserves keys written by newer launchers, and replaces the file atomically
// so an interrupted save never leaves a truncated configuration behind.
#include "spm/spm_core.h"
#include "spm_json.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;
namespace json = spm::json;

namespace {

void SafeStrCopy(char* destination, size_t capacity, const char* source) {
    if (!destination || capacity == 0) {
        return;
    }
    if (!source) {
        destination[0] = '\0';
        return;
    }
    size_t index = 0;
    while (index + 1 < capacity && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

bool OneOf(std::string_view value, std::initializer_list<std::string_view> allowed) {
    for (const std::string_view candidate : allowed) {
        if (value == candidate) {
            return true;
        }
    }
    return false;
}

bool IsDecimal(std::string_view value, size_t minDigits, size_t maxDigits) {
    if (value.size() < minDigits || value.size() > maxDigits) {
        return false;
    }
    for (const char c : value) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

bool IsResolution(std::string_view value) {
    const size_t separator = value.find('x');
    if (separator == std::string_view::npos || separator == 0 ||
        separator + 1 >= value.size()) {
        return false;
    }
    return IsDecimal(value.substr(0, separator), 2, 5) &&
           IsDecimal(value.substr(separator + 1), 2, 5);
}

bool ValidLanguage(std::string_view value) {
    return OneOf(value, {"auto", "en", "es_419", "es_ES", "pt_BR", "fr", "de", "it", "ru",
                         "ja", "zh_CN"});
}

bool ValidGame(std::string_view value) {
    return OneOf(value, {"u1", "u2", "u3"});
}

bool ValidGraphicsApi(std::string_view value) {
    return OneOf(value, {"dx9", "dx11", "dx12", "vulkan"});
}

bool ValidBudget(std::string_view value) {
    // 0 means automatic; the rest are the presets exposed by the launcher.
    return OneOf(value, {"0", "512", "820", "1024", "1720", "2048", "3200", "4096",
                         "6144", "8192"});
}

bool ValidQuality(std::string_view value) {
    return OneOf(value, {"low", "medium", "high", "ultra"});
}

bool ValidSsao(std::string_view value) {
    return OneOf(value, {"off", "half", "full"});
}

bool ValidAnisotropy(std::string_view value) {
    return OneOf(value, {"1", "2", "4", "8", "16"});
}

bool ValidFpsLimit(std::string_view value) {
    if (value.empty() || value.size() > 4) {
        return false;
    }
    int parsed = 0;
    for (const char c : value) {
        if (c < '0' || c > '9') {
            return false;
        }
        parsed = parsed * 10 + (c - '0');
    }
    return parsed >= 0 && parsed <= 1000;
}

bool ValidVSync(std::string_view value) {
    return value == "0" || value == "1";
}

bool ValidDisplayMode(std::string_view value) {
    return OneOf(value, {"fullscreen", "borderless", "windowed"});
}

bool ValidFramePacing(std::string_view value) {
    return OneOf(value, {"hybrid", "waitable", "sleep", "off"});
}

bool ValidUpscaler(std::string_view value) {
    return OneOf(value, {"off", "fsr1", "fsr3", "fsr4", "dlaa", "dlss"});
}

bool ValidPrompts(std::string_view value) {
    return OneOf(value, {"auto", "ps", "xbox", "kb"});
}

bool ValidAntiAliasing(std::string_view value) {
    return OneOf(value, {"off", "fxaa", "smaa", "msaa2x", "msaa4x", "msaa8x", "dlaa"});
}

bool ValidFrameGeneration(std::string_view value) {
    return OneOf(value, {"off", "fsr2x", "dlss2x", "dlss3x", "dlss4x", "dlss5x", "dlss6x",
                         "dynamic"});
}

bool ValidReflex(std::string_view value) {
    return OneOf(value, {"off", "on", "boost"});
}

bool ValidDynamicResolution(std::string_view value) {
    return value == "0" || value == "1";
}

bool ValidNeuralMode(std::string_view value) {
    return OneOf(value, {"auto", "enabled", "disabled"});
}

bool ValidStorageOptimization(std::string_view value) {
    return OneOf(value, {"auto", "nvme", "ssd", "hdd"});
}

bool ValidGpuId(std::string_view value) {
    // Vulkan device index, "-1" (automatic) or an adapter LUID.
    if (value == "-1") {
        return true;
    }
    if (!value.empty() && value.size() <= 16) {
        bool digits = true;
        for (const char c : value) {
            if ((c < '0' || c > '9') && c != '-') {
                digits = false;
                break;
            }
        }
        if (digits) {
            return true;
        }
    }
    return value.size() <= 31;
}

std::string SanitizeIniValue(const char* value) {
    std::string result = value ? value : "";
    for (char& c : result) {
        if (c == '\n' || c == '\r') {
            c = ' ';
        }
    }
    return result;
}

bool WriteFileAtomic(const fs::path& path, const std::string& contents) {
    std::error_code ec;
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
        if (ec) {
            return false;
        }
    }

    fs::path temporary = path;
    temporary += ".tmp";
#ifdef _WIN32
    temporary += "." + std::to_string(GetCurrentProcessId());
#else
    temporary += "." + std::to_string(static_cast<long long>(::getpid()));
#endif

    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.flush();
        if (!out) {
            out.close();
            fs::remove(temporary, ec);
            return false;
        }
    }

#ifdef _WIN32
    if (!MoveFileExW(temporary.wstring().c_str(), path.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fs::remove(temporary, ec);
        return false;
    }
#else
    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        fs::remove(temporary, ec);
        return false;
    }
#endif
    return true;
}

void SetString(const json::Value& root, const char* key, char* destination, size_t capacity,
               bool (*validator)(std::string_view)) {
    const json::Value* value = root.Find(key);
    if (!value || !value->IsString()) {
        return;
    }
    if (validator && !validator(value->string)) {
        return;
    }
    SafeStrCopy(destination, capacity, value->string.c_str());
}

double ReadNumber(const json::Value& root, const char* key, double fallback, double minimum,
                  double maximum) {
    const json::Value* value = root.Find(key);
    double parsed = fallback;
    if (value && value->IsNumber()) {
        parsed = value->number;
    } else if (value && value->IsString()) {
        char* end = nullptr;
        errno = 0;
        const double converted = std::strtod(value->string.c_str(), &end);
        if (end == value->string.c_str() || *end != '\0' || errno == ERANGE) {
            return fallback;
        }
        parsed = converted;
    } else {
        return fallback;
    }
    if (!std::isfinite(parsed)) {
        return fallback;
    }
    return std::clamp(parsed, minimum, maximum);
}

int ReadBool(const json::Value& root, const char* key, int fallback) {
    const json::Value* value = root.Find(key);
    if (!value) {
        return fallback;
    }
    if (value->IsBool()) {
        return value->boolean ? 1 : 0;
    }
    if (value->IsNumber()) {
        return value->number != 0.0 ? 1 : 0;
    }
    if (value->IsString()) {
        if (value->string == "true" || value->string == "1") return 1;
        if (value->string == "false" || value->string == "0") return 0;
    }
    return fallback;
}

json::Value MakeString(const char* value) {
    json::Value result;
    result.type = json::Type::String;
    result.string = value ? value : "";
    return result;
}

json::Value MakeNumber(double value) {
    json::Value result;
    result.type = json::Type::Number;
    result.number = value;
    return result;
}

json::Value MakeBool(bool value) {
    json::Value result;
    result.type = json::Type::Bool;
    result.boolean = value;
    return result;
}

} // namespace

void SPM_GetDefaultConfig(SPMConfig* outConfig) {
    if (!outConfig) return;
    std::memset(outConfig, 0, sizeof(*outConfig));

    SafeStrCopy(outConfig->language, sizeof(outConfig->language), "auto");
    // The game directory must never default to a personal path.
    outConfig->gameDir[0] = '\0';
    SafeStrCopy(outConfig->selectedGame, sizeof(outConfig->selectedGame), "u1");
    SafeStrCopy(outConfig->graphicsApi, sizeof(outConfig->graphicsApi), "dx11");
    SafeStrCopy(outConfig->vramBudgetMb, sizeof(outConfig->vramBudgetMb), "0");
    SafeStrCopy(outConfig->framePacingMode, sizeof(outConfig->framePacingMode), "hybrid");
    SafeStrCopy(outConfig->textureQuality, sizeof(outConfig->textureQuality), "medium");
    SafeStrCopy(outConfig->shadowQuality, sizeof(outConfig->shadowQuality), "medium");
    SafeStrCopy(outConfig->ssaoQuality, sizeof(outConfig->ssaoQuality), "full");
    SafeStrCopy(outConfig->anisotropy, sizeof(outConfig->anisotropy), "16");
    SafeStrCopy(outConfig->fpsLimit, sizeof(outConfig->fpsLimit), "60");
    SafeStrCopy(outConfig->vsync, sizeof(outConfig->vsync), "1");
    SafeStrCopy(outConfig->displayMode, sizeof(outConfig->displayMode), "fullscreen");
    SafeStrCopy(outConfig->resolution, sizeof(outConfig->resolution), "1920x1080");
    outConfig->outputResolution[0] = '\0';
    SafeStrCopy(outConfig->upscaler, sizeof(outConfig->upscaler), "off");
    outConfig->selectedGpuName[0] = '\0';
    SafeStrCopy(outConfig->selectedGpuId, sizeof(outConfig->selectedGpuId), "-1");

    outConfig->mouseSensitivity = 1.0;
    outConfig->mouseAimSensitivity = 1.0;
    outConfig->mouseVerticalSensitivity = 1.0;
    outConfig->mouseInvertY = 0;
    outConfig->mouseInvertX = 0;
    outConfig->stickSpeed = 1.0;
    outConfig->vibration = 1;
    SafeStrCopy(outConfig->prompts, sizeof(outConfig->prompts), "auto");
    SafeStrCopy(outConfig->antiAliasing, sizeof(outConfig->antiAliasing), "off");
    SafeStrCopy(outConfig->frameGeneration, sizeof(outConfig->frameGeneration), "off");
    SafeStrCopy(outConfig->reflexMode, sizeof(outConfig->reflexMode), "off");
    SafeStrCopy(outConfig->streamingBudgetMb, sizeof(outConfig->streamingBudgetMb), "0");
    SafeStrCopy(outConfig->dynamicResolution, sizeof(outConfig->dynamicResolution), "0");
    outConfig->sharpening = 0.0;
    SafeStrCopy(outConfig->neuralAssistMode, sizeof(outConfig->neuralAssistMode), "auto");
    SafeStrCopy(outConfig->storageOptimization, sizeof(outConfig->storageOptimization), "auto");

    SafeStrCopy(outConfig->keyMap, sizeof(outConfig->keyMap),
                "ls_up=W,ls_down=S,ls_left=A,ls_right=D,rs_up=Up,rs_down=Down,rs_left=Left,"
                "rs_right=Right,left=1,right=2,up=3,down=4,cross=Space,circle=Left Shift,"
                "square=F,triangle=R,l1=Q,r1=E,l3=Left Shift,r3=V,touchpad_left=Tab,"
                "options=Escape,options=Return");
    SafeStrCopy(outConfig->padMap, sizeof(outConfig->padMap), "jump=Cross,fire=R2,aim=L2");
    SafeStrCopy(outConfig->mouseMap, sizeof(outConfig->mouseMap),
                "left=wheel_down,right=wheel_up,l2=right,r2=left,l1=x1,r1=x2,r3=middle");
}

int SPM_LoadConfig(const char* path, SPMConfig* outConfig) {
    if (!path || !outConfig) return 0;
    SPM_GetDefaultConfig(outConfig);

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return 0;
    }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (text.empty()) {
        return 0;
    }

    json::Value root;
    std::string error;
    if (!json::Parse(text, root, &error) || !root.IsObject()) {
        return 0;
    }

    SetString(root, "Language", outConfig->language, sizeof(outConfig->language), ValidLanguage);
    SetString(root, "GameDir", outConfig->gameDir, sizeof(outConfig->gameDir), nullptr);
    SetString(root, "SelectedGame", outConfig->selectedGame, sizeof(outConfig->selectedGame),
              ValidGame);
    SetString(root, "GraphicsApi", outConfig->graphicsApi, sizeof(outConfig->graphicsApi),
              ValidGraphicsApi);
    SetString(root, "VramBudgetMb", outConfig->vramBudgetMb, sizeof(outConfig->vramBudgetMb),
              ValidBudget);
    SetString(root, "FramePacingMode", outConfig->framePacingMode,
              sizeof(outConfig->framePacingMode), ValidFramePacing);
    SetString(root, "TextureQuality", outConfig->textureQuality,
              sizeof(outConfig->textureQuality), ValidQuality);
    SetString(root, "ShadowQuality", outConfig->shadowQuality, sizeof(outConfig->shadowQuality),
              ValidQuality);
    SetString(root, "SsaoQuality", outConfig->ssaoQuality, sizeof(outConfig->ssaoQuality),
              ValidSsao);
    SetString(root, "Anisotropy", outConfig->anisotropy, sizeof(outConfig->anisotropy),
              ValidAnisotropy);
    SetString(root, "FpsLimit", outConfig->fpsLimit, sizeof(outConfig->fpsLimit), ValidFpsLimit);
    SetString(root, "VSync", outConfig->vsync, sizeof(outConfig->vsync), ValidVSync);
    SetString(root, "DisplayMode", outConfig->displayMode, sizeof(outConfig->displayMode),
              ValidDisplayMode);
    SetString(root, "Resolution", outConfig->resolution, sizeof(outConfig->resolution),
              IsResolution);
    SetString(root, "OutputResolution", outConfig->outputResolution,
              sizeof(outConfig->outputResolution), IsResolution);
    SetString(root, "Upscaler", outConfig->upscaler, sizeof(outConfig->upscaler), ValidUpscaler);
    SetString(root, "gpu", outConfig->selectedGpuName, sizeof(outConfig->selectedGpuName), nullptr);
    SetString(root, "gpu_id", outConfig->selectedGpuId, sizeof(outConfig->selectedGpuId),
              ValidGpuId);
    SetString(root, "prompts", outConfig->prompts, sizeof(outConfig->prompts), ValidPrompts);
    SetString(root, "key_map", outConfig->keyMap, sizeof(outConfig->keyMap), nullptr);
    SetString(root, "pad_map", outConfig->padMap, sizeof(outConfig->padMap), nullptr);
    SetString(root, "mouse_map", outConfig->mouseMap, sizeof(outConfig->mouseMap), nullptr);
    SetString(root, "AntiAliasing", outConfig->antiAliasing, sizeof(outConfig->antiAliasing),
              ValidAntiAliasing);
    SetString(root, "FrameGeneration", outConfig->frameGeneration,
              sizeof(outConfig->frameGeneration), ValidFrameGeneration);
    SetString(root, "ReflexMode", outConfig->reflexMode, sizeof(outConfig->reflexMode),
              ValidReflex);
    SetString(root, "StreamingBudgetMb", outConfig->streamingBudgetMb,
              sizeof(outConfig->streamingBudgetMb), ValidBudget);
    SetString(root, "DynamicResolution", outConfig->dynamicResolution,
              sizeof(outConfig->dynamicResolution), ValidDynamicResolution);
    SetString(root, "NeuralAssistMode", outConfig->neuralAssistMode,
              sizeof(outConfig->neuralAssistMode), ValidNeuralMode);
    SetString(root, "StorageOptimization", outConfig->storageOptimization,
              sizeof(outConfig->storageOptimization), ValidStorageOptimization);

    outConfig->mouseSensitivity =
        ReadNumber(root, "MouseSensitivity", outConfig->mouseSensitivity, 0.01, 10.0);
    outConfig->mouseAimSensitivity =
        ReadNumber(root, "MouseAimSensitivity", outConfig->mouseAimSensitivity, 0.01, 10.0);
    outConfig->mouseVerticalSensitivity = ReadNumber(
        root, "MouseVerticalSensitivity", outConfig->mouseVerticalSensitivity, 0.01, 10.0);
    outConfig->stickSpeed = ReadNumber(root, "StickSpeed", outConfig->stickSpeed, 0.01, 10.0);
    outConfig->sharpening = ReadNumber(root, "Sharpening", outConfig->sharpening, 0.0, 1.0);
    outConfig->mouseInvertY = ReadBool(root, "MouseInvertY", outConfig->mouseInvertY);
    outConfig->mouseInvertX = ReadBool(root, "MouseInvertX", outConfig->mouseInvertX);
    outConfig->vibration = ReadBool(root, "Vibration", outConfig->vibration);

    return 1;
}

int SPM_SaveConfig(const char* path, const SPMConfig* config) {
    if (!path || !config) return 0;

    const fs::path target(path);
    json::Value root;
    root.type = json::Type::Object;

    // Start from the on-disk document so keys written by other tools survive.
    // A malformed file is moved aside instead of being silently destroyed.
    std::error_code ec;
    if (fs::exists(target, ec) && fs::file_size(target, ec) > 0 && !ec) {
        // The stream must be closed before the rename below: Windows refuses to
        // move a file that is still open without FILE_SHARE_DELETE, which made
        // saving over a corrupt file fail with "access denied".
        std::string text;
        {
            std::ifstream existing(target, std::ios::binary);
            if (!existing.is_open()) {
                return 0;
            }
            text.assign(std::istreambuf_iterator<char>(existing),
                        std::istreambuf_iterator<char>());
        }
        json::Value parsed;
        if (!text.empty() && json::Parse(text, parsed, nullptr) && parsed.IsObject()) {
            root = std::move(parsed);
        } else {
            fs::path backup = target;
            backup += ".invalid";
#ifdef _WIN32
            backup += "." + std::to_string(GetCurrentProcessId());
#else
            backup += "." + std::to_string(static_cast<long long>(::getpid()));
#endif
            fs::rename(target, backup, ec);
            if (ec) {
                // Never risk destroying a file we could not preserve.
                return 0;
            }
        }
    }

    root.Set("Language", MakeString(config->language));
    root.Set("GameDir", MakeString(config->gameDir));
    root.Set("SelectedGame", MakeString(config->selectedGame));
    root.Set("GraphicsApi", MakeString(config->graphicsApi));
    root.Set("VramBudgetMb", MakeString(config->vramBudgetMb));
    root.Set("FramePacingMode", MakeString(config->framePacingMode));
    root.Set("TextureQuality", MakeString(config->textureQuality));
    root.Set("ShadowQuality", MakeString(config->shadowQuality));
    root.Set("SsaoQuality", MakeString(config->ssaoQuality));
    root.Set("Anisotropy", MakeString(config->anisotropy));
    root.Set("FpsLimit", MakeString(config->fpsLimit));
    root.Set("VSync", MakeString(config->vsync));
    root.Set("DisplayMode", MakeString(config->displayMode));
    root.Set("Resolution", MakeString(config->resolution));
    root.Set("OutputResolution", MakeString(config->outputResolution));
    root.Set("Upscaler", MakeString(config->upscaler));
    root.Set("MouseSensitivity", MakeNumber(config->mouseSensitivity));
    root.Set("MouseAimSensitivity", MakeNumber(config->mouseAimSensitivity));
    root.Set("MouseVerticalSensitivity", MakeNumber(config->mouseVerticalSensitivity));
    root.Set("MouseInvertY", MakeBool(config->mouseInvertY != 0));
    root.Set("MouseInvertX", MakeBool(config->mouseInvertX != 0));
    root.Set("StickSpeed", MakeNumber(config->stickSpeed));
    root.Set("Vibration", MakeBool(config->vibration != 0));
    root.Set("prompts", MakeString(config->prompts));
    root.Set("gpu", MakeString(config->selectedGpuName));
    root.Set("gpu_id", MakeString(config->selectedGpuId));
    root.Set("key_map", MakeString(config->keyMap));
    root.Set("pad_map", MakeString(config->padMap));
    root.Set("mouse_map", MakeString(config->mouseMap));
    root.Set("AntiAliasing", MakeString(config->antiAliasing));
    root.Set("FrameGeneration", MakeString(config->frameGeneration));
    root.Set("ReflexMode", MakeString(config->reflexMode));
    root.Set("StreamingBudgetMb", MakeString(config->streamingBudgetMb));
    root.Set("DynamicResolution", MakeString(config->dynamicResolution));
    root.Set("NeuralAssistMode", MakeString(config->neuralAssistMode));
    root.Set("StorageOptimization", MakeString(config->storageOptimization));
    root.Set("Sharpening", MakeNumber(config->sharpening));

    return WriteFileAtomic(target, json::Serialize(root, 2) + "\n") ? 1 : 0;
}

int SPM_SyncConfigToIni(const char* workspaceRoot, const SPMConfig* config) {
    if (!workspaceRoot || !config) return 0;

    const fs::path workspace(workspaceRoot);
    std::error_code ec;
    fs::path userDir = workspace / "analysis" / "uncharted-user";
    if (fs::exists(workspace / "core", ec)) {
        userDir = workspace / "core" / "analysis" / "uncharted-user";
    } else if (fs::exists(workspace / "Project", ec)) {
        userDir = workspace / "Project" / "analysis" / "uncharted-user";
    }

    fs::create_directories(userDir, ec);
    if (ec) {
        return 0;
    }

    const std::vector<std::pair<std::string, std::string>> values = {
        {"graphics_api", SanitizeIniValue(config->graphicsApi)},
        {"vram_budget_mb", SanitizeIniValue(config->vramBudgetMb)},
        {"display_mode", SanitizeIniValue(config->displayMode)},
        {"resolution", SanitizeIniValue(config->resolution)},
        {"frame_pacer", SanitizeIniValue(config->framePacingMode)},
        {"fps_limit", SanitizeIniValue(config->fpsLimit)},
        {"vsync", SanitizeIniValue(config->vsync)},
        {"texture_quality", SanitizeIniValue(config->textureQuality)},
        {"shadow_quality", SanitizeIniValue(config->shadowQuality)},
        {"ssao_quality", SanitizeIniValue(config->ssaoQuality)},
        {"anisotropy", SanitizeIniValue(config->anisotropy)},
        {"upscaler", SanitizeIniValue(config->upscaler)},
        {"anti_aliasing", SanitizeIniValue(config->antiAliasing)},
        {"frame_generation", SanitizeIniValue(config->frameGeneration)},
        {"reflex_mode", SanitizeIniValue(config->reflexMode)},
        {"streaming_budget_mb", SanitizeIniValue(config->streamingBudgetMb)},
        {"dynamic_resolution", SanitizeIniValue(config->dynamicResolution)},
        {"neural_assist_mode", SanitizeIniValue(config->neuralAssistMode)},
        {"storage_optimization", SanitizeIniValue(config->storageOptimization)},
    };

    char sharpening[32];
    std::snprintf(sharpening, sizeof(sharpening), "%.6g", config->sharpening);

    const char* games[] = {"u1", "u2", "u3", "probe"};
    for (const char* game : games) {
        const fs::path iniPath =
            std::strcmp(game, "probe") == 0
                ? userDir / "probe.ini"
                : userDir / (std::string("probe-") + game + ".ini");

        std::vector<std::string> lines;
        {
            std::ifstream input(iniPath);
            std::string line;
            while (std::getline(input, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                lines.push_back(line);
            }
        }

        auto setValue = [&](const std::string& key, const std::string& value) {
            const std::string prefix = key + "=";
            for (std::string& line : lines) {
                if (line.rfind(prefix, 0) == 0) {
                    line = prefix + value;
                    return;
                }
            }
            lines.push_back(prefix + value);
        };

        for (const auto& [key, value] : values) {
            setValue(key, value);
        }
        if (config->outputResolution[0] != '\0') {
            setValue("output_res", SanitizeIniValue(config->outputResolution));
        }
        setValue("sharpening", sharpening);

        std::string contents;
        for (const std::string& line : lines) {
            contents += line;
            contents.push_back('\n');
        }
        if (!WriteFileAtomic(iniPath, contents)) {
            return 0;
        }
    }

    return 1;
}
