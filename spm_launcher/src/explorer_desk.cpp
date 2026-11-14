// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection — Explorer's Desk Interface
#include "explorer_desk.hpp"
#include "ui_theme.hpp"
#include "game_launcher.hpp"
#include "spm/spm_core.h"
#include "imgui.h"
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

namespace spm {

ExplorerDesk::ExplorerDesk() {
    SPM_GetDefaultConfig(&config_);
}

ExplorerDesk::~ExplorerDesk() {
    if (!workspaceRoot_.empty()) {
        std::string cfgPath = (fs::path(workspaceRoot_) / "launcher.json").string();
        SPM_SaveConfig(cfgPath.c_str(), &config_);
        SPM_SyncConfigToIni(workspaceRoot_.c_str(), &config_);
    }
}

const char* ExplorerDesk::Tr(const char* key, const char* fallback) const {
    const char* lang = config_.language;
    if (std::strcmp(lang, "auto") == 0) {
        lang = SPM_DetectSystemLanguage();
    }
    return SPM_GetLocalizedString(lang, key, fallback);
}

void ExplorerDesk::Initialize(const std::string& workspaceRoot) {
    workspaceRoot_ = workspaceRoot;

    // Detect GPUs
    SPMGpuInfo detected[8]{};
    int count = 0;
    if (SPM_DetectGpus(detected, 8, &count) && count > 0) {
        gpus_.assign(detected, detected + count);
        recommendedGpuIndex_ = SPM_GetRecommendedGpuIndex(detected, count);
        activeGpuIndex_ = (recommendedGpuIndex_ >= 0 && recommendedGpuIndex_ < count) ? recommendedGpuIndex_ : 0;
    }

    // Load saved configuration
    std::string cfgPath = (fs::path(workspaceRoot_) / "launcher.json").string();
    SPM_LoadConfig(cfgPath.c_str(), &config_);

    // If GPU wasn't saved, set recommended
    if (config_.selectedGpuName[0] == '\0' && !gpus_.empty()) {
        strncpy(config_.selectedGpuName, gpus_[activeGpuIndex_].name, sizeof(config_.selectedGpuName) - 1);
        strncpy(config_.selectedGpuId, gpus_[activeGpuIndex_].stableLuid, sizeof(config_.selectedGpuId) - 1);
    }
}

void ExplorerDesk::ApplyPreset(int presetIndex) {
    // 0: iGPU (820MB), 1: BAJO (1GB), 2: MEDIO (2GB), 3: ALTO, 4: ULTRA
    switch (presetIndex) {
        case 0: // iGPU (820MB)
            strncpy(config_.vramBudgetMb, "820", sizeof(config_.vramBudgetMb));
            strncpy(config_.textureQuality, "low", sizeof(config_.textureQuality));
            strncpy(config_.shadowQuality, "low", sizeof(config_.shadowQuality));
            strncpy(config_.ssaoQuality, "off", sizeof(config_.ssaoQuality));
            strncpy(config_.anisotropy, "2", sizeof(config_.anisotropy));
            strncpy(config_.upscaler, "fsr1", sizeof(config_.upscaler));
            strncpy(config_.fpsLimit, "30", sizeof(config_.fpsLimit));
            strncpy(config_.streamingBudgetMb, "820", sizeof(config_.streamingBudgetMb));
            strncpy(config_.antiAliasing, "off", sizeof(config_.antiAliasing));
            strncpy(config_.frameGeneration, "off", sizeof(config_.frameGeneration));
            strncpy(config_.reflexMode, "off", sizeof(config_.reflexMode));
            break;
        case 1: // BAJO (1GB)
            strncpy(config_.vramBudgetMb, "1720", sizeof(config_.vramBudgetMb));
            strncpy(config_.textureQuality, "medium", sizeof(config_.textureQuality));
            strncpy(config_.shadowQuality, "low", sizeof(config_.shadowQuality));
            strncpy(config_.ssaoQuality, "off", sizeof(config_.ssaoQuality));
            strncpy(config_.anisotropy, "4", sizeof(config_.anisotropy));
            strncpy(config_.upscaler, "fsr3", sizeof(config_.upscaler));
            strncpy(config_.fpsLimit, "60", sizeof(config_.fpsLimit));
            strncpy(config_.streamingBudgetMb, "2048", sizeof(config_.streamingBudgetMb));
            strncpy(config_.antiAliasing, "smaa", sizeof(config_.antiAliasing));
            strncpy(config_.frameGeneration, "off", sizeof(config_.frameGeneration));
            strncpy(config_.reflexMode, "on", sizeof(config_.reflexMode));
            strncpy(config_.streamingBudgetMb, "1024", sizeof(config_.streamingBudgetMb));
            strncpy(config_.antiAliasing, "fxaa", sizeof(config_.antiAliasing));
            strncpy(config_.frameGeneration, "off", sizeof(config_.frameGeneration));
            strncpy(config_.reflexMode, "on", sizeof(config_.reflexMode));
            break;
        case 2: // MEDIO (2GB)
            strncpy(config_.vramBudgetMb, "3200", sizeof(config_.vramBudgetMb));
            strncpy(config_.textureQuality, "medium", sizeof(config_.textureQuality));
            strncpy(config_.shadowQuality, "medium", sizeof(config_.shadowQuality));
            strncpy(config_.ssaoQuality, "half", sizeof(config_.ssaoQuality));
            strncpy(config_.anisotropy, "8", sizeof(config_.anisotropy));
            strncpy(config_.upscaler, "off", sizeof(config_.upscaler));
            strncpy(config_.fpsLimit, "60", sizeof(config_.fpsLimit));
            break;
        case 3: // ALTO
            strncpy(config_.vramBudgetMb, "6144", sizeof(config_.vramBudgetMb));
            strncpy(config_.textureQuality, "high", sizeof(config_.textureQuality));
            strncpy(config_.shadowQuality, "high", sizeof(config_.shadowQuality));
            strncpy(config_.ssaoQuality, "full", sizeof(config_.ssaoQuality));
            strncpy(config_.anisotropy, "16", sizeof(config_.anisotropy));
            strncpy(config_.upscaler, "off", sizeof(config_.upscaler));
            strncpy(config_.fpsLimit, "120", sizeof(config_.fpsLimit));
            strncpy(config_.streamingBudgetMb, "4096", sizeof(config_.streamingBudgetMb));
            strncpy(config_.antiAliasing, "dlaa", sizeof(config_.antiAliasing));
            strncpy(config_.frameGeneration, "dlss_2x", sizeof(config_.frameGeneration));
            strncpy(config_.reflexMode, "boost", sizeof(config_.reflexMode));
            break;
        case 4: // ULTRA
            strncpy(config_.vramBudgetMb, "0", sizeof(config_.vramBudgetMb));
            strncpy(config_.textureQuality, "ultra", sizeof(config_.textureQuality));
            strncpy(config_.shadowQuality, "ultra", sizeof(config_.shadowQuality));
            strncpy(config_.ssaoQuality, "full", sizeof(config_.ssaoQuality));
            strncpy(config_.anisotropy, "16", sizeof(config_.anisotropy));
            strncpy(config_.upscaler, "off", sizeof(config_.upscaler));
            strncpy(config_.fpsLimit, "0", sizeof(config_.fpsLimit));
            strncpy(config_.streamingBudgetMb, "8192", sizeof(config_.streamingBudgetMb));
            strncpy(config_.antiAliasing, "dlaa", sizeof(config_.antiAliasing));
            strncpy(config_.frameGeneration, "dynamic", sizeof(config_.frameGeneration));
            strncpy(config_.reflexMode, "boost", sizeof(config_.reflexMode));
            break;
    }

    std::string cfgPath = (fs::path(workspaceRoot_) / "launcher.json").string();
    SPM_SaveConfig(cfgPath.c_str(), &config_);
    SPM_SyncConfigToIni(workspaceRoot_.c_str(), &config_);
}

void ExplorerDesk::Render() {
    ImGuiIO& io = ImGui::GetIO();

    // Sample frame time for telemetry
    frameTimes_[frameTimeHead_] = io.DeltaTime * 1000.0f;
    frameTimeHead_ = (frameTimeHead_ + 1) % 60;

    // Sample memory if running
    uint64_t ws = 0, commit = 0;
    GameLauncher::Instance().GetMemoryStats(ws, commit);
    if (ws > 0) workingSetMb_ = ws / (1024 * 1024);
    if (commit > 0) commitMb_ = commit / (1024 * 1024);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("ExplorerDeskMain", nullptr, windowFlags);

    RenderHeader();

    ImGui::Separator();

    // Main layout: Sidebar (Left) + Content (Right)
    float sidebarWidth = 220.0f;
    float contentHeight = io.DisplaySize.y - 120.0f; // Header ~60px, Footer ~50px, Padding

    ImGui::BeginChild("Sidebar", ImVec2(sidebarWidth, contentHeight), true);
    RenderSidebar();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("ContentArea", ImVec2(0, contentHeight), true);
    switch (activeTab_) {
        case 0: RenderTabExpeditions(); break;
        case 1: RenderTabGraphics(); break;
        case 2: RenderTabDisplay(); break;
        case 3: RenderTabControls(); break;
        case 4: RenderTabTelemetry(); break;
        case 5: RenderTabUpdates(); break;
    }
    ImGui::EndChild();

    ImGui::Separator();

    RenderFooter();

    ImGui::End();
}

void ExplorerDesk::RenderHeader() {
    ImGui::PushStyleColor(ImGuiCol_Text, ui::Palette::GoldPrimary);
    ImGui::TextUnformatted("UNCHARTED: SIC PARVIS MAGNA COLLECTION");
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::TextDisabled(" | %s (v%s)", Tr("nav_title", "Explorer's Desk"), SPM_GetCoreVersion());

    // Top Right Controls: Presets and Language
    float rightOffset = 580.0f;
    ImGui::SameLine(ImGui::GetWindowWidth() - rightOffset);

    ImGui::TextUnformatted(Tr("lbl_presets", "PREAJUSTES:"));
    ImGui::SameLine();
    if (ImGui::SmallButton("iGPU (820MB)")) ApplyPreset(0);
    ImGui::SameLine();
    if (ImGui::SmallButton("BAJO (1GB)")) ApplyPreset(1);
    ImGui::SameLine();
    if (ImGui::SmallButton("MEDIO (2GB)")) ApplyPreset(2);
    ImGui::SameLine();
    if (ImGui::SmallButton("ALTO")) ApplyPreset(3);
    ImGui::SameLine();
    if (ImGui::SmallButton("ULTRA")) ApplyPreset(4);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    const char* languages[] = { "auto", "en", "es_419", "es_ES", "pt_BR", "fr", "de", "it", "ru", "ja", "zh_CN" };
    const char* langNames[] = { "Auto (Sistema)", "English", "Español (LatAm)", "Español (ES)", "Português", "Français", "Deutsch", "Italiano", "Русский", "日本語", "简体中文" };
    int currentLangIdx = 0;
    for (int i = 0; i < 11; ++i) {
        if (std::strcmp(config_.language, languages[i]) == 0) {
            currentLangIdx = i;
            break;
        }
    }
    if (ImGui::Combo("##LangCombo", &currentLangIdx, langNames, 11)) {
        strncpy(config_.language, languages[currentLangIdx], sizeof(config_.language) - 1);
        std::string cfgPath = (fs::path(workspaceRoot_) / "launcher.json").string();
        SPM_SaveConfig(cfgPath.c_str(), &config_);
    }
}

void ExplorerDesk::RenderSidebar() {
    auto NavButton = [this](const char* label, int tabIdx) {
        bool isActive = (activeTab_ == tabIdx);
        if (isActive) {
            ImGui::PushStyleColor(ImGuiCol_Button, ui::Palette::GoldBorder);
            ImGui::PushStyleColor(ImGuiCol_Text, ui::Palette::GoldHover);
        }
        if (ImGui::Button(label, ImVec2(-1, 38.0f))) {
            activeTab_ = tabIdx;
        }
        if (isActive) {
            ImGui::PopStyleColor(2);
        }
    };

    NavButton(Tr("tab_expeditions", "EXPEDICIONES"), 0);
    ImGui::Spacing();
    NavButton(Tr("tab_graphics", "GRÁFICOS Y VRAM"), 1);
    ImGui::Spacing();
    NavButton(Tr("tab_display", "PANTALLA Y ESCALADO"), 2);
    ImGui::Spacing();
    NavButton(Tr("tab_controls", "CONTROLES Y JUEGO"), 3);
    ImGui::Spacing();
    NavButton(Tr("tab_telemetry", "TELEMETRÍA Y LOGS"), 4);
    ImGui::Spacing();
    NavButton("ACTUALIZACIONES", 5);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::Palette::ErrorRed);
    if (ImGui::Button(Tr("btn_exit", "SALIR"), ImVec2(-1, 32.0f))) {
        RequestClose();
    }
    ImGui::PopStyleColor();
}

