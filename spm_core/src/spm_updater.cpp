// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Platform-Locked Automatic Updater Module
//
// The release metadata is parsed as real JSON (no substring 'parsing'), the
// download URL must be HTTPS, the platform/architecture is validated before a
// package is offered and an update is only announced when GitHub published a
// SHA-256 digest for the asset: an update the launcher cannot verify is not
// offered at all.
#include "spm/spm_core.h"
#include "spm_internal.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#else
#ifdef SPM_HAVE_CURL
#include <curl/curl.h>
#endif
#endif

// ---------------------------------------------------------------------------
// Self-contained SHA-256 (no external dependency on any platform).
// ---------------------------------------------------------------------------
namespace {

struct Sha256Context {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
};

constexpr uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2};

uint32_t RotateRight(uint32_t value, uint32_t bits) {
    return (value >> bits) | (value << (32u - bits));
}

void Sha256Transform(Sha256Context& ctx, const uint8_t data[]) {
    uint32_t words[64];
    for (uint32_t index = 0; index < 16; ++index) {
        const uint32_t offset = index * 4;
        words[index] = (static_cast<uint32_t>(data[offset]) << 24) |
                       (static_cast<uint32_t>(data[offset + 1]) << 16) |
                       (static_cast<uint32_t>(data[offset + 2]) << 8) |
                       static_cast<uint32_t>(data[offset + 3]);
    }
    for (uint32_t index = 16; index < 64; ++index) {
        const uint32_t s0 = RotateRight(words[index - 15], 7) ^
                            RotateRight(words[index - 15], 18) ^ (words[index - 15] >> 3);
        const uint32_t s1 = RotateRight(words[index - 2], 17) ^
                            RotateRight(words[index - 2], 19) ^ (words[index - 2] >> 10);
        words[index] = words[index - 16] + s0 + words[index - 7] + s1;
    }

    uint32_t a = ctx.state[0];
    uint32_t b = ctx.state[1];
    uint32_t c = ctx.state[2];
    uint32_t d = ctx.state[3];
    uint32_t e = ctx.state[4];
    uint32_t f = ctx.state[5];
    uint32_t g = ctx.state[6];
    uint32_t h = ctx.state[7];

    for (uint32_t index = 0; index < 64; ++index) {
        const uint32_t s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
        const uint32_t choose = (e & f) ^ (~e & g);
        const uint32_t temp1 = h + s1 + choose + kSha256K[index] + words[index];
        const uint32_t s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = s0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx.state[0] += a;
    ctx.state[1] += b;
    ctx.state[2] += c;
    ctx.state[3] += d;
    ctx.state[4] += e;
    ctx.state[5] += f;
    ctx.state[6] += g;
    ctx.state[7] += h;
}

void Sha256Init(Sha256Context& ctx) {
    ctx.datalen = 0;
    ctx.bitlen = 0;
    ctx.state[0] = 0x6a09e667;
    ctx.state[1] = 0xbb67ae85;
    ctx.state[2] = 0x3c6ef372;
    ctx.state[3] = 0xa54ff53a;
    ctx.state[4] = 0x510e527f;
    ctx.state[5] = 0x9b05688c;
    ctx.state[6] = 0x1f83d9ab;
    ctx.state[7] = 0x5be0cd19;
}

void Sha256Update(Sha256Context& ctx, const uint8_t data[], size_t length) {
    for (size_t index = 0; index < length; ++index) {
        ctx.data[ctx.datalen] = data[index];
        ctx.datalen++;
        if (ctx.datalen == 64) {
            Sha256Transform(ctx, ctx.data);
            ctx.bitlen += 512;
            ctx.datalen = 0;
        }
    }
}

void Sha256Final(Sha256Context& ctx, uint8_t hash[32]) {
    uint32_t index = ctx.datalen;
    if (ctx.datalen < 56) {
        ctx.data[index++] = 0x80;
        while (index < 56) {
            ctx.data[index++] = 0x00;
        }
    } else {
        ctx.data[index++] = 0x80;
        while (index < 64) {
            ctx.data[index++] = 0x00;
        }
        Sha256Transform(ctx, ctx.data);
        std::memset(ctx.data, 0, 56);
    }

    ctx.bitlen += static_cast<uint64_t>(ctx.datalen) * 8u;
    ctx.data[63] = static_cast<uint8_t>(ctx.bitlen);
    ctx.data[62] = static_cast<uint8_t>(ctx.bitlen >> 8);
    ctx.data[61] = static_cast<uint8_t>(ctx.bitlen >> 16);
    ctx.data[60] = static_cast<uint8_t>(ctx.bitlen >> 24);
    ctx.data[59] = static_cast<uint8_t>(ctx.bitlen >> 32);
    ctx.data[58] = static_cast<uint8_t>(ctx.bitlen >> 40);
    ctx.data[57] = static_cast<uint8_t>(ctx.bitlen >> 48);
    ctx.data[56] = static_cast<uint8_t>(ctx.bitlen >> 56);
    Sha256Transform(ctx, ctx.data);

    for (uint32_t word = 0; word < 8; ++word) {
        hash[word * 4] = static_cast<uint8_t>(ctx.state[word] >> 24);
        hash[word * 4 + 1] = static_cast<uint8_t>(ctx.state[word] >> 16);
        hash[word * 4 + 2] = static_cast<uint8_t>(ctx.state[word] >> 8);
        hash[word * 4 + 3] = static_cast<uint8_t>(ctx.state[word]);
    }
}

std::string ToLowerAscii(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char c : text) {
        const unsigned char byte = static_cast<unsigned char>(c);
        result.push_back(static_cast<char>(std::tolower(byte)));
    }
    return result;
}

