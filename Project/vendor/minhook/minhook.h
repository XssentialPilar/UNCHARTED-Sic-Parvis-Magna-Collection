// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection — VTable & Function Hook Bridge (MinHook-compatible API)
#pragma once

#include <cstddef>
#include <cstdint>

namespace unc::vendor::minhook {

enum class HookStatus : int32_t {
    Ok = 0,
    AlreadyInitialized = 1,
    NotInitialized = 2,
    AlreadyCreated = 3,
    NotCreated = 4,
    Enabled = 5,
    Disabled = 6,
    NotExecutable = 7,
    UnsupportedFunction = 8,
    MemoryAllocFailed = 9,
    MemoryProtectFailed = 10,
    InvalidArgument = 11,
};

struct VTableHookRecord {
    void** vtable_slot{nullptr};
    void* detour_target{nullptr};
    void* original_target{nullptr};
    bool enabled{false};
};

HookStatus Initialize() noexcept;
HookStatus Uninitialize() noexcept;
HookStatus CreateVTableHook(void** vtable_base, size_t slot_index, void* detour,
                            void** out_original, VTableHookRecord& out_record) noexcept;
HookStatus EnableVTableHook(VTableHookRecord& record) noexcept;
HookStatus DisableVTableHook(VTableHookRecord& record) noexcept;

} // namespace unc::vendor::minhook