void ExplorerDesk::RenderTabExpeditions() {
    ImGui::PushStyleColor(ImGuiCol_Text, ui::Palette::GoldPrimary);
    ImGui::TextUnformatted(Tr("tab_expeditions", "EXPEDICIONES"));
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // Game Directory Configuration
    ImGui::TextUnformatted(Tr("lbl_game_dir", "Directorio de Archivos del Juego (CUSA02344):"));
    static char gameDirBuf[512];
    strncpy(gameDirBuf, config_.gameDir, sizeof(gameDirBuf));
    ImGui::SetNextItemWidth(-120.0f);
    if (ImGui::InputText("##GameDirInput", gameDirBuf, sizeof(gameDirBuf))) {
        strncpy(config_.gameDir, gameDirBuf, sizeof(config_.gameDir) - 1);
        std::string cfgPath = (fs::path(workspaceRoot_) / "launcher.json").string();
        SPM_SaveConfig(cfgPath.c_str(), &config_);
    }
    ImGui::SameLine();
    if (ImGui::Button(Tr("btn_browse", "BUSCAR..."), ImVec2(110.0f, 0))) {
        // Folder selection can be pasted or typed directly
    }

    // Validation Status Check
    SPMValidationResult valRes{};
    int valStatus = SPM_ValidateGameFiles(config_.gameDir, &valRes);
    if (config_.gameDir[0] == '\0') {
        ImGui::TextColored(ui::Palette::WarningOrange, "Estado: Ningún directorio seleccionado. Por favor especifica la ruta de tu copia.");
    } else if (valStatus && valRes.isValid) {
        ImGui::TextColored(ui::Palette::SuccessGreen, "Estado: Copia del juego verificada (%u archivos válidos).", valRes.totalFilesChecked);
    } else {
        ImGui::TextColored(ui::Palette::ErrorRed, "Estado: Faltan archivos requeridos en la carpeta seleccionada (%u faltantes).", valRes.missingFilesCount);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Game Cards (U1, U2, U3)
    const char* games[] = { "u1", "u2", "u3" };
    const char* titles[] = {
        Tr("u1_title", "Uncharted: El Tesoro de Drake"),
        Tr("u2_title", "Uncharted 2: El Reino de los Ladrones"),
        Tr("u3_title", "Uncharted 3: La Traición de Drake")
    };
    const char* descs[] = {
        "Expedición al Amazonas y la Isla Perdida (CUSA02320 / CUSA02344)",
        "Expedición tras la Piedra Cintamani y Shambhala (CUSA02344)",
        "Expedición en busca de la Atlántida de las Arenas (CUSA02344)"
    };

    float cardWidth = (ImGui::GetContentRegionAvail().x - 24.0f) / 3.0f;
    for (int i = 0; i < 3; ++i) {
        if (i > 0) ImGui::SameLine();

        bool isSelected = (std::strcmp(config_.selectedGame, games[i]) == 0);
        if (isSelected) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.18f, 0.17f, 0.13f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_Border, ui::Palette::GoldPrimary);
        }

        std::string cardId = "Card_" + std::string(games[i]);
        ImGui::BeginChild(cardId.c_str(), ImVec2(cardWidth, 200.0f), true);

        ImGui::PushStyleColor(ImGuiCol_Text, isSelected ? ui::Palette::GoldHover : ui::Palette::ParchmentText);
        ImGui::TextWrapped("%s", titles[i]);
        ImGui::PopStyleColor();

        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextDisabled("%s", descs[i]);

        ImGui::Spacing();
        ImGui::SetCursorPosY(155.0f);
        if (ImGui::Button(isSelected ? "SELECCIONADO" : "ELEGIR EXPEDICIÓN", ImVec2(-1, 32.0f))) {
            strncpy(config_.selectedGame, games[i], sizeof(config_.selectedGame) - 1);
            std::string cfgPath = (fs::path(workspaceRoot_) / "launcher.json").string();
            SPM_SaveConfig(cfgPath.c_str(), &config_);
            SPM_SyncConfigToIni(workspaceRoot_.c_str(), &config_);
        }

        ImGui::EndChild();

        if (isSelected) {
            ImGui::PopStyleColor(2);
        }
    }

    ImGui::Spacing();
    ImGui::Spacing();

    // Big Action Button
    ProcessState procState = GameLauncher::Instance().GetState();
    if (procState == ProcessState::Running) {
        ImGui::PushStyleColor(ImGuiCol_Button, ui::Palette::ErrorRed);
        if (ImGui::Button("DETENER EJECUCIÓN DEL JUEGO", ImVec2(-1, 46.0f))) {
            GameLauncher::Instance().StopGame();
        }
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ui::Palette::GoldPrimary);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ui::Palette::GoldHover);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.08f, 0.08f, 1.0f));

        char launchLabel[128];
        snprintf(launchLabel, sizeof(launchLabel), "▶ INICIAR EXPEDICIÓN (%s)", config_.selectedGame);
        if (ImGui::Button(launchLabel, ImVec2(-1, 46.0f))) {
            GameLauncher::Instance().StartGame(config_, workspaceRoot_);
        }
        ImGui::PopStyleColor(3);
    }
}