void CopyString(char* destination, size_t capacity, const char* source) {
    if (destination == nullptr || capacity == 0) {
        return;
    }
    if (source == nullptr) {
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

// ---------------------------------------------------------------------------
// HTTPS GET
// ---------------------------------------------------------------------------
#ifdef _WIN32

std::wstring WidenAscii(std::string_view text) {
    std::wstring result;
    result.reserve(text.size());
    for (const char c : text) {
        result.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    }
    return result;
}

struct WinHttpCloser {
    void operator()(HINTERNET handle) const {
        if (handle != nullptr) {
            WinHttpCloseHandle(handle);
        }
    }
};

using WinHttpHandle = std::unique_ptr<void, WinHttpCloser>;

bool HttpGet(std::string_view host, std::string_view path, std::string& outBody) {
    constexpr size_t kMaxResponseBytes = 4u * 1024u * 1024u;

    WinHttpHandle session(WinHttpOpen(L"SPM-Core-Updater/1.0",
                                      WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        return false;
    }
    WinHttpSetTimeouts(session.get(), 5000, 5000, 10000, 10000);

    const std::wstring wideHost = WidenAscii(host);
    WinHttpHandle connection(
        WinHttpConnect(session.get(), wideHost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection) {
        return false;
    }

    const std::wstring widePath = WidenAscii(path);
    WinHttpHandle request(WinHttpOpenRequest(connection.get(), L"GET", widePath.c_str(), nullptr,
                                             WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             WINHTTP_FLAG_SECURE));
    if (!request) {
        return false;
    }

    const wchar_t* headers = L"Accept: application/vnd.github+json\r\n"
                             L"User-Agent: SPM-Core-Updater/1.0\r\n"
                             L"X-GitHub-Api-Version: 2022-11-28\r\n";
    if (!WinHttpSendRequest(request.get(), headers, static_cast<DWORD>(-1),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) {
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(request.get(),
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                             WINHTTP_NO_HEADER_INDEX) ||
        statusCode != 200) {
        return false;
    }

    outBody.clear();
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) {
            return false;
        }
        if (available == 0) {
            break;
        }
        const size_t remaining = kMaxResponseBytes - outBody.size();
        if (remaining == 0) {
            return false; // response too large for a release document
        }
        const DWORD toRead = static_cast<DWORD>(std::min<size_t>(available, remaining));
        std::vector<char> buffer(toRead);
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), buffer.data(), toRead, &read)) {
            return false;
        }
        if (read == 0) {
            break;
        }
        outBody.append(buffer.data(), read);
    }
    return true;
}

#else

#ifdef SPM_HAVE_CURL

std::once_flag g_curlInitFlag;

size_t CurlWriteCallback(char* data, size_t size, size_t count, void* userData) {
    constexpr size_t kMaxResponseBytes = 4u * 1024u * 1024u;
    auto* body = static_cast<std::string*>(userData);
    const size_t bytes = size * count;
    if (body->size() + bytes > kMaxResponseBytes) {
        return 0; // abort the transfer
    }
    body->append(data, bytes);
    return bytes;
}

bool HttpGet(std::string_view host, std::string_view path, std::string& outBody) {
    std::call_once(g_curlInitFlag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        return false;
    }

    const std::string url = "https://" + std::string(host) + std::string(path);
    struct curl_slist* headers = curl_slist_append(nullptr, "Accept: application/vnd.github+json");
    if (headers != nullptr) {
        struct curl_slist* extended =
            curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");
        if (extended != nullptr) {
            headers = extended;
        }
    }

    outBody.clear();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SPM-Core-Updater/1.0");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outBody);

    const CURLcode result = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);

    if (headers != nullptr) {
        curl_slist_free_all(headers);
    }
    curl_easy_cleanup(curl);

    return result == CURLE_OK && status == 200;
}

#else

bool HttpGet(std::string_view, std::string_view, std::string&) {
    // Built without libcurl: report "unavailable" instead of spawning a shell
    // command (which also bypassed TLS verification and timeouts).
    return false;
}

