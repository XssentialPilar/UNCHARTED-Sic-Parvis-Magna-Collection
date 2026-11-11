cmake_minimum_required(VERSION 3.24)
project(spm_launcher LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../gow3_pc/gpu/third_party/imgui")

set(SPM_LAUNCHER_SOURCES
    src/main.cpp
    src/ui_theme.cpp
    src/explorer_desk.cpp
    src/game_launcher.cpp
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
    ${IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)

add_executable(spm_launcher ${SPM_LAUNCHER_SOURCES})

target_include_directories(spm_launcher PRIVATE
    src
    ${CMAKE_CURRENT_SOURCE_DIR}/../spm_core/include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

target_link_libraries(spm_launcher PRIVATE SPMCore)

if(WIN32)
    set_target_properties(spm_launcher PROPERTIES OUTPUT_NAME "Start Uncharted")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        target_include_directories(spm_launcher PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/SDL3/SDL3-3.2.8/x86_64-w64-mingw32/include
        )
        target_link_libraries(spm_launcher PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/SDL3/SDL3-3.2.8/x86_64-w64-mingw32/lib/libSDL3.dll.a
        )
    else()
        target_include_directories(spm_launcher PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/SDL3/SDL3-3.2.8/i686-w64-mingw32/include
        )
        target_link_libraries(spm_launcher PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../vendor/SDL3/SDL3-3.2.8/i686-w64-mingw32/lib/libSDL3.dll.a
        )
    endif()
    target_link_libraries(spm_launcher PRIVATE dxgi d3d11 winmm)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(SDL3_BIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/SDL3/SDL3-3.2.8/x86_64-w64-mingw32/bin")
    else()
        set(SDL3_BIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/SDL3/SDL3-3.2.8/i686-w64-mingw32/bin")
    endif()
    add_custom_command(TARGET spm_launcher POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SDL3_BIN_DIR}/SDL3.dll"
            $<TARGET_FILE_DIR:spm_launcher>/SDL3.dll
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:SPMCore>
            $<TARGET_FILE_DIR:spm_launcher>/
    )
else()
    set_target_properties(spm_launcher PROPERTIES OUTPUT_NAME "uncharted-launcher")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
    target_link_libraries(spm_launcher PRIVATE PkgConfig::SDL3)
endif()

if(UNC_SPM_BUILD_TESTS)
    add_test(NAME test_spm_launcher_headless COMMAND spm_launcher --test-run)
    set_tests_properties(test_spm_launcher_headless PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:spm_launcher>;PATH=path_list_prepend:$<TARGET_FILE_DIR:SPMCore>"
    )
endif()
