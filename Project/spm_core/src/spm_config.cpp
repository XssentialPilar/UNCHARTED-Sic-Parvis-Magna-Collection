// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Configuration Management & INI Sync Module
#include "spm/spm_core.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>

namespace fs = std::filesystem;

static void SafeStrCopy(char* dest, size_t destSize, const char* src) {
    if (!dest || destSize == 0) return;
    if (!src) {
        dest[0] = '\0';
        return;
    }
    size_t i = 0;
    while (i + 1 < destSize && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

void SPM_GetDefaultConfig(SPMConfig* outConfig) {
    if (!outConfig) return;
    memset(outConfig, 0, sizeof(*outConfig));

    SafeStrCopy(outConfig->language, sizeof(outConfig->language), "auto");
    // Crucial requirement: Default GameDir MUST be empty (never hardcoded to any personal directory)
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
        "ls_up=W,ls_down=S,ls_left=A,ls_right=D,rs_up=Up,rs_down=Down,rs_left=Left,rs_right=Right,left=1,right=2,up=3,down=4,cross=Space,circle=Left Shift,square=F,triangle=R,l1=Q,r1=E,l3=Left Shift,r3=V,touchpad_left=Tab,options=Escape,options=Return");
    SafeStrCopy(outConfig->padMap, sizeof(outConfig->padMap),
        "jump=Cross,fire=R2,aim=L2");
    SafeStrCopy(outConfig->mouseMap, sizeof(outConfig->mouseMap),
        "left=wheel_down,right=wheel_up,l2=right,r2=left,l1=x1,r1=x2,r3=middle");
}

static std::string ExtractJsonString(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return "";

    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return "";

    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";

    size_t endPos = json.find('"', pos + 1);
    if (endPos == std::string::npos) return "";

    std::string val = json.substr(pos + 1, endPos - pos - 1);
    // Unescape basic json characters if needed
    std::string unescaped;
    for (size_t i = 0; i < val.size(); ++i) {
        if (val[i] == '\\' && i + 1 < val.size()) {
            if (val[i+1] == '\\') { unescaped += '\\'; i++; }
            else if (val[i+1] == '/') { unescaped += '/'; i++; }
            else if (val[i+1] == '"') { unescaped += '"'; i++; }
            else { unescaped += val[i]; }
        } else {
            unescaped += val[i];
        }
    }
    return unescaped;
}

static double ExtractJsonNumber(const std::string& json, const std::string& key, double defaultVal) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return defaultVal;

    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return defaultVal;

    size_t start = json.find_first_of("-0123456789.", pos + 1);
    if (start == std::string::npos) return defaultVal;

    size_t end = json.find_first_not_of("-0123456789.", start);
    std::string numStr = (end != std::string::npos) ? json.substr(start, end - start) : json.substr(start);
    try {
        return std::stod(numStr);
    } catch (...) {
        return defaultVal;
    }
}

static int ExtractJsonBool(const std::string& json, const std::string& key, int defaultVal) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return defaultVal;

    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return defaultVal;

    size_t valStart = json.find_first_not_of(" \t\r\n", pos + 1);
    if (valStart == std::string::npos) return defaultVal;

    if (json.compare(valStart, 4, "true") == 0) return 1;
    if (json.compare(valStart, 5, "false") == 0) return 0;
    return defaultVal;
}

