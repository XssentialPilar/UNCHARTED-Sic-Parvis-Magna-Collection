// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Platform-Locked Automatic Updater Module
#include "spm/spm_core.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <iomanip>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

// --- Self-contained SHA-256 for zero external dependencies ---
namespace {

struct SHA256_CTX {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
};

#define ROTRIGHT(a,b) (((a) >> (b)) | ((a) << (32-(b))))
#define CH(x,y,z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x) (ROTRIGHT(x,2) ^ ROTRIGHT(x,13) ^ ROTRIGHT(x,22))
#define EP1(x) (ROTRIGHT(x,6) ^ ROTRIGHT(x,11) ^ ROTRIGHT(x,25))
#define SIG0(x) (ROTRIGHT(x,7) ^ ROTRIGHT(x,18) ^ ((x) >> 3))
#define SIG1(x) (ROTRIGHT(x,17) ^ ROTRIGHT(x,19) ^ ((x) >> 10))

static const uint32_t kK[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static void SHA256_Transform(SHA256_CTX *ctx, const uint8_t data[]) {
    uint32_t a, b, c, d, e, f, g, h, i, j, t1, t2, m[64];
    for (i = 0, j = 0; i < 16; ++i, j += 4)
        m[i] = (data[j] << 24) | (data[j + 1] << 16) | (data[j + 2] << 8) | (data[j + 3]);
    for ( ; i < 64; ++i)
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
    e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];

    for (i = 0; i < 64; ++i) {
        t1 = h + EP1(e) + CH(e,f,g) + kK[i] + m[i];
        t2 = EP0(a) + MAJ(a,b,c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
    ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void SHA256_Init(SHA256_CTX *ctx) {
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85; ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c; ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
}

static void SHA256_Update(SHA256_CTX *ctx, const uint8_t data[], size_t len) {
    for (size_t i = 0; i < len; ++i) {
        ctx->data[ctx->datalen] = data[i];
        ctx->datalen++;
        if (ctx->datalen == 64) {
            SHA256_Transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void SHA256_Final(SHA256_CTX *ctx, uint8_t hash[]) {
    uint32_t i = ctx->datalen;
    if (ctx->datalen < 56) {
        ctx->data[i++] = 0x80;
        while (i < 56) ctx->data[i++] = 0x00;
    } else {
        ctx->data[i++] = 0x80;
        while (i < 64) ctx->data[i++] = 0x00;
        SHA256_Transform(ctx, ctx->data);
        memset(ctx->data, 0, 56);
    }

    ctx->bitlen += ctx->datalen * 8;
    ctx->data[63] = ctx->bitlen;
    ctx->data[62] = ctx->bitlen >> 8;
    ctx->data[61] = ctx->bitlen >> 16;
    ctx->data[60] = ctx->bitlen >> 24;
    ctx->data[59] = ctx->bitlen >> 32;
    ctx->data[58] = ctx->bitlen >> 40;
    ctx->data[57] = ctx->bitlen >> 48;
    ctx->data[56] = ctx->bitlen >> 56;
    SHA256_Transform(ctx, ctx->data);

    for (i = 0; i < 4; ++i) {
        hash[i]      = (ctx->state[0] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 4]  = (ctx->state[1] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 8]  = (ctx->state[2] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 12] = (ctx->state[3] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 16] = (ctx->state[4] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 20] = (ctx->state[5] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 24] = (ctx->state[6] >> (24 - i * 8)) & 0x000000ff;
        hash[i + 28] = (ctx->state[7] >> (24 - i * 8)) & 0x000000ff;
    }
}

static int CompareSemVer(const char* v1, const char* v2) {
    if (!v1 || !v2) return 0;
    while (*v1 == 'v' || *v1 == 'V') v1++;
    while (*v2 == 'v' || *v2 == 'V') v2++;

    int major1 = 0, minor1 = 0, patch1 = 0;
    int major2 = 0, minor2 = 0, patch2 = 0;
    sscanf(v1, "%d.%d.%d", &major1, &minor1, &patch1);
    sscanf(v2, "%d.%d.%d", &major2, &minor2, &patch2);

    if (major1 != major2) return major1 - major2;
    if (minor1 != minor2) return minor1 - minor2;
    return patch1 - patch2;
}

} // namespace

int SPM_ValidateUpdatePackage(const char* packagePath, const char* expectedSha256, uint64_t expectedSize) {
    if (!packagePath || !expectedSha256) return 0;

    std::ifstream file(packagePath, std::ios::binary);
    if (!file.is_open()) return 0;

    if (expectedSize > 0) {
        file.seekg(0, std::ios::end);
        uint64_t actualSize = file.tellg();
        file.seekg(0, std::ios::beg);
        if (actualSize != expectedSize) return 0;
    }

    SHA256_CTX ctx;
    SHA256_Init(&ctx);

    std::vector<uint8_t> buffer(64 * 1024);
    while (file.good()) {
        file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
        std::streamsize bytesRead = file.gcount();
        if (bytesRead > 0) {
            SHA256_Update(&ctx, buffer.data(), static_cast<size_t>(bytesRead));
        }
    }

    uint8_t hash[32];
    SHA256_Final(&ctx, hash);

    char actualHex[65];
    for (int i = 0; i < 32; ++i) {
        snprintf(actualHex + (i * 2), 3, "%02x", hash[i]);
    }
    actualHex[64] = '\0';

#ifdef _WIN32
    return (_stricmp(actualHex, expectedSha256) == 0) ? 1 : 0;
#else
    return (strcasecmp(actualHex, expectedSha256) == 0) ? 1 : 0;
#endif
}

int SPM_CheckUpdate(const char* currentVersion, const char* platformId, SPMUpdateInfo* outUpdate) {
    if (!outUpdate) return 0;
    memset(outUpdate, 0, sizeof(*outUpdate));

    const char* curVer = currentVersion ? currentVersion : "0.5.2";
    const char* targetPlat = platformId ? platformId : SPM_GetPlatformIdString();

    strncpy(outUpdate->currentVersion, curVer, sizeof(outUpdate->currentVersion) - 1);
    strncpy(outUpdate->targetPlatformId, targetPlat, sizeof(outUpdate->targetPlatformId) - 1);
    outUpdate->updateAvailable = 0;

#ifdef _WIN32
    HINTERNET hSession = WinHttpOpen(L"SPM-Core-Updater/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return 0;

    HINTERNET hConnect = WinHttpConnect(hSession, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return 0;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET",
                                           L"/repos/XssentialPilar/UNCHARTED-Sic-Parvis-Magna-Collection/releases/latest",
                                           NULL, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 0;
    }

    std::string responseBody;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, NULL)) {

        DWORD dwSize = 0;
        DWORD dwDownloaded = 0;
        do {
            dwSize = 0;
            if (WinHttpQueryDataAvailable(hRequest, &dwSize) && dwSize > 0) {
                std::vector<char> tempBuf(dwSize + 1);
                if (WinHttpReadData(hRequest, tempBuf.data(), dwSize, &dwDownloaded)) {
                    tempBuf[dwDownloaded] = '\0';
                    responseBody.append(tempBuf.data(), dwDownloaded);
                }
            }
        } while (dwSize > 0);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
#else
    std::string cmd = "curl -s -H \"Accept: application/vnd.github+json\" -H \"User-Agent: SPM-Core-Updater/1.0\" "
                      "\"https://api.github.com/repos/XssentialPilar/UNCHARTED-Sic-Parvis-Magna-Collection/releases/latest\"";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return 0;

    char buffer[4096];
    std::string responseBody;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        responseBody += buffer;
    }
    pclose(pipe);
#endif

    if (responseBody.empty()) return 0;

    size_t tagPos = responseBody.find("\"tag_name\":");
    if (tagPos == std::string::npos) return 0;

    size_t quoteStart = responseBody.find('"', tagPos + 11);
    if (quoteStart == std::string::npos) return 0;
    size_t quoteEnd = responseBody.find('"', quoteStart + 1);
    if (quoteEnd == std::string::npos) return 0;

    std::string latestTag = responseBody.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
    strncpy(outUpdate->latestVersion, latestTag.c_str(), sizeof(outUpdate->latestVersion) - 1);

    if (CompareSemVer(latestTag.c_str(), curVer) > 0) {
        std::string platPattern = std::string("-") + targetPlat;
        size_t assetPos = responseBody.find(platPattern);
        if (assetPos != std::string::npos) {
            size_t urlPos = responseBody.find("\"browser_download_url\":", assetPos);
            if (urlPos != std::string::npos) {
                size_t uStart = responseBody.find('"', urlPos + 23);
                size_t uEnd = responseBody.find('"', uStart + 1);
                if (uStart != std::string::npos && uEnd != std::string::npos) {
                    std::string downloadUrl = responseBody.substr(uStart + 1, uEnd - uStart - 1);
                    strncpy(outUpdate->downloadUrl, downloadUrl.c_str(), sizeof(outUpdate->downloadUrl) - 1);
                    outUpdate->updateAvailable = 1;
                }
            }
        }
    }

    return 1;
}
