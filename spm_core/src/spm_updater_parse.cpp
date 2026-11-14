// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Release document parsing, version and asset selection helpers.
//
// Kept in its own translation unit so the unit tests can compile it directly
// instead of exporting C++ STL types across the SPMCore DLL boundary.
#include "spm_internal.h"
#include "spm_json.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace {

bool IsHexDigit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

std::string ToLowerAscii(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char c : text) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return result;
}

bool EndsWith(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() &&
           text.substr(text.size() - suffix.size()) == suffix;
}

bool ExtractSha256(std::string_view digest, std::string& outHex) {
    constexpr std::string_view prefix = "sha256:";
    std::string_view payload = digest;
    if (payload.size() > prefix.size() && payload.substr(0, prefix.size()) == prefix) {
        payload.remove_prefix(prefix.size());
    }
    if (payload.size() != 64) {
        return false;
    }
    for (const char c : payload) {
        if (!IsHexDigit(c)) {
            return false;
        }
    }
    outHex = ToLowerAscii(payload);
    return true;
}

} // namespace

namespace spm::updater {

bool IsSha256Hex(std::string_view text) {
    if (text.size() != 64) {
        return false;
    }
    for (const char c : text) {
        if (!IsHexDigit(c)) {
            return false;
        }
    }
    return true;
}

std::string NormalizeVersion(std::string_view tag) {
    if (!tag.empty() && (tag.front() == 'v' || tag.front() == 'V')) {
        tag.remove_prefix(1);
    }
    return std::string(tag);
}

bool CompareSemVer(std::string_view left, std::string_view right, int* outComparison) {
    if (outComparison == nullptr) {
        return false;
    }

    auto parse = [](std::string_view text, int parts[3]) {
        if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) {
            text.remove_prefix(1);
        }
        size_t position = 0;
        for (int index = 0; index < 3; ++index) {
            if (position >= text.size() ||
                !std::isdigit(static_cast<unsigned char>(text[position]))) {
                return false;
            }
            int value = 0;
            int digits = 0;
            while (position < text.size() &&
                   std::isdigit(static_cast<unsigned char>(text[position]))) {
                value = value * 10 + (text[position] - '0');
                if (value > 100000) {
                    return false;
                }
                ++position;
                ++digits;
            }
            if (digits > 6) {
                return false;
            }
            parts[index] = value;
            if (index < 2) {
                if (position >= text.size() || text[position] != '.') {
                    return false;
                }
                ++position;
            }
        }
        // Allow "-beta.1", "+build" and an empty suffix after the patch.
        return position == text.size() || text[position] == '-' || text[position] == '+';
    };

    int leftParts[3]{};
    int rightParts[3]{};
    if (!parse(left, leftParts) || !parse(right, rightParts)) {
        return false;
    }

    for (int index = 0; index < 3; ++index) {
        if (leftParts[index] != rightParts[index]) {
            *outComparison = leftParts[index] < rightParts[index] ? -1 : 1;
            return true;
        }
    }
    *outComparison = 0;
    return true;
}

bool ParseReleaseJson(std::string_view body, ReleaseInfo& out) {
    out = ReleaseInfo{};

    json::Value root;
    if (!json::Parse(body, root) || !root.IsObject()) {
        return false;
    }

    const json::Value* tag = root.Find("tag_name");
    if (tag == nullptr || !tag->IsString() || tag->string.empty()) {
        return false;
    }
    out.tagName = tag->string;

    if (const json::Value* title = root.Find("name");
        title != nullptr && title->IsString()) {
        out.title = title->string;
    }

    const json::Value* assets = root.Find("assets");
    if (assets != nullptr && assets->IsArray()) {
        for (const json::Value& entry : assets->array) {
            if (!entry.IsObject()) {
                continue;
            }
            const json::Value* name = entry.Find("name");
            const json::Value* url = entry.Find("browser_download_url");
            if (name == nullptr || !name->IsString() || url == nullptr || !url->IsString()) {
                continue;
            }
            ReleaseAsset asset;
            asset.name = name->string;
            asset.downloadUrl = url->string;

            if (const json::Value* size = entry.Find("size");
                size != nullptr && size->IsNumber() && size->number > 0) {
                asset.sizeBytes = static_cast<uint64_t>(size->number);
            }
            if (const json::Value* digest = entry.Find("digest");
                digest != nullptr && digest->IsString()) {
                asset.hasDigest = ExtractSha256(digest->string, asset.sha256Hex);
            }
            out.assets.push_back(std::move(asset));
        }
    }
    return true;
}

bool ValidPlatformId(std::string_view platformId) {
    return platformId == "win7-x86" || platformId == "win7-x64" ||
           platformId == "win8-x86" || platformId == "win8-x64" ||
           platformId == "win81-x86" || platformId == "win81-x64" ||
           platformId == "win10-x86" || platformId == "win10-x64" ||
           platformId == "win11-x64" || platformId == "linux-x64";
}

bool SelectAsset(const ReleaseInfo& release, std::string_view platformId,
                 ReleaseAsset* outAsset) {
    if (outAsset == nullptr || !ValidPlatformId(platformId)) {
        return false;
    }

    const bool windows = platformId.rfind("win", 0) == 0;
    if (windows && EndsWith(platformId, "-x86")) {
        // There is no 32-bit package: refusing is better than offering a
        // binary the machine cannot run.
        return false;
    }

    for (const ReleaseAsset& asset : release.assets) {
        const std::string lowerName = ToLowerAscii(asset.name);
        const bool matches =
            windows ? (lowerName.find("windows") != std::string::npos &&
                       EndsWith(lowerName, ".zip"))
                    : (lowerName.find("linux") != std::string::npos &&
                       lowerName.find("x64") != std::string::npos);
        if (matches) {
            *outAsset = asset;
            return true;
        }
    }
    return false;
}

} // namespace spm::updater