int SPM_LoadConfig(const char* path, SPMConfig* outConfig) {
    if (!path || !outConfig) return 0;

    std::ifstream file(path);
    if (!file.is_open()) return 0;

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();
    if (json.empty()) return 0;

    SPM_GetDefaultConfig(outConfig);

    auto loadStr = [&](const char* key, char* dest, size_t destSize) {
        std::string s = ExtractJsonString(json, key);
        if (!s.empty()) {
            SafeStrCopy(dest, destSize, s.c_str());
        }
    };

    loadStr("Language", outConfig->language, sizeof(outConfig->language));
    loadStr("GameDir", outConfig->gameDir, sizeof(outConfig->gameDir));
    loadStr("SelectedGame", outConfig->selectedGame, sizeof(outConfig->selectedGame));
    loadStr("GraphicsApi", outConfig->graphicsApi, sizeof(outConfig->graphicsApi));
    loadStr("VramBudgetMb", outConfig->vramBudgetMb, sizeof(outConfig->vramBudgetMb));
    loadStr("FramePacingMode", outConfig->framePacingMode, sizeof(outConfig->framePacingMode));
    loadStr("TextureQuality", outConfig->textureQuality, sizeof(outConfig->textureQuality));
    loadStr("ShadowQuality", outConfig->shadowQuality, sizeof(outConfig->shadowQuality));
    loadStr("SsaoQuality", outConfig->ssaoQuality, sizeof(outConfig->ssaoQuality));
    loadStr("Anisotropy", outConfig->anisotropy, sizeof(outConfig->anisotropy));
    loadStr("FpsLimit", outConfig->fpsLimit, sizeof(outConfig->fpsLimit));
    loadStr("VSync", outConfig->vsync, sizeof(outConfig->vsync));
    loadStr("DisplayMode", outConfig->displayMode, sizeof(outConfig->displayMode));
    loadStr("Resolution", outConfig->resolution, sizeof(outConfig->resolution));
    loadStr("OutputResolution", outConfig->outputResolution, sizeof(outConfig->outputResolution));
    loadStr("Upscaler", outConfig->upscaler, sizeof(outConfig->upscaler));
    loadStr("gpu", outConfig->selectedGpuName, sizeof(outConfig->selectedGpuName));
    loadStr("gpu_id", outConfig->selectedGpuId, sizeof(outConfig->selectedGpuId));
    loadStr("prompts", outConfig->prompts, sizeof(outConfig->prompts));
    loadStr("key_map", outConfig->keyMap, sizeof(outConfig->keyMap));
    loadStr("pad_map", outConfig->padMap, sizeof(outConfig->padMap));
    loadStr("mouse_map", outConfig->mouseMap, sizeof(outConfig->mouseMap));
    loadStr("AntiAliasing", outConfig->antiAliasing, sizeof(outConfig->antiAliasing));
    loadStr("FrameGeneration", outConfig->frameGeneration, sizeof(outConfig->frameGeneration));
    loadStr("ReflexMode", outConfig->reflexMode, sizeof(outConfig->reflexMode));
    loadStr("StreamingBudgetMb", outConfig->streamingBudgetMb, sizeof(outConfig->streamingBudgetMb));
    loadStr("DynamicResolution", outConfig->dynamicResolution, sizeof(outConfig->dynamicResolution));
    loadStr("NeuralAssistMode", outConfig->neuralAssistMode, sizeof(outConfig->neuralAssistMode));
    loadStr("StorageOptimization", outConfig->storageOptimization, sizeof(outConfig->storageOptimization));
    outConfig->sharpening = ExtractJsonNumber(json, "Sharpening", outConfig->sharpening);

    outConfig->mouseSensitivity = ExtractJsonNumber(json, "MouseSensitivity", outConfig->mouseSensitivity);
    outConfig->mouseAimSensitivity = ExtractJsonNumber(json, "MouseAimSensitivity", outConfig->mouseAimSensitivity);
    outConfig->mouseVerticalSensitivity = ExtractJsonNumber(json, "MouseVerticalSensitivity", outConfig->mouseVerticalSensitivity);
    outConfig->stickSpeed = ExtractJsonNumber(json, "StickSpeed", outConfig->stickSpeed);

    outConfig->mouseInvertY = ExtractJsonBool(json, "MouseInvertY", outConfig->mouseInvertY);
    outConfig->mouseInvertX = ExtractJsonBool(json, "MouseInvertX", outConfig->mouseInvertX);
    outConfig->vibration = ExtractJsonBool(json, "Vibration", outConfig->vibration);

    return 1;
}

static std::string EscapeJsonString(const char* str) {
    if (!str) return "";
    std::string res;
    for (size_t i = 0; str[i] != '\0'; ++i) {
        if (str[i] == '\\') res += "\\\\";
        else if (str[i] == '"') res += "\\\"";
        else res += str[i];
    }
    return res;
}

