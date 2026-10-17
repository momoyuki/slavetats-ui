#pragma once
#include <array>
#include <optional>
namespace stui::native {
using CameraVector = std::array<float, 3>;
enum class BrowserCameraFocus { none, wholeBody, face, body, hands, feet };
struct BrowserCameraMotion {
    float orbitX{}, orbitY{}, zoom{}, panX{}, panY{};
    BrowserCameraFocus focus{BrowserCameraFocus::none};
    bool reset{};
    float frameCenterX{0.5F};
    [[nodiscard]] bool empty() const noexcept {
        return orbitX == 0 && orbitY == 0 && zoom == 0 && panX == 0 && panY == 0 &&
            focus == BrowserCameraFocus::none && !reset;
    }
};
struct BrowserCameraView {
    CameraVector position{};
    float yaw{}, pitch{};
    bool operator==(const BrowserCameraView&) const = default;
};
struct BrowserCameraLandmarks {
    std::optional<CameraVector> boundsCenter, head, torso, leftHand, rightHand, leftFoot, rightFoot;
    float boundsRadius{};
};
[[nodiscard]] std::optional<CameraVector> resolveBrowserCameraFocus(
    const BrowserCameraLandmarks& geometry, BrowserCameraFocus focus) noexcept;
[[nodiscard]] CameraVector browserCameraForward(const BrowserCameraView& view) noexcept;
[[nodiscard]] std::optional<BrowserCameraView> browserCameraViewFromForward(
    const CameraVector& position, const CameraVector& forward) noexcept;
void applyBrowserCameraMotion(BrowserCameraView& view, CameraVector& focus,
    const BrowserCameraMotion& motion) noexcept;
[[nodiscard]] BrowserCameraView centerBrowserCameraView(
    const BrowserCameraView& view, const CameraVector& focus, float lateralDistance) noexcept;
// Restore the captured state's inputs independently of the current view/root.
template <class State>
struct BrowserCameraControlSnapshot {
    decltype(State::freeRotation) freeRotation{};
    bool enabled{};
    decltype(State::posOffsetExpected) expectedOffset{};
    decltype(State::posOffsetActual) actualOffset{};
    float targetZoom{}, currentZoom{};

    BrowserCameraControlSnapshot() = default;
    explicit BrowserCameraControlSnapshot(const State& state)
        : freeRotation(state.freeRotation), enabled(state.freeRotationEnabled),
          expectedOffset(state.posOffsetExpected), actualOffset(state.posOffsetActual),
          targetZoom(state.targetZoomOffset), currentZoom(state.currentZoomOffset) {}
    void preserveOffsets(State& state) const {
        // Hold the displayed offset, not an unfinished interpolation target.
        state.posOffsetExpected = actualOffset;
        state.posOffsetActual = actualOffset;
    }
    void restore(State& state) const {
        state.freeRotation = freeRotation;
        state.freeRotationEnabled = enabled;
        state.posOffsetExpected = expectedOffset;
        state.posOffsetActual = actualOffset;
        state.targetZoomOffset = targetZoom;
        state.currentZoomOffset = currentZoom;
    }
};
struct BrowserCameraState {
    float yaw{};
    float pitch{};
    float zoom{};
    float minimumZoom{};
    float maximumZoom{1.0F};
    bool operator==(const BrowserCameraState&) const = default;
};
// Native free-look controls already include the engine's relative orientation.
[[nodiscard]] BrowserCameraState browserCameraStateFromNative(
    float yaw, float pitch, float zoom, bool freeRotationEnabled) noexcept;
[[nodiscard]] BrowserCameraState advanceBrowserCameraState(
    BrowserCameraState state, float orbitX, float orbitY, float wheel) noexcept;
}  // namespace stui::native
