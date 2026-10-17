#include "native/BrowserCameraSession.h"

namespace stui::native {

BrowserCameraSession::BrowserCameraSession(BrowserCameraPort& port) noexcept : m_port(port) {}

void BrowserCameraSession::setTarget(std::uint32_t formId, bool menuOnRight) {
    if (formId != m_targetFormId || menuOnRight != m_menuOnRight) {
        close();
        m_targetFormId = formId;
        m_menuOnRight = menuOnRight;
        m_port.rememberView(formId, menuOnRight);
    }
    if (m_active && !m_port.isTargetLoaded(m_targetFormId)) {
        close();
        m_targetFormId = formId;
    }
}

void BrowserCameraSession::move(float orbitX, float orbitY, float zoom) {
    control({orbitX, orbitY, zoom});
}
void BrowserCameraSession::control(const BrowserCameraMotion& motion) {
    if (!m_active) {
        if (m_targetFormId == 0 ||
            motion.empty() ||
            !m_port.isTargetLoaded(m_targetFormId)) return;
        m_active = m_port.begin(m_targetFormId, m_menuOnRight);
        if (!m_active) return;
    }
    if (!m_port.isTargetLoaded(m_targetFormId)) {
        close();
        return;
    }
    m_active = m_port.control(motion);
}

void BrowserCameraSession::close() {
    if (m_active) m_port.restore();
    m_active = false;
    m_targetFormId = 0;
}

bool BrowserCameraSession::active() const noexcept { return m_active; }

}  // namespace stui::native
