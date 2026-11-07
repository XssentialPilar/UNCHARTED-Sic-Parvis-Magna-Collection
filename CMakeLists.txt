cmake_minimum_required(VERSION 3.24)
project(UnchartedSicParvisMagna VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(UNC_SPM_BUILD_TESTS "Build Uncharted: Sic Parvis Magna Collection unit tests" ON)
option(UNC_SPM_BUILD_GOW3_RUNTIME "Build the full gow3_pc PS4-to-PC runtime & Vulkan shim" OFF)

add_library(unc_spm_native STATIC
    native/core/core.cpp
    native/core/arena_allocator.cpp
    native/core/frame_pacer.cpp
    native/graphics/dx9_backend.cpp
    native/graphics/dx11_backend.cpp
    native/graphics/dx12_backend.cpp
    native/graphics/dynamic_ring_buffer.cpp
    native/graphics/swapchain_hook.cpp
    native/graphics/transient_rt_pool.cpp
    native/graphics/vram_budget_mgr.cpp
    native/input/input_mapper.cpp
    native/input/gameinput_handler.cpp
    native/input/sdl_handler.cpp
)

target_include_directories(unc_spm_native
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/minhook
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/d3d12
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sdl3
)

target_compile_definitions(unc_spm_native PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC)
    target_compile_options(unc_spm_native PRIVATE
        /W4 /WX
        /MP
        /fp:fast
        /Oi /Ot
        /GS-
        /GR-
    )
    target_link_options(unc_spm_native INTERFACE /OPT:REF /OPT:ICF /LTCG)
else()
    target_compile_options(unc_spm_native PRIVATE -Wall -Wextra -Werror -O3 -ffast-math)
endif()

if(UNC_SPM_BUILD_TESTS)
    enable_testing()
    add_executable(spm_invariants_test gow3_pc/tests/test_spm_invariants.cpp)
    target_link_libraries(spm_invariants_test PRIVATE unc_spm_native)
    target_compile_options(spm_invariants_test PRIVATE -UNDEBUG)
    add_test(NAME spm_invariants_test COMMAND spm_invariants_test)
endif()

if(UNC_SPM_BUILD_GOW3_RUNTIME)
    add_subdirectory(gow3_pc/gpu)
endif()
