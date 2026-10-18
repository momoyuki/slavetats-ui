#include "runtime/MenuSideSetting.h"

#include <cmath>
#include <tuple>
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
    m_initialCameraOffsetLeft = 0.0F;
    m_initialCameraOffsetRight = 0.0F;
    m_initialCameraPanLeft = {};
    m_initialCameraPanRight = {};
    for (const auto [key, side] : {
             std::pair{"initialCameraOffsetLeft", MenuSide::left},
             std::pair{"initialCameraOffsetRight", MenuSide::right}}) {
        if (!config->contains(key)) continue;
        const auto& value = config->at(key);
        if (!value.is_number()) return false;
        const float offset = value.get<float>();
        if (!std::isfinite(offset) || offset < -0.15F || offset > 0.15F) return false;
        (side == MenuSide::left ? m_initialCameraOffsetLeft : m_initialCameraOffsetRight) = offset;
    }
    for (const auto [key, side, xAxis] : {
             std::tuple{"initialCameraPanLeftX", MenuSide::left, true},
             std::tuple{"initialCameraPanLeftY", MenuSide::left, false},
             std::tuple{"initialCameraPanRightX", MenuSide::right, true},
             std::tuple{"initialCameraPanRightY", MenuSide::right, false}}) {
        if (!config->contains(key)) continue;
        const auto& value = config->at(key);
        if (!value.is_number()) return false;
        const float offset = value.get<float>();
        if (!std::isfinite(offset) || std::abs(offset) > 500.0F) return false;
        auto& pan = side == MenuSide::left ? m_initialCameraPanLeft : m_initialCameraPanRight;
        (xAxis ? pan.x : pan.y) = offset;
    }
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

float MenuSideSetting::initialCameraOffset(MenuSide side) const noexcept {
    return side == MenuSide::left ? m_initialCameraOffsetLeft : m_initialCameraOffsetRight;
}

bool MenuSideSetting::setInitialCameraOffset(MenuSide side, float offset) {
    if (!m_config || !std::isfinite(offset) || offset < -0.15F || offset > 0.15F) return false;
    const auto result = m_config->update([side, offset](nlohmann::json& config) -> ConfigUpdateResult {
        config[side == MenuSide::left ? "initialCameraOffsetLeft" : "initialCameraOffsetRight"] = offset;
        return {};
    });
    if (!result) return false;
    (side == MenuSide::left ? m_initialCameraOffsetLeft : m_initialCameraOffsetRight) = offset;
    return true;
}
CameraPanSetting MenuSideSetting::initialCameraPan(MenuSide side) const noexcept {
    return side == MenuSide::left ? m_initialCameraPanLeft : m_initialCameraPanRight;
}
bool MenuSideSetting::setInitialCameraPan(MenuSide side, CameraPanSetting pan) {
    if (!m_config || !std::isfinite(pan.x) || !std::isfinite(pan.y) ||
        std::abs(pan.x) > 500.0F || std::abs(pan.y) > 500.0F) return false;
    const auto result = m_config->update([side, pan](nlohmann::json& config) -> ConfigUpdateResult {
        const char* xKey = side == MenuSide::left ? "initialCameraPanLeftX" : "initialCameraPanRightX";
        const char* yKey = side == MenuSide::left ? "initialCameraPanLeftY" : "initialCameraPanRightY";
        config[xKey] = pan.x;
        config[yKey] = pan.y;
        return {};
    });
    if (!result) return false;
    (side == MenuSide::left ? m_initialCameraPanLeft : m_initialCameraPanRight) = pan;
    return true;
}

}  // namespace stui::runtime
