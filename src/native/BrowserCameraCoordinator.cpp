#include "native/BrowserCameraCoordinator.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace stui::native {

BrowserCameraCoordinator::BrowserCameraCoordinator(BrowserCameraPort& port, Schedule schedule)
    : m_session(port), m_schedule(std::move(schedule)) {}

void BrowserCameraCoordinator::setOpen(bool open) {
    {
        const std::scoped_lock lock(m_mutex);
        m_open = open;
        if (!open) {
            m_targetFormId = 0;
            m_pending = {};
        }
        ++m_revision;
    }
    scheduleIfNeeded();
}

void BrowserCameraCoordinator::setTarget(
    std::uint32_t formId, bool menuOnRight, float frameCenterX,
    BrowserCameraPan initialPan) {
    {
        const std::scoped_lock lock(m_mutex);
        if (!m_open) return;
        if (m_targetFormId == formId && m_menuOnRight == menuOnRight) return;
        m_targetFormId = formId;
        m_menuOnRight = menuOnRight;
        m_pending = {};
        if (formId == 0x14) {
            m_pending.focus = BrowserCameraFocus::wholeBody;
            m_pending.initialFrame = true;
            m_pending.frameCenterX = std::isfinite(frameCenterX) && frameCenterX > 0.0F &&
                frameCenterX < 1.0F ? frameCenterX : (menuOnRight ? 0.25F : 0.75F);
            if (std::isfinite(initialPan.x) && std::isfinite(initialPan.y) &&
                std::abs(initialPan.x) <= 500.0F && std::abs(initialPan.y) <= 500.0F) {
                m_pending.initialPanX = initialPan.x;
                m_pending.initialPanY = initialPan.y;
            }
        }
        ++m_revision;
    }
    scheduleIfNeeded();
}

void BrowserCameraCoordinator::move(float orbitX, float orbitY, float zoom) {
    control({orbitX, orbitY, zoom});
}
void BrowserCameraCoordinator::control(const BrowserCameraMotion& motion) {
    {
        const std::scoped_lock lock(m_mutex);
        if (!m_open) return;
        if (motion.reset || motion.focus != BrowserCameraFocus::none) m_pending = {};
        if (!motion.empty() && motion.focus == BrowserCameraFocus::none && !motion.reset &&
            m_pending.focus != BrowserCameraFocus::none) m_pending = {};
        if (!m_pending.reset && m_pending.focus == BrowserCameraFocus::none) {
            m_pending.orbitX = std::clamp(m_pending.orbitX + motion.orbitX, -80.0F, 80.0F);
            m_pending.orbitY = std::clamp(m_pending.orbitY + motion.orbitY, -80.0F, 80.0F);
            m_pending.zoom = std::clamp(m_pending.zoom + motion.zoom, -3.0F, 3.0F);
            m_pending.panX = std::clamp(m_pending.panX + motion.panX, -80.0F, 80.0F);
            m_pending.panY = std::clamp(m_pending.panY + motion.panY, -80.0F, 80.0F);
        }
        if (motion.reset) m_pending.reset = true;
        if (motion.focus != BrowserCameraFocus::none) m_pending.focus = motion.focus;
        m_pending.frameCenterX = motion.frameCenterX;
        ++m_revision;
    }
    scheduleIfNeeded();
}

void BrowserCameraCoordinator::refresh() {
    {
        const std::scoped_lock lock(m_mutex);
        if (!m_open) return;
        ++m_revision;
    }
    scheduleIfNeeded();
}

void BrowserCameraCoordinator::resetGameSession() {
    {
        const std::scoped_lock lock(m_mutex);
        m_open = false;
        m_targetFormId = 0;
        m_pending = {};
        m_resetRequested = true;
        ++m_revision;
    }
    scheduleIfNeeded();
}

void BrowserCameraCoordinator::scheduleIfNeeded() {
    {
        const std::scoped_lock lock(m_mutex);
        if (m_scheduled) return;
        m_scheduled = true;
    }
    try {
        m_schedule([this] { drain(); });
    } catch (...) {
        const std::scoped_lock lock(m_mutex);
        m_scheduled = false;
    }
}

void BrowserCameraCoordinator::drain() {
    for (;;) {
        bool open;
        bool menuOnRight;
        std::uint32_t target;
        std::uint64_t revision;
        BrowserCameraMotion motion;
        bool resetRequested;
        {
            const std::scoped_lock lock(m_mutex);
            open = m_open;
            menuOnRight = m_menuOnRight;
            target = m_targetFormId;
            revision = m_revision;
            motion = std::exchange(m_pending, {});
            resetRequested = std::exchange(m_resetRequested, false);
        }
        if (resetRequested) {
            m_session.close();
            m_session.clearRememberedViews();
        }
        if (!open || target == 0) {
            m_session.close();
        } else {
            m_session.setTarget(target, menuOnRight);
            m_session.control(motion);
        }
        {
            const std::scoped_lock lock(m_mutex);
            if (m_revision == revision) {
                m_scheduled = false;
                return;
            }
        }
    }
}

}  // namespace stui::native