#endif // SPM_HAVE_CURL
#endif // _WIN32

bool LookupEnvFlag(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return false;
    }
    return std::strcmp(value, "0") != 0;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
int SPM_ValidateUpdatePackage(const char* packagePath, const char* expectedSha256,
                              uint64_t expectedSize) {
    if (packagePath == nullptr || expectedSha256 == nullptr) {
        return 0;
    }
    if (!spm::updater::IsSha256Hex(expectedSha256)) {
        return 0;
    }

    std::ifstream file(packagePath, std::ios::binary);
    if (!file.is_open()) {
        return 0;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff endPosition = file.tellg();
    file.seekg(0, std::ios::beg);
    if (endPosition < 0 || !file.good()) {
        return 0;
    }
    const uint64_t actualSize = static_cast<uint64_t>(endPosition);
    if (expectedSize > 0 && actualSize != expectedSize) {
        return 0;
    }

    Sha256Context context;
    Sha256Init(context);

    std::vector<uint8_t> buffer;
    try {
        buffer.resize(64u * 1024u);
    } catch (...) {
        return 0;
    }

    while (file.good()) {
        file.read(reinterpret_cast<char*>(buffer.data()),
                  static_cast<std::streamsize>(buffer.size()));
        const std::streamsize bytesRead = file.gcount();
        if (bytesRead > 0) {
            Sha256Update(context, buffer.data(), static_cast<size_t>(bytesRead));
        }
    }
    if (file.bad()) {
        return 0;
    }

    uint8_t hash[32];
    Sha256Final(context, hash);

    char actualHex[65];
    for (size_t index = 0; index < 32; ++index) {
        std::snprintf(actualHex + index * 2, 3, "%02x",
                      static_cast<unsigned>(hash[index]));
    }
    actualHex[64] = '\0';

    return ToLowerAscii(actualHex) == ToLowerAscii(expectedSha256) ? 1 : 0;
}

int SPM_CheckUpdate(const char* currentVersion, const char* platformId,
                    SPMUpdateInfo* outUpdate) {
    if (outUpdate == nullptr) {
        return 0;
    }
    std::memset(outUpdate, 0, sizeof(*outUpdate));

    const char* versionSource =
        (currentVersion != nullptr && *currentVersion != '\0') ? currentVersion
                                                               : SPM_GetCoreVersion();
    const char* platformSource =
        (platformId != nullptr && *platformId != '\0') ? platformId
                                                       : SPM_GetPlatformIdString();

    const std::string version = spm::updater::NormalizeVersion(versionSource);
    CopyString(outUpdate->currentVersion, sizeof(outUpdate->currentVersion), version.c_str());
    CopyString(outUpdate->targetPlatformId, sizeof(outUpdate->targetPlatformId),
               platformSource);

    if (!spm::updater::ValidPlatformId(platformSource)) {
        return 0;
    }
    int comparison = 0;
    if (!spm::updater::CompareSemVer(version, version, &comparison)) {
        return 0; // caller passed something that is not a version
    }

    if (LookupEnvFlag("SPM_UPDATER_OFFLINE")) {
        return 0;
    }

    constexpr const char* kReleaseHost = "api.github.com";
    constexpr const char* kReleasePath =
        "/repos/XssentialPilar/UNCHARTED-Sic-Parvis-Magna-Collection/releases/latest";

    std::string body;
    if (!HttpGet(kReleaseHost, kReleasePath, body)) {
        return 0;
    }

    spm::updater::ReleaseInfo release;
    if (!spm::updater::ParseReleaseJson(body, release)) {
        return 0;
    }

    const std::string latest = spm::updater::NormalizeVersion(release.tagName);
    CopyString(outUpdate->latestVersion, sizeof(outUpdate->latestVersion), latest.c_str());
    CopyString(outUpdate->releaseTitle, sizeof(outUpdate->releaseTitle), release.title.c_str());

    int versionComparison = 0;
    if (!spm::updater::CompareSemVer(latest, version, &versionComparison) ||
        versionComparison <= 0) {
        return 1;
    }

    spm::updater::ReleaseAsset asset;
    if (!spm::updater::SelectAsset(release, platformSource, &asset)) {
        return 1; // newer release exists but not for this platform
    }
    if (asset.downloadUrl.rfind("https://", 0) != 0) {
        return 1;
    }
    if (!asset.hasDigest) {
        // No published SHA-256 means the launcher cannot verify the package;
        // offering it would bypass the integrity requirement.
        return 1;
    }

    CopyString(outUpdate->downloadUrl, sizeof(outUpdate->downloadUrl),
               asset.downloadUrl.c_str());
    CopyString(outUpdate->sha256Expected, sizeof(outUpdate->sha256Expected),
               asset.sha256Hex.c_str());
    outUpdate->expectedSizeBytes = asset.sizeBytes;
    outUpdate->updateAvailable = 1;
    return 1;
}