void ExplorerDesk::RenderTabGraphics() {
    ImGui::PushStyleColor(ImGuiCol_Text, ui::Palette::GoldPrimary);
    ImGui::TextUnformatted(Tr("tab_graphics", "GRÁFICOS Y VRAM"));
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // GPU Selector
    ImGui::TextUnformatted(Tr("lbl_gpu_device", "Dispositivo Gráfico (GPU):"));
    if (!gpus_.empty()) {
        std::vector<const char*> gpuNames;
        for (const auto& g : gpus_) gpuNames.push_back(g.name);
        ImGui::SetNextItemWidth(380.0f);
        if (ImGui::Combo("##GpuCombo", &activeGpuIndex_, gpuNames.data(), (int)gpuNames.size())) {
            strncpy(config_.selectedGpuName, gpus_[activeGpuIndex_].name, sizeof(config_.selectedGpuName) - 1);
            strncpy(config_.selectedGpuId, gpus_[activeGpuIndex_].stableLuid, sizeof(config_.selectedGpuId) - 1);
            std::string cfgPath = (fs::path(workspaceRoot_) / "launcher.json").string();
            SPM_SaveConfig(cfgPath.c_str(), &config_);
        }
        ImGui::SameLine();
        const auto& act = gpus_[activeGpuIndex_];
        ImGui::TextDisabled("VRAM: %llu MB | %s", act.dedicatedVramBytes / (1024 * 1024), act.isDiscrete ? "dGPU" : "iGPU");
    }

    ImGui::Spacing();

    // Graphics API
    ImGui::TextUnformatted(Tr("lbl_graphics_api", "API Gráfica:"));
    const char* apis[] = { "dx11", "dx12", "vulkan", "dx9" };
    const char* apiNames[] = { "Direct3D 11 (Recomendada)", "Direct3D 12 (Experimental)", "Vulkan (Cross-Platform)", "Direct3D 9 (Compatibilidad)" };
    int apiIdx = 0;
    for (int i = 0; i < 4; ++i) {
        if (std::strcmp(config_.graphicsApi, apis[i]) == 0) { apiIdx = i; break; }
    }
    ImGui::SetNextItemWidth(380.0f);
    if (ImGui::Combo("##ApiCombo", &apiIdx, apiNames, 4)) {
        strncpy(config_.graphicsApi, apis[apiIdx], sizeof(config_.graphicsApi) - 1);
        std::string cfgPath = (fs::path(workspaceRoot_) / "launcher.json").string();
        SPM_SaveConfig(cfgPath.c_str(), &config_);
    }

    ImGui::Spacing();

    // VRAM Budget Manager
    ImGui::TextUnformatted(Tr("lbl_vram_budget", "Límite de Presupuesto VRAM (VramBudgetManager):"));
    const char* vramBudgets[] = { "0", "820", "1720", "3200", "6144" };
    const char* vramLabels[] = { "Auto (Sin límite artificial)", "iGPU (820 MB - Texturas estrictas)", "Bajo (1720 MB - GPUs 2GB)", "Medio (3200 MB - GPUs 4GB)", "Alto (6144 MB - GPUs 6GB+)" };
    int vramIdx = 0;
    for (int i = 0; i < 5; ++i) {
        if (std::strcmp(config_.vramBudgetMb, vramBudgets[i]) == 0) { vramIdx = i; break; }
    }
    ImGui::SetNextItemWidth(380.0f);
    if (ImGui::Combo("##VramCombo", &vramIdx, vramLabels, 5)) {
        strncpy(config_.vramBudgetMb, vramBudgets[vramIdx], sizeof(config_.vramBudgetMb) - 1);
        std::string cfgPath = (fs::path(workspaceRoot_) / "launcher.json").string();
        SPM_SaveConfig(cfgPath.c_str(), &config_);
    }
    ImGui::TextDisabled("VramBudgetManager activa desalojo LRU de mips para texturas no usadas en >5min y limita subidas a 16MB/frame.");

    ImGui::Spacing();

    // Frame Pacer
    ImGui::TextUnformatted(Tr("lbl_frame_pacing", "Estabilizador de Cuadros (Frame Pacer):"));
    const char* pacers[] = { "hybrid", "waitable", "sleep", "off" };
    const char* pacerLabels[] = { "Hybrid Pacer (Temporizador Alta Precisión + Spin)", "Waitable Swapchain (Latencia Mínima DXGI)", "Sleep Pacer (Bajo consumo CPU)", "Desactivado (Sin sincronización manual)" };
    int pacerIdx = 0;
    for (int i = 0; i < 4; ++i) {
        if (std::strcmp(config_.framePacingMode, pacers[i]) == 0) { pacerIdx = i; break; }
    }
    ImGui::SetNextItemWidth(380.0f);
    if (ImGui::Combo("##PacerCombo", &pacerIdx, pacerLabels, 4)) {
        strncpy(config_.framePacingMode, pacers[pacerIdx], sizeof(config_.framePacingMode) - 1);
        std::string cfgPath = (fs::path(workspaceRoot_) / "launcher.json").string();
        SPM_SaveConfig(cfgPath.c_str(), &config_);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Quality Settings Table
    if (ImGui::BeginTable("QualitySettingsTable", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(Tr("lbl_texture_quality", "Calidad de Texturas:"));
        const char* qValues[] = { "low", "medium", "high", "ultra" };
        const char* qLabels[] = { "Bajo (Mip 2/1)", "Medio (Mip 1)", "Alto (Original PS4)", "Ultra (Anisotrópico 16x)" };
        int texIdx = 1;
        for (int i = 0; i < 4; ++i) if (std::strcmp(config_.textureQuality, qValues[i]) == 0) { texIdx = i; break; }
        if (ImGui::Combo("##TexCombo", &texIdx, qLabels, 4)) {
            strncpy(config_.textureQuality, qValues[texIdx], sizeof(config_.textureQuality) - 1);
        }

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(Tr("lbl_shadow_quality", "Calidad de Sombras:"));
        const char* shadowLabels[] = { "Bajo (512x512)", "Medio (1024x1024)", "Alto (2048x2048)", "Ultra (4096x4096)" };
        int shdIdx = 1;
        for (int i = 0; i < 4; ++i) if (std::strcmp(config_.shadowQuality, qValues[i]) == 0) { shdIdx = i; break; }
        if (ImGui::Combo("##ShdCombo", &shdIdx, shadowLabels, 4)) {
            strncpy(config_.shadowQuality, qValues[shdIdx], sizeof(config_.shadowQuality) - 1);
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(Tr("lbl_ssao", "Oclusión Ambiental (SSAO):"));
        const char* ssaoValues[] = { "off", "half", "full" };
        const char* ssaoLabels[] = { "Desactivado", "Half-Resolution (Rápido)", "Full-Resolution (Detallado)" };
        int ssaoIdx = 1;
        for (int i = 0; i < 3; ++i) if (std::strcmp(config_.ssaoQuality, ssaoValues[i]) == 0) { ssaoIdx = i; break; }
        if (ImGui::Combo("##SsaoCombo", &ssaoIdx, ssaoLabels, 3)) {
            strncpy(config_.ssaoQuality, ssaoValues[ssaoIdx], sizeof(config_.ssaoQuality) - 1);
        }

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(Tr("lbl_anisotropy", "Filtrado Anisotrópico:"));
        const char* anisoValues[] = { "1", "2", "4", "8", "16" };
        const char* anisoLabels[] = { "1x (Bilineal)", "2x", "4x", "8x", "16x (Máxima Nitidez)" };
        int anisoIdx = 2;
        for (int i = 0; i < 5; ++i) if (std::strcmp(config_.anisotropy, anisoValues[i]) == 0) { anisoIdx = i; break; }
        if (ImGui::Combo("##AnisoCombo", &anisoIdx, anisoLabels, 5)) {
            strncpy(config_.anisotropy, anisoValues[anisoIdx], sizeof(config_.anisotropy) - 1);
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(Tr("AaLabel", "Anti-Aliasing (AA):"));
        const char* aaValues[] = { "off", "fxaa", "smaa", "msaa2x", "msaa4x", "msaa8x", "dlaa" };
        const char* aaLabels[] = { "Desactivado (Off)", "FXAA (Rápido)", "SMAA (Subpíxel)", "MSAA 2x", "MSAA 4x", "MSAA 8x", "NVIDIA DLAA" };
        int aaIdx = 0;
        for (int i = 0; i < 7; ++i) if (std::strcmp(config_.antiAliasing, aaValues[i]) == 0) { aaIdx = i; break; }
        if (ImGui::Combo("##AaCombo", &aaIdx, aaLabels, 7)) {
            strncpy(config_.antiAliasing, aaValues[aaIdx], sizeof(config_.antiAliasing) - 1);
        }

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(Tr("StreamingBudgetLabel", "Presupuesto de Streaming (SpmAssetStreamer):"));
        const char* streamBudgets[] = { "820", "1024", "2048", "4096", "8192" };
        const char* streamLabels[] = { "820 MB (iGPU / Límite Bajo)", "1024 MB (1 GB)", "2048 MB (2 GB Estándar)", "4096 MB (4 GB Alto)", "8192 MB (8 GB+ Ultra)" };
        int streamIdx = 2;
        for (int i = 0; i < 5; ++i) if (std::strcmp(config_.streamingBudgetMb, streamBudgets[i]) == 0) { streamIdx = i; break; }
        if (ImGui::Combo("##StreamCombo", &streamIdx, streamLabels, 5)) {
            strncpy(config_.streamingBudgetMb, streamBudgets[streamIdx], sizeof(config_.streamingBudgetMb) - 1);
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(Tr("NeuralAssistLabel", "SPM Neural Assist (Aceleración NPU):"));
        const char* neuralValues[] = { "auto", "enabled", "disabled" };
        const char* neuralLabels[] = {
            Tr("NeuralAuto", "Automático (Detección Óptima NPU/CPU) ★"),
            Tr("NeuralEnabled", "Activado (Forzar Aceleración NPU / DirectML)"),
            Tr("NeuralDisabled", "Desactivado (Streaming Heurístico Convencional)")
        };
        int neuralIdx = 0;
        for (int i = 0; i < 3; ++i) if (std::strcmp(config_.neuralAssistMode, neuralValues[i]) == 0) { neuralIdx = i; break; }
        if (ImGui::Combo("##NeuralCombo", &neuralIdx, neuralLabels, 3)) {
            strncpy(config_.neuralAssistMode, neuralValues[neuralIdx], sizeof(config_.neuralAssistMode) - 1);
        }

        ImGui::TableNextColumn();
        ImGui::TextUnformatted(Tr("StorageTypeLabel", "Optimización de Almacenamiento:"));
        const char* storageValues[] = { "auto", "nvme", "ssd", "hdd" };
        const char* storageLabels[] = {
            "Automático (Detección Óptima NVMe/SSD/HDD) ★",
            Tr("StorageNvme", "NVMe SSD (DirectStorage Ultra-Rápido ~5000 MB/s)"),
            Tr("StorageSsd", "SATA SSD (Rápido Estándar ~550 MB/s)"),
            Tr("StorageHdd", "HDD Mecánico (Modo Secuencial de Bajo Impacto ~140 MB/s)")
        };
        int storageIdx = 0;
        for (int i = 0; i < 4; ++i) if (std::strcmp(config_.storageOptimization, storageValues[i]) == 0) { storageIdx = i; break; }
        if (ImGui::Combo("##StorageCombo", &storageIdx, storageLabels, 4)) {
            strncpy(config_.storageOptimization, storageValues[storageIdx], sizeof(config_.storageOptimization) - 1);
        }

        ImGui::EndTable();
    }
}

void ExplorerDesk::RenderTabDisplay() {
    ImGui::PushStyleColor(ImGuiCol_Text, ui::Palette::GoldPrimary);
    ImGui::TextUnformatted(Tr("tab_display", "PANTALLA Y ESCALADO"));
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // Display Mode
    ImGui::TextUnformatted(Tr("lbl_display_mode", "Modo de Pantalla:"));
    const char* dispModes[] = { "fullscreen", "borderless", "windowed" };
    const char* dispLabels[] = { "Pantalla Completa Exclusiva", "Ventana Sin Bordes (Borderless)", "Ventana Estándar" };
    int dispIdx = 1;
    for (int i = 0; i < 3; ++i) if (std::strcmp(config_.displayMode, dispModes[i]) == 0) { dispIdx = i; break; }
    ImGui::SetNextItemWidth(380.0f);
    if (ImGui::Combo("##DispCombo", &dispIdx, dispLabels, 3)) {
        strncpy(config_.displayMode, dispModes[dispIdx], sizeof(config_.displayMode) - 1);
    }

    ImGui::Spacing();

    // Resolution
    ImGui::TextUnformatted(Tr("lbl_resolution", "Resolución de Renderizado:"));
    const char* resValues[] = { "1280x720", "1600x900", "1920x1080", "2560x1440", "3840x2160" };
    int resIdx = 2;
    for (int i = 0; i < 5; ++i) if (std::strcmp(config_.resolution, resValues[i]) == 0) { resIdx = i; break; }
    ImGui::SetNextItemWidth(380.0f);
    if (ImGui::Combo("##ResCombo", &resIdx, resValues, 5)) {
        strncpy(config_.resolution, resValues[resIdx], sizeof(config_.resolution) - 1);
    }

    ImGui::Spacing();

    // Upscaler
    ImGui::TextUnformatted(Tr("lbl_upscaler", "Escalador y Super-Resolución:"));
    const char* upscalers[] = { "off", "fsr1", "fsr3", "fsr4", "dlaa", "dlss" };
    const char* upscalerLabels[] = { "Desactivado (Nativo)", "AMD FSR 1.0 (Espacial Ligero)", "AMD FSR 3.1 (Temporal)", "AMD FSR 4.0", "DLAA (Anti-Aliasing Nativo)", "NVIDIA DLSS" };
    int upIdx = 0;
    for (int i = 0; i < 6; ++i) if (std::strcmp(config_.upscaler, upscalers[i]) == 0) { upIdx = i; break; }
    ImGui::SetNextItemWidth(380.0f);
    if (ImGui::Combo("##UpscalerCombo", &upIdx, upscalerLabels, 6)) {
        strncpy(config_.upscaler, upscalers[upIdx], sizeof(config_.upscaler) - 1);
    }

    ImGui::Spacing();

    // Multi-Frame Generation
    ImGui::TextUnformatted(Tr("FrameGenLabel", "Generación de Fotogramas (Frame Gen):"));
    const char* fgValues[] = { "off", "fsr_fg", "dlss_2x", "dlss_3x", "dlss_4x", "dlss_5x", "dlss_6x", "dynamic" };
    const char* fgLabels[] = { "Desactivado (Off)", "AMD FSR Frame Generation", "DLSS Frame Gen (2X)", "DLSS MFG (3X)", "DLSS MFG (4X)", "DLSS MFG (5X)", "DLSS MFG (6X)", "Dynamic MFG (Adaptativo)" };
    int fgIdx = 0;
    for (int i = 0; i < 8; ++i) if (std::strcmp(config_.frameGeneration, fgValues[i]) == 0) { fgIdx = i; break; }
    ImGui::SetNextItemWidth(380.0f);
    if (ImGui::Combo("##FgCombo", &fgIdx, fgLabels, 8)) {
        strncpy(config_.frameGeneration, fgValues[fgIdx], sizeof(config_.frameGeneration) - 1);
    }

    ImGui::Spacing();

    // NVIDIA Reflex Low Latency
    ImGui::TextUnformatted(Tr("ReflexLabel", "NVIDIA Reflex Latency:"));
    const char* reflexValues[] = { "off", "on", "boost" };
    const char* reflexLabels[] = { "Desactivado (Off)", "Activado (Reflex On)", "Activado + Boost (Máximo Rendimiento)" };
    int reflexIdx = 0;
    for (int i = 0; i < 3; ++i) if (std::strcmp(config_.reflexMode, reflexValues[i]) == 0) { reflexIdx = i; break; }
    ImGui::SetNextItemWidth(380.0f);
    if (ImGui::Combo("##ReflexCombo", &reflexIdx, reflexLabels, 3)) {
        strncpy(config_.reflexMode, reflexValues[reflexIdx], sizeof(config_.reflexMode) - 1);
    }

    ImGui::Spacing();

    // FPS Limit & VSync
    ImGui::TextUnformatted(Tr("lbl_fps_limit", "Límite de Fotogramas por Segundo:"));
    const char* fpsValues[] = { "30", "60", "90", "120", "0" };
    const char* fpsLabels[] = { "30 FPS", "60 FPS", "90 FPS", "120 FPS", "Sin Límite" };
    int fpsIdx = 1;
    for (int i = 0; i < 5; ++i) if (std::strcmp(config_.fpsLimit, fpsValues[i]) == 0) { fpsIdx = i; break; }
    ImGui::SetNextItemWidth(380.0f);
    if (ImGui::Combo("##FpsCombo", &fpsIdx, fpsLabels, 5)) {
        strncpy(config_.fpsLimit, fpsValues[fpsIdx], sizeof(config_.fpsLimit) - 1);
    }

    ImGui::Spacing();
    bool vsyncEnabled = (std::strcmp(config_.vsync, "1") == 0);
    if (ImGui::Checkbox(Tr("lbl_vsync", "Sincronización Vertical (VSync)"), &vsyncEnabled)) {
        strncpy(config_.vsync, vsyncEnabled ? "1" : "0", sizeof(config_.vsync));
    }
}

void ExplorerDesk::RenderTabControls() {
    ImGui::PushStyleColor(ImGuiCol_Text, ui::Palette::GoldPrimary);
    ImGui::TextUnformatted(Tr("tab_controls", "CONTROLES Y JUEGO"));
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // Mouse Settings
    ImGui::TextUnformatted("Configuración Avanzada de Ratón y Apuntado:");
    float mouseSens = (float)config_.mouseSensitivity;
    if (ImGui::SliderFloat("Sensibilidad General del Ratón", &mouseSens, 0.1f, 5.0f, "%.1fx")) {
        config_.mouseSensitivity = mouseSens;
    }
    float aimSens = (float)config_.mouseAimSensitivity;
    if (ImGui::SliderFloat("Sensibilidad al Apuntar con Arma", &aimSens, 0.1f, 5.0f, "%.1fx")) {
        config_.mouseAimSensitivity = aimSens;
    }
    float vertSens = (float)config_.mouseVerticalSensitivity;
    if (ImGui::SliderFloat("Sensibilidad Vertical del Ratón", &vertSens, 0.1f, 5.0f, "%.1fx")) {
        config_.mouseVerticalSensitivity = vertSens;
    }

    bool invY = (config_.mouseInvertY != 0);
    if (ImGui::Checkbox("Invertir Eje Y del Ratón (Mirar Arriba/Abajo)", &invY)) {
        config_.mouseInvertY = invY ? 1 : 0;
    }
    ImGui::SameLine();
    bool invX = (config_.mouseInvertX != 0);
    if (ImGui::Checkbox("Invertir Eje X del Ratón", &invX)) {
        config_.mouseInvertX = invX ? 1 : 0;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Gamepad Settings
    ImGui::TextUnformatted("Mando y Dispositivo de Control:");
    float stickSpeed = (float)config_.stickSpeed;
    if (ImGui::SliderFloat("Velocidad de Cámara con Palanca", &stickSpeed, 0.5f, 3.0f, "%.1fx")) {
        config_.stickSpeed = stickSpeed;
    }

    bool rumble = (config_.vibration != 0);
    if (ImGui::Checkbox("Vibración de Mando (Rumble Háptico)", &rumble)) {
        config_.vibration = rumble ? 1 : 0;
    }

    const char* promptValues[] = { "auto", "ps", "xbox", "kb" };
    const char* promptLabels[] = { "Automático (Detectado)", "PlayStation (DualShock / DualSense)", "Xbox / XInput", "Teclado y Ratón" };
    int promptIdx = 0;
    for (int i = 0; i < 4; ++i) if (std::strcmp(config_.prompts, promptValues[i]) == 0) { promptIdx = i; break; }
    ImGui::SetNextItemWidth(380.0f);
    if (ImGui::Combo("Iconos de Botones en Pantalla", &promptIdx, promptLabels, 4)) {
        strncpy(config_.prompts, promptValues[promptIdx], sizeof(config_.prompts) - 1);
    }
}

void ExplorerDesk::RenderTabTelemetry() {
    ImGui::PushStyleColor(ImGuiCol_Text, ui::Palette::GoldPrimary);
    ImGui::TextUnformatted(Tr("tab_telemetry", "TELEMETRÍA Y LOGS"));
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    // Process Status & Memory
    ProcessState state = GameLauncher::Instance().GetState();
    uint32_t pid = GameLauncher::Instance().GetProcessId();

    const char* stateStr = "Inactivo";
    ImVec4 stateColor = ui::Palette::TextMuted;
    if (state == ProcessState::Starting) { stateStr = "Iniciando..."; stateColor = ui::Palette::WarningOrange; }
    else if (state == ProcessState::Running) { stateStr = "En Ejecución"; stateColor = ui::Palette::SuccessGreen; }
    else if (state == ProcessState::Terminated) { stateStr = "Finalizado"; stateColor = ui::Palette::ParchmentText; }

    ImGui::Text("Estado del Proceso: ");
    ImGui::SameLine();
    ImGui::TextColored(stateColor, "%s [PID: %u]", stateStr, pid);

    ImGui::SameLine(350.0f);
    ImGui::Text("RAM Física (Working Set): %llu MB", workingSetMb_);
    ImGui::SameLine(600.0f);
    ImGui::Text("RAM Asignada (Commit): %llu MB", commitMb_);

    ImGui::Spacing();

    // Frame Time Graph
    ImGui::PlotLines("Tiempo por Cuadro (ms)", frameTimes_, 60, frameTimeHead_, nullptr, 0.0f, 33.3f, ImVec2(-1, 60.0f));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Live Logs Viewer
    ImGui::TextUnformatted("Registro de Ejecución (Logs):");
    ImGui::SameLine();
    if (ImGui::SmallButton("Limpiar Logs")) {
        GameLauncher::Instance().ClearLogs();
    }

    ImGui::BeginChild("LogViewer", ImVec2(-1, 160.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
    auto logs = GameLauncher::Instance().GetLogs(300);
    for (const auto& entry : logs) {
        ImGui::TextDisabled("[%s]", entry.timestamp.c_str());
        ImGui::SameLine();
        if (entry.isError) {
            ImGui::TextColored(ui::Palette::ErrorRed, "%s", entry.message.c_str());
        } else {
            ImGui::TextUnformatted(entry.message.c_str());
        }
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

void ExplorerDesk::RenderTabUpdates() {
    ImGui::PushStyleColor(ImGuiCol_Text, ui::Palette::GoldPrimary);
    ImGui::TextUnformatted("ACTUALIZACIONES Y CANALES DE DISTRIBUCIÓN");
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();

    const char* platId = SPM_GetPlatformIdString();
    const char* curVer = SPM_GetCoreVersion();

    ImGui::Text("Identificador Inmutable de Plataforma: ");
    ImGui::SameLine();
    ImGui::TextColored(ui::Palette::GoldHover, "%s", platId);

    ImGui::Text("Versión Actual Instalada: ");
    ImGui::SameLine();
    ImGui::TextColored(ui::Palette::ParchmentText, "v%s", curVer);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::TextWrapped("El sistema de actualización SPMCore v0.5.2 Beta garantiza la integridad total de los datos del usuario. Al recibir una nueva versión, se preservan intactos: saves, partidas, shader caches compilados (analysis), perfiles de usuario (user), y la configuración del lanzador (launcher.json). Solo se descargan paquetes coincidentes exactamente con el identificador inmutable de tu sistema operativo y arquitectura (%s).", platId);

    ImGui::Spacing();

    if (!isCheckingUpdate_) {
        if (ImGui::Button("BUSCAR ACTUALIZACIONES AHORA", ImVec2(280.0f, 38.0f))) {
            isCheckingUpdate_ = true;
            updateStatusMsg_ = "Consultando repositorio GitHub oficial...";
            std::thread([this, curVer, platId]() {
                int res = SPM_CheckUpdate(curVer, platId, &updateInfo_);
                isCheckingUpdate_ = false;
                updateChecked_ = true;
                if (res && updateInfo_.updateAvailable) {
                    updateStatusMsg_ = "¡Nueva versión disponible: " + std::string(updateInfo_.latestVersion) + "!";
                } else {
                    updateStatusMsg_ = "Tu versión (v" + std::string(curVer) + ") está completamente actualizada para " + std::string(platId) + ".";
                }
            }).detach();
        }
    } else {
        ImGui::TextColored(ui::Palette::WarningOrange, "Buscando actualizaciones para %s...", platId);
    }

    if (!updateStatusMsg_.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(updateInfo_.updateAvailable ? ui::Palette::SuccessGreen : ui::Palette::ParchmentText, "%s", updateStatusMsg_.c_str());
        if (updateInfo_.updateAvailable) {
            ImGui::BulletText("Título: %s", updateInfo_.releaseTitle);
            ImGui::BulletText("URL de Descarga: %s", updateInfo_.downloadUrl);
            ImGui::BulletText("SHA-256 Esperado: %s", updateInfo_.sha256Expected);
            ImGui::BulletText("Tamaño Esperado: %llu bytes", updateInfo_.expectedSizeBytes);
        }
    }
}

void ExplorerDesk::RenderFooter() {
    // Status Bar
    const char* gpuName = config_.selectedGpuName[0] ? config_.selectedGpuName : "Auto (Recomendada)";
    ImGui::TextDisabled("GPU: %s | API: %s | VRAM Cap: %s MB | Res: %s | Pacer: %s",
        gpuName, config_.graphicsApi, config_.vramBudgetMb, config_.resolution, config_.framePacingMode);

    ImGui::SameLine(ImGui::GetWindowWidth() - 200.0f);
    if (ImGui::Button("GUARDAR CONFIG", ImVec2(180.0f, 26.0f))) {
        std::string cfgPath = (fs::path(workspaceRoot_) / "launcher.json").string();
        SPM_SaveConfig(cfgPath.c_str(), &config_);
        SPM_SyncConfigToIni(workspaceRoot_.c_str(), &config_);
        GameLauncher::Instance().AppendLog("Configuración guardada y sincronizada con EngineConfig.ini");
    }
}

} // namespace spm
