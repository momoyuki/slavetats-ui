#include "native/BrowserCameraPose.h"
#include <algorithm>
#include <cmath>
namespace stui::native {
BrowserCameraState advanceBrowserCameraState(
    BrowserCameraState state, float orbitX, float orbitY, float wheel) noexcept {
    if (!std::isfinite(orbitX) || !std::isfinite(orbitY) || !std::isfinite(wheel)) return state;
    if (orbitX != 0.0F) state.yaw += std::clamp(orbitX, -80.0F, 80.0F) * 0.006F;
    if (orbitY != 0.0F) {
        state.pitch = std::clamp(state.pitch + std::clamp(orbitY, -80.0F, 80.0F) * 0.006F,
            -1.2F, 1.2F);
    }
    if (wheel != 0.0F) {
        state.zoom = std::clamp(state.zoom - std::clamp(wheel, -3.0F, 3.0F) * 0.05F,
            state.minimumZoom, state.maximumZoom);
    }
    return state;
}
BrowserCameraState browserCameraStateFromNative(
    float yaw, float pitch, float zoom, bool freeRotationEnabled) noexcept {
    if (!std::isfinite(yaw) || !std::isfinite(pitch) || !std::isfinite(zoom)) return {};
    return {freeRotationEnabled ? yaw : 0.0F, freeRotationEnabled ? pitch : 0.0F,
        zoom, std::min(zoom, 0.0F), std::max(zoom, 1.0F)};
}
}  // namespace stui::native

