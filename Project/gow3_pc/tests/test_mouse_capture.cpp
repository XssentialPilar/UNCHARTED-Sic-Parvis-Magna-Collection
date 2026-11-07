// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <chrono>
#include <thread>
#include "gow3_mouse.h"
#include "gow3_settings.h"

namespace {
SDL_Window* test_window = reinterpret_cast<SDL_Window*>(uintptr_t{1});
bool focused = true, relative = false, fail_once = true;
unsigned attempts = 0;
}

namespace Gow3Settings {
Values& Get() { static Values values; return values; }
}

// Controlled SDL boundary: reproduce a driver refusing capture, a lost relative mode, and
// SDL/native focus disagreement without taking the user's actual mouse.
extern "C" {
SDL_WindowFlags SDLCALL SDL_GetWindowFlags(SDL_Window*) {
    return focused ? SDL_WINDOW_INPUT_FOCUS : 0;
}
SDL_Window* SDLCALL SDL_GetKeyboardFocus() { return focused ? test_window : nullptr; }
bool SDLCALL SDL_GetWindowRelativeMouseMode(SDL_Window*) { return relative; }
bool SDLCALL SDL_SetWindowRelativeMouseMode(SDL_Window*, bool value) {
    ++attempts;
    if (fail_once) { fail_once = false; return false; }
    relative = value;
    return true;
}
bool SDLCALL SDL_SetHintWithPriority(const char*, const char*, SDL_HintPriority) { return true; }
const char* SDLCALL SDL_GetError() { return "simulated capture failure"; }
}

int main() {
    Gow3Settings::Get().mouse = true;
    float x, y;
    unsigned buttons;
    Gow3Mouse::Update(test_window, true);
    assert(gow3gpu_mouse_take(&x, &y, &buttons) == 0); // Failure must not publish captured=true.
    Gow3Mouse::Update(test_window, true);
    assert(attempts == 1); // Backoff avoids a syscall storm on a persistent driver failure.
    std::this_thread::sleep_for(std::chrono::milliseconds{260});
    Gow3Mouse::Update(test_window, true);
    assert(relative && gow3gpu_mouse_take(&x, &y, &buttons) == 1);
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.xrel = 12;
    event.motion.yrel = -4;
    assert(Gow3Mouse::OnEvent(event));
    assert(gow3gpu_mouse_take(&x, &y, &buttons) == 1 && x == 12 && y == -4);
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.button = SDL_BUTTON_LEFT;
    Gow3Mouse::OnEvent(event);
    gow3gpu_mouse_take(&x, &y, &buttons);
    assert(buttons == 1);
    relative = false; // OS/overlay lost the mode even though the port still thinks it owns it.
    Gow3Mouse::Update(test_window, true);
    assert(relative && attempts == 3);
    Gow3Mouse::Update(test_window, false); // Menu or text dialog releases all buttons.
    assert(!relative && gow3gpu_mouse_take(&x, &y, &buttons) == 0 && buttons == 0);
    focused = false;
    Gow3Mouse::Update(test_window, true);
    assert(!relative);
    Gow3Mouse::Update(test_window, true, true); // Windows foreground can repair SDL focus loss.
    assert(relative);
    Gow3Settings::Get().mouse = false;
    Gow3Mouse::Update(test_window, true, true);
    assert(!relative);
}
