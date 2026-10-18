#pragma once

#include <functional>

namespace stui::native {

enum class CameraControlResult { granted, alreadyOwned, denied };

class CameraControlLease {
public:
    using Request = std::function<CameraControlResult()>;
    using Action = std::function<void()>;

    CameraControlLease(Request request, Action release, Action returnToGoal = {});

    [[nodiscard]] bool acquire();
    void release();
    [[nodiscard]] bool held() const noexcept;

private:
    Request m_request;
    Action m_release;
    Action m_returnToGoal;
    bool m_held{};
};

}  // namespace stui::native