namespace stui::native {
namespace {
bool finiteVector(const CameraVector& v) {
    return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]);
}
float vectorLength(const CameraVector& v) {
    return std::sqrt(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
}
std::optional<CameraVector> validBone(const std::optional<CameraVector>& bone) {
    return bone && finiteVector(*bone) ? bone : std::nullopt;
}
std::optional<CameraVector> midpoint(
    const std::optional<CameraVector>& a, const std::optional<CameraVector>& b) {
    const auto left = validBone(a), right = validBone(b);
    if (!left) return right;
    if (!right) return left;
    return CameraVector{((*left)[0]+(*right)[0])/2, ((*left)[1]+(*right)[1])/2,
        ((*left)[2]+(*right)[2])/2};
}
}
std::optional<CameraVector> resolveBrowserCameraFocus(
    const BrowserCameraLandmarks& g, BrowserCameraFocus focus) noexcept {
    std::optional<CameraVector> bone;
    switch (focus) {
    case BrowserCameraFocus::face: bone = validBone(g.head); break;
    case BrowserCameraFocus::body: bone = validBone(g.torso); break;
    case BrowserCameraFocus::hands: bone = midpoint(g.leftHand, g.rightHand); break;
    case BrowserCameraFocus::feet: bone = midpoint(g.leftFoot, g.rightFoot); break;
    case BrowserCameraFocus::wholeBody:
        if (validBone(g.head) && midpoint(g.leftFoot, g.rightFoot))
            bone = midpoint(g.head, midpoint(g.leftFoot, g.rightFoot));
        break;
    default: return std::nullopt;
    }
    if (bone) return bone;
    if (!validBone(g.boundsCenter) || !std::isfinite(g.boundsRadius) || g.boundsRadius <= 0)
        return std::nullopt;
    auto result = *g.boundsCenter;
    if (focus == BrowserCameraFocus::face) result[2] += g.boundsRadius * 0.7F;
    if (focus == BrowserCameraFocus::feet) result[2] -= g.boundsRadius * 0.8F;
    return result;
}
CameraVector browserCameraForward(const BrowserCameraView& v) noexcept {
    return {std::sin(v.yaw)*std::cos(v.pitch), std::cos(v.yaw)*std::cos(v.pitch),
        -std::sin(v.pitch)};
}
std::optional<BrowserCameraView> browserCameraViewFromForward(
    const CameraVector& position, const CameraVector& forward) noexcept {
    if (!finiteVector(position) || !finiteVector(forward)) return std::nullopt;
    const float length = vectorLength(forward);
    if (length < 0.0001F) return std::nullopt;
    return BrowserCameraView{position, std::atan2(forward[0],forward[1]),
        -std::asin(std::clamp(forward[2]/length,-1.0F,1.0F))};
}
void applyBrowserCameraMotion(BrowserCameraView& v, CameraVector& focus,
    const BrowserCameraMotion& m) noexcept {
    if (m.empty()) return;
    if (!finiteVector(v.position) || !finiteVector(focus) ||
        !std::isfinite(m.orbitX) || !std::isfinite(m.orbitY) || !std::isfinite(m.zoom) ||
        !std::isfinite(m.panX) || !std::isfinite(m.panY)) return;
    const float yawDelta = std::clamp(m.orbitX, -80.0F, 80.0F) * 0.003F;
    const float pitch = m.orbitY == 0 ? v.pitch :
        std::clamp(v.pitch + std::clamp(m.orbitY, -80.0F, 80.0F)*0.003F, -1.4F, 1.4F);
    const float pitchDelta = pitch - v.pitch;
    CameraVector offset{v.position[0]-focus[0],v.position[1]-focus[1],v.position[2]-focus[2]};
    if (yawDelta != 0) {
        const float c = std::cos(yawDelta), s = std::sin(yawDelta);
        offset = {offset[0]*c + offset[1]*s, -offset[0]*s + offset[1]*c, offset[2]};
    }
    v.yaw += yawDelta;
    const CameraVector right{std::cos(v.yaw), -std::sin(v.yaw), 0};
    if (pitchDelta != 0) {
        const float c=std::cos(pitchDelta), s=std::sin(-pitchDelta);
        const float dot=right[0]*offset[0]+right[1]*offset[1];
        const CameraVector cross{right[1]*offset[2],-right[0]*offset[2],
            right[0]*offset[1]-right[1]*offset[0]};
        for (int i=0;i<3;++i) offset[i]=offset[i]*c+cross[i]*s+right[i]*dot*(1-c);
    }
    v.pitch = pitch;
    const float distance = vectorLength(offset);
    if (m.zoom != 0 && distance > 0) {
        const float next = std::clamp(distance * std::exp(-std::clamp(m.zoom,-3.0F,3.0F)*0.1F),5.0F,5000.0F);
        for (float& component : offset) component *= next / distance;
    }
    const CameraVector up{std::sin(v.yaw)*std::sin(v.pitch),
        std::cos(v.yaw)*std::sin(v.pitch),std::cos(v.pitch)};
    const float panScale = std::clamp(distance * 0.0015F, 0.02F, 3.0F);
    for (int i=0;i<3;++i) {
        focus[i] += (right[i]*std::clamp(m.panX,-80.0F,80.0F) +
            up[i]*std::clamp(m.panY,-80.0F,80.0F))*panScale;
        v.position[i] = focus[i]+offset[i];
    }
}
BrowserCameraView centerBrowserCameraView(
    const BrowserCameraView& v, const CameraVector& focus, float lateral) noexcept {
    if (!finiteVector(v.position) || !finiteVector(focus) || !std::isfinite(lateral)) return v;
    const float distance = vectorLength({v.position[0]-focus[0],v.position[1]-focus[1],v.position[2]-focus[2]});
    lateral = std::clamp(lateral,-distance*0.9F,distance*0.9F);
    const float depth=std::sqrt(std::max(0.0F,distance*distance-lateral*lateral));
    const auto forward=browserCameraForward(v);
    const CameraVector right{std::cos(v.yaw),-std::sin(v.yaw),0};
    auto result=v;
    for(int i=0;i<3;++i) result.position[i]=focus[i]-forward[i]*depth-right[i]*lateral;
    return result;
}
}  // namespace stui::native
