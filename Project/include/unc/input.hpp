// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection — Decoupled Input Coordinate Remapping & Handlers
#pragma once

#include "unc/core.hpp"
#include "unc/graphics.hpp"

#include <cstdint>
#include <memory>

namespace unc::input {

enum class InputBackendKind : uint8_t {
    GameInput = 0,
    Sdl3 = 1,
};

struct ClientPoint {
    double x{0.0};
    double y{0.0};
};

struct GamePoint {
    double x{0.0};
    double y{0.0};
};

struct InputFrameState {
    ClientPoint raw_cursor{};
    GamePoint remapped_cursor{};
    double relative_yaw_delta{0.0};
    double relative_pitch_delta{0.0};
    float left_stick_x{0.0f};
    float left_stick_y{0.0f};
    float right_stick_x{0.0f};
    float right_stick_y{0.0f};
    float left_trigger{0.0f};
    float right_trigger{0.0f};
    uint32_t buttons_mask{0U};
    bool window_has_focus{true};
    bool relative_mouse_confirmed{false};
};

// Invariant 2.2: Input Coordinate Remapping
// X_game = (X_client - Viewport_X) * (Width_internal / Viewport_width)
// Y_game = (Y_client - Viewport_Y) * (Height_internal / Viewport_height)
// Strictly decouples X and Y scaling factors.
[[nodiscard]] constexpr GamePoint RemapClientToGameCoordinates(
    const ClientPoint& client_pos, const graphics::ViewportRect& viewport,
    uint32_t internal_width, uint32_t internal_height) noexcept {
    if (viewport.width <= 0.0 || viewport.height <= 0.0 || internal_width == 0U ||
        internal_height == 0U) {
        return GamePoint{0.0, 0.0};
    }
    const double scale_x = static_cast<double>(internal_width) / viewport.width;
    const double scale_y = static_cast<double>(internal_height) / viewport.height;
    const double x_game = (client_pos.x - viewport.x) * scale_x;
    const double y_game = (client_pos.y - viewport.y) * scale_y;
    return GamePoint{x_game, y_game};
}

class IInputHandler {
public:
    virtual ~IInputHandler() = default;

    [[nodiscard]] virtual InputBackendKind GetBackendKind() const noexcept = 0;
    virtual HRESULT Initialize() = 0;
    virtual void SetWindowFocus(bool focused) noexcept = 0;
    virtual void SetRelativeMouseConfirmed(bool confirmed) noexcept = 0;
    virtual void SubmitRawMouseState(ClientPoint client_cursor, double raw_dx,
                                     double raw_dy) noexcept = 0;
    [[nodiscard]] virtual InputFrameState PollFrame(
        const graphics::ViewportRect& active_viewport, uint32_t internal_width,
        uint32_t internal_height) noexcept = 0;
};

[[nodiscard]] std::unique_ptr<IInputHandler> CreateGameInputHandler();
[[nodiscard]] std::unique_ptr<IInputHandler> CreateSdlInputHandler();

} // namespace unc::input

#include "unc/input/input_mapper.hpp"
