#include "native/BrowserCameraPose.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
void expect(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool near(float a, float b) { return std::abs(a - b) < 0.001F; }
struct FakeNativeState {
    std::array<float, 2> freeRotation{};
    bool freeRotationEnabled{};
    std::array<float, 3> posOffsetExpected{}, posOffsetActual{};
    float targetZoomOffset{}, currentZoomOffset{};
    bool operator==(const FakeNativeState&) const = default;
};
void controlsRestoreAfterViewReplacement() {
    using namespace stui::native;
    FakeNativeState original{{0.2F, 0.3F}, false, {1, 2, 3}, {4, 5, 6}, 0.7F, 0.8F};
    const BrowserCameraControlSnapshot<FakeNativeState> saved(original);
    auto captured = original;
    captured.freeRotationEnabled = true;
    captured.freeRotation = {1, 1};
    captured.posOffsetExpected = {9, 9, 9};
    captured.posOffsetActual = {8, 8, 8};
    captured.targetZoomOffset = captured.currentZoomOffset = 0;
    FakeNativeState replacement{};
    const auto replacementBefore = replacement;
    saved.restore(captured);
    expect(captured == original, "all written controls must restore on the captured state");
    expect(replacement == replacementBefore, "replacement camera state must remain untouched");
}
void capturedOffsetsSurviveCameraOwnershipCleanup() {
    using namespace stui::native;
    FakeNativeState state{{-2.776F, 0.161F}, true,
        {25.0F, -157.67F, -24.86F}, {25.0F, -157.67F, -24.86F}, 0.025F, 0.025F};
    const BrowserCameraControlSnapshot<FakeNativeState> saved(state);
    // SmoothCam's first update after ownership resets native shoulder offsets.
    state.posOffsetExpected = state.posOffsetActual = {30.0F, 0.0F, -10.0F};
    state.freeRotation = {-2.8F, 0.2F};
    state.targetZoomOffset = 0.05F;
    saved.preserveOffsets(state);
    expect(state.posOffsetExpected == saved.expectedOffset &&
        state.posOffsetActual == saved.actualOffset,
        "ownership cleanup must not replace captured framing offsets");
    expect(state.freeRotation == std::array<float, 2>{-2.8F, 0.2F} &&
        state.targetZoomOffset == 0.05F,
        "preserving framing must not undo the user's orbit or zoom");
    saved.restore(state);
    expect(state.targetZoomOffset == 0.025F,
        "closing must still restore the original zoom after framing preservation");
    FakeNativeState easing{{0, 0}, true, {30, 0, -10}, {25, -157, -25}, 0.025F, 0.025F};
    const BrowserCameraControlSnapshot<FakeNativeState> easingSaved(easing);
    easingSaved.preserveOffsets(easing);
    expect(easing.posOffsetExpected == easingSaved.actualOffset &&
        easing.posOffsetActual == easingSaved.actualOffset,
        "unfinished interpolation must freeze at actual framing without a repeated easing step");
    easingSaved.restore(easing);
    expect(easing.posOffsetExpected == easingSaved.expectedOffset &&
        easing.posOffsetActual == easingSaved.actualOffset,
        "close must restore distinct expected and actual offsets exactly");
}
void stateHandoffAndGestures() {
    using namespace stui::native;
    const auto initial = browserCameraStateFromNative(1.3F, 0.3F, 0.4F, true);
    expect(advanceBrowserCameraState(initial, 0, 0, 0) == initial,
        "idle refresh must not drift captured controls");
    const auto moved = advanceBrowserCameraState(initial, 10, 10, 1);
    expect(moved.yaw > initial.yaw && moved.pitch > initial.pitch,
        "drag intent must change both native rotation controls");
    expect(moved.zoom < initial.zoom, "wheel forward must reduce native distance");
    auto bounded = advanceBrowserCameraState({0, 0, 0.5F}, 10000, 10000, 10000);
    expect(near(bounded.yaw, 0.48F) && near(bounded.pitch, 0.48F),
        "single pointer spikes must be bounded");
    for (int i = 0; i < 100; ++i)
        bounded = advanceBrowserCameraState(bounded, 80, 80, 3);
    expect(bounded.pitch <= 1.2F && bounded.zoom == 0,
        "native pitch and zoom must remain bounded");
    expect(advanceBrowserCameraState(initial,
        std::numeric_limits<float>::quiet_NaN(), 0, 0) == initial,
        "nonfinite input must not corrupt native camera state");
}
void nativeHandoffDoesNotReinterpretControls() {
    using namespace stui::native;
    const auto initial = browserCameraStateFromNative(3.122F, 0.0F, -0.2F, true);
    expect(initial.yaw == 3.122F && initial.pitch == 0 && initial.zoom == -0.2F,
        "handoff must preserve native free rotation and signed zoom exactly");
    const auto horizontal = advanceBrowserCameraState(initial, 9, 0, 0);
    expect(horizontal.pitch == initial.pitch && horizontal.zoom == initial.zoom,
        "horizontal drag must not change native pitch or signed zoom");
    expect(advanceBrowserCameraState(initial, 0, 0, 0) == initial,
        "idle refresh must retain negative native zoom");
    const auto inactive = browserCameraStateFromNative(2, 1, -0.125F, false);
    expect(inactive.yaw == 0 && inactive.pitch == 0 && inactive.zoom == -0.125F,
        "enabling native free look must ignore stale disabled rotation inputs");
    auto zoomed = advanceBrowserCameraState(initial, 0, 0, -1);
    expect(zoomed.zoom > initial.zoom, "wheel backward must increase signed zoom");
    for (int i = 0; i < 100; ++i)
        zoomed = advanceBrowserCameraState(zoomed, 0, 0, 3);
    expect(zoomed.zoom == initial.zoom,
        "zoom minimum must stay at captured negative baseline across gestures");
    const auto extreme = browserCameraStateFromNative(3.2F, 1.3F, -0.2F, true);
    expect(advanceBrowserCameraState(extreme, 0, 0, 0) == extreme,
        "zero input must not normalize native angles or clamp original pitch");
    const auto seam = advanceBrowserCameraState(initial, 10, 0, 0);
    expect(seam.yaw > initial.yaw,
        "crossing pi must retain continuous yaw for native interpolation");
}
void focusUsesBonesAndScaledBounds() {
    using namespace stui::native;
    BrowserCameraLandmarks body;
    body.boundsCenter = CameraVector{10, 20, 80};
    body.boundsRadius = 80;
    body.head = CameraVector{10, 20, 155};
    body.leftFoot = CameraVector{5, 20, 5};
    body.rightFoot = CameraVector{15, 20, 5};
    expect(resolveBrowserCameraFocus(body, BrowserCameraFocus::face) == body.head,
        "face focus must use the actual head bone");
    expect(resolveBrowserCameraFocus(body, BrowserCameraFocus::wholeBody) == body.boundsCenter,
        "whole body focus must use the midpoint of head and feet");
    body.head.reset();
    const auto shortHead = resolveBrowserCameraFocus(body, BrowserCameraFocus::face);
    body.boundsCenter = CameraVector{10, 20, 160};
    body.boundsRadius = 160;
    const auto tallHead = resolveBrowserCameraFocus(body, BrowserCameraFocus::face);
    expect(shortHead && tallHead && (*tallHead)[2] > (*shortHead)[2] * 1.8F,
        "missing-bone fallback must scale with the actor bounds");
    body.boundsRadius = std::numeric_limits<float>::quiet_NaN();
    expect(!resolveBrowserCameraFocus(body, BrowserCameraFocus::face),
        "invalid bounds must not produce a fabricated pivot");
}
void worldCameraMotionIsContinuous() {
    using namespace stui::native;
    BrowserCameraView view{{25, -200, 80}, 0, 0};
    CameraVector focus{0, 0, 80};
    const auto initial = view;
    applyBrowserCameraMotion(view, focus, {});
    expect(view == initial, "no input must preserve the actual world camera view");
    const auto originalFocus = focus;
    applyBrowserCameraMotion(view, focus, {.panX = 10, .panY = 20});
    expect(near(view.position[0] - initial.position[0], focus[0] - originalFocus[0]) &&
        near(view.position[2] - initial.position[2], focus[2] - originalFocus[2]),
        "pan must move camera and pivot together without changing orientation");
    expect(view.yaw == 0 && view.pitch == 0, "pan must not rotate the camera");
    const float beforeDistance = std::abs(view.position[1] - focus[1]);
    applyBrowserCameraMotion(view, focus, {.zoom = 1});
    expect(std::abs(view.position[1] - focus[1]) < beforeDistance,
        "wheel forward must approach the active focus");
    const auto centered = centerBrowserCameraView(initial, originalFocus, 0.0F);
    expect(centered.position[0] == 0 && centered.yaw == initial.yaw,
        "Center must align focus without resetting orientation");
    const BrowserCameraView straight{{0, -200, 80}, 0, 0};
    const CameraVector bodyFocus{0, 0, 80};
    const auto frameLeft = centerBrowserCameraView(straight, bodyFocus, -40.0F);
    const auto frameRight = centerBrowserCameraView(straight, bodyFocus, 40.0F);
    const auto distanceToBody = [&](const BrowserCameraView& framed) {
        const auto dx = framed.position[0] - bodyFocus[0];
        const auto dy = framed.position[1] - bodyFocus[1];
        const auto dz = framed.position[2] - bodyFocus[2];
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    };
    expect(frameLeft.position[0] > 0 && frameRight.position[0] < 0 &&
        near(distanceToBody(frameLeft), 200.0F) &&
        near(distanceToBody(frameRight), 200.0F) &&
        frameLeft.yaw == straight.yaw && frameRight.pitch == straight.pitch,
        "side framing must mirror placement while preserving distance and angle");
    const BrowserCameraView angled{{-54000, -78000, -2400}, 2.3F, -0.4F};
    const auto roundTrip = browserCameraViewFromForward(angled.position, browserCameraForward(angled));
    expect(roundTrip && near(roundTrip->yaw, angled.yaw) && near(roundTrip->pitch, angled.pitch),
        "native forward/pitch conversion must round trip without flipping vertical direction");
    expect(!browserCameraViewFromForward(angled.position, {0, 0, 0}),
        "invalid orientation must not start a camera session");
}
}
int main() {
    try {
        stateHandoffAndGestures();
        controlsRestoreAfterViewReplacement();
        nativeHandoffDoesNotReinterpretControls();
        capturedOffsetsSurviveCameraOwnershipCleanup();
        focusUsesBonesAndScaledBounds();
        worldCameraMotionIsContinuous();
        std::cout << "PASS native browser camera controls\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
