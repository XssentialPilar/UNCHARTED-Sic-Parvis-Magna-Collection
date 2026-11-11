// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - Master Library Unit & Initialization
#include "spm/spm_core.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    (void)hinstDLL;
    (void)lpvReserved;

    switch (fdwReason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hinstDLL);
            SPM_InitTimerResolution();
            break;
        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
#else
__attribute__((constructor))
static void SPMCore_Initialize(void) {
    SPM_InitTimerResolution();
}
#endif
