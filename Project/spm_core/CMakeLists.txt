cmake_minimum_required(VERSION 3.20)
project(SPMCore VERSION 0.6.1 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable position independent code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Includes
include_directories(include src)

set(SPM_CORE_SOURCES
    src/spm_core.cpp
    src/spm_platform.cpp
    src/spm_hardware.cpp
    src/spm_config.cpp
    src/spm_localization.cpp
    src/spm_updater.cpp
    src/spm_streaming.cpp
    src/spm_launcher_bridge.cpp
    src/spm_hardware_caps.cpp
    src/spm_neural_assist.cpp
)

add_library(SPMCore SHARED ${SPM_CORE_SOURCES})
target_compile_definitions(SPMCore PRIVATE SPM_CORE_EXPORTS)
target_include_directories(SPMCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if (WIN32)
    set_target_properties(SPMCore PROPERTIES
        PREFIX ""
        OUTPUT_NAME "SPMCore"
    )
    target_link_options(SPMCore PRIVATE -static -static-libgcc)
    target_link_libraries(SPMCore PRIVATE
        dxgi
        d3d11
        winmm
        psapi
        winhttp
    )
else()
    target_link_libraries(SPMCore PRIVATE
        pthread
        dl
    )
    set_target_properties(SPMCore PROPERTIES
        PREFIX "lib"
        OUTPUT_NAME "SPMCore"
    )
endif()

# Automated Test Suites
enable_testing()

macro(add_spm_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} tests/${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE SPMCore)
    if (WIN32)
        target_link_options(${TEST_NAME} PRIVATE -static -static-libgcc)
    endif()
    target_include_directories(${TEST_NAME} PRIVATE include)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endmacro()

add_spm_test(test_spm_platform test_platform.cpp)
add_spm_test(test_spm_hardware test_hardware.cpp)
add_spm_test(test_spm_localization test_localization.cpp)
add_spm_test(test_spm_packages test_updater.cpp)
add_spm_test(test_spm_config test_config.cpp)
add_spm_test(test_spm_streaming test_streaming.cpp)
add_spm_test(test_spm_hardware_caps test_hardware_caps.cpp)
add_spm_test(test_spm_neural_assist test_neural_assist.cpp)
add_spm_test(benchmark_neural_modes benchmark_neural_modes.cpp)
