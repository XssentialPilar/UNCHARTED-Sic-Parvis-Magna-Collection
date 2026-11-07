# Uncharted: Sic Parvis Magna Collection — Input Pipeline Specification

## 1. Hybrid Input Architecture (`GameInput` + `SDL3`)

The input subsystem in **Uncharted: Sic Parvis Magna Collection** combines two complementary backends under the unified `unc::input::IInputHandler` interface (`include/unc/input.hpp`):

1. **`GameInputHandler` (`native/input/gameinput_handler.cpp`):**
   - Low-latency polling for mouse deltas, keyboard state, and XInput/GameInput controllers on Windows.
   - Zero-allocation per-frame state snapshot (`unc::input::InputFrameState`).
2. **`SdlInputHandler` (`native/input/sdl_handler.cpp`) & Runtime Mouse-Look (`src/runtime_mouselook.c`):**
   - Cross-platform SDL3 gamepad + relative mouse capture (`SDL_SetWindowRelativeMouseMode`).
   - Confirmed relative-mode gating: relative mouse deltas are only accumulated into camera yaw/pitch when the host window holds focus and relative capture is confirmed by SDL, preventing phantom camera spins during `Alt+Tab`.

---

## 2. Mathematical Invariant 2.2: Decoupled Coordinate Remapping

When the user interacts with menus, map markers, or journal overlays in windowed, borderless fullscreen, 16:10, 21:9, or 32:9 aspect ratios, raw `HWND` client coordinates $(X_{\text{client}}, Y_{\text{client}})$ must be translated to the internal render resolution $(\text{Width}_{\text{internal}}, \text{Height}_{\text{internal}})$ through the active viewport rectangle $(\text{Viewport}_X, \text{Viewport}_Y, \text{Viewport}_{\text{width}}, \text{Viewport}_{\text{height}})$:

$$X_{\text{game}} = (X_{\text{client}} - \text{Viewport}_X) \times \left( \frac{\text{Width}_{\text{internal}}}{\text{Viewport}_{\text{width}}} \right)$$

$$Y_{\text{game}} = (Y_{\text{client}} - \text{Viewport}_Y) \times \left( \frac{\text{Height}_{\text{internal}}}{\text{Viewport}_{\text{height}}} \right)$$

### Key Guarantees
- **Strict Axis Independence:** The horizontal scale factor $\frac{\text{Width}_{\text{internal}}}{\text{Viewport}_{\text{width}}}$ never contaminates $Y_{\text{game}}$, and $\frac{\text{Height}_{\text{internal}}}{\text{Viewport}_{\text{height}}}$ never contaminates $X_{\text{game}}$.
- **Viewport Offset Subtraction First:** Letterbox (`Viewport_Y > 0`) and pillarbox (`Viewport_X > 0`) offsets are subtracted prior to scaling.
- **Clamping & Safety:** Zero-dimension viewports safely return `(0.0, 0.0)` without division-by-zero traps (`noexcept`).

---

## 3. Camera Mouse-Look & Controller Deadzone Pipeline

1. **Raw Delta Accumulation:** High-frequency mouse motion events are accumulated in atomic integer micro-units per frame.
2. **Sensitivity & Aspect Compensation:** Mouse yaw and pitch sensitivity scale uniformly regardless of window size, with optional vertical inversion (`GOW3_MOUSE_INVERT_Y`).
3. **Radial Deadzone & Curve Mapping:** DualShock 4 / DualSense / XInput thumbsticks apply radial deadzone filtering before blending with mouse-look camera injection.
