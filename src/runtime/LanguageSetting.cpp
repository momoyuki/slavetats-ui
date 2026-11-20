#include "runtime/LanguageSetting.h"
#include <nlohmann/json.hpp>
#include <utility>

namespace stui::runtime {
LanguageSetting::LanguageSetting(std::shared_ptr<PluginConfigFile> config) : m_config(std::move(config)) {}
bool LanguageSetting::load() {
    m_language = "en";
    if (!m_config) return false;
    const auto config = m_config->read();
    if (!config) return false;
    if (!config->contains("language")) return true;
    const auto& value = config->at("language");
    if (!value.is_string()) return false;
    const auto code = value.get<std::string>();
    if (!localization::catalog().contains(code)) return false;
    m_language = code;
    return true;
}
bool LanguageSetting::select(std::string_view code) {
    if (!m_config || !localization::catalog().contains(code)) return false;
    const auto saved = m_config->update([code](nlohmann::json& config) -> ConfigUpdateResult {
        config["language"] = code;
        return {};
    });
    if (!saved) return false;
    m_language = code;
    return true;
}
}  // namespace stui::runtime
