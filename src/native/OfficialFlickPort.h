#pragma once

#include "native/FlickLauncher.h"

namespace stui::native {

class OfficialFlickPort final : public FlickPort {
public:
    [[nodiscard]] bool connect() noexcept override;
    [[nodiscard]] bool registerTool(FlickLauncher& launcher) noexcept override;
    void closeMenu() noexcept override;
};

}  // namespace stui::native
