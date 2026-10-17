#pragma once

#include <cstdint>
#include "native/BrowserCameraPose.h"

namespace stui::native {

class BrowserCameraPort {
public:
    virtual ~BrowserCameraPort() = default;
    virtual void rememberView(std::uint32_t formId, bool menuOnRight) = 0;
    [[nodiscard]] virtual bool begin(std::uint32_t formId, bool menuOnRight) = 0;
    [[nodiscard]] virtual bool isTargetLoaded(std::uint32_t formId) = 0;
    // False means the port already released an invalidated camera session.
    [[nodiscard]] virtual bool control(const BrowserCameraMotion& motion) = 0;
    virtual void restore() = 0;
    virtual void clearRememberedViews() {}
};

// All methods run on the game thread. The port owns camera snapshots and SmoothCam leases.
// A Player target may acquire camera control for one initial side-aware framing intent.
class BrowserCameraSession {
public:
    explicit BrowserCameraSession(BrowserCameraPort& port) noexcept;

    void setTarget(std::uint32_t formId, bool menuOnRight = true);
    void move(float orbitX, float orbitY, float zoom);
    void control(const BrowserCameraMotion& motion);
    void close();
    void clearRememberedViews();
    [[nodiscard]] bool active() const noexcept;

private:
    BrowserCameraPort& m_port;
    std::uint32_t m_targetFormId{};
    bool m_active{};
    bool m_menuOnRight{true};
};

}  // namespace stui::native
