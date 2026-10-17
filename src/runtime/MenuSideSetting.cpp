#include "runtime/MenuSideSetting.h"

#include <utility>

#include <nlohmann/json.hpp>

namespace stui::runtime {

MenuSideSetting::MenuSideSetting(std::shared_ptr<PluginConfigFile> config)
    : m_config(std::move(config)) {}

bool MenuSideSetting::load() {
    if (!m_config) return false;
    const auto config = m_config->read();
    if (!config) return false;
    m_side = MenuSide::left;
    if (!config->contains("menuSide")) {
        return true;
    }
    const auto& value = config->at("menuSide");
    if (value == "left") {
        m_side = MenuSide::left;
        return true;
    }
    if (value == "right") {
        m_side = MenuSide::right;
        return true;
    }
    return false;
}

bool MenuSideSetting::select(MenuSide side) {
    if (!m_config) return false;
    const auto result = m_config->update([side](nlohmann::json& config) -> ConfigUpdateResult {
        config["menuSide"] = side == MenuSide::left ? "left" : "right";
        return {};
    });
    if (!result) return false;
    m_side = side;
    return true;
}

MenuSide MenuSideSetting::side() const noexcept { return m_side; }

}  // namespace stui::runtime
