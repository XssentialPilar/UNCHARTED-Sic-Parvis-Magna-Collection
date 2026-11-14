#include "gow3_mouse.h"
#include "gow3_settings.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace Gow3Mouse {
namespace {

std::mutex mutex;
float acc_x = 0, acc_y = 0;
float look_x = 0, look_y = 0;
std::chrono::steady_clock::time_point look_taken{};
unsigned look_calls = 0; ///< camera hook calls (gow3gpu_mouse_look_calls)
bool look_paused = false; ///< the pad gives the mouse to the stick: hooks get no movement
bool injected = false;
unsigned held = 0; // bits 0-4: SDL buttons 1-5
std::chrono::steady_clock::time_point wheel_until[2];
std::atomic<bool> captured{false};

constexpr unsigned WheelUp = 1u << 5, WheelDown = 1u << 6;

} // namespace

// The settings' mouse switch (menu Controls tab, launcher; GOW3_MOUSE=0 overrides it at start).
bool Enabled() {
    return Gow3Settings::Get().mouse;
}

void Update(SDL_Window* window, bool may_capture, bool foreground) {
    const bool focused = foreground || SDL_GetKeyboardFocus() == window ||
                         (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    const bool want = Enabled() && may_capture && focused;
    const bool actual = SDL_GetWindowRelativeMouseMode(window);
    if (want == captured.load(std::memory_order_relaxed) && actual == want) {
        return;
    }
    static auto retry_after = std::chrono::steady_clock::time_point{};
    static bool failed = false;
    if (want != actual && failed && std::chrono::steady_clock::now() < retry_after) {
        return;
    }
    if (want && !actual) {
        // gow3 24 Oct 2026: raw, linear counts. SDL3 relative mode reads raw input and leaves the
        // system acceleration curve off by default; the hints and a user's SDL_* environment
        // variables could switch a curve or a scale on, so they are pinned here (override
        // priority beats the environment); nothing sets a motion transform. The counts then only
        // get the sensitivity: no acceleration or smoothing anywhere on the mouse-look path.
        SDL_SetHintWithPriority(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "0", SDL_HINT_OVERRIDE);
        SDL_SetHintWithPriority(SDL_HINT_MOUSE_RELATIVE_SPEED_SCALE, "1", SDL_HINT_OVERRIDE);
        SDL_SetHintWithPriority(SDL_HINT_MOUSE_RELATIVE_WARP_MOTION, "0", SDL_HINT_OVERRIDE);
    }
    // SpartanPort 3.0.0 also commits capture only on SDL success; recover and retry here.
    if (want != actual && !SDL_SetWindowRelativeMouseMode(window, want)) {
        if (!failed) {
            std::fprintf(stderr, "Input: mouse capture failed (%s); retrying\n", SDL_GetError());
        }
        failed = true;
        retry_after = std::chrono::steady_clock::now() + std::chrono::milliseconds{250};
        // Publish the real state, so the pad/camera do not believe a failed capture succeeded.
    } else {
        failed = false;
    }
    const bool active = want && SDL_GetWindowRelativeMouseMode(window);
    std::scoped_lock lock{mutex};
    const bool was_captured = captured.load(std::memory_order_relaxed);
    captured.store(active, std::memory_order_relaxed);
    acc_x = acc_y = 0;
    look_x = look_y = 0;
    held = 0; // no button stays pressed across a release (Alt+Tab with the trigger held)
    wheel_until[0] = wheel_until[1] = {};
    if (active != was_captured) {
        std::printf("Input: mouse %s\n", active ? "captured (relative raw input)" : "released");
    }
}

bool OnEvent(const SDL_Event& event) {
    if (!captured) {
        return false;
    }
    std::scoped_lock lock{mutex};
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        acc_x += event.motion.xrel;
        acc_y += event.motion.yrel;
        look_x += event.motion.xrel;
        look_y += event.motion.yrel;
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button >= 1 && event.button.button <= 5) {
            const unsigned bit = 1u << (event.button.button - 1);
            held = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? held | bit : held & ~bit;
        }
        return true;
    case SDL_EVENT_MOUSE_WHEEL:
        if (event.wheel.y != 0) {
            wheel_until[event.wheel.y > 0 ? 0 : 1] =
                std::chrono::steady_clock::now() + std::chrono::milliseconds{100};
        }
        return true;
    default:
        return false;
    }
}

} // namespace Gow3Mouse

extern "C" int gow3gpu_mouse_take_look(float* dx, float* dy) {
    using namespace Gow3Mouse;
    std::scoped_lock lock{mutex};
    const auto now = std::chrono::steady_clock::now();
    const bool fresh = now - look_taken < std::chrono::milliseconds{250};
    look_taken = now;
    ++look_calls;
    if (look_paused) {  // the mouse is the stick now: drop what the hook would turn
        look_x = look_y = 0;
        *dx = *dy = 0.0f;
        return 0;
    }
    *dx = fresh ? look_x : 0.0f;
    *dy = fresh ? look_y : 0.0f;
    look_x = look_y = 0;
    return captured || injected ? 1 : 0;
}

extern "C" void gow3gpu_mouse_inject(float dx, float dy) {
    using namespace Gow3Mouse;
    std::scoped_lock lock{mutex};
    look_x += dx;
    look_y += dy;
    acc_x += dx;
    acc_y += dy;
    injected = true;
}

extern "C" unsigned gow3gpu_mouse_look_calls(void) {
    using namespace Gow3Mouse;
    std::scoped_lock lock{mutex};
    return look_calls;
}

extern "C" void gow3gpu_mouse_look_pause(int paused) {
    using namespace Gow3Mouse;
    std::scoped_lock lock{mutex};
    look_paused = paused != 0;
}

extern "C" int gow3gpu_mouse_look_recent(void) {
    using namespace Gow3Mouse;
    std::scoped_lock lock{mutex};
    return std::chrono::steady_clock::now() - look_taken < std::chrono::milliseconds{200} ? 1 : 0;
}

extern "C" int gow3gpu_mouse_take(float* dx, float* dy, unsigned* buttons) {
    using namespace Gow3Mouse;
    std::scoped_lock lock{mutex};
    *dx = acc_x;
    *dy = acc_y;
    acc_x = acc_y = 0;
    if (injected) {  // tests (pad file mx= my=): the stick path too
        *buttons = held;
        return 1;
    }
    const auto now = std::chrono::steady_clock::now();
    *buttons = held | (now < wheel_until[0] ? WheelUp : 0) | (now < wheel_until[1] ? WheelDown : 0);
    return captured ? 1 : 0;
}
