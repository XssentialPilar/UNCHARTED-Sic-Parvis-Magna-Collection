// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore Test Suite - Release metadata parsing, SHA-256 and offline behaviour
#include "spm/spm_core.h"
#include "spm_internal.h"
#include "test_check.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

constexpr const char* kSampleRelease =
    "{\n"
    "  \"tag_name\": \"v0.7.0\",\n"
    "  \"name\": \"Uncharted 0.7.0\",\n"
    "  \"draft\": false,\n"
    "  \"prerelease\": false,\n"
    "  \"assets\": [\n"
    "    {\n"
    "      \"name\": \"Uncharted-windows.zip\",\n"
    "      \"browser_download_url\": \"https://github.com/example/releases/download/v0.7.0/Uncharted-windows.zip\",\n"
    "      \"size\": 123456,\n"
    "      \"digest\": \"sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"\n"
    "    },\n"
    "    {\n"
    "      \"name\": \"Uncharted-SPMCore-0.7.0-linux-x64.tar.gz\",\n"
    "      \"browser_download_url\": \"https://github.com/example/releases/download/v0.7.0/Uncharted-SPMCore-0.7.0-linux-x64.tar.gz\",\n"
    "      \"size\": 654321,\n"
    "      \"digest\": \"sha256:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"\n"
    "    },\n"
    "    {\n"
    "      \"name\": \"Source code (zip)\",\n"
    "      \"browser_download_url\": \"https://github.com/example/archive/v0.7.0.zip\",\n"
    "      \"size\": 100,\n"
    "      \"digest\": null\n"
    "    }\n"
    "  ]\n"
    "}\n";

} // namespace

int main() {
    std::printf("[TEST] Running test_spm_updater...\n");

    // 1. SHA-256 validation with known vectors.
    const fs::path packagePath =
        fs::temp_directory_path() / "spm_test_update_pkg.bin";
    {
        std::ofstream output(packagePath, std::ios::binary | std::ios::trunc);
        output << "abc";
    }
    CHECK(SPM_ValidateUpdatePackage(
              packagePath.string().c_str(),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 3) == 1);
    CHECK(SPM_ValidateUpdatePackage(
              packagePath.string().c_str(),
              "0000000000000000000000000000000000000000000000000000000000000000", 3) == 0);
    CHECK(SPM_ValidateUpdatePackage(
              packagePath.string().c_str(),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 4) == 0);
    // Malformed digests are rejected before touching the file.
    CHECK(SPM_ValidateUpdatePackage(packagePath.string().c_str(), "abc", 3) == 0);
    CHECK(SPM_ValidateUpdatePackage(packagePath.string().c_str(), nullptr, 3) == 0);
    CHECK(SPM_ValidateUpdatePackage(nullptr,
                                    "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                                    3) == 0);

    // Uppercase digests are accepted (GitHub historically used lowercase).
    CHECK(SPM_ValidateUpdatePackage(
              packagePath.string().c_str(),
              "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD", 0) == 1);

    std::error_code ec;
    fs::remove(packagePath, ec);

    // 2. Semantic version comparison.
    int comparison = 0;
    REQUIRE(spm::updater::CompareSemVer("0.7.0", "0.6.1", &comparison));
    CHECK(comparison == 1);
    REQUIRE(spm::updater::CompareSemVer("v0.6.1", "0.6.1", &comparison));
    CHECK(comparison == 0);
    REQUIRE(spm::updater::CompareSemVer("0.6.1", "0.6.10", &comparison));
    CHECK(comparison == -1);
    REQUIRE(spm::updater::CompareSemVer("1.0.0-beta.2", "1.0.0", &comparison));
    CHECK(comparison == 0);
    CHECK(!spm::updater::CompareSemVer("banana", "0.6.1", &comparison));
    CHECK(!spm::updater::CompareSemVer("0.6", "0.6.1", &comparison));
    CHECK(spm::updater::NormalizeVersion("v1.2.3") == "1.2.3");
    CHECK(spm::updater::NormalizeVersion("V1.2.3") == "1.2.3");
    CHECK(spm::updater::NormalizeVersion("1.2.3") == "1.2.3");

    // 3. Release JSON parsing and asset selection.
    spm::updater::ReleaseInfo release;
    REQUIRE(spm::updater::ParseReleaseJson(kSampleRelease, release));
    CHECK(release.tagName == "v0.7.0");
    CHECK(release.title == "Uncharted 0.7.0");
    CHECK(release.assets.size() == 3);

    spm::updater::ReleaseAsset asset;
    REQUIRE(spm::updater::SelectAsset(release, "win10-x64", &asset));
    CHECK(asset.name == "Uncharted-windows.zip");
    CHECK(asset.hasDigest);
    CHECK(asset.sha256Hex ==
          "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    CHECK(asset.sizeBytes == 123456);
    CHECK(asset.downloadUrl.rfind("https://", 0) == 0);

    REQUIRE(spm::updater::SelectAsset(release, "win11-x64", &asset));
    CHECK(asset.name == "Uncharted-windows.zip");

    // There is no 32-bit package: the x86 platform must not receive the x64 zip.
    CHECK(!spm::updater::SelectAsset(release, "win10-x86", &asset));

    REQUIRE(spm::updater::SelectAsset(release, "linux-x64", &asset));
    CHECK(asset.name == "Uncharted-SPMCore-0.7.0-linux-x64.tar.gz");
    CHECK(asset.sizeBytes == 654321);
    CHECK(!spm::updater::SelectAsset(release, "playstation", &asset));

    spm::updater::ReleaseInfo broken;
    CHECK(!spm::updater::ParseReleaseJson("{ not json", broken));
    CHECK(!spm::updater::ParseReleaseJson("{\"name\":\"no tag\"}", broken));

    // 4. Offline mode fills the structure but performs no request and never
    //    claims an update it did not verify.
#ifdef _WIN32
    _putenv_s("SPM_UPDATER_OFFLINE", "1");
#else
    setenv("SPM_UPDATER_OFFLINE", "1", 1);
#endif

    SPMUpdateInfo update{};
    CHECK(SPM_CheckUpdate("0.6.1", "win10-x64", &update) == 0);
    CHECK(std::strcmp(update.currentVersion, "0.6.1") == 0);
    CHECK(std::strcmp(update.targetPlatformId, "win10-x64") == 0);
    CHECK(update.updateAvailable == 0);
    CHECK(update.downloadUrl[0] == '\0');
    CHECK(update.sha256Expected[0] == '\0');

    // Unknown platform ids are rejected instead of inheriting another channel.
    CHECK(SPM_CheckUpdate("0.6.1", "playstation", &update) == 0);
    CHECK(SPM_CheckUpdate(nullptr, nullptr, nullptr) == 0);

    return spm_test::Finish("test_spm_updater");
}
