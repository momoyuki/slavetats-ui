#pragma once

#include "native/BrowserCameraSession.h"

#include <cstdint>
#include <functional>
#include <mutex>

namespace stui::native {

// Coalesces render-thread intent into one scheduled game-thread camera task.
class BrowserCameraCoordinator {
public:
    using Task = std::function<void()>;
    using Schedule = std::function<void(Task)>;

    BrowserCameraCoordinator(BrowserCameraPort& port, Schedule schedule);

    void setOpen(bool open);
    void setTarget(std::uint32_t formId, bool menuOnRight = true,
        float frameCenterX = -1.0F, BrowserCameraPan initialPan = {});
    void move(float orbitX, float orbitY, float zoom);
    void control(const BrowserCameraMotion& motion);
    void refresh();
    void resetGameSession();

private:
    void scheduleIfNeeded();
    void drain();

    BrowserCameraSession m_session;
    Schedule m_schedule;
    std::mutex m_mutex;
    bool m_open{};
    bool m_scheduled{};
    bool m_resetRequested{};
    bool m_menuOnRight{true};
    std::uint32_t m_targetFormId{};
    std::uint64_t m_revision{};
    BrowserCameraMotion m_pending;
};

}  // namespace stui::native
