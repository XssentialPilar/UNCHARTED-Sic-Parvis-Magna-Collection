// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection — Native C++ / SDL3 Launcher
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "explorer_desk.hpp"
#include "ui_theme.hpp"
#include "spm/spm_core.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

static std::string FindWorkspaceRoot(const char* argv0) {
    fs::path current = fs::current_path();
    std::vector<fs::path> candidates = {
        current,
        current / "..",
        current / "../..",
        fs::path(argv0).parent_path(),
        fs::path(argv0).parent_path() / ".."
    };

    for (const auto& p : candidates) {
        if (fs::exists(p / "launcher.json") || fs::exists(p / "VERSION") || fs::exists(p / "Project")) {
            return fs::canonical(p).string();
        }
    }
    return current.string();
}

int main(int argc, char* argv[]) {
    bool testRun = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--test-run") == 0 || std::strcmp(argv[i], "--headless") == 0) {
            testRun = true;
            break;
        }
    }

    std::string root = FindWorkspaceRoot(argv[0]);
    printf("[SPMLauncher] Uncharted: Sic Parvis Magna Collection v%s\n", SPM_GetCoreVersion());
    printf("[SPMLauncher] Platform: %s | Workspace Root: %s\n", SPM_GetPlatformIdString(), root.c_str());

    // Headless / Test-run mode for CI/CTest validation
    if (testRun) {
        printf("[SPMLauncher] Running automated invariant test mode (--test-run)...\n");
        spm::ExplorerDesk desk;
        desk.Initialize(root);
        const SPMConfig& cfg = desk.GetConfig();
        if (cfg.language[0] == '\0') {
            fprintf(stderr, "[ERR] Language configuration is empty.\n");
            return 1;
        }
        printf("[SPMLauncher] Configuration verified: Lang=%s, API=%s, SelectedGame=%s\n",
            cfg.language, cfg.graphicsApi, cfg.selectedGame);
        printf("[SPMLauncher] Automated test passed successfully.\n");
        return 0;
    }

    // Initialize SDL3
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "[ERR] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // Window Setup
    const int initialWidth = 1180;
    const int initialHeight = 720;
    SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;

    SDL_Window* window = SDL_CreateWindow(
        "UNCHARTED: Sic Parvis Magna Collection — Explorer's Desk",
        initialWidth,
        initialHeight,
        windowFlags
    );

    if (!window) {
        fprintf(stderr, "[ERR] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        fprintf(stderr, "[ERR] SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

    // Apply DPI scaling
    float dpiScale = SDL_GetWindowDisplayScale(window);
    if (dpiScale < 1.0f) dpiScale = 1.0f;
    spm::ui::ApplyExplorersDeskTheme(dpiScale);

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    spm::ExplorerDesk desk;
    desk.Initialize(root);

    // Main Loop
    bool running = true;
    while (running && !desk.ShouldClose()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window)) {
                running = false;
            }
        }

        // Start the Dear ImGui frame
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Render Explorer's Desk
        desk.Render();

        // Rendering
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 16, 18, 21, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    // Cleanup
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    printf("[SPMLauncher] Shutdown completed.\n");
    return 0;
}
