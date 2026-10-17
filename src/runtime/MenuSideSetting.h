#pragma once

#include "runtime/PluginConfigFile.h"

#include <memory>

namespace stui::runtime {

enum class MenuSide { left, right };

class MenuSideSetting {
public:
    explicit MenuSideSetting(std::shared_ptr<PluginConfigFile> config);

    [[nodiscard]] bool load();
    [[nodiscard]] bool select(MenuSide side);
    [[nodiscard]] MenuSide side() const noexcept;

private:
    std::shared_ptr<PluginConfigFile> m_config;
    MenuSide m_side{MenuSide::left};
};

}  // namespace stui::runtime
