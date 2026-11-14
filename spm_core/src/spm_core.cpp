// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Master Library Unit & Initialization
//
// DllMain only performs loader-safe work. Timer resolution and every other
// global resource are initialized and released through the explicit API, never
// from under the loader lock.
#include "spm/spm_core.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)lpvReserved;
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}
#endif
