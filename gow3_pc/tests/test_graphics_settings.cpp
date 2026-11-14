#include "test_environment.h"
// gow3.ini graphics keys (gow3_settings.cpp, gow3_graphics.h).
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include "gow3_graphics.h"
#include "gow3_settings.h"

namespace G = Gow3Graphics;

static std::string Read(const char* path) {
    std::ifstream in(path);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

int main() {
    assert(G::ParseDisplayMode("borderless") == G::Borderless && G::ParseDisplayMode("bogus") == G::Windowed);
    assert(G::ParseFpsLimit(240) == G::MaxFpsLimit && G::ParseFpsLimit(75) == 75);
    assert(G::ParseResolution("1440p") == 3 && G::ParseResolution("8K") == 0);
    assert(G::ParseEngineFps(240) == 240 && G::ParseEngineFps(90) == 120);
    assert(G::ParseFramesQueued(2) == 2 && G::ParseFramesQueued(0) == 0 && G::ParseFramesQueued(5) == 1);
    assert(G::ParseRcasStrength(100) == 100 && G::ParseRcasStrength(101) == 75 && G::ParseRcasStrength(-1) == 75);
    assert(G::RcasAttenuation(100) == 0.0f && G::RcasAttenuation(0) == 2.0f && G::RcasAttenuation(75) == 0.5f);
    assert(G::FsrPassRuns(true, false, 1280, 720, 2560, 1440) && !G::FsrPassRuns(false, false, 1280, 720, 2560, 1440));
    assert(G::FsrPassRuns(false, true, 1920, 1080, 1920, 1080) && !G::FsrPassRuns(true, false, 1920, 1080, 1920, 1080));
    assert(!G::FsrPassRuns(true, true, 2560, 1440, 1920, 1080));  // larger image: regular scaling
    assert(G::EffectiveFps(0, 120) == 120 && G::EffectiveFps(240, 120) == 120 && G::EffectiveFps(60, 240) == 60);
    // Presentation is capped independently from the engine's simulation rate.
    assert(G::PresentLimit(120) == 120 && G::PresentLimit(240) == G::MaxFpsLimit);
    assert(G::PresentLimit(0) == 0 && G::ParseFpsLimit(0) == 0);
    assert(G::PresentLimit(G::MaxFpsLimit + 1) == G::MaxFpsLimit);
    assert(G::PresentLimit(30) == 30);

    const char* path = "graphics-settings-test.ini";
    {
        std::ofstream out(path);
        out << "display_mode=borderless\nvsync=0\nfps_limit=240\nasync_shaders=0\n"
               "render_resolution=1440p\nengine_fps=240\ndeferred_readback=0\nstale_readback=1\n"
               "fsr1=1\nrcas=0\nrcas_strength=40\nframes_queued=2\n";
    }
    SetTestEnv("GOW3_CONFIG", path);
    SetTestEnv("GOW3_ASYNC_SHADERS", "");
    auto& v = Gow3Settings::Get();
    Gow3Settings::Load();
    assert(v.display_mode == G::Borderless && !v.vsync && v.fps_limit == G::MaxFpsLimit && !v.async_shaders);
    assert(v.render_resolution == 3 && v.engine_fps == 240 && !v.deferred_readback && v.stale_readback);
    assert(v.fsr1 && !v.rcas && v.rcas_strength == 40 && v.frames_queued == 2);
    assert(!Gow3Settings::GraphicsNeedRestart());
    assert(v.applied_engine_fps == 240);  // no GOW3_ENGINE_FPS: the configured rate
    SetTestEnv("GOW3_ENGINE_FPS", "60");    // run.py: the frame rate patch did not apply
    Gow3Settings::Load();
    assert(v.engine_fps == 240 && v.applied_engine_fps == 60 && !Gow3Settings::GraphicsNeedRestart());
    SetTestEnv("GOW3_ENGINE_FPS", "");
    v.engine_fps = 120;
    assert(!Gow3Settings::GraphicsNeedRestart());
    v.deferred_readback = true;
    assert(Gow3Settings::GraphicsNeedRestart());
    v.deferred_readback = false;
    v.fps_limit = 60;  // live: no restart because of it
    v.engine_fps = 240;
    assert(!Gow3Settings::GraphicsNeedRestart());

    Gow3Settings::Save();
    const std::string saved = Read(path);
    for (const char* line : {"display_mode=borderless\n", "vsync=0\n", "fps_limit=60\n", "async_shaders=0\n",
                             "render_resolution=1440p\n", "engine_fps=240\n", "deferred_readback=0\n",
                             "stale_readback=1\n", "fsr1=1\n", "rcas=0\n", "rcas_strength=40\n",
                             "frames_queued=2\n"}) {
        assert(saved.find(line) != std::string::npos);
    }

    {
        std::ofstream out(path);
        out << "display_mode=huge\nfps_limit=7\nrender_resolution=8K\nengine_fps=1\n"
               "rcas_strength=250\nframes_queued=9\n";
    }
    SetTestEnv("GOW3_ASYNC_SHADERS", "1");  // the environment still wins at start
    v.async_shaders = false;
    Gow3Settings::Load();
    assert(v.display_mode == G::Windowed && v.fps_limit == 60 && v.render_resolution == 0 && v.engine_fps == 120);
    assert(v.rcas_strength == 75 && v.frames_queued == 1);
    assert(v.async_shaders);
    {
        std::ofstream out(path);
        out << "cheat_double_jumps=1\ncheat_very_hard=1\n";
    }
    Gow3Settings::Load();
    Gow3Settings::Save();
    const std::string cheats = Read(path);
    assert(cheats.find("cheat_double_jumps=1\n") != std::string::npos);
    assert(cheats.find("cheat_very_hard=1\n") != std::string::npos);

    // Verify in-game preferences and controls persistence
    {
        v.mouse = true;
        v.mouse_sens = 2.5f;
        v.mouse_aim_sens = 1.25f;
        v.mouse_vertical = 0.85f;
        v.mouse_invert = true;
        v.mouse_invert_x = false;
        v.vibration = false;
        v.stick_speed = 1.5f;
        {
            std::scoped_lock lock{Gow3Settings::ControlsMutex()};
            v.pad_map = "jump=Cross,fire=R2,aim=L2";
            v.key_map = "jump=Space,melee=F,reload=R";
            v.mouse_map = "fire=left,aim=right";
        }
        Gow3Settings::Save();
    }
    {
        // Reset in-memory values and load from file to verify persistence
        v.mouse_sens = 1.0f;
        v.vibration = true;
        {
            std::scoped_lock lock{Gow3Settings::ControlsMutex()};
            v.pad_map.clear();
            v.key_map.clear();
            v.mouse_map.clear();
        }
        Gow3Settings::Load();
        assert(v.mouse && v.mouse_invert && !v.mouse_invert_x);
        assert(std::abs(v.mouse_sens - 2.5f) < 0.01f);
        assert(std::abs(v.mouse_aim_sens - 1.25f) < 0.01f);
        assert(std::abs(v.mouse_vertical - 0.85f) < 0.01f);
        assert(!v.vibration);
        assert(std::abs(v.stick_speed - 1.5f) < 0.01f);
        {
            std::scoped_lock lock{Gow3Settings::ControlsMutex()};
            assert(v.pad_map == "jump=Cross,fire=R2,aim=L2");
            assert(v.key_map == "jump=Space,melee=F,reload=R");
            assert(v.mouse_map == "fire=left,aim=right");
        }
    }

    std::remove(path);
    std::puts("PASS: graphics settings and controls persistence");
}
