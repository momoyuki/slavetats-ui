#pragma once

#include "runtime/PluginConfigFile.h"

#include <memory>

namespace stui::runtime {

enum class MenuSide { left, right };
struct CameraPanSetting {
    float x{}, y{};
    bool operator==(const CameraPanSetting&) const = default;
};

class MenuSideSetting {
public:
    explicit MenuSideSetting(std::shared_ptr<PluginConfigFile> config);

    [[nodiscard]] bool load();
    [[nodiscard]] bool select(MenuSide side);
    [[nodiscard]] MenuSide side() const noexcept;
    [[nodiscard]] float initialCameraOffset(MenuSide side) const noexcept;
    [[nodiscard]] bool setInitialCameraOffset(MenuSide side, float offset);
    [[nodiscard]] CameraPanSetting initialCameraPan(MenuSide side) const noexcept;
    [[nodiscard]] bool setInitialCameraPan(MenuSide side, CameraPanSetting pan);

private:
    std::shared_ptr<PluginConfigFile> m_config;
    MenuSide m_side{MenuSide::left};
    float m_initialCameraOffsetLeft{};
    float m_initialCameraOffsetRight{};
    CameraPanSetting m_initialCameraPanLeft{}, m_initialCameraPanRight{};
};

}  // namespace stui::runtime
