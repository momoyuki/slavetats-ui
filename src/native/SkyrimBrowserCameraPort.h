#pragma once

#include "native/BrowserCameraSession.h"

#include <memory>

namespace stui::native {

class SkyrimBrowserCameraPort final : public BrowserCameraPort {
public:
    SkyrimBrowserCameraPort();
    ~SkyrimBrowserCameraPort() override;

    SkyrimBrowserCameraPort(const SkyrimBrowserCameraPort&) = delete;
    SkyrimBrowserCameraPort& operator=(const SkyrimBrowserCameraPort&) = delete;

    void setSmoothCamInterface(void* interfaceInstance, int version) noexcept;
    void rememberView(std::uint32_t formId, bool menuOnRight) override;
    [[nodiscard]] bool begin(std::uint32_t formId, bool menuOnRight) override;
    [[nodiscard]] bool isTargetLoaded(std::uint32_t formId) override;
    [[nodiscard]] bool control(const BrowserCameraMotion& motion) override;
    void restore() override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace stui::native
