cmake_minimum_required(VERSION 3.20)
project(SPMCore VERSION 0.6.1 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable position independent code
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Optional sanitizer build used by the audit/validation runs. Kept opt-in so a
# normal Release package is byte-for-byte unaffected.
option(SPM_ENABLE_SANITIZERS "Build SPMCore and its tests with ASan/UBSan" OFF)
if (SPM_ENABLE_SANITIZERS AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

# Includes
include_directories(include src)

set(SPM_CORE_SOURCES
    src/spm_core.cpp
    src/spm_platform.cpp
    src/spm_hardware.cpp
    src/spm_config.cpp
    src/spm_localization.cpp
    src/spm_updater.cpp
    src/spm_updater_parse.cpp
    src/spm_streaming.cpp
    src/spm_launcher_bridge.cpp
    src/spm_hardware_caps.cpp
    src/spm_neural_assist.cpp
    src/spm_scheduler.cpp
    src/spm_sysinfo.cpp
    src/spm_json.cpp
)

add_library(SPMCore SHARED ${SPM_CORE_SOURCES})
target_compile_definitions(SPMCore PRIVATE
    SPM_CORE_EXPORTS
    SPM_CORE_VERSION_STRING="${PROJECT_VERSION}"
)
target_include_directories(SPMCore PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# The project builds with -Werror / /WX: no warnings are allowed to accumulate.
if (MSVC)
    target_compile_options(SPMCore PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(SPMCore PRIVATE -Wall -Wextra -Werror)
endif()

find_package(Threads REQUIRED)

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
        setupapi
        Threads::Threads
    )
else()
    target_link_libraries(SPMCore PRIVATE
        Threads::Threads
        dl
    )
    set_target_properties(SPMCore PROPERTIES
        PREFIX "lib"
        OUTPUT_NAME "SPMCore"
    )

    # Linux update checks use libcurl when the development package is present.
    # Without it the updater reports "unavailable" instead of spawning a shell.
    find_package(CURL QUIET)
    if (CURL_FOUND)
        target_compile_definitions(SPMCore PRIVATE SPM_HAVE_CURL=1)
        target_link_libraries(SPMCore PRIVATE CURL::libcurl)
    endif()
endif()

# Automated Test Suites
enable_testing()

macro(add_spm_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} tests/${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE SPMCore Threads::Threads)
    if (WIN32)
        # The sanitizer runtime is a DLL; a fully static test executable cannot
        # link against it, so only the regular package uses -static.
        if (NOT SPM_ENABLE_SANITIZERS)
            target_link_options(${TEST_NAME} PRIVATE -static -static-libgcc)
        endif()
    endif()
    target_include_directories(${TEST_NAME} PRIVATE include)
    target_compile_definitions(${TEST_NAME} PRIVATE SPM_EXPECTED_CORE_VERSION="${PROJECT_VERSION}")
    if (MSVC)
        target_compile_options(${TEST_NAME} PRIVATE /W4 /WX)
    else()
        target_compile_options(${TEST_NAME} PRIVATE -Wall -Wextra -Werror)
    endif()
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endmacro()

add_spm_test(test_spm_platform test_platform.cpp)
add_spm_test(test_spm_hardware test_hardware.cpp)
add_spm_test(test_spm_localization test_localization.cpp)
add_spm_test(test_spm_packages test_updater.cpp)
# The release parser is compiled into the test as well: exporting C++/STL
# internals from the DLL just for a unit test would be worse than a second copy.
target_sources(test_spm_packages PRIVATE src/spm_updater_parse.cpp src/spm_json.cpp)
add_spm_test(test_spm_config test_config.cpp)
add_spm_test(test_spm_streaming test_streaming.cpp)
add_spm_test(test_spm_hardware_caps test_hardware_caps.cpp)
add_spm_test(test_spm_neural_assist test_neural_assist.cpp)
add_spm_test(test_spm_scheduler test_scheduler.cpp)
add_spm_test(test_spm_concurrency test_concurrency.cpp)
add_spm_test(test_spm_streaming_io test_streaming_io.cpp)
add_spm_test(test_spm_config_robust test_config_robust.cpp)
add_spm_test(test_spm_abi test_abi.c)
add_spm_test(benchmark_neural_modes benchmark_neural_modes.cpp)
