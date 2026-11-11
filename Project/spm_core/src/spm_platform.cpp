// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Platform Identification & Detection Module
#include "spm/spm_core.h"
#include <cstring>
#include <cstdio>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef LONG (WINAPI *RtlGetVersionFn)(PRTL_OSVERSIONINFOW);

static void QueryWindowsVersion(DWORD* major, DWORD* minor, DWORD* build, BOOL* is64Bit) {
    *major = 0;
    *minor = 0;
    *build = 0;
    *is64Bit = FALSE;

    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    if (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
        *is64Bit = TRUE;
    }

    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        RtlGetVersionFn fnRtlGetVersion = (RtlGetVersionFn)GetProcAddress(hNtdll, "RtlGetVersion");
        if (fnRtlGetVersion) {
            RTL_OSVERSIONINFOW rovi{};
            rovi.dwOSVersionInfoSize = sizeof(rovi);
            if (fnRtlGetVersion(&rovi) == 0) {
                *major = rovi.dwMajorVersion;
                *minor = rovi.dwMinorVersion;
                *build = rovi.dwBuildNumber;
                return;
            }
        }
    }

    // Fallback: peek KUSER_SHARED_DATA (safe in all user-mode processes on Windows NT)
    // 0x7FFE0260 = NtMajorVersion, 0x7FFE0264 = NtMinorVersion
    #if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
    const volatile ULONG* pKUserMajor = (const volatile ULONG*)0x7FFE0260;
    const volatile ULONG* pKUserMinor = (const volatile ULONG*)0x7FFE0264;
    *major = *pKUserMajor;
    *minor = *pKUserMinor;
    #endif
}

SPMPlatformId SPM_GetPlatformId(void) {
    DWORD major = 0, minor = 0, build = 0;
    BOOL is64Bit = FALSE;
    QueryWindowsVersion(&major, &minor, &build, &is64Bit);

    if (major == 6 && minor == 1) {
        return is64Bit ? SPM_PLATFORM_WIN7_X64 : SPM_PLATFORM_WIN7_X86;
    }
    if (major == 6 && minor == 2) {
        return is64Bit ? SPM_PLATFORM_WIN8_X64 : SPM_PLATFORM_WIN8_X86;
    }
    if (major == 6 && minor == 3) {
        return is64Bit ? SPM_PLATFORM_WIN81_X64 : SPM_PLATFORM_WIN81_X86;
    }
    if (major == 10 && minor == 0) {
        if (build >= 22000 && is64Bit) {
            return SPM_PLATFORM_WIN11_X64;
        }
        return is64Bit ? SPM_PLATFORM_WIN10_X64 : SPM_PLATFORM_WIN10_X86;
    }

    // Default to Win10 x64 if on NT 10+
    if (major >= 10) {
        return is64Bit ? SPM_PLATFORM_WIN10_X64 : SPM_PLATFORM_WIN10_X86;
    }

    return is64Bit ? SPM_PLATFORM_WIN7_X64 : SPM_PLATFORM_WIN7_X86;
}

#else

#include <sys/utsname.h>

SPMPlatformId SPM_GetPlatformId(void) {
    struct utsname u{};
    if (uname(&u) == 0) {
        if (strcmp(u.machine, "x86_64") == 0) {
            return SPM_PLATFORM_LINUX_X64;
        }
    }
    return SPM_PLATFORM_LINUX_X64;
}

#endif

const char* SPM_GetPlatformIdString(void) {
    switch (SPM_GetPlatformId()) {
        case SPM_PLATFORM_WIN7_X86:  return "win7-x86";
        case SPM_PLATFORM_WIN7_X64:  return "win7-x64";
        case SPM_PLATFORM_WIN8_X86:  return "win8-x86";
        case SPM_PLATFORM_WIN8_X64:  return "win8-x64";
        case SPM_PLATFORM_WIN81_X86: return "win81-x86";
        case SPM_PLATFORM_WIN81_X64: return "win81-x64";
        case SPM_PLATFORM_WIN10_X86: return "win10-x86";
        case SPM_PLATFORM_WIN10_X64: return "win10-x64";
        case SPM_PLATFORM_WIN11_X64: return "win11-x64";
        case SPM_PLATFORM_LINUX_X64: return "linux-x64";
        default:                     return "win10-x64";
    }
}

const char* SPM_GetCoreVersion(void) {
    return "0.6.1";
}
