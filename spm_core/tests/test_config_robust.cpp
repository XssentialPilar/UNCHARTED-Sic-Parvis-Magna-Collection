// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore Test Suite - Configuration robustness, preservation and INI sync
#include "spm/spm_core.h"
#include "test_check.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

long CurrentPid() {
#ifdef _WIN32
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}

std::string ReadWholeFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

bool DirectoryHasTemporaryFile(const fs::path& directory) {
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, ec)) {
        if (entry.path().filename().string().find(".tmp") != std::string::npos) {
            return true;
        }
    }
    return false;
}

void SetString(char* destination, size_t capacity, const char* value) {
    std::snprintf(destination, capacity, "%s", value);
}

} // namespace

int main() {
    std::printf("[TEST] Running test_spm_config_robust...\n");

    const fs::path root =
        fs::temp_directory_path() / ("spm_config_robust_" + std::to_string(CurrentPid()));
    std::error_code ec;
    fs::remove_all(root, ec);
    REQUIRE(fs::create_directories(root, ec));

    const fs::path configPath = root / "nested" / "launcher_config.json";

    SPMConfig defaults{};
    SPM_GetDefaultConfig(&defaults);
    CHECK(std::strcmp(defaults.language, "auto") == 0);
    CHECK(std::strcmp(defaults.neuralAssistMode, "auto") == 0);
    // The game directory must never be hardcoded to a personal path.
    CHECK(defaults.gameDir[0] == '\0');

    SPMConfig loaded{};
    CHECK(SPM_LoadConfig(configPath.string().c_str(), &loaded) == 0);
    CHECK(std::strcmp(loaded.language, "auto") == 0);
    CHECK(loaded.mouseSensitivity == 1.0);

    // 1. Known keys load and unknown keys survive a save.
    const std::string original =
        "{\n"
        "  \"Language\": \"fr\",\n"
        "  \"FpsLimit\": \"120\",\n"
        "  \"NeuralAssistMode\": \"disabled\",\n"
        "  \"MouseSensitivity\": 2.5,\n"
        "  \"UnknownFutureKey\": 42,\n"
        "  \"Another\": { \"nested\": [1, 2, 3] }\n"
        "}\n";
    fs::create_directories(configPath.parent_path(), ec);
    REQUIRE(!ec);
    {
        std::ofstream output(configPath, std::ios::binary | std::ios::trunc);
        output << original;
    }

    REQUIRE(SPM_LoadConfig(configPath.string().c_str(), &loaded) == 1);
    CHECK(std::strcmp(loaded.language, "fr") == 0);
    CHECK(std::strcmp(loaded.fpsLimit, "120") == 0);
    CHECK(std::strcmp(loaded.neuralAssistMode, "disabled") == 0);
    CHECK(loaded.mouseSensitivity == 2.5);

    REQUIRE(SPM_SaveConfig(configPath.string().c_str(), &loaded) == 1);
    const std::string saved = ReadWholeFile(configPath);
    CHECK(saved.find("UnknownFutureKey") != std::string::npos);
    CHECK(saved.find("\"Another\"") != std::string::npos);
    CHECK(saved.find("\"Language\": \"fr\"") != std::string::npos);
    CHECK(!DirectoryHasTemporaryFile(configPath.parent_path()));

    SPMConfig reloaded{};
    REQUIRE(SPM_LoadConfig(configPath.string().c_str(), &reloaded) == 1);
    CHECK(std::strcmp(reloaded.language, "fr") == 0);
    CHECK(reloaded.mouseSensitivity == 2.5);

    // 2. Invalid values are rejected individually and fall back to defaults.
    {
        std::ofstream output(configPath, std::ios::binary | std::ios::trunc);
        output << "{\n"
                  "  \"Language\": \"klingon\",\n"
                  "  \"FpsLimit\": \"99999\",\n"
                  "  \"Resolution\": \"nope\",\n"
                  "  \"NeuralAssistMode\": \"turbo\",\n"
                  "  \"VramBudgetMb\": \"123\",\n"
                  "  \"MouseSensitivity\": 999,\n"
                  "  \"VSync\": \"maybe\"\n"
                  "}\n";
    }
    SPMConfig invalid{};
    REQUIRE(SPM_LoadConfig(configPath.string().c_str(), &invalid) == 1);
    CHECK(std::strcmp(invalid.language, "auto") == 0);
    CHECK(std::strcmp(invalid.fpsLimit, "60") == 0);
    CHECK(std::strcmp(invalid.resolution, "1920x1080") == 0);
    CHECK(std::strcmp(invalid.neuralAssistMode, "auto") == 0);
    CHECK(std::strcmp(invalid.vramBudgetMb, "0") == 0);
    CHECK(std::strcmp(invalid.vsync, "1") == 0);
    CHECK(invalid.mouseSensitivity == 10.0); // clamped, not silently reset

    // 3. A corrupt file is backed up before it is replaced, never deleted.
    const std::string corrupt = "{ this is not json";
    {
        std::ofstream output(configPath, std::ios::binary | std::ios::trunc);
        output << corrupt;
    }
    SPMConfig corruptLoaded{};
    CHECK(SPM_LoadConfig(configPath.string().c_str(), &corruptLoaded) == 0);
    CHECK(std::strcmp(corruptLoaded.language, "auto") == 0);

    REQUIRE(SPM_SaveConfig(configPath.string().c_str(), &defaults) == 1);
    const fs::path backup =
        fs::path(configPath.string() + ".invalid." + std::to_string(CurrentPid()));
    CHECK(fs::exists(backup));
    CHECK(ReadWholeFile(backup) == corrupt);
    CHECK(ReadWholeFile(configPath).find("\"Language\"") != std::string::npos);
    CHECK(SPM_LoadConfig(configPath.string().c_str(), &corruptLoaded) == 1);
    CHECK(!DirectoryHasTemporaryFile(configPath.parent_path()));

    // 4. INI synchronization preserves unrelated lines and updates managed keys.
    const fs::path workspace = root / "workspace";
    const fs::path userDir = workspace / "analysis" / "uncharted-user";
    REQUIRE(fs::create_directories(userDir, ec));
    {
        std::ofstream output(userDir / "probe.ini", std::ios::binary | std::ios::trunc);
        output << "custom_key=keepme\nfps_limit=30\n";
    }

    SPMConfig iniConfig{};
    SPM_GetDefaultConfig(&iniConfig);
    SetString(iniConfig.fpsLimit, sizeof(iniConfig.fpsLimit), "144");
    SetString(iniConfig.textureQuality, sizeof(iniConfig.textureQuality), "high");

    REQUIRE(SPM_SyncConfigToIni(workspace.string().c_str(), &iniConfig) == 1);
    const std::string probeIni = ReadWholeFile(userDir / "probe.ini");
    CHECK(probeIni.find("custom_key=keepme") != std::string::npos);
    CHECK(probeIni.find("fps_limit=144") != std::string::npos);
    CHECK(probeIni.find("texture_quality=high") != std::string::npos);
    CHECK(fs::exists(userDir / "probe-u1.ini"));
    CHECK(fs::exists(userDir / "probe-u2.ini"));
    CHECK(fs::exists(userDir / "probe-u3.ini"));
    CHECK(!DirectoryHasTemporaryFile(userDir));

    fs::remove_all(root, ec);
    return spm_test::Finish("test_spm_config_robust");
}
