cmake_minimum_required(VERSION 3.24)
project(uncharted_portable_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)
enable_testing()
foreach(name arena_allocations gc_policy cache_io_queue async_compiler warmup_inbox readback_queue)
    add_executable(${name} ../test_${name}.cpp)
    target_include_directories(${name} PRIVATE ../../gpu/shadps4)
    target_compile_options(${name} PRIVATE -UNDEBUG -Wall -Wextra -Werror)
    target_link_libraries(${name} PRIVATE Threads::Threads)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 30)
endforeach()
add_executable(frame_generation ../test_frame_generation.cpp
    ../../gpu/shadps4/video_core/renderer_vulkan/frame_generation_policy.cpp)
target_include_directories(frame_generation PRIVATE ../../gpu/shadps4)
target_compile_options(frame_generation PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(frame_generation PRIVATE Threads::Threads)
add_test(NAME frame_generation COMMAND frame_generation)
set_tests_properties(frame_generation PROPERTIES TIMEOUT 30)

add_executable(spm_invariants ../test_spm_invariants.cpp
    ../../../native/core/core.cpp
    ../../../native/core/arena_allocator.cpp
    ../../../native/core/frame_pacer.cpp
    ../../../native/graphics/dx11_backend.cpp
    ../../../native/graphics/dx12_backend.cpp
    ../../../native/graphics/dynamic_ring_buffer.cpp
    ../../../native/graphics/swapchain_hook.cpp
    ../../../native/graphics/transient_rt_pool.cpp
    ../../../native/graphics/vram_budget_mgr.cpp
    ../../../native/input/input_mapper.cpp
    ../../../native/input/gameinput_handler.cpp
    ../../../native/input/sdl_handler.cpp)
target_include_directories(spm_invariants PRIVATE
    ../../../include
    ../../../vendor/minhook
    ../../../vendor/d3d12
    ../../../vendor/sdl3)
target_compile_options(spm_invariants PRIVATE -UNDEBUG -Wall -Wextra -Werror)
target_link_libraries(spm_invariants PRIVATE Threads::Threads)
add_test(NAME spm_invariants COMMAND spm_invariants)
set_tests_properties(spm_invariants PROPERTIES TIMEOUT 30)