int SPM_SaveConfig(const char* path, const SPMConfig* config) {
    if (!path || !config) return 0;

    std::error_code ec;
    fs::path p(path);
    if (p.has_parent_path()) {
        fs::create_directories(p.parent_path(), ec);
    }

    std::ofstream out(path);
    if (!out.is_open()) return 0;

    out << "{\n";
    out << "  \"Language\": \"" << EscapeJsonString(config->language) << "\",\n";
    out << "  \"GameDir\": \"" << EscapeJsonString(config->gameDir) << "\",\n";
    out << "  \"SelectedGame\": \"" << EscapeJsonString(config->selectedGame) << "\",\n";
    out << "  \"GraphicsApi\": \"" << EscapeJsonString(config->graphicsApi) << "\",\n";
    out << "  \"VramBudgetMb\": \"" << EscapeJsonString(config->vramBudgetMb) << "\",\n";
    out << "  \"FramePacingMode\": \"" << EscapeJsonString(config->framePacingMode) << "\",\n";
    out << "  \"TextureQuality\": \"" << EscapeJsonString(config->textureQuality) << "\",\n";
    out << "  \"ShadowQuality\": \"" << EscapeJsonString(config->shadowQuality) << "\",\n";
    out << "  \"SsaoQuality\": \"" << EscapeJsonString(config->ssaoQuality) << "\",\n";
    out << "  \"Anisotropy\": \"" << EscapeJsonString(config->anisotropy) << "\",\n";
    out << "  \"FpsLimit\": \"" << EscapeJsonString(config->fpsLimit) << "\",\n";
    out << "  \"VSync\": \"" << EscapeJsonString(config->vsync) << "\",\n";
    out << "  \"DisplayMode\": \"" << EscapeJsonString(config->displayMode) << "\",\n";
    out << "  \"Resolution\": \"" << EscapeJsonString(config->resolution) << "\",\n";
    out << "  \"OutputResolution\": \"" << EscapeJsonString(config->outputResolution) << "\",\n";
    out << "  \"Upscaler\": \"" << EscapeJsonString(config->upscaler) << "\",\n";
    out << "  \"MouseSensitivity\": " << config->mouseSensitivity << ",\n";
    out << "  \"MouseAimSensitivity\": " << config->mouseAimSensitivity << ",\n";
    out << "  \"MouseVerticalSensitivity\": " << config->mouseVerticalSensitivity << ",\n";
    out << "  \"MouseInvertY\": " << (config->mouseInvertY ? "true" : "false") << ",\n";
    out << "  \"MouseInvertX\": " << (config->mouseInvertX ? "true" : "false") << ",\n";
    out << "  \"StickSpeed\": " << config->stickSpeed << ",\n";
    out << "  \"Vibration\": " << (config->vibration ? "true" : "false") << ",\n";
    out << "  \"prompts\": \"" << EscapeJsonString(config->prompts) << "\",\n";
    out << "  \"gpu\": \"" << EscapeJsonString(config->selectedGpuName) << "\",\n";
    out << "  \"gpu_id\": \"" << EscapeJsonString(config->selectedGpuId) << "\",\n";
    out << "  \"key_map\": \"" << EscapeJsonString(config->keyMap) << "\",\n";
    out << "  \"pad_map\": \"" << EscapeJsonString(config->padMap) << "\",\n";
    out << "  \"mouse_map\": \"" << EscapeJsonString(config->mouseMap) << "\",\n";
    out << "  \"AntiAliasing\": \"" << EscapeJsonString(config->antiAliasing) << "\",\n";
    out << "  \"FrameGeneration\": \"" << EscapeJsonString(config->frameGeneration) << "\",\n";
    out << "  \"ReflexMode\": \"" << EscapeJsonString(config->reflexMode) << "\",\n";
    out << "  \"StreamingBudgetMb\": \"" << EscapeJsonString(config->streamingBudgetMb) << "\",\n";
    out << "  \"DynamicResolution\": \"" << EscapeJsonString(config->dynamicResolution) << "\",\n";
    out << "  \"NeuralAssistMode\": \"" << EscapeJsonString(config->neuralAssistMode) << "\",\n";
    out << "  \"StorageOptimization\": \"" << EscapeJsonString(config->storageOptimization) << "\",\n";
    out << "  \"Sharpening\": " << config->sharpening << "\n";
    out << "}\n";

    return 1;
}

int SPM_SyncConfigToIni(const char* workspaceRoot, const SPMConfig* config) {
    if (!workspaceRoot || !config) return 0;

    fs::path ws(workspaceRoot);
    fs::path userDir = fs::exists(ws / "core")
        ? (ws / "core" / "analysis" / "uncharted-user")
        : (fs::exists(ws / "Project")
            ? (ws / "Project" / "analysis" / "uncharted-user")
            : (ws / "analysis" / "uncharted-user"));

    std::error_code ec;
    fs::create_directories(userDir, ec);

    const char* games[] = { "u1", "u2", "u3", "probe" };
    for (const char* g : games) {
        fs::path iniPath = (strcmp(g, "probe") == 0)
            ? (userDir / "probe.ini")
            : (userDir / (std::string("probe-") + g + ".ini"));

        std::vector<std::string> lines;
        if (fs::exists(iniPath)) {
            std::ifstream in(iniPath);
            std::string line;
            while (std::getline(in, line)) {
                lines.push_back(line);
            }
        }

        auto setVal = [&](const std::string& key, const std::string& val) {
            std::string prefix = key + "=";
            for (auto& l : lines) {
                if (l.rfind(prefix, 0) == 0) {
                    l = prefix + val;
                    return;
                }
            }
            lines.push_back(prefix + val);
        };

        setVal("graphics_api", config->graphicsApi);
        setVal("vram_budget_mb", config->vramBudgetMb);
        setVal("display_mode", config->displayMode);
        setVal("resolution", config->resolution);
        if (config->outputResolution[0] != '\0') {
            setVal("output_res", config->outputResolution);
        }
        setVal("frame_pacer", config->framePacingMode);
        setVal("fps_limit", config->fpsLimit);
        setVal("vsync", config->vsync);
        setVal("texture_quality", config->textureQuality);
        setVal("shadow_quality", config->shadowQuality);
        setVal("ssao_quality", config->ssaoQuality);
        setVal("anisotropy", config->anisotropy);
        setVal("upscaler", config->upscaler);
        setVal("anti_aliasing", config->antiAliasing);
        setVal("frame_generation", config->frameGeneration);
        setVal("reflex_mode", config->reflexMode);
        setVal("streaming_budget_mb", config->streamingBudgetMb);
        setVal("dynamic_resolution", config->dynamicResolution);
        setVal("sharpening", std::to_string(config->sharpening));
        setVal("neural_assist_mode", config->neuralAssistMode);
        setVal("storage_optimization", config->storageOptimization);

        std::ofstream out(iniPath);
        if (out.is_open()) {
            for (const auto& l : lines) {
                out << l << "\n";
            }
        }
    }

    return 1;
}
