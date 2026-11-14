// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Internal helpers shared by the modules (not part of the public ABI).
#ifndef SPM_INTERNAL_H
#define SPM_INTERNAL_H

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace spm {

struct CpuTopologyInfo {
    uint32_t logicalCores{0};
    uint32_t physicalCores{0};
    bool heterogeneous{false};
};

// Queries the real topology through the platform API. physicalCores is never
// guessed from logicalCores / 2: when the OS cannot report it, it falls back to
// the logical count (which is always a safe lower bound for worker budgeting).
CpuTopologyInfo QueryCpuTopology();

namespace updater {

struct ReleaseAsset {
    std::string name;
    std::string downloadUrl;
    std::string sha256Hex; // lowercase, empty when GitHub published no digest
    uint64_t sizeBytes{0};
    bool hasDigest{false};
};

struct ReleaseInfo {
    std::string tagName;
    std::string title;
    std::vector<ReleaseAsset> assets;
};

// Parses the JSON returned by GET /repos/{owner}/{repo}/releases/latest.
bool ParseReleaseJson(std::string_view body, ReleaseInfo& out);

// True for platform identifiers this build can actually install on.
bool ValidPlatformId(std::string_view platformId);

// Picks the asset that matches the platform/architecture. There is only one
// universal Windows x64 package and one Linux x64 package, so matching is by
// package name and never by substring of the platform id alone.
bool SelectAsset(const ReleaseInfo& release, std::string_view platformId,
                 ReleaseAsset* outAsset);

// Strict "major.minor.patch" comparison (a leading v is accepted). Returns
// false when either side is not a semantic version.
bool CompareSemVer(std::string_view left, std::string_view right, int* outComparison);

// Strips a leading 'v'/'V' from a release tag.
std::string NormalizeVersion(std::string_view tag);

bool IsSha256Hex(std::string_view text);

} // namespace updater
} // namespace spm

#endif // SPM_INTERNAL_H
