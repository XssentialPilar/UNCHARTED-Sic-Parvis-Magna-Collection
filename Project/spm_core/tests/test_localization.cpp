// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore Test Suite - Localization Completeness
#include "spm/spm_core.h"
#include <cstdio>
#include <cassert>
#include <cstring>

static const char* kAllKeys[] = {
    "AppTitle",
    "NavExpeditions",
    "NavGraphics",
    "NavDisplay",
    "NavControls",
    "NavTelemetry",
    "NavQuit",
    "PresetsLabel",
    "PresetIgpu",
    "PresetLow",
    "PresetMed",
    "PresetHigh",
    "PresetUltra",
    "ActiveGpu",
    "Dedicated",
    "Integrated",
    "SelectExpedition",
    "Part1Title",
    "Part1Sub",
    "Part2Title",
    "Part2Sub",
    "Part3Title",
    "Part3Sub",
    "BtnPlay",
    "BtnStarting",
    "BtnStop",
    "ReadyStatus",
    "GraphicsHeader",
    "GpuActiveLabel",
    "GpuActiveSub",
    "ApiLabel",
    "ApiSub",
    "VramCapLabel",
    "VramCapSub",
    "TexQualityLabel",
    "TexQualitySub",
    "ShadowQualityLabel",
    "ShadowQualitySub",
    "DynamicShadowsLabel",
    "DynamicShadowsSub",
    "ViewDistanceLabel",
    "ViewDistanceSub",
    "SsaoLabel",
    "SsaoSub",
    "AnisoLabel",
    "AnisoSub",
    "ChkFullResTex",
    "ChkHiresShadows",
    "DisplayHeader",
    "ResLabel",
    "ResSub",
    "UpscalerLabel",
    "UpscalerSub",
    "FpsLimitLabel",
    "FpsLimitSub",
    "FramePacerLabel",
    "FramePacerSub",
    "ChkVsync",
    "ChkMotionBlur",
    "ChkDepthOfField",
    "ControlsHeader",
    "GameDirLabel",
    "BtnBrowse",
    "MouseSectionHeader",
    "MouseSensLabel",
    "MouseAimSensLabel",
    "MouseVertLabel",
    "ChkInvertY",
    "ChkInvertX",
    "GamepadSectionHeader",
    "StickSpeedLabel",
    "ChkVibration",
    "PromptsLabel",
    "PromptsAuto",
    "PromptsPs",
    "PromptsXbox",
    "PromptsPc",
    "KeybindsHeader",
    "KeybindsSub",
    "BtnResetControls",
    "BtnSaveControls",
    "ActionMoveForward",
    "ActionMoveBackward",
    "ActionMoveLeft",
    "ActionMoveRight",
    "ActionLookUp",
    "ActionLookDown",
    "ActionLookLeft",
    "ActionLookRight",
    "ActionJump",
    "ActionCover",
    "ActionMelee",
    "ActionInteract",
    "ActionAim",
    "ActionFire",
    "ActionReload",
    "ActionGrenade",
    "ActionSprint",
    "ActionZoom",
    "ActionWeapon1",
    "ActionWeapon2",
    "ActionWeapon3",
    "ActionWeapon4",
    "ActionJournal",
    "ActionPause",
    "ActionOverlay",
    "PressKeyPrompt",
    "TelemetryHeader",
    "BtnVerify",
    "BtnClearConsole",
    "LanguageLabel",
    // --- v0.6.0 Update & Graphics Integration Keys ---
    "UpdateBannerTitle",
    "UpdateBannerDesc",
    "BtnUpdate",
    "UpdateDownloading",
    "UpdateVerifying",
    "UpdateApplying",
    "UpdateFailed",
    "GpuDedicated",
    "GpuIntegrated",
    "VramUltraLow",
    "VramLow",
    "VramMed",
    "VramHigh",
    "VramUnlimited",
    "TexLow",
    "TexMed",
    "TexHigh",
    "TexUltra",
    "ShadowLow",
    "ShadowMed",
    "ShadowHigh",
    "ShadowUltra",
    "DynShadowsOn",
    "DynShadowsOff",
    "AoOff",
    "AoHalf",
    "AoFull",
    "AoGtao",
    "Aniso1",
    "Aniso16",
    "AaOff",
    "AaFxaa",
    "AaSmaa",
    "AaMsaa2x",
    "AaMsaa4x",
    "AaMsaa8x",
    "AaDlaa",
    "FgOff",
    "FgFsr2x",
    "FgDlss2x",
    "FgDlss3x",
    "FgDlss4x",
    "FgDlss5x",
    "FgDlss6x",
    "FgDynamic",
    "NeuralAssistLabel",
    "NeuralAssistSub",
    "NeuralAuto",
    "NeuralEnabled",
    "NeuralDisabled",
    "NpuDetectedLabel",
    "NpuActiveBackendLabel",
    "StorageTypeLabel",
    "StorageTypeSub",
    "StorageNvme",
    "StorageSsd",
    "StorageHdd"
};

static const char* kLanguages[] = {
    "es_419", "es_ES", "en", "pt_BR", "fr", "de", "it", "ru", "ja", "zh_CN"
};

int main() {
    printf("[TEST] Running test_spm_localization...\n");

    const char* sysLang = SPM_DetectSystemLanguage();
    printf("  Detected System Language: %s\n", sysLang);
    assert(sysLang != nullptr);
    assert(strlen(sysLang) >= 2);

    size_t keyCount = sizeof(kAllKeys) / sizeof(kAllKeys[0]);
    size_t langCount = sizeof(kLanguages) / sizeof(kLanguages[0]);
    printf("  Testing %u keys across %u languages (%u total string validations)...\n",
           (unsigned int)keyCount, (unsigned int)langCount, (unsigned int)(keyCount * langCount));

    int missingCount = 0;
    for (size_t l = 0; l < langCount; ++l) {
        const char* lang = kLanguages[l];
        for (size_t k = 0; k < keyCount; ++k) {
            const char* key = kAllKeys[k];
            const char* val = SPM_GetLocalizedString(lang, key, nullptr);
            if (!val || val[0] == '\0' || strcmp(val, key) == 0) {
                printf("  [ERROR] Missing or untranslated key '%s' in language '%s'\n", key, lang);
                missingCount++;
            }
        }
    }

    printf("  Validation complete. Missing/untranslated keys: %d\n", missingCount);
    assert(missingCount == 0);

    // Test fallback behavior
    const char* fallbackTest = SPM_GetLocalizedString("es_419", "NonExistentKey123", "CustomFallback");
    assert(strcmp(fallbackTest, "CustomFallback") == 0);

    printf("[PASS] test_spm_localization passed with 100%% translation coverage across all 10 languages.\n");
    return 0;
}
