cmake_minimum_required(VERSION 3.24)
project(UnchartedSicParvisMagna VERSION 0.6.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(UNC_SPM_BUILD_TESTS "Build Uncharted: Sic Parvis Magna Collection unit tests" ON)
option(UNC_SPM_BUILD_GOW3_RUNTIME "Build the full gow3_pc PS4-to-PC runtime & Vulkan shim" OFF)

if(UNC_SPM_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(Project/spm_core)
add_subdirectory(Project/spm_launcher)

add_library(unc_spm_native STATIC
    Project/native/core/core.cpp
    Project/native/core/arena_allocator.cpp
    Project/native/core/frame_pacer.cpp
    Project/native/graphics/dx9_backend.cpp
    Project/native/graphics/dx11_backend.cpp
    Project/native/graphics/dx12_backend.cpp
    Project/native/graphics/spm_graphics_core.cpp
    Project/native/graphics/dynamic_ring_buffer.cpp
    Project/native/graphics/swapchain_hook.cpp
    Project/native/graphics/transient_rt_pool.cpp
    Project/native/graphics/vram_budget_mgr.cpp
    Project/native/input/input_mapper.cpp
    Project/native/input/gameinput_handler.cpp
    Project/native/input/sdl_handler.cpp
)

target_include_directories(unc_spm_native
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/Project/include
        ${CMAKE_CURRENT_SOURCE_DIR}/Project/vendor/minhook
        ${CMAKE_CURRENT_SOURCE_DIR}/Project/vendor/d3d12
        ${CMAKE_CURRENT_SOURCE_DIR}/Project/vendor/sdl3
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
    add_executable(spm_invariants_test Project/gow3_pc/tests/test_spm_invariants.cpp)
    target_link_libraries(spm_invariants_test PRIVATE unc_spm_native)
    target_compile_options(spm_invariants_test PRIVATE -UNDEBUG)
    add_test(NAME spm_invariants_test COMMAND spm_invariants_test)

    add_executable(frame_generation_test
        Project/gow3_pc/tests/test_frame_generation.cpp
        Project/gow3_pc/gpu/shadps4/video_core/renderer_vulkan/frame_generation_policy.cpp
    )
    target_include_directories(frame_generation_test PRIVATE
        Project/gow3_pc/gpu/shadps4
    )
    target_compile_options(frame_generation_test PRIVATE -UNDEBUG)
    add_test(NAME frame_generation_test COMMAND frame_generation_test)
endif()

if(UNC_SPM_BUILD_GOW3_RUNTIME)
    add_subdirectory(Project/gow3_pc/gpu)
endif()
