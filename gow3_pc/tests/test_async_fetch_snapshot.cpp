// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <future>
#include "shader_recompiler/frontend/fetch_shader.h"
#include "shader_recompiler/resource.h"
#include "common/logging/log.h"

extern "C" { uint32_t runtime_disabled_optimizations = 0; }
void assert_fail_impl() { std::abort(); }
[[noreturn]] void unreachable_impl() { std::abort(); }
namespace Common::Log {
bool ShouldLog(Level) { return false; }
void Write(std::string_view, Level, const char*, int, const char*, std::string&&) {}
}

static void TestGarbageEffectBufferMappedZero() {
    struct Scope {
        u64 (*prev)(u64, u64) = Shader::gpu_mapped_extent;
        ~Scope() { Shader::gpu_mapped_extent = prev; }
    } scope;

    // 1. Mapped size == 0 on a non-null V# must be treated as garbage when inspected
    //    (effect shader or odd-looking V# >64 MiB / >=8 KiB stride / unaligned base).
    Shader::gpu_mapped_extent = [](u64, u64) -> u64 { return 0; };
    AmdGpu::Buffer unmapped{};
    unmapped.base_address = 0x100000ULL;
    unmapped.stride = 16;
    unmapped.num_records = 64;
    assert(Shader::IsGarbageEffectBuffer(0x8f067540ULL, unmapped));

    AmdGpu::Buffer odd_unmapped{};
    odd_unmapped.base_address = 0x100000ULL;
    odd_unmapped.stride = 8192;
    odd_unmapped.num_records = 64;
    assert(Shader::IsGarbageEffectBuffer(0x1234ULL, odd_unmapped));

    // 2. Open-ended V# (num_records >= 0xFFFF0000) with mapped_size == 0 must also be treated as garbage,
    //    whereas an open-ended V# backed by a valid mapped extent stays valid.
    AmdGpu::Buffer open_ended{};
    open_ended.base_address = 0x200000ULL;
    open_ended.stride = 16;
    open_ended.num_records = 0xFFFFFFFFu;
    assert(Shader::IsGarbageEffectBuffer(0x1234ULL, open_ended));

    Shader::gpu_mapped_extent = [](u64, u64) -> u64 { return 4096ULL; };
    assert(!Shader::IsGarbageEffectBuffer(0x1234ULL, open_ended));

    // 3. Finite V# whose mapped extent is smaller than stride * num_records must be treated as garbage.
    AmdGpu::Buffer truncated{};
    truncated.base_address = 0x300000ULL;
    truncated.stride = 8192;
    truncated.num_records = 2; // 16384 bytes, but mock says 4096 mapped
    assert(Shader::IsGarbageEffectBuffer(0x1234ULL, truncated));
}

int main() {
    using namespace Shader::Gcn;
    // s_load_dwordx4 s[8:11], s[2:3], 0; tbuffer_load_format_xyzw; s_setpc_b64.
    std::array<u32, 4> code{0xC0000000u | (2u << 22) | (8u << 15) | (1u << 9) | (1u << 8),
                            0xE8000000u | (3u << 16), 2u << 16, 0xBE802000u};
    AmdGpu::Buffer buffer{};
    buffer.base_address = 0x12340;
    buffer.stride = 16;
    std::array<u32, 4> ud{};
    const auto* code_pointer = code.data();
    const auto* buffer_pointer = &buffer;
    std::memcpy(ud.data(), &code_pointer, sizeof(code_pointer));
    std::memcpy(ud.data() + 2, &buffer_pointer, sizeof(buffer_pointer));
    Shader::Info info;
    info.has_fetch_shader = true;
    info.fetch_shader_sgpr_base = 0;
    info.user_data = ud;
    FetchShaderSnapshot snapshot;
    snapshot.Capture(info);
    assert(snapshot.fetch && snapshot.fetch->attributes.size() == 1);
    assert(snapshot.buffers[0].base_address == 0x12340);

    // Both guest pointers are invalid after capture. A compiler worker must not dereference them.
    ud.fill(0);
    auto worker = std::async(std::launch::async, [&] {
        ScopedFetchShaderSnapshot scope{snapshot};
        const auto fetch = ParseFetchShader(info);
        assert(fetch && fetch->attributes.size() == 1);
        const auto sharp = fetch->attributes[0].GetSharp(info);
        assert(sharp.base_address == 0x12340 && sharp.GetStride() == 16);
        FetchShaderSnapshot nested;
        nested.info = &info;
        {
            ScopedFetchShaderSnapshot inner{nested};
            assert(!ParseFetchShader(info));
        }
        assert(ParseFetchShader(info));
    });
    worker.get();
    info.has_fetch_shader = false;
    assert(!ParseFetchShader(info));
    TestGarbageEffectBufferMappedZero();
}
