#include "native/CameraControlLease.h"

#include <utility>

namespace stui::native {

CameraControlLease::CameraControlLease(
    Request request, Action release, Action returnToGoal)
    : m_request(std::move(request)),
      m_release(std::move(release)),
      m_returnToGoal(std::move(returnToGoal)) {}

bool CameraControlLease::acquire() {
    if (m_held) return true;
    if (!m_request) return false;
    const auto result = m_request();
    m_held = result == CameraControlResult::granted ||
        result == CameraControlResult::alreadyOwned;
    return m_held;
}

void CameraControlLease::release() {
    if (!m_held) return;
    if (m_returnToGoal) m_returnToGoal();
    if (m_release) m_release();
    m_held = false;
}

bool CameraControlLease::held() const noexcept { return m_held; }

}  // namespace stui::native
