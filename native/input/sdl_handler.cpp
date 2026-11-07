// SPDX-License-Identifier: GPL-2.0-or-later
// Uncharted: Sic Parvis Magna Collection — SDL3 Input & Confirmed Relative Mouse-Look Handler

#include "unc/input.hpp"
#include "sdl3_input.h"

namespace unc::input {

namespace {

class SdlInputHandler final : public IInputHandler {
public:
    SdlInputHandler() noexcept = default;
    ~SdlInputHandler() override = default;

    [[nodiscard]] InputBackendKind GetBackendKind() const noexcept override {
        return InputBackendKind::Sdl3;
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
        const bool focus_ok = !capture_config_.require_window_focus || window_focused_;
        const bool rel_ok = !capture_config_.require_relative_confirmation || relative_confirmed_;
        if (focus_ok && rel_ok) {
            const double sign_y = capture_config_.invert_y ? -1.0 : 1.0;
            accumulated_dx_ += raw_dx * capture_config_.default_yaw_sensitivity;
            accumulated_dy_ += raw_dy * capture_config_.default_pitch_sensitivity * sign_y;
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
    vendor::sdl3::SdlMouseCaptureConfig capture_config_{};
    ClientPoint raw_cursor_{};
    double accumulated_dx_{0.0};
    double accumulated_dy_{0.0};
};

} // namespace

std::unique_ptr<IInputHandler> CreateSdlInputHandler() {
    return std::make_unique<SdlInputHandler>();
}

} // namespace unc::input
