/* Windows 7-compatible libc++ std::chrono clock definitions for static libc++ builds.
 *
 * MSYS2 CLANG64's libc++.a(chrono.cpp.obj) imports GetSystemTimePreciseAsFileTime
 * statically, which does not exist on Windows 7 SP1 (kernel32/kernelbase added it
 * in Windows 8). Linking this translation unit before -lc++ satisfies the six
 * chrono symbols statically while resolving GetSystemTimePreciseAsFileTime via
 * GetProcAddress and falling back to GetSystemTimeAsFileTime on Windows 7. */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <chrono>
#include <cstring>

static long long query_qpc_ns() noexcept {
    static LARGE_INTEGER freq = []() noexcept {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER cnt{};
    QueryPerformanceCounter(&cnt);
    if (freq.QuadPart <= 0) {
        return 0;
    }
    const long long sec = cnt.QuadPart / freq.QuadPart;
    const long long rem = cnt.QuadPart % freq.QuadPart;
    return sec * 1000000000LL + (rem * 1000000000LL) / freq.QuadPart;
}

namespace std {
inline namespace _LIBCPP_ABI_NAMESPACE {
namespace chrono {

const bool steady_clock::is_steady;
steady_clock::time_point steady_clock::now() noexcept {
    return time_point(duration(query_qpc_ns()));
}

const bool system_clock::is_steady;
system_clock::time_point system_clock::now() noexcept {
    using PreciseFn = void(WINAPI*)(LPFILETIME);
    static const PreciseFn precise_fn = []() noexcept -> PreciseFn {
        if (HMODULE kb = GetModuleHandleW(L"kernelbase.dll")) {
            if (FARPROC p = GetProcAddress(kb, "GetSystemTimePreciseAsFileTime")) {
                PreciseFn f = nullptr;
                std::memcpy(&f, &p, sizeof(f));
                return f;
            }
        }
        if (HMODULE k32 = GetModuleHandleW(L"kernel32.dll")) {
            if (FARPROC p = GetProcAddress(k32, "GetSystemTimePreciseAsFileTime")) {
                PreciseFn f = nullptr;
                std::memcpy(&f, &p, sizeof(f));
                return f;
            }
        }
        return nullptr;
    }();
    FILETIME ft{};
    if (precise_fn) {
        precise_fn(&ft);
    } else {
        GetSystemTimeAsFileTime(&ft);
    }
    ULARGE_INTEGER ui{};
    ui.LowPart = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;
    constexpr unsigned long long kEpochDiff100Ns = 116444736000000000ULL;
    const long long usec = static_cast<long long>((ui.QuadPart - kEpochDiff100Ns) / 10ULL);
    return time_point(duration(usec));
}

time_t system_clock::to_time_t(const time_point& t) noexcept {
    return static_cast<time_t>(duration_cast<seconds>(t.time_since_epoch()).count());
}

system_clock::time_point system_clock::from_time_t(time_t t) noexcept {
    return time_point(duration_cast<duration>(seconds(t)));
}

} // namespace chrono
} // namespace _LIBCPP_ABI_NAMESPACE
} // namespace std
