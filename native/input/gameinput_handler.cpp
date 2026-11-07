// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection — GameInput Low-Latency Handler

#include "unc/input.hpp"

namespace unc::input {

namespace {

class GameInputHandler final : public IInputHandler {
public:
    GameInputHandler() noexcept = default;
    ~GameInputHandler() override = default;

    [[nodiscard]] InputBackendKind GetBackendKind() const noexcept override {
        return InputBackendKind::GameInput;
    }

    HRESULT Initialize() override {
        initialized_ = true;
        return S_OK;
    }

    void SetWindowFocus(bool focused) noexcept override {
        window_focused_ = focused;
        if (!focused) {
            accumulated_dx_ = 0.0;
            accumulated_dy_ = 0.0;
        }
    }

    void SetRelativeMouseConfirmed(bool confirmed) noexcept override {
        relative_confirmed_ = confirmed;
        if (!confirmed) {
            accumulated_dx_ = 0.0;
            accumulated_dy_ = 0.0;
        }
    }

    void SubmitRawMouseState(ClientPoint client_cursor, double raw_dx,
                             double raw_dy) noexcept override {
        raw_cursor_ = client_cursor;
        if (window_focused_ && relative_confirmed_) {
            accumulated_dx_ += raw_dx;
            accumulated_dy_ += raw_dy;
        }
    }

    [[nodiscard]] InputFrameState PollFrame(const graphics::ViewportRect& active_viewport,
                                            uint32_t internal_width,
                                            uint32_t internal_height) noexcept override {
        InputFrameState frame{};
        frame.raw_cursor = raw_cursor_;
        frame.remapped_cursor = RemapClientToGameCoordinates(raw_cursor_, active_viewport,
                                                             internal_width, internal_height);
        frame.window_has_focus = window_focused_;
        frame.relative_mouse_confirmed = relative_confirmed_;
        if (window_focused_ && relative_confirmed_) {
            frame.relative_yaw_delta = accumulated_dx_;
            frame.relative_pitch_delta = accumulated_dy_;
        }
        accumulated_dx_ = 0.0;
        accumulated_dy_ = 0.0;
        return frame;
    }

private:
    bool initialized_{false};
    bool window_focused_{true};
    bool relative_confirmed_{false};
    ClientPoint raw_cursor_{};
    double accumulated_dx_{0.0};
    double accumulated_dy_{0.0};
};

} // namespace

std::unique_ptr<IInputHandler> CreateGameInputHandler() {
    return std::make_unique<GameInputHandler>();
}

} // namespace unc::input
