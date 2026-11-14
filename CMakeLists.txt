cmake_minimum_required(VERSION 3.24)
project(UnchartedSicParvisMagna VERSION 0.6.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(UNC_SPM_BUILD_TESTS "Build Uncharted: Sic Parvis Magna Collection unit tests" ON)
option(UNC_SPM_BUILD_GOW3_RUNTIME "Build the full gow3_pc PS4-to-PC runtime & Vulkan shim" OFF)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Project/spm_core/CMakeLists.txt")
    set(UNC_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/Project")
else()
    set(UNC_SRC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

if(UNC_SPM_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(${UNC_SRC_ROOT}/spm_core spm_core)
if(EXISTS "${UNC_SRC_ROOT}/spm_launcher/CMakeLists.txt" AND (NOT WIN32 OR EXISTS "${UNC_SRC_ROOT}/vendor/SDL3/SDL3-3.2.8"))
    add_subdirectory(${UNC_SRC_ROOT}/spm_launcher spm_launcher)
endif()

add_library(unc_spm_native STATIC
    ${UNC_SRC_ROOT}/native/core/core.cpp
    ${UNC_SRC_ROOT}/native/core/arena_allocator.cpp
    ${UNC_SRC_ROOT}/native/core/frame_pacer.cpp
    ${UNC_SRC_ROOT}/native/graphics/dx9_backend.cpp
    ${UNC_SRC_ROOT}/native/graphics/dx11_backend.cpp
    ${UNC_SRC_ROOT}/native/graphics/dx12_backend.cpp
    ${UNC_SRC_ROOT}/native/graphics/spm_graphics_core.cpp
    ${UNC_SRC_ROOT}/native/graphics/dynamic_ring_buffer.cpp
    ${UNC_SRC_ROOT}/native/graphics/swapchain_hook.cpp
    ${UNC_SRC_ROOT}/native/graphics/transient_rt_pool.cpp
    ${UNC_SRC_ROOT}/native/graphics/vram_budget_mgr.cpp
    ${UNC_SRC_ROOT}/native/input/input_mapper.cpp
    ${UNC_SRC_ROOT}/native/input/gameinput_handler.cpp
    ${UNC_SRC_ROOT}/native/input/sdl_handler.cpp
)

target_include_directories(unc_spm_native
    PUBLIC
        ${UNC_SRC_ROOT}/include
        ${UNC_SRC_ROOT}/vendor/minhook
        ${UNC_SRC_ROOT}/vendor/d3d12
        ${UNC_SRC_ROOT}/vendor/sdl3
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
    add_executable(spm_invariants_test ${UNC_SRC_ROOT}/gow3_pc/tests/test_spm_invariants.cpp)
    target_link_libraries(spm_invariants_test PRIVATE unc_spm_native)
    target_compile_options(spm_invariants_test PRIVATE -UNDEBUG)
    add_test(NAME spm_invariants_test COMMAND spm_invariants_test)

    add_executable(frame_generation_test
        ${UNC_SRC_ROOT}/gow3_pc/tests/test_frame_generation.cpp
        ${UNC_SRC_ROOT}/gow3_pc/gpu/shadps4/video_core/renderer_vulkan/frame_generation_policy.cpp
    )
    target_include_directories(frame_generation_test PRIVATE
        ${UNC_SRC_ROOT}/gow3_pc/gpu/shadps4
    )
    target_compile_options(frame_generation_test PRIVATE -UNDEBUG)
    add_test(NAME frame_generation_test COMMAND frame_generation_test)
endif()

if(UNC_SPM_BUILD_GOW3_RUNTIME)
    add_subdirectory(${UNC_SRC_ROOT}/gow3_pc/gpu gow3_gpu)
endif()
